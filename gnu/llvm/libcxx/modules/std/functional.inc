// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [func.invoke], invoke
  using std::invoke;
#if _LIBCPP_STD_VER >= 23
  using std::invoke_r;
#endif

  // [refwrap], reference_wrapper
  using std::reference_wrapper;

  using std::cref;
  using std::ref;

  // [arithmetic.operations], arithmetic operations
  using std::divides;
  using std::minus;
  using std::modulus;
  using std::multiplies;
  using std::negate;
  using std::plus;
  // [comparisons], comparisons
  using std::equal_to;
  using std::greater;
  using std::greater_equal;
  using std::less;
  using std::less_equal;
  using std::not_equal_to;

  // [comparisons.three.way], class compare_three_way
  using std::compare_three_way;

  // [logical.operations], logical operations
  using std::logical_and;
  using std::logical_not;
  using std::logical_or;

  // [bitwise.operations], bitwise operations
  using std::bit_and;
  using std::bit_not;
  using std::bit_or;
  using std::bit_xor;

  // [func.identity], identity
  using std::identity;

  // [func.not.fn], function template not_fn
  using std::not_fn;

  // [func.bind.partial], function templates bind_front and bind_back
  using std::bind_front;
#if _LIBCPP_STD_VER >= 23
  using std::bind_back;
#endif

  // [func.bind], bind
  using std::is_bind_expression;
  using std::is_bind_expression_v;
  using std::is_placeholder;
  using std::is_placeholder_v;

  using std::bind;

  namespace placeholders {
    // M is the implementation-defined number of placeholders
    using std::placeholders::_1;
    using std::placeholders::_10;
    using std::placeholders::_2;
    using std::placeholders::_3;
    using std::placeholders::_4;
    using std::placeholders::_5;
    using std::placeholders::_6;
    using std::placeholders::_7;
    using std::placeholders::_8;
    using std::placeholders::_9;
  } // namespace placeholders

  // [func.memfn], member function adaptors
  using std::mem_fn;

  // [func.wrap], polymorphic function wrappers
  using std::bad_function_call;

  using std::function;

  using std::swap;

  using std::operator==;

  // [func.wrap.move], move only wrapper
  // using std::move_only_function;

  // [func.search], searchers
  using std::default_searcher;

  using std::boyer_moore_searcher;

  using std::boyer_moore_horspool_searcher;

  // [unord.hash], class template hash
  using std::hash;

  namespace ranges {
    // [range.cmp], concept-constrained comparisons
    using std::ranges::equal_to;
    using std::ranges::greater;
    using std::ranges::greater_equal;
    using std::ranges::less;
    using std::ranges::less_equal;
    using std::ranges::not_equal_to;
  } // namespace ranges
} // namespace std
