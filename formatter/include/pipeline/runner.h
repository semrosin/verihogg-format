#pragma once
#include <filesystem>
#include <functional>
#include <gsl/span>
#include <ostream>

#include "data/format_style.h"

namespace format {

struct Streams {
  std::ostream* out;
  std::ostream* err;
};

// Resolves the style for a source file. Called once per file.
using StyleProvider = std::function<FormatStyle(const std::filesystem::path&)>;

auto runFormatter(gsl::span<const std::filesystem::path> files,
                  const StyleProvider& style, const RunConfig& run,
                  Streams streams) -> int;
}  // namespace format
