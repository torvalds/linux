// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_CONTAINER_ADAPTOR_H
#define _LIBCPP___FORMAT_CONTAINER_ADAPTOR_H

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#include <__config>
#include <__format/concepts.h>
#include <__format/formatter.h>
#include <__format/range_default_formatter.h>
#include <__fwd/queue.h>
#include <__fwd/stack.h>
#include <__ranges/ref_view.h>
#include <__type_traits/is_const.h>
#include <__type_traits/maybe_const.h>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

// [container.adaptors.format] only specifies the library should provide the
// formatter specializations, not which header should provide them.
// Since <format> includes a lot of headers, add these headers here instead of
// adding more dependencies like, locale, optinal, string, tuple, etc. to the
// adaptor headers. To use the format functions users already include <format>.

template <class _Adaptor, class _CharT>
struct _LIBCPP_TEMPLATE_VIS __formatter_container_adaptor {
private:
  using __maybe_const_container = __fmt_maybe_const<typename _Adaptor::container_type, _CharT>;
  using __maybe_const_adaptor   = __maybe_const<is_const_v<__maybe_const_container>, _Adaptor>;
  formatter<ranges::ref_view<__maybe_const_container>, _CharT> __underlying_;

public:
  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return __underlying_.parse(__ctx);
  }

  template <class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator
  format(__maybe_const_adaptor& __adaptor, _FormatContext& __ctx) const {
    return __underlying_.format(__adaptor.__get_container(), __ctx);
  }
};

template <class _CharT, class _Tp, formattable<_CharT> _Container>
struct _LIBCPP_TEMPLATE_VIS formatter<queue<_Tp, _Container>, _CharT>
    : public __formatter_container_adaptor<queue<_Tp, _Container>, _CharT> {};

template <class _CharT, class _Tp, class _Container, class _Compare>
struct _LIBCPP_TEMPLATE_VIS formatter<priority_queue<_Tp, _Container, _Compare>, _CharT>
    : public __formatter_container_adaptor<priority_queue<_Tp, _Container, _Compare>, _CharT> {};

template <class _CharT, class _Tp, formattable<_CharT> _Container>
struct _LIBCPP_TEMPLATE_VIS formatter<stack<_Tp, _Container>, _CharT>
    : public __formatter_container_adaptor<stack<_Tp, _Container>, _CharT> {};

#endif //_LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_CONTAINER_ADAPTOR_H
