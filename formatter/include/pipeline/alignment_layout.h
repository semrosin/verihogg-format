#pragma once

#include <cstddef>
#include <vector>

namespace format::alignment {

// A parser-independent table calculation. Gaps are indexed by column, with
// the first gap unused. Widths do not include the gap before a cell.
struct Layout {
  std::vector<size_t> offsets;
  size_t width = 0;
};

[[nodiscard]] auto calculateLayout(const std::vector<size_t> &widths,
                                   const std::vector<size_t> &gaps) -> Layout;

} // namespace format::alignment
