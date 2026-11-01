#include "pipeline/alignment_layout.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {
using format::AlignmentPolicy;
using format::alignment::applyPolicy;
using format::alignment::calculateLayout;
using format::alignment::FormatSpacing;
using format::alignment::Group;
using format::alignment::PreserveSource;
using format::alignment::Row;

constexpr size_t kColumnLimit = 100;
constexpr size_t kNarrowColumnLimit = 5;

// Two explicitly supplied cells per row. Source offsets are independent of
// any parser and point into the caller-owned source slice.
auto makeGroup(std::string_view source,
               const std::array<std::string_view, 2>& types) -> Group {
  Group group{.rows = {}, .original_text = source};
  size_t position = 0;
  for (const auto type : types) {
    const auto type_offset = source.find(type, position);
    const auto name_offset =
        source.find_first_of("WT", type_offset + type.size());
    group.rows.push_back(Row{
        .indentation = 2,
        .tokens = {{type, 0, type_offset},
                   {source.substr(name_offset, 1), 1, name_offset}},
        .cells = {{0, 1}, {1, 2}},
    });
    position = name_offset + 1;
  }
  return group;
}

TEST(AlignmentLayoutTest, CalculatesOffsetsAndDetectsOverflow) {
  const auto layout = calculateLayout({3, 5, 2}, {0, 1, 2});
  EXPECT_EQ(layout.offsets, (std::vector<size_t>{0, 4, 11}));
  EXPECT_EQ(layout.width, 13);
  EXPECT_TRUE(calculateLayout({1}, {}).offsets.empty());
  EXPECT_TRUE(calculateLayout({std::numeric_limits<size_t>::max(), 1}, {0, 1})
                  .offsets.empty());
}

TEST(AlignmentPolicyTest, AlignAndFlushLeftUseProvidedCellsAndMinimumSpacing) {
  auto group = makeGroup("int W\ntype T", {"int", "type"});
  const auto aligned =
      applyPolicy(group, AlignmentPolicy::kAlign, kColumnLimit);
  ASSERT_TRUE(aligned);
  if (!aligned) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<FormatSpacing>(*aligned));
  EXPECT_EQ(std::get<FormatSpacing>(*aligned).spaces,
            (std::vector<std::vector<size_t>>{{0, 2}, {0, 1}}));
  const auto flushed = applyPolicy(group, AlignmentPolicy::kFlushLeft, 1);
  ASSERT_TRUE(flushed);
  if (!flushed) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<FormatSpacing>(*flushed));
  EXPECT_EQ(std::get<FormatSpacing>(*flushed).spaces,
            (std::vector<std::vector<size_t>>{{0, 1}, {0, 1}}));
  EXPECT_FALSE(applyPolicy(group, AlignmentPolicy::kAlign, 7));
  EXPECT_EQ(group.rows.at(0).tokens.at(1).min_spaces, 1);
}

TEST(AlignmentPolicyTest, PreserveReturnsExactSourceAndRejectsInvalidOffsets) {
  constexpr std::string_view source = "\tint   W\r\n  type T\n";
  auto group = makeGroup(source, {"int", "type"});
  const auto result = applyPolicy(group, AlignmentPolicy::kPreserve, 1);
  ASSERT_TRUE(result);
  if (!result) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<PreserveSource>(*result));
  EXPECT_EQ(std::get<PreserveSource>(*result).text, source);
  group.rows.at(1).tokens.at(1).source_offset = 0;
  EXPECT_FALSE(applyPolicy(group, AlignmentPolicy::kPreserve, kColumnLimit));
  group.original_text.reset();
  EXPECT_FALSE(applyPolicy(group, AlignmentPolicy::kInfer, kColumnLimit));
}

