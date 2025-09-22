//===-- sanitizer_region_test.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_range.h"

#include <algorithm>

#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_common.h"

namespace __sanitizer {

class SanitizerCommon
    : public testing::TestWithParam<std::tuple<
          std::vector<Range>, std::vector<Range>, std::vector<Range>>> {};

TEST_P(SanitizerCommon, Intersect) {
  {
    InternalMmapVector<Range> output;
    Intersect(std::get<0>(GetParam()), std::get<1>(GetParam()), output);
    EXPECT_EQ(std::get<2>(GetParam()),
              std::vector<Range>(output.begin(), output.end()));
  }
  {
    InternalMmapVector<Range> output;
    Intersect(std::get<1>(GetParam()), std::get<0>(GetParam()), output);
    EXPECT_EQ(std::get<2>(GetParam()),
              std::vector<Range>(output.begin(), output.end()));
  }
}

static void PrintTo(const Range &r, std::ostream *os) {
  *os << "[" << r.begin << ", " << r.end << ")";
}

static const std::tuple<std::vector<Range>, std::vector<Range>,
                        std::vector<Range>>
    kTests[] = {
        {{}, {}, {}},
        {{{100, 1000}}, {{5000, 10000}}, {}},
        {{{100, 1000}, {200, 2000}}, {{5000, 10000}, {6000, 11000}}, {}},
        {{{100, 1000}}, {{100, 1000}}, {{100, 1000}}},
        {{{100, 1000}}, {{50, 150}}, {{100, 150}}},
        {{{100, 1000}}, {{150, 250}}, {{150, 250}}},
        {{{100, 1000}, {100, 1000}}, {{100, 1000}}, {{100, 1000}}},
        {{{100, 1000}}, {{500, 1500}}, {{500, 1000}}},
        {{{100, 200}}, {{200, 300}, {1, 1000}}, {{100, 200}}},
        {{{100, 200}, {200, 300}}, {{100, 300}}, {{100, 300}}},
        {{{100, 200}, {200, 300}, {300, 400}}, {{150, 350}}, {{150, 350}}},
        {{{100, 200}, {300, 400}, {500, 600}},
         {{0, 1000}},
         {{100, 200}, {300, 400}, {500, 600}}},
};

INSTANTIATE_TEST_SUITE_P(SanitizerCommonEmpty, SanitizerCommon,
                         testing::ValuesIn(kTests));

}  // namespace __sanitizer
