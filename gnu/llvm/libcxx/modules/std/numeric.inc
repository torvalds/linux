// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [accumulate], accumulate
  using std::accumulate;

  // [reduce], reduce
  using std::reduce;

  // [inner.product], inner product
  using std::inner_product;

  // [transform.reduce], transform reduce
  using std::transform_reduce;

  // [partial.sum], partial sum
  using std::partial_sum;

  // [exclusive.scan], exclusive scan
  using std::exclusive_scan;

  // [inclusive.scan], inclusive scan
  using std::inclusive_scan;

  // [transform.exclusive.scan], transform exclusive scan
  using std::transform_exclusive_scan;

  // [transform.inclusive.scan], transform inclusive scan
  using std::transform_inclusive_scan;

  // [adjacent.difference], adjacent difference
  using std::adjacent_difference;

  // [numeric.iota], iota
  using std::iota;

  namespace ranges {
    // using std::ranges::iota_result;
    // using std::ranges::iota;
  } // namespace ranges

  // [numeric.ops.gcd], greatest common divisor
  using std::gcd;

  // [numeric.ops.lcm], least common multiple
  using std::lcm;

  // [numeric.ops.midpoint], midpoint
  using std::midpoint;

#if _LIBCPP_STD_VER >= 26
  // [numeric.sat], saturation arithmetic
  using std::add_sat;
  using std::div_sat;
  using std::mul_sat;
  using std::saturate_cast;
  using std::sub_sat;
#endif

} // namespace std
