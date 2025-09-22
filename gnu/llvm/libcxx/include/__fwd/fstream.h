//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_FSTREAM_H
#define _LIBCPP___FWD_FSTREAM_H

#include <__config>
#include <__fwd/string.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _CharT, class _Traits = char_traits<_CharT> >
class _LIBCPP_TEMPLATE_VIS basic_filebuf;
template <class _CharT, class _Traits = char_traits<_CharT> >
class _LIBCPP_TEMPLATE_VIS basic_ifstream;
template <class _CharT, class _Traits = char_traits<_CharT> >
class _LIBCPP_TEMPLATE_VIS basic_ofstream;
template <class _CharT, class _Traits = char_traits<_CharT> >
class _LIBCPP_TEMPLATE_VIS basic_fstream;

using filebuf  = basic_filebuf<char>;
using ifstream = basic_ifstream<char>;
using ofstream = basic_ofstream<char>;
using fstream  = basic_fstream<char>;

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
using wfilebuf  = basic_filebuf<wchar_t>;
using wifstream = basic_ifstream<wchar_t>;
using wofstream = basic_ofstream<wchar_t>;
using wfstream  = basic_fstream<wchar_t>;
#endif

template <class _CharT, class _Traits>
class _LIBCPP_PREFERRED_NAME(filebuf) _LIBCPP_IF_WIDE_CHARACTERS(_LIBCPP_PREFERRED_NAME(wfilebuf)) basic_filebuf;
template <class _CharT, class _Traits>
class _LIBCPP_PREFERRED_NAME(ifstream) _LIBCPP_IF_WIDE_CHARACTERS(_LIBCPP_PREFERRED_NAME(wifstream)) basic_ifstream;
template <class _CharT, class _Traits>
class _LIBCPP_PREFERRED_NAME(ofstream) _LIBCPP_IF_WIDE_CHARACTERS(_LIBCPP_PREFERRED_NAME(wofstream)) basic_ofstream;
template <class _CharT, class _Traits>
class _LIBCPP_PREFERRED_NAME(fstream) _LIBCPP_IF_WIDE_CHARACTERS(_LIBCPP_PREFERRED_NAME(wfstream)) basic_fstream;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_FSTREAM_H
