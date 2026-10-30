#include "data/syntax_context.h"

#include <gtest/gtest.h>
#include <slang/parsing/Token.h>
#include <slang/parsing/TokenKind.h>

#include <string_view>
#include <vector>

#include "data/lex_context.h"

namespace {

TEST(SyntaxContextTest, TypedefParameterDoesNotIncludeParameterExpressions) {
  constexpr std::string_view source =
      "module m; typedef foo#(pkg#(N)::value) foo_t; endmodule";
  LexContext lex;
  const auto tokens = lex.lex_string(source);
  const auto context = format::parseSyntaxContext(lex.source_text(), tokens);
  std::vector<format::TokenSyntaxContext> hashes;
  for (const auto& token : tokens) {
    if (token.kind == slang::parsing::TokenKind::Hash) {
      hashes.push_back(context.lookup(token));
    }
  }
  ASSERT_EQ(hashes.size(), 2U);
  EXPECT_TRUE(hashes.at(0).typedef_class_parameter);
  EXPECT_FALSE(hashes.at(1).typedef_class_parameter);
}

TEST(SyntaxContextTest, StructTypedefDoesNotMarkMemberClassParameters) {
  constexpr std::string_view source =
      "module m; typedef struct { foo#(N) field; } aggregate_t; "
      "typedef foo#(N) alias_t; endmodule";
  LexContext lex;
  const auto tokens = lex.lex_string(source);
  const auto context = format::parseSyntaxContext(lex.source_text(), tokens);
  std::vector<format::TokenSyntaxContext> hashes;
  for (const auto& token : tokens) {
    if (token.kind == slang::parsing::TokenKind::Hash) {
      hashes.push_back(context.lookup(token));
    }
  }
  ASSERT_EQ(hashes.size(), 2U);
  EXPECT_FALSE(hashes.at(0).typedef_class_parameter);
  EXPECT_TRUE(hashes.at(1).typedef_class_parameter);
}

TEST(SyntaxContextTest, ConflictingConditionalRolesAreNotClassified) {
  constexpr std::string_view source =
      "module m;\n"
      "`ifdef T\n"
      "typedef\n"
      "`else\n"
      "assign x =\n"
      "`endif\n"
      "foo[i+1]\n"
      "`ifdef T\n"
      "alias_t\n"
      "`endif\n"
      ";\nendmodule\n";
  LexContext lex;
  const auto tokens = lex.lex_string(source);
  const auto context = format::parseSyntaxContext(lex.source_text(), tokens);
  for (const auto& token : tokens) {
    if (token.kind == slang::parsing::TokenKind::OpenBracket) {
      const auto role = context.lookup(token);
      EXPECT_EQ(role.availability, format::SyntaxAvailability::kAmbiguous);
      return;
    }
  }
  FAIL() << "selection token not found";
}

}  // namespace
