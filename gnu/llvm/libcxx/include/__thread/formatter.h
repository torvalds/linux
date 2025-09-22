// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___THREAD_FORMATTER_H
#define _LIBCPP___THREAD_FORMATTER_H

#include <__concepts/arithmetic.h>
#include <__config>
#include <__format/concepts.h>
#include <__format/format_parse_context.h>
#include <__format/formatter.h>
#include <__format/formatter_integral.h>
#include <__format/parser_std_format_spec.h>
#include <__thread/id.h>
#include <__type_traits/conditional.h>
#include <__type_traits/is_pointer.h>
#include <__type_traits/is_same.h>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 23

_LIBCPP_BEGIN_NAMESPACE_STD

#  ifndef _LIBCPP_HAS_NO_THREADS

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<__thread_id, _CharT> {
public:
  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return __parser_.__parse(__ctx, __format_spec::__fields_fill_align_width);
  }

  template <class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator format(__thread_id __id, _FormatContext& __ctx) const {
    // In __thread/support/pthread.h, __libcpp_thread_id is either a
    // unsigned long long or a pthread_t.
    //
    // The type of pthread_t is left unspecified in POSIX so it can be any
    // type. The most logical types are an integral or pointer.
    // On Linux systems pthread_t is an unsigned long long.
    // On Apple systems pthread_t is a pointer type.
    //
    // Note the output should match what the stream operator does. Since
    // the ostream operator has been shipped years before this formatter
    // was added to the Standard, this formatter does what the stream
    // operator does. This may require platform specific changes.

    using _Tp = decltype(__get_underlying_id(__id));
    using _Cp = conditional_t<integral<_Tp>, _Tp, conditional_t<is_pointer_v<_Tp>, uintptr_t, void>>;
    static_assert(!is_same_v<_Cp, void>, "unsupported thread::id type, please file a bug report");

    __format_spec::__parsed_specifications<_CharT> __specs = __parser_.__get_parsed_std_specifications(__ctx);
    if constexpr (is_pointer_v<_Tp>) {
      __specs.__std_.__alternate_form_ = true;
      __specs.__std_.__type_           = __format_spec::__type::__hexadecimal_lower_case;
    }
    return __formatter::__format_integer(reinterpret_cast<_Cp>(__get_underlying_id(__id)), __ctx, __specs);
  }

  __format_spec::__parser<_CharT> __parser_{.__alignment_ = __format_spec::__alignment::__right};
};

#  endif // !_LIBCPP_HAS_NO_THREADS

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 23

#endif // _LIBCPP___THREAD_FORMATTER_H
