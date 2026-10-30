#include "data/syntax_context.h"

#include <slang/parsing/Preprocessor.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>
#include <slang/util/Bag.h>

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace format {
namespace {

using slang::parsing::Token;
using slang::parsing::TokenKind;
using slang::syntax::SyntaxKind;
using slang::syntax::SyntaxNode;

[[nodiscard]] auto collectConditionalNames(gsl::span<const Token> tokens,
                                           std::set<std::string>& names)
    -> bool {
  for (size_t i = 0; i < tokens.size(); ++i) {
    const auto directive = tokens[i];
    const auto text = directive.rawText();
    if (directive.kind != TokenKind::Directive ||
        (text != "`ifdef" && text != "`ifndef" && text != "`elsif")) {
      continue;
    }
    if (++i == tokens.size()) {
      return false;
    }
    if (tokens[i].kind == TokenKind::Identifier) {
      names.emplace(tokens[i].rawText());
      continue;
    }
    if (tokens[i].kind != TokenKind::OpenParenthesis) {
      return false;
    }
    size_t depth = 1;
    for (++i; i < tokens.size() && depth > 0; ++i) {
      const auto token = tokens[i];
      switch (token.kind) {
        case TokenKind::Identifier:
          names.emplace(token.rawText());
          break;
        case TokenKind::OpenParenthesis:
          ++depth;
          break;
        case TokenKind::CloseParenthesis:
          --depth;
          break;
        case TokenKind::Exclamation:
        case TokenKind::DoubleAnd:
        case TokenKind::DoubleOr:
        case TokenKind::MinusArrow:
        case TokenKind::LessThanMinusArrow:
          break;
        default:
          return false;
      }
    }
    if (depth != 0) {
      return false;
    }
    --i;
  }
  return true;
}

struct TraversalContext {
  bool in_selection = false;
  bool in_typedef_type = false;
  bool in_type_name = false;
  bool trusted = true;
};

struct Visitor {
  std::reference_wrapper<std::unordered_map<size_t, TokenSyntaxContext>> tokens;
  slang::BufferID source_buffer;
  std::reference_wrapper<const slang::Diagnostics> diagnostics;
  std::unordered_map<const SyntaxNode*, bool> reliability_cache;

  [[nodiscard]] auto complete(const SyntaxNode& node) const -> bool {
    for (uint32_t i = 0; i < node.getChildCount(); ++i) {
      if (const auto* child = node.childNode(i)) {
        if (!complete(*child)) {
          return false;
        }
      } else if (const auto token = node.childToken(i)) {
        if (token.isMissing() || token.location().buffer() != source_buffer) {
          return false;
        }
        for (const auto& trivia : token.trivia()) {
          if (trivia.kind == slang::parsing::TriviaKind::SkippedTokens ||
              trivia.kind == slang::parsing::TriviaKind::SkippedSyntax) {
            return false;
          }
        }
      }
    }
    return true;
  }

  [[nodiscard]] auto reliable(const SyntaxNode& node) const -> bool {
    const auto first = node.getFirstToken();
    const auto last = node.getLastToken();
    if (!first || !last || !complete(node) ||
        first.location().buffer() != source_buffer ||
        last.location().buffer() != source_buffer) {
      return false;
    }
    const size_t start = first.location().offset();
    const size_t end = last.location().offset() + last.rawText().size();
    for (const auto& diagnostic : diagnostics.get()) {
      if (diagnostic.isError() &&
          diagnostic.location.buffer() == source_buffer &&
          diagnostic.location.offset() >= start &&
          diagnostic.location.offset() <= end) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] auto reliableEnclosingConstruct(const SyntaxNode& node)
      -> bool {
    const SyntaxNode* enclosing = &node;
    while (enclosing->parent) {
      if (slang::syntax::StatementSyntax::isKind(enclosing->kind) ||
          slang::syntax::MemberSyntax::isKind(enclosing->kind) ||
          enclosing->kind == SyntaxKind::FunctionPrototype ||
          enclosing->kind == SyntaxKind::ModuleHeader ||
          enclosing->kind == SyntaxKind::InterfaceHeader ||
          enclosing->kind == SyntaxKind::ProgramHeader ||
          enclosing->kind == SyntaxKind::PackageHeader) {
        break;
      }
      enclosing = enclosing->parent.get();
    }
    const auto it = reliability_cache.find(enclosing);
    if (it != reliability_cache.end()) {
      return it->second;
    }
    const bool valid = reliable(*enclosing);
    reliability_cache.emplace(enclosing, valid);
    return valid;
  }

  auto record(Token token, bool in_selection, bool typedef_parameter,
              bool trusted) -> void {
    if (!token || token.isMissing() ||
        token.location().buffer() != source_buffer) {
      return;
    }
    auto& context = tokens.get()[token.location().offset()];
    context.availability =
        trusted ? SyntaxAvailability::kKnown : SyntaxAvailability::kAmbiguous;
    context.in_selection = in_selection;
    context.typedef_class_parameter = typedef_parameter;
  }

  auto visit(const SyntaxNode& node, TraversalContext context = {}) -> void {
    if (node.kind == SyntaxKind::ElementSelect ||
        node.kind == SyntaxKind::VariableDimension ||
        (context.in_typedef_type && node.kind == SyntaxKind::ClassName)) {
      context.trusted = context.trusted && reliableEnclosingConstruct(node);
    }
    if (node.kind == SyntaxKind::NamedType) {
      context.in_type_name = true;
    }
    if (node.kind == SyntaxKind::ElementSelect) {
      // Slang uses ElementSelect for dimensions attached to named types too.
      // Expressions inside the dimension can still contain actual selections.
      context.in_selection = !context.in_type_name;
    } else if (node.kind == SyntaxKind::VariableDimension) {
      context.in_selection = false;
    }

    const auto* typedef_node =
        node.kind == SyntaxKind::TypedefDeclaration
            ? &node.as<slang::syntax::TypedefDeclarationSyntax>()
            : nullptr;
    const auto* class_node = node.kind == SyntaxKind::ClassName
                                 ? &node.as<slang::syntax::ClassNameSyntax>()
                                 : nullptr;
    const auto hash = class_node && context.in_typedef_type
                          ? class_node->parameters->hash
                          : Token{};
    for (uint32_t i = 0; i < node.getChildCount(); ++i) {
      if (const auto* child = node.childNode(i)) {
        auto child_context = context;
        if (typedef_node) {
          child_context.in_typedef_type = child == typedef_node->type.get() &&
                                          child->kind == SyntaxKind::NamedType;
        } else if (node.kind == SyntaxKind::ParameterValueAssignment) {
          child_context.in_typedef_type = false;
        }
        if (node.kind == SyntaxKind::ElementSelect ||
            node.kind == SyntaxKind::ParameterValueAssignment) {
          child_context.in_type_name = false;
        }
        visit(*child, child_context);
      } else if (const auto token = node.childToken(i)) {
        record(token, context.in_selection, false, context.trusted);
      }
    }
    if (hash) {
      // The hash is a child of ParameterValueAssignment, not ClassName.
      record(hash, context.in_selection, true, context.trusted);
    }
  }
};

}  // namespace

auto SyntaxContext::lookup(Token token) const -> TokenSyntaxContext {
  if (token.location().buffer() != source_buffer_) {
    return {};
  }
  const auto it = tokens_.find(token.location().offset());
  return it == tokens_.end() ? TokenSyntaxContext{} : it->second;
}

auto parseSyntaxContext(std::string_view source, gsl::span<const Token> tokens)
    -> SyntaxContext {
  SyntaxContext result;
  if (source.empty() || tokens.empty()) {
    return result;
  }
  result.source_buffer_ = tokens.front().location().buffer();
  std::set<std::string> names;
  if (!collectConditionalNames(tokens, names)) {
    return result;
  }

  // Exhaustive configurations avoid assigning a role when its syntax changes
  // between conditional branches. An unclassified token is preserved later.
  constexpr size_t kMaxConditionalNames = 6;
  if (names.size() > kMaxConditionalNames) {
    return result;
  }
  const std::vector<std::string> macro_names(names.begin(), names.end());
  const size_t configurations = size_t{1} << macro_names.size();
  for (size_t bits = 0; bits < configurations; ++bits) {
    slang::SourceManager source_manager;
    slang::parsing::PreprocessorOptions preprocessor_options;
    preprocessor_options.maxIncludeDepth = 0;
    for (size_t i = 0; i < macro_names.size(); ++i) {
      if ((bits & (size_t{1} << i)) != 0) {
        preprocessor_options.predefines.push_back(macro_names.at(i));
      }
    }
    const slang::Bag options{preprocessor_options};
    auto tree = slang::syntax::SyntaxTree::fromFileInMemory(
        source, source_manager, "formatter-input", "", options);
    const auto ids = tree->getSourceBufferIds();
    if (ids.empty()) {
      continue;
    }
    std::unordered_map<size_t, TokenSyntaxContext> parsed;
    Visitor{std::ref(parsed), ids.front(), std::cref(tree->diagnostics()), {}}
        .visit(tree->root());
    for (const auto& [offset, context] : parsed) {
      const auto [it, inserted] = result.tokens_.try_emplace(offset, context);
      if (!inserted && (it->second.availability != SyntaxAvailability::kKnown ||
                        context.availability != SyntaxAvailability::kKnown ||
                        it->second.in_selection != context.in_selection ||
                        it->second.typedef_class_parameter !=
                            context.typedef_class_parameter)) {
        it->second.availability = SyntaxAvailability::kAmbiguous;
      }
    }
  }
  return result;
}

}  // namespace format
