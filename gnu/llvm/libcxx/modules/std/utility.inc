// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [utility.swap], swap
  using std::swap;

  // [utility.exchange], exchange
  using std::exchange;

  // [forward], forward/move
  using std::forward;
#if _LIBCPP_STD_VER >= 23
  using std::forward_like;
#endif
  using std::move;
  using std::move_if_noexcept;

  // [utility.as.const], as_const
  using std::as_const;

  // [declval], declval
  using std::declval;

  // [utility.intcmp], integer comparison functions
  using std::cmp_equal;
  using std::cmp_not_equal;

  using std::cmp_greater;
  using std::cmp_greater_equal;
  using std::cmp_less;
  using std::cmp_less_equal;

  using std::in_range;

#if _LIBCPP_STD_VER >= 23
  // [utility.underlying], to_underlying
  using std::to_underlying;

  // [utility.unreachable], unreachable
  using std::unreachable;
#endif // _LIBCPP_STD_VER >= 23

  // [intseq], compile-time integer sequences
  using std::index_sequence;
  using std::integer_sequence;

  using std::make_index_sequence;
  using std::make_integer_sequence;

  using std::index_sequence_for;

  // [pairs], class template pair
  using std::pair;

#if _LIBCPP_STD_VER >= 23
  using std::basic_common_reference;
  using std::common_type;
#endif
  // [pairs.spec], pair specialized algorithms
  using std::operator==;
  using std::operator<=>;

  using std::make_pair;

  // [pair.astuple], tuple-like access to pair
  using std::tuple_element;
  using std::tuple_size;

  using std::get;

  // [pair.piecewise], pair piecewise construction
  using std::piecewise_construct;
  using std::piecewise_construct_t;

  // in-place construction
  using std::in_place;
  using std::in_place_t;

  using std::in_place_type;
  using std::in_place_type_t;

  using std::in_place_index;
  using std::in_place_index_t;

  // [depr.relops]
  namespace rel_ops {
    using rel_ops::operator!=;
    using rel_ops::operator>;
    using rel_ops::operator<=;
    using rel_ops::operator>=;
  } // namespace rel_ops
} // namespace std
