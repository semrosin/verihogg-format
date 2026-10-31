#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "data/format_style.h"

namespace format::alignment {

// A parser-independent table calculation. Gaps are indexed by column, with
// the first gap unused. Widths do not include the gap before a cell.
struct Layout {
  std::vector<size_t> offsets;
  size_t width = 0;
};

[[nodiscard]] auto calculateLayout(const std::vector<size_t>& widths,
                                   const std::vector<size_t>& gaps) -> Layout;

struct Token {
  std::string_view text;
  size_t min_spaces = 0;
  // Offset within Group::original_text; required for preserve and infer.
  std::optional<size_t> source_offset;
};

struct Cell {
  size_t begin = 0;
  size_t end = 0;  // exclusive
};

struct Row {
  size_t indentation = 0;
  std::vector<Token> tokens;
  // Ordered, non-empty ranges covering all tokens in this row.
  std::vector<Cell> cells;
};

struct Group {
  std::vector<Row> rows;
  // Exact source range containing the tokens, comments included as tokens.
  std::optional<std::string_view> original_text;
};

struct Result {
  AlignmentPolicy policy = AlignmentPolicy::kFlushLeft;
  // Minimum or aligned spaces before each token, indexed by row and token.
  std::vector<std::vector<size_t>> spaces;
  // Present when preserving; the caller must output this exact source range.
  std::optional<std::string> preserved_text;
};

// Returns no result for invalid inputs, unavailable source, unsupported source
// whitespace in infer, or alignment exceeding the limit. No input is modified.
[[nodiscard]] auto applyPolicy(const Group& group, AlignmentPolicy policy,
                               size_t column_limit) -> std::optional<Result>;

}  // namespace format::alignment
