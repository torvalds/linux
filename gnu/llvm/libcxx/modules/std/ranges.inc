// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  namespace ranges {
    inline namespace __cpo {
      // [range.access], range access
      using std::ranges::__cpo::begin;
      using std::ranges::__cpo::cbegin;
      using std::ranges::__cpo::cend;
      using std::ranges::__cpo::crbegin;
      using std::ranges::__cpo::crend;
      using std::ranges::__cpo::end;
      using std::ranges::__cpo::rbegin;
      using std::ranges::__cpo::rend;

      using std::ranges::__cpo::cdata;
      using std::ranges::__cpo::data;
      using std::ranges::__cpo::empty;
      using std::ranges::__cpo::size;
      using std::ranges::__cpo::ssize;
    } // namespace __cpo

    // [range.range], ranges
    using std::ranges::range;

    using std::ranges::enable_borrowed_range;

    using std::ranges::borrowed_range;

    // using std::ranges::const_iterator_t;
    // using std::ranges::const_sentinel_t;
    using std::ranges::iterator_t;
    // using std::ranges::range_const_reference_t;
    using std::ranges::range_common_reference_t;
    using std::ranges::range_difference_t;
    using std::ranges::range_reference_t;
    using std::ranges::range_rvalue_reference_t;
    using std::ranges::range_size_t;
    using std::ranges::range_value_t;
    using std::ranges::sentinel_t;

    // [range.sized], sized ranges
    using std::ranges::disable_sized_range;
    using std::ranges::sized_range;

    // [range.view], views
    using std::ranges::enable_view;
    using std::ranges::view;
    using std::ranges::view_base;

    // [range.refinements], other range refinements
    using std::ranges::bidirectional_range;
    using std::ranges::common_range;
    // using std::ranges::constant_range;
    using std::ranges::contiguous_range;
    using std::ranges::forward_range;
    using std::ranges::input_range;
    using std::ranges::output_range;
    using std::ranges::random_access_range;
    using std::ranges::viewable_range;

    // [view.interface], class template view_­interface
    using std::ranges::view_interface;

    // [range.subrange], sub-ranges
    using std::ranges::subrange;
    using std::ranges::subrange_kind;

    using std::ranges::get;
  } // namespace ranges

  using std::ranges::get;

  namespace ranges {

    // [range.dangling], dangling iterator handling
    using std::ranges::dangling;

    // [range.elementsof], class template elements_­of
    // using std::ranges::elements_of;

    using std::ranges::borrowed_iterator_t;

    using std::ranges::borrowed_subrange_t;

#if _LIBCPP_STD_VER >= 23
    // [range.utility.conv], range conversions
    using std::ranges::to;
#endif

    // [range.empty], empty view
    using std::ranges::empty_view;

    namespace views {
      using std::ranges::views::empty;
    }

    // [range.single], single view
    using std::ranges::single_view;

    namespace views {
      using std::ranges::views::single;
    } // namespace views

    // [range.iota], iota view
    using std::ranges::iota_view;

    namespace views {
      using std::ranges::views::iota;
    } // namespace views

#if _LIBCPP_STD_VER >= 23
    // [range.repeat], repeat view
    using std::ranges::repeat_view;

    namespace views {
      using std::ranges::views::repeat;
    } // namespace views
#endif // _LIBCPP_STD_VER >= 23

#ifndef _LIBCPP_HAS_NO_LOCALIZATION
    // [range.istream], istream view
    using std::ranges::basic_istream_view;
    using std::ranges::istream_view;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    using std::ranges::wistream_view;
#  endif

    namespace views {
      using std::ranges::views::istream;
    }
#endif // _LIBCPP_HAS_NO_LOCALIZATION

    // Note: This declaration not in the synopsis or explicitly in the wording.
    // However it is needed for the range adaptors.
    // [range.adaptor.object]/3
    //   The template parameter D for range_adaptor_closure may be an
    //   incomplete type. If an expression of type cv D is used as an operand
    //   to the | operator, D shall be complete and model
    //   derived_from<range_adaptor_closure<D>>. The behavior of an expression
    //   involving an object of type cv D as an operand to the | operator is
    //   undefined if overload resolution selects a program-defined operator|
    //   function.
    // This is used internally in C++20 mode.
    using std::ranges::operator|;
#if _LIBCPP_STD_VER >= 23
    // [range.adaptor.object], range adaptor objects
    using std::ranges::range_adaptor_closure;
#endif

    // [range.all], all view
    namespace views {
      using std::ranges::views::all;
      using std::ranges::views::all_t;
    } // namespace views

    // [range.ref.view], ref view
    using std::ranges::ref_view;

    // [range.owning.view], owning view
    using std::ranges::owning_view;

#if _LIBCPP_STD_VER >= 23
    // [range.as.rvalue], as rvalue view
    using std::ranges::as_rvalue_view;

    namespace views {
      using std::ranges::views::as_rvalue;
    } // namespace views
#endif // _LIBCPP_STD_VER >= 23

    // [range.filter], filter view
    using std::ranges::filter_view;

    namespace views {
      using std::ranges::views::filter;
    } // namespace views

    // [range.transform], transform view
    using std::ranges::transform_view;

    namespace views {
      using std::ranges::views::transform;
    } // namespace views

    // [range.take], take view
    using std::ranges::take_view;

    namespace views {
      using std::ranges::views::take;
    } // namespace views

    // [range.take.while], take while view
    using std::ranges::take_while_view;

    namespace views {
      using std::ranges::views::take_while;
    } // namespace views

    // [range.drop], drop view
    using std::ranges::drop_view;

    namespace views {
      using std::ranges::views::drop;
    } // namespace views

    // [range.drop.while], drop while view
    using std::ranges::drop_while_view;

    namespace views {
      using std::ranges::views::drop_while;
    } // namespace views

    using std::ranges::join_view;

    namespace views {
      using std::ranges::views::join;
    } // namespace views
#if 0
    using std::ranges::join_with_view;

    namespace views {
      using std::ranges::views::join_with;
    } // namespace views
#endif
    using std::ranges::lazy_split_view;

    // [range.split], split view
    using std::ranges::split_view;

    namespace views {
      using std::ranges::views::lazy_split;
      using std::ranges::views::split;
    } // namespace views

    // [range.counted], counted view
    namespace views {
      using std::ranges::views::counted;
    } // namespace views

    // [range.common], common view
    using std::ranges::common_view;

    namespace views {
      using std::ranges::views::common;
    } // namespace views

    // [range.reverse], reverse view
    using std::ranges::reverse_view;

    namespace views {
      using std::ranges::views::reverse;
    } // namespace views

    // [range.as.const], as const view
#if 0
    using std::ranges::as_const_view;

    namespace views {
      using std::ranges::views::as_const;
    } // namespace views
#endif
    // [range.elements], elements view
    using std::ranges::elements_view;

    using std::ranges::keys_view;
    using std::ranges::values_view;

    namespace views {
      using std::ranges::views::elements;
      using std::ranges::views::keys;
      using std::ranges::views::values;
    } // namespace views

#if _LIBCPP_STD_VER >= 23
    // [range.zip], zip view
    using std::ranges::zip_view;

    namespace views {
      using std::ranges::views::zip;
    } // namespace views
#endif // _LIBCPP_STD_VER >= 23

#if 0
    // [range.zip.transform], zip transform view
    using std::ranges::zip_transform_view;

    namespace views {
      using std::ranges::views::zip_transform;
    }

    using std::ranges::adjacent_view;

    namespace views {
      using std::ranges::views::adjacent;
      using std::ranges::views::pairwise;
    } // namespace views

    using std::ranges::adjacent_transform_view;

    namespace views {
      using std::ranges::views::adjacent_transform;
      using std::ranges::views::pairwise_transform;
    } // namespace views

    using std::ranges::chunk_view;

    using std::ranges::chunk_view<V>;

    namespace views {
      using std::ranges::views::chunk;
    }

    using std::ranges::slide_view;

    namespace views {
      using std::ranges::views::slide;
    }
#endif

#if _LIBCPP_STD_VER >= 23
    // [range.chunk.by], chunk by view
    using std::ranges::chunk_by_view;

    namespace views {
      using std::ranges::views::chunk_by;
    }
#endif // _LIBCPP_STD_VER >= 23

#if 0
    // [range.stride], stride view
    using std::ranges::stride_view;

    namespace views {
      using std::ranges::views::stride;
    }

    using std::ranges::cartesian_product_view;

    namespace views {
      using std::ranges::views::cartesian_product;
    }
#endif
  } // namespace ranges

  namespace views = ranges::views;

  using std::tuple_element;
  using std::tuple_size;

#if _LIBCPP_STD_VER >= 23
  using std::from_range;
  using std::from_range_t;
#endif // _LIBCPP_STD_VER >= 23
} // namespace std
