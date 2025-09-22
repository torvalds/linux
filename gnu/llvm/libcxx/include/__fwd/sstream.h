//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_SSTREAM_H
#define _LIBCPP___FWD_SSTREAM_H

#include <__config>
#include <__fwd/memory.h>
#include <__fwd/string.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _CharT, class _Traits = char_traits<_CharT>, class _Allocator = allocator<_CharT> >
class _LIBCPP_TEMPLATE_VIS basic_stringbuf;

template <class _CharT, class _Traits = char_traits<_CharT>, class _Allocator = allocator<_CharT> >
class _LIBCPP_TEMPLATE_VIS basic_istringstream;
template <class _CharT, class _Traits = char_traits<_CharT>, class _Allocator = allocator<_CharT> >
class _LIBCPP_TEMPLATE_VIS basic_ostringstream;
template <class _CharT, class _Traits = char_traits<_CharT>, class _Allocator = allocator<_CharT> >
class _LIBCPP_TEMPLATE_VIS basic_stringstream;

using stringbuf     = basic_stringbuf<char>;
using istringstream = basic_istringstream<char>;
using ostringstream = basic_ostringstream<char>;
using stringstream  = basic_stringstream<char>;

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
using wstringbuf     = basic_stringbuf<wchar_t>;
using wistringstream = basic_istringstream<wchar_t>;
using wostringstream = basic_ostringstream<wchar_t>;
using wstringstream  = basic_stringstream<wchar_t>;
#endif

template <class _CharT, class _Traits, class _Allocator>
class _LIBCPP_PREFERRED_NAME(stringbuf) _LIBCPP_IF_WIDE_CHARACTERS(_LIBCPP_PREFERRED_NAME(wstringbuf)) basic_stringbuf;
template <class _CharT, class _Traits, class _Allocator>
class _LIBCPP_PREFERRED_NAME(istringstream)
    _LIBCPP_IF_WIDE_CHARACTERS(_LIBCPP_PREFERRED_NAME(wistringstream)) basic_istringstream;
template <class _CharT, class _Traits, class _Allocator>
class _LIBCPP_PREFERRED_NAME(ostringstream)
    _LIBCPP_IF_WIDE_CHARACTERS(_LIBCPP_PREFERRED_NAME(wostringstream)) basic_ostringstream;
template <class _CharT, class _Traits, class _Allocator>
class _LIBCPP_PREFERRED_NAME(stringstream)
    _LIBCPP_IF_WIDE_CHARACTERS(_LIBCPP_PREFERRED_NAME(wstringstream)) basic_stringstream;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_SSTREAM_H
