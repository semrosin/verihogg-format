#pragma once

#include <slang/parsing/Token.h>

#include <gsl/span>
#include <string>
#include <string_view>
#include <vector>

#include "data/format_style.h"
#include "data/format_warning.h"

namespace format {
struct FormatResult {
  std::string formatted_text;
  std::vector<FormatWarning> warnings;
};

// Pass the exact LexContext::source_text() for syntax-dependent formatting and
// byte-exact recovery. Without it, syntax-dependent spacing is not applied.
auto format(gsl::span<const slang::parsing::Token> tokens, FormatStyle style,
            std::string_view original_source) -> FormatResult;
}  // namespace format
