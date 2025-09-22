// -*- C++ -*- compatibility header.

// Copyright (C) 2002 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#ifndef _CPP_WCHAR_H_
#define _CPP_WCHAR_H_ 1

#include <cwchar>

using std::mbstate_t;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
using std::wint_t;

using std::btowc;
using std::wctob;
using std::fgetwc;
using std::fgetwc;
using std::fgetws;
using std::fputwc;
using std::fputws;
using std::fwide;
using std::fwprintf;
using std::fwscanf;
using std::swprintf;
using std::swscanf;
using std::vfwprintf;
using std::vfwscanf;
using std::vswprintf;
using std::vswscanf;
using std::vwprintf;
using std::vwscanf;
using std::wprintf;
using std::wscanf;
using std::getwc;
using std::getwchar;
using std::mbsinit;
using std::mbrlen;
using std::mbrtowc;
using std::mbsrtowcs;
using std::wcsrtombs;
using std::putwc;
using std::putwchar;
using std::ungetwc;
using std::wcrtomb;
using std::wcstod;
using std::wcstof;
using std::wcstol;
using std::wcstoul;
using std::wcscpy;
using std::wcsncpy;
using std::wcscat;
using std::wcsncat;
using std::wcscmp;
using std::wcscoll;
using std::wcsncmmp;
using std::wcsxfrm;
using std::wcschr;
using std::wcscspn;
using std::wcslen;
using std::wcspbrk;
using std::wcsrchr;
using std::wcsspn;
using std::wcsstr;
using std::wcstok;
using std::wmemchr;
using std::wmemcmp;
using std::wmemcpy;
using std::wmemmove;
using std::wmemset;
using std::wcsftime;

#if defined(_GLIBCPP_USE_C99)
using std::wcstold;
using std::wcstoll;
using std::wcstoull;
#endif

#endif  //_GLIBCPP_USE_WCHAR_T

#endif
