// TR1 code repetition for bind -*- C++ -*-

// Copyright (C) 2005 Free Software Foundation, Inc.
// Written by Douglas Gregor <doug.gregor -at- gmail.com>
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
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/** @file tr1/bind_repeat.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _GLIBCXX_BIND_REPEAT_HEADER
#  error Internal error: _GLIBCXX_BIND_REPEAT_HEADER must be set
#endif /* _GLIBCXX_BIND_REPEAT_HEADER */

#define _GLIBCXX_BIND_NUM_ARGS 0
#define _GLIBCXX_BIND_COMMA
#define _GLIBCXX_BIND_TEMPLATE_PARAMS
#define _GLIBCXX_BIND_TEMPLATE_ARGS
#define _GLIBCXX_BIND_PARAMS
#define _GLIBCXX_BIND_ARGS
#  include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

#define _GLIBCXX_BIND_NUM_ARGS 1
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1
#define _GLIBCXX_BIND_PARAMS _U1& __u1
#define _GLIBCXX_BIND_ARGS __u1
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS
#define _GLIBCXX_BIND_NUM_ARGS 2
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1, typename _U2
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1, _U2
#define _GLIBCXX_BIND_PARAMS _U1& __u1, _U2& __u2
#define _GLIBCXX_BIND_ARGS __u1, __u2
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

#define _GLIBCXX_BIND_NUM_ARGS 3
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1, typename _U2, typename _U3
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1, _U2, _U3
#define _GLIBCXX_BIND_PARAMS _U1& __u1, _U2& __u2, _U3& __u3
#define _GLIBCXX_BIND_ARGS __u1, __u2, __u3
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

#define _GLIBCXX_BIND_NUM_ARGS 4
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1, typename _U2, typename _U3, typename _U4
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1, _U2, _U3, _U4
#define _GLIBCXX_BIND_PARAMS _U1& __u1, _U2& __u2, _U3& __u3, _U4& __u4
#define _GLIBCXX_BIND_ARGS __u1, __u2, __u3, __u4
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

#define _GLIBCXX_BIND_NUM_ARGS 5
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1, typename _U2, typename _U3, typename _U4, typename _U5
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1, _U2, _U3, _U4, _U5
#define _GLIBCXX_BIND_PARAMS _U1& __u1, _U2& __u2, _U3& __u3, _U4& __u4, _U5& __u5
#define _GLIBCXX_BIND_ARGS __u1, __u2, __u3, __u4, __u5
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

#define _GLIBCXX_BIND_NUM_ARGS 6
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1, _U2, _U3, _U4, _U5, _U6
#define _GLIBCXX_BIND_PARAMS _U1& __u1, _U2& __u2, _U3& __u3, _U4& __u4, _U5& __u5, _U6& __u6
#define _GLIBCXX_BIND_ARGS __u1, __u2, __u3, __u4, __u5, __u6
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

#define _GLIBCXX_BIND_NUM_ARGS 7
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6, typename _U7
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1, _U2, _U3, _U4, _U5, _U6, _U7
#define _GLIBCXX_BIND_PARAMS _U1& __u1, _U2& __u2, _U3& __u3, _U4& __u4, _U5& __u5, _U6& __u6, _U7& __u7
#define _GLIBCXX_BIND_ARGS __u1, __u2, __u3, __u4, __u5, __u6, __u7
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

#define _GLIBCXX_BIND_NUM_ARGS 8
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6, typename _U7, typename _U8
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1, _U2, _U3, _U4, _U5, _U6, _U7, _U8
#define _GLIBCXX_BIND_PARAMS _U1& __u1, _U2& __u2, _U3& __u3, _U4& __u4, _U5& __u5, _U6& __u6, _U7& __u7, _U8& __u8
#define _GLIBCXX_BIND_ARGS __u1, __u2, __u3, __u4, __u5, __u6, __u7, __u8
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

#define _GLIBCXX_BIND_NUM_ARGS 9
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6, typename _U7, typename _U8, typename _U9
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1, _U2, _U3, _U4, _U5, _U6, _U7, _U8, _U9
#define _GLIBCXX_BIND_PARAMS _U1& __u1, _U2& __u2, _U3& __u3, _U4& __u4, _U5& __u5, _U6& __u6, _U7& __u7, _U8& __u8, _U9& __u9
#define _GLIBCXX_BIND_ARGS __u1, __u2, __u3, __u4, __u5, __u6, __u7, __u8, __u9
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

#define _GLIBCXX_BIND_NUM_ARGS 10
#define _GLIBCXX_BIND_COMMA ,
#define _GLIBCXX_BIND_TEMPLATE_PARAMS typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6, typename _U7, typename _U8, typename _U9, typename _U10
#define _GLIBCXX_BIND_TEMPLATE_ARGS _U1, _U2, _U3, _U4, _U5, _U6, _U7, _U8, _U9, _U10
#define _GLIBCXX_BIND_PARAMS _U1& __u1, _U2& __u2, _U3& __u3, _U4& __u4, _U5& __u5, _U6& __u6, _U7& __u7, _U8& __u8, _U9& __u9, _U10& __u10
#define _GLIBCXX_BIND_ARGS __u1, __u2, __u3, __u4, __u5, __u6, __u7, __u8, __u9, __u10
#include _GLIBCXX_BIND_REPEAT_HEADER
#undef _GLIBCXX_BIND_ARGS
#undef _GLIBCXX_BIND_PARAMS
#undef _GLIBCXX_BIND_TEMPLATE_ARGS
#undef _GLIBCXX_BIND_TEMPLATE_PARAMS
#undef _GLIBCXX_BIND_COMMA
#undef _GLIBCXX_BIND_NUM_ARGS