TEST(AlignmentPolicyTest, InferMatchesVeribleThresholds) {
  auto small = makeGroup("int W\ntype T", {"int", "type"});
  auto result = applyPolicy(small, AlignmentPolicy::kInfer, kColumnLimit);
  ASSERT_TRUE(result);
  if (!result) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<FormatSpacing>(*result));
  EXPECT_EQ(std::get<FormatSpacing>(*result).spaces,
            (std::vector<std::vector<size_t>>{{0, 2}, {0, 1}}));

  auto flush =
      makeGroup("int   W\nsome_long_name T", {"int", "some_long_name"});
  result = applyPolicy(flush, AlignmentPolicy::kInfer, kColumnLimit);
  ASSERT_TRUE(result);
  if (!result) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<FormatSpacing>(*result));
  EXPECT_EQ(std::get<FormatSpacing>(*result).spaces,
            (std::vector<std::vector<size_t>>{{0, 1}, {0, 1}}));
  result = applyPolicy(flush, AlignmentPolicy::kInfer, kNarrowColumnLimit);
  ASSERT_TRUE(result);
  if (!result) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<FormatSpacing>(*result));
  EXPECT_EQ(std::get<FormatSpacing>(*result).spaces,
            (std::vector<std::vector<size_t>>{{0, 1}, {0, 1}}));

  constexpr std::string_view ambiguous = "int    W\nsome_long_name T";
  auto preserve = makeGroup(ambiguous, {"int", "some_long_name"});
  result = applyPolicy(preserve, AlignmentPolicy::kInfer, kColumnLimit);
  ASSERT_TRUE(result);
  if (!result) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<PreserveSource>(*result));
  EXPECT_EQ(std::get<PreserveSource>(*result).text, ambiguous);

  auto align =
      makeGroup("int     W\nsome_long_name T", {"int", "some_long_name"});
  result = applyPolicy(align, AlignmentPolicy::kInfer, kColumnLimit);
  ASSERT_TRUE(result);
  if (!result) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<FormatSpacing>(*result));
  EXPECT_EQ(std::get<FormatSpacing>(*result).spaces,
            (std::vector<std::vector<size_t>>{{0, 12}, {0, 1}}));
}

TEST(AlignmentPolicyTest, PreservesMultilineCommentsAsSourceTokens) {
  constexpr std::string_view source = "logic /* first\n second */ name;\n";
  Group group{
      .rows = {{.indentation = 0,
                .tokens = {{"logic", 0, 0},
                           {"/* first\n second */", 1, source.find("/*")},
                           {"name;", 1, source.find("name;")}},
                .cells = {{0, 2}, {2, 3}}}},
      .original_text = source};
  const auto result = applyPolicy(group, AlignmentPolicy::kPreserve, 1);
  ASSERT_TRUE(result);
  if (!result) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<PreserveSource>(*result));
  EXPECT_EQ(std::get<PreserveSource>(*result).text, source);
  EXPECT_FALSE(applyPolicy(group, AlignmentPolicy::kAlign, kColumnLimit));
}

TEST(AlignmentPolicyTest, EmptySingleInvalidAndMultilineInputs) {
  Group empty;
  EXPECT_TRUE(applyPolicy(empty, AlignmentPolicy::kAlign, 0));
  EXPECT_FALSE(applyPolicy(empty, AlignmentPolicy::kPreserve, 0));
  auto single = makeGroup("int W\ntype T", {"int", "type"});
  single.rows.pop_back();
  const auto result = applyPolicy(single, AlignmentPolicy::kAlign, 1);
  ASSERT_TRUE(result);
  if (!result) {
    return;
  }
  ASSERT_TRUE(std::holds_alternative<FormatSpacing>(*result));
  EXPECT_EQ(std::get<FormatSpacing>(*result).spaces.at(0).at(1), 1);
  single.rows.at(0).cells.at(1) = {0, 2};
  EXPECT_FALSE(applyPolicy(single, AlignmentPolicy::kAlign, kColumnLimit));

  auto multiline = makeGroup("int\n  W\ntype T", {"int", "type"});
  EXPECT_TRUE(applyPolicy(multiline, AlignmentPolicy::kPreserve, kColumnLimit));
  EXPECT_FALSE(applyPolicy(multiline, AlignmentPolicy::kInfer, kColumnLimit));
}
}  // namespace
