// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Do not attempt to use LLVM ostream etc from gtest.
// #define GTEST_NO_LLVM_SUPPORT 1

#include "../nsan.h"
#include "gtest/gtest.h"

#include <cmath>

namespace __nsan {

template <typename FT, auto next> void TestFT() {
  // Basic local tests anchored at 0.0.
  ASSERT_EQ(GetULPDiff<FT>(0.0, 0.0), 0);
  ASSERT_EQ(GetULPDiff<FT>(-0.0, 0.0), 0);
  ASSERT_EQ(GetULPDiff<FT>(next(-0.0, -1.0), 0.0), 1);
  ASSERT_EQ(GetULPDiff<FT>(next(0.0, 1.0), -0.0), 1);
  ASSERT_EQ(GetULPDiff<FT>(next(-0.0, -1.0), next(0.0, 1.0)), 2);
  // Basic local tests anchored at 2.0.
  ASSERT_EQ(GetULPDiff<FT>(next(2.0, 1.0), 2.0), 1);
  ASSERT_EQ(GetULPDiff<FT>(next(2.0, 3.0), 2.0), 1);
  ASSERT_EQ(GetULPDiff<FT>(next(2.0, 1.0), next(2.0, 3.0)), 2);

  ASSERT_NE(GetULPDiff<FT>(-0.01, 0.01), kMaxULPDiff);

  // Basic local tests anchored at a random number.
  const FT X = 4863.5123;
  const FT To = 2 * X;
  FT Y = X;
  ASSERT_EQ(GetULPDiff<FT>(X, Y), 0);
  ASSERT_EQ(GetULPDiff<FT>(-X, -Y), 0);
  Y = next(Y, To);
  ASSERT_EQ(GetULPDiff<FT>(X, Y), 1);
  ASSERT_EQ(GetULPDiff<FT>(-X, -Y), 1);
  Y = next(Y, To);
  ASSERT_EQ(GetULPDiff<FT>(X, Y), 2);
  ASSERT_EQ(GetULPDiff<FT>(-X, -Y), 2);
  Y = next(Y, To);
  ASSERT_EQ(GetULPDiff<FT>(X, Y), 3);
  ASSERT_EQ(GetULPDiff<FT>(-X, -Y), 3);

  // Values with larger differences.
  static constexpr const __sanitizer::u64 MantissaSize =
      __sanitizer::u64{1} << FTInfo<FT>::kMantissaBits;
  ASSERT_EQ(GetULPDiff<FT>(1.0, next(2.0, 1.0)), MantissaSize - 1);
  ASSERT_EQ(GetULPDiff<FT>(1.0, 2.0), MantissaSize);
  ASSERT_EQ(GetULPDiff<FT>(1.0, next(2.0, 3.0)), MantissaSize + 1);
  ASSERT_EQ(GetULPDiff<FT>(1.0, 3.0), (3 * MantissaSize) / 2);
}

TEST(NSanTest, Float) { TestFT<float, nextafterf>(); }

TEST(NSanTest, Double) {
  TestFT<double, static_cast<double (*)(double, double)>(nextafter)>();
}

TEST(NSanTest, Float128) {
  // Very basic tests. FIXME: improve when we have nextafter<__float128>.
  ASSERT_EQ(GetULPDiff<__float128>(0.0, 0.0), 0);
  ASSERT_EQ(GetULPDiff<__float128>(-0.0, 0.0), 0);
  ASSERT_NE(GetULPDiff<__float128>(-0.01, 0.01), kMaxULPDiff);
}

} // end namespace __nsan
