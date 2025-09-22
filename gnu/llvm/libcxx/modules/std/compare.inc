// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {

  // [cmp.categories], comparison category types
  using std::partial_ordering;
  using std::strong_ordering;
  using std::weak_ordering;

  // named comparison functions
  using std::is_eq;
  using std::is_gt;
  using std::is_gteq;
  using std::is_lt;
  using std::is_lteq;
  using std::is_neq;

  // [cmp.common], common comparison category type
  using std::common_comparison_category;
  using std::common_comparison_category_t;

  // [cmp.concept], concept three_way_comparable
  using std::three_way_comparable;
  using std::three_way_comparable_with;

  // [cmp.result], result of three-way comparison
  using std::compare_three_way_result;

  using std::compare_three_way_result_t;

  // [comparisons.three.way], class compare_three_way
  using std::compare_three_way;

  // [cmp.alg], comparison algorithms
  inline namespace __cpo {
    using std::__cpo::compare_partial_order_fallback;
    using std::__cpo::compare_strong_order_fallback;
    using std::__cpo::compare_weak_order_fallback;
    using std::__cpo::partial_order;
    using std::__cpo::strong_order;
    using std::__cpo::weak_order;
  } // namespace __cpo

} // namespace std
