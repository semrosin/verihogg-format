#include "pipeline/alignment_layout.h"

#include <limits>

namespace format::alignment {

namespace {

[[nodiscard]] auto add(size_t a, size_t b, size_t &result) -> bool {
  if (b > std::numeric_limits<size_t>::max() - a) {
    return false;
  }
  result = a + b;
  return true;
}

} // namespace

auto calculateLayout(const std::vector<size_t> &widths,
                     const std::vector<size_t> &gaps) -> Layout {
  if (widths.size() != gaps.size()) {
    return {};
  }
  Layout result{.offsets = std::vector<size_t>(widths.size(), 0), .width = 0};
  for (size_t i = 1; i < widths.size(); ++i) {
    size_t previous_end = 0;
    if (!add(result.offsets.at(i - 1), widths.at(i - 1), previous_end) ||
        !add(previous_end, gaps.at(i), result.offsets.at(i))) {
      return {};
    }
  }
  if (!widths.empty() &&
      !add(result.offsets.back(), widths.back(), result.width)) {
    return {};
  }
  return result;
}

} // namespace format::alignment
