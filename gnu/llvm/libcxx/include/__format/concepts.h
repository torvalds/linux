// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_CONCEPTS_H
#define _LIBCPP___FORMAT_CONCEPTS_H

#include <__concepts/same_as.h>
#include <__concepts/semiregular.h>
#include <__config>
#include <__format/format_parse_context.h>
#include <__fwd/format.h>
#include <__fwd/tuple.h>
#include <__tuple/tuple_size.h>
#include <__type_traits/is_specialization.h>
#include <__type_traits/remove_const.h>
#include <__type_traits/remove_reference.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

/// The character type specializations of \ref formatter.
template <class _CharT>
concept __fmt_char_type =
    same_as<_CharT, char>
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    || same_as<_CharT, wchar_t>
#  endif
    ;

// The output iterator isn't specified. A formatter should accept any
// output_iterator. This iterator is a minimal iterator to test the concept.
// (Note testing for (w)format_context would be a valid choice, but requires
// selecting the proper one depending on the type of _CharT.)
template <class _CharT>
using __fmt_iter_for = _CharT*;

template <class _Tp, class _Context, class _Formatter = typename _Context::template formatter_type<remove_const_t<_Tp>>>
concept __formattable_with =
    semiregular<_Formatter> &&
    requires(_Formatter& __f,
             const _Formatter& __cf,
             _Tp&& __t,
             _Context __fc,
             basic_format_parse_context<typename _Context::char_type> __pc) {
      { __f.parse(__pc) } -> same_as<typename decltype(__pc)::iterator>;
      { __cf.format(__t, __fc) } -> same_as<typename _Context::iterator>;
    };

template <class _Tp, class _CharT>
concept __formattable =
    __formattable_with<remove_reference_t<_Tp>, basic_format_context<__fmt_iter_for<_CharT>, _CharT>>;

#  if _LIBCPP_STD_VER >= 23
template <class _Tp, class _CharT>
concept formattable = __formattable<_Tp, _CharT>;

// [tuple.like] defines a tuple-like exposition only concept. This concept is
// not related to that. Therefore it uses a different name for the concept.
//
// TODO FMT Add a test to validate we fail when using that concept after P2165
// has been implemented.
template <class _Tp>
concept __fmt_pair_like =
    __is_specialization_v<_Tp, pair> || (__is_specialization_v<_Tp, tuple> && tuple_size_v<_Tp> == 2);

#  endif //_LIBCPP_STD_VER >= 23
#endif   //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_CONCEPTS_H
