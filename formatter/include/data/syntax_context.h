#pragma once

#include <slang/parsing/Token.h>

#include <cstddef>
#include <cstdint>
#include <gsl/span>
#include <string_view>
#include <unordered_map>

namespace format {

// Parser-derived facts for one token in the original source buffer.
enum class SyntaxAvailability : uint8_t { kUnknown, kKnown, kAmbiguous };

struct TokenSyntaxContext {
  SyntaxAvailability availability = SyntaxAvailability::kUnknown;
  bool in_selection = false;
  bool typedef_class_parameter = false;
};

class SyntaxContext {
 public:
  [[nodiscard]] auto lookup(slang::parsing::Token token) const
      -> TokenSyntaxContext;

 private:
  friend auto parseSyntaxContext(std::string_view source,
                                 gsl::span<const slang::parsing::Token> tokens)
      -> SyntaxContext;
  std::unordered_map<size_t, TokenSyntaxContext> tokens_;
  slang::BufferID source_buffer_;
};

// Parse the original source without expanding external include files. Facts
// from all conditional configurations must agree; unsupported or ambiguous
// contexts remain unavailable and must not drive syntax-dependent formatting.
[[nodiscard]] auto parseSyntaxContext(
    std::string_view source, gsl::span<const slang::parsing::Token> tokens)
    -> SyntaxContext;

}  // namespace format
