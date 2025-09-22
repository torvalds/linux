// TR1 code repetition -*- C++ -*-

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

/** @file tr1/repeat.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _GLIBCXX_REPEAT_HEADER
#  error Internal error: _GLIBCXX_REPEAT_HEADER must be set
#endif /* _GLIBCXX_REPEAT_HEADER */

#ifndef _GLIBCXX_TUPLE_ALL_TEMPLATE_PARAMS
#  define _GLIBCXX_TUPLE_ALL_TEMPLATE_PARAMS typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6, typename _T7, typename _T8, typename _T9, typename _T10
#  define _GLIBCXX_TUPLE_ALL_TEMPLATE_PARAMS_UNNAMED typename, typename, typename, typename, typename, typename, typename, typename, typename, typename
#  define _GLIBCXX_TUPLE_ALL_TEMPLATE_ARGS _T1, _T2, _T3, _T4, _T5, _T6, _T7, _T8, _T9, _T10
#endif

#define _GLIBCXX_NUM_ARGS 0
#define _GLIBCXX_COMMA
#define _GLIBCXX_TEMPLATE_PARAMS
#define _GLIBCXX_TEMPLATE_ARGS
#define _GLIBCXX_PARAMS
#define _GLIBCXX_REF_PARAMS
#define _GLIBCXX_ARGS
#define _GLIBCXX_COMMA_SHIFTED
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#define _GLIBCXX_PARAMS_SHIFTED
#define _GLIBCXX_ARGS_SHIFTED
#define _GLIBCXX_BIND_MEMBERS_INIT
#define _GLIBCXX_BIND_MEMBERS
#define _GLIBCXX_MU_GET_TUPLE_ARGS
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV)
#define _GLIBCXX_BIND_V_ARGS
#define _GLIBCXX_TUPLE_ADD_CREF
#define _GLIBCXX_TUPLE_COPY_INIT
#define _GLIBCXX_TUPLE_ASSIGN
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#define _GLIBCXX_TEMPLATE_PARAMS_U
#define _GLIBCXX_TEMPLATE_ARGS_U
#define _GLIBCXX_REF_WRAP_PARAMS
#define _GLIBCXX_REF_TEMPLATE_ARGS
#define _GLIBCXX_NUM_ARGS_PLUS_1 1
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T1
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS

#define _GLIBCXX_NUM_ARGS 1
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1
#define _GLIBCXX_TEMPLATE_ARGS _T1
#define _GLIBCXX_PARAMS _T1 __a1
#define _GLIBCXX_REF_PARAMS _T1& __a1
#define _GLIBCXX_ARGS __a1
#define _GLIBCXX_COMMA_SHIFTED
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#define _GLIBCXX_PARAMS_SHIFTED
#define _GLIBCXX_ARGS_SHIFTED
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1
#define _GLIBCXX_TEMPLATE_ARGS_U _U1
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&
#define _GLIBCXX_NUM_ARGS_PLUS_1 2
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T2
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS

#define _GLIBCXX_NUM_ARGS 2
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1, typename _T2
#define _GLIBCXX_TEMPLATE_ARGS _T1, _T2
#define _GLIBCXX_PARAMS _T1 __a1, _T2 __a2
#define _GLIBCXX_REF_PARAMS _T1& __a1, _T2& __a2
#define _GLIBCXX_ARGS __a1, __a2
#define _GLIBCXX_COMMA_SHIFTED ,
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED typename _T1
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED _T1
#define _GLIBCXX_PARAMS_SHIFTED _T1 __a1
#define _GLIBCXX_ARGS_SHIFTED __a1
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1; _T2 _M_arg2;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1), _M_arg2(__a2)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple), ::std::tr1::get<1>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T2> _CV(_T2, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T2>()(_M_arg2, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1, typename __add_c_ref<_T2>::type __a2
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1), _M_arg2(__in._M_arg2)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1; _M_arg2 = __in._M_arg2;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass, typename _T2 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type, typename __strip_reference_wrapper<_T2>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1, typename _U2
#define _GLIBCXX_TEMPLATE_ARGS_U _U1, _U2
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1), ref(__a2)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&, _T2&
#define _GLIBCXX_NUM_ARGS_PLUS_1 3
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T3
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS
#define _GLIBCXX_NUM_ARGS 3
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1, typename _T2, typename _T3
#define _GLIBCXX_TEMPLATE_ARGS _T1, _T2, _T3
#define _GLIBCXX_PARAMS _T1 __a1, _T2 __a2, _T3 __a3
#define _GLIBCXX_REF_PARAMS _T1& __a1, _T2& __a2, _T3& __a3
#define _GLIBCXX_ARGS __a1, __a2, __a3
#define _GLIBCXX_COMMA_SHIFTED ,
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED typename _T1, typename _T2
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED _T1, _T2
#define _GLIBCXX_PARAMS_SHIFTED _T1 __a1, _T2 __a2
#define _GLIBCXX_ARGS_SHIFTED __a1, __a2
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1; _T2 _M_arg2; _T3 _M_arg3;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1), _M_arg2(__a2), _M_arg3(__a3)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple), ::std::tr1::get<1>(__tuple), ::std::tr1::get<2>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T2> _CV(_T2, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T3> _CV(_T3, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T2>()(_M_arg2, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T3>()(_M_arg3, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1, typename __add_c_ref<_T2>::type __a2, typename __add_c_ref<_T3>::type __a3
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1), _M_arg2(__in._M_arg2), _M_arg3(__in._M_arg3)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1; _M_arg2 = __in._M_arg2; _M_arg3 = __in._M_arg3;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass, typename _T2 = _NullClass, typename _T3 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type, typename __strip_reference_wrapper<_T2>::__type, typename __strip_reference_wrapper<_T3>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1, typename _U2, typename _U3
#define _GLIBCXX_TEMPLATE_ARGS_U _U1, _U2, _U3
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1), ref(__a2), ref(__a3)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&, _T2&, _T3&
#define _GLIBCXX_NUM_ARGS_PLUS_1 4
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T4
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS
#define _GLIBCXX_NUM_ARGS 4
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1, typename _T2, typename _T3, typename _T4
#define _GLIBCXX_TEMPLATE_ARGS _T1, _T2, _T3, _T4
#define _GLIBCXX_PARAMS _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4
#define _GLIBCXX_REF_PARAMS _T1& __a1, _T2& __a2, _T3& __a3, _T4& __a4
#define _GLIBCXX_ARGS __a1, __a2, __a3, __a4
#define _GLIBCXX_COMMA_SHIFTED ,
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED typename _T1, typename _T2, typename _T3
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED _T1, _T2, _T3
#define _GLIBCXX_PARAMS_SHIFTED _T1 __a1, _T2 __a2, _T3 __a3
#define _GLIBCXX_ARGS_SHIFTED __a1, __a2, __a3
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1; _T2 _M_arg2; _T3 _M_arg3; _T4 _M_arg4;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1), _M_arg2(__a2), _M_arg3(__a3), _M_arg4(__a4)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple), ::std::tr1::get<1>(__tuple), ::std::tr1::get<2>(__tuple), ::std::tr1::get<3>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T2> _CV(_T2, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T3> _CV(_T3, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T4> _CV(_T4, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T2>()(_M_arg2, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T3>()(_M_arg3, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T4>()(_M_arg4, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1, typename __add_c_ref<_T2>::type __a2, typename __add_c_ref<_T3>::type __a3, typename __add_c_ref<_T4>::type __a4
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1), _M_arg2(__in._M_arg2), _M_arg3(__in._M_arg3), _M_arg4(__in._M_arg4)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1; _M_arg2 = __in._M_arg2; _M_arg3 = __in._M_arg3; _M_arg4 = __in._M_arg4;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass, typename _T2 = _NullClass, typename _T3 = _NullClass, typename _T4 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type, typename __strip_reference_wrapper<_T2>::__type, typename __strip_reference_wrapper<_T3>::__type, typename __strip_reference_wrapper<_T4>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1, typename _U2, typename _U3, typename _U4
#define _GLIBCXX_TEMPLATE_ARGS_U _U1, _U2, _U3, _U4
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1), ref(__a2), ref(__a3), ref(__a4)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&, _T2&, _T3&, _T4&
#define _GLIBCXX_NUM_ARGS_PLUS_1 5
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T5
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS
#define _GLIBCXX_NUM_ARGS 5
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1, typename _T2, typename _T3, typename _T4, typename _T5
#define _GLIBCXX_TEMPLATE_ARGS _T1, _T2, _T3, _T4, _T5
#define _GLIBCXX_PARAMS _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5
#define _GLIBCXX_REF_PARAMS _T1& __a1, _T2& __a2, _T3& __a3, _T4& __a4, _T5& __a5
#define _GLIBCXX_ARGS __a1, __a2, __a3, __a4, __a5
#define _GLIBCXX_COMMA_SHIFTED ,
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED typename _T1, typename _T2, typename _T3, typename _T4
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED _T1, _T2, _T3, _T4
#define _GLIBCXX_PARAMS_SHIFTED _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4
#define _GLIBCXX_ARGS_SHIFTED __a1, __a2, __a3, __a4
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1; _T2 _M_arg2; _T3 _M_arg3; _T4 _M_arg4; _T5 _M_arg5;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1), _M_arg2(__a2), _M_arg3(__a3), _M_arg4(__a4), _M_arg5(__a5)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple), ::std::tr1::get<1>(__tuple), ::std::tr1::get<2>(__tuple), ::std::tr1::get<3>(__tuple), ::std::tr1::get<4>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T2> _CV(_T2, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T3> _CV(_T3, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T4> _CV(_T4, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T5> _CV(_T5, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T2>()(_M_arg2, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T3>()(_M_arg3, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T4>()(_M_arg4, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T5>()(_M_arg5, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1, typename __add_c_ref<_T2>::type __a2, typename __add_c_ref<_T3>::type __a3, typename __add_c_ref<_T4>::type __a4, typename __add_c_ref<_T5>::type __a5
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1), _M_arg2(__in._M_arg2), _M_arg3(__in._M_arg3), _M_arg4(__in._M_arg4), _M_arg5(__in._M_arg5)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1; _M_arg2 = __in._M_arg2; _M_arg3 = __in._M_arg3; _M_arg4 = __in._M_arg4; _M_arg5 = __in._M_arg5;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass, typename _T2 = _NullClass, typename _T3 = _NullClass, typename _T4 = _NullClass, typename _T5 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type, typename __strip_reference_wrapper<_T2>::__type, typename __strip_reference_wrapper<_T3>::__type, typename __strip_reference_wrapper<_T4>::__type, typename __strip_reference_wrapper<_T5>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1, typename _U2, typename _U3, typename _U4, typename _U5
#define _GLIBCXX_TEMPLATE_ARGS_U _U1, _U2, _U3, _U4, _U5
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1), ref(__a2), ref(__a3), ref(__a4), ref(__a5)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&, _T2&, _T3&, _T4&, _T5&
#define _GLIBCXX_NUM_ARGS_PLUS_1 6
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T6
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS
#define _GLIBCXX_NUM_ARGS 6
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6
#define _GLIBCXX_TEMPLATE_ARGS _T1, _T2, _T3, _T4, _T5, _T6
#define _GLIBCXX_PARAMS _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5, _T6 __a6
#define _GLIBCXX_REF_PARAMS _T1& __a1, _T2& __a2, _T3& __a3, _T4& __a4, _T5& __a5, _T6& __a6
#define _GLIBCXX_ARGS __a1, __a2, __a3, __a4, __a5, __a6
#define _GLIBCXX_COMMA_SHIFTED ,
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED typename _T1, typename _T2, typename _T3, typename _T4, typename _T5
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED _T1, _T2, _T3, _T4, _T5
#define _GLIBCXX_PARAMS_SHIFTED _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5
#define _GLIBCXX_ARGS_SHIFTED __a1, __a2, __a3, __a4, __a5
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1; _T2 _M_arg2; _T3 _M_arg3; _T4 _M_arg4; _T5 _M_arg5; _T6 _M_arg6;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1), _M_arg2(__a2), _M_arg3(__a3), _M_arg4(__a4), _M_arg5(__a5), _M_arg6(__a6)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple), ::std::tr1::get<1>(__tuple), ::std::tr1::get<2>(__tuple), ::std::tr1::get<3>(__tuple), ::std::tr1::get<4>(__tuple), ::std::tr1::get<5>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T2> _CV(_T2, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T3> _CV(_T3, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T4> _CV(_T4, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T5> _CV(_T5, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T6> _CV(_T6, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T2>()(_M_arg2, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T3>()(_M_arg3, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T4>()(_M_arg4, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T5>()(_M_arg5, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T6>()(_M_arg6, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1, typename __add_c_ref<_T2>::type __a2, typename __add_c_ref<_T3>::type __a3, typename __add_c_ref<_T4>::type __a4, typename __add_c_ref<_T5>::type __a5, typename __add_c_ref<_T6>::type __a6
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1), _M_arg2(__in._M_arg2), _M_arg3(__in._M_arg3), _M_arg4(__in._M_arg4), _M_arg5(__in._M_arg5), _M_arg6(__in._M_arg6)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1; _M_arg2 = __in._M_arg2; _M_arg3 = __in._M_arg3; _M_arg4 = __in._M_arg4; _M_arg5 = __in._M_arg5; _M_arg6 = __in._M_arg6;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass, typename _T2 = _NullClass, typename _T3 = _NullClass, typename _T4 = _NullClass, typename _T5 = _NullClass, typename _T6 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type, typename __strip_reference_wrapper<_T2>::__type, typename __strip_reference_wrapper<_T3>::__type, typename __strip_reference_wrapper<_T4>::__type, typename __strip_reference_wrapper<_T5>::__type, typename __strip_reference_wrapper<_T6>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6
#define _GLIBCXX_TEMPLATE_ARGS_U _U1, _U2, _U3, _U4, _U5, _U6
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1), ref(__a2), ref(__a3), ref(__a4), ref(__a5), ref(__a6)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&, _T2&, _T3&, _T4&, _T5&, _T6&
#define _GLIBCXX_NUM_ARGS_PLUS_1 7
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T7
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS
#define _GLIBCXX_NUM_ARGS 7
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6, typename _T7
#define _GLIBCXX_TEMPLATE_ARGS _T1, _T2, _T3, _T4, _T5, _T6, _T7
#define _GLIBCXX_PARAMS _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5, _T6 __a6, _T7 __a7
#define _GLIBCXX_REF_PARAMS _T1& __a1, _T2& __a2, _T3& __a3, _T4& __a4, _T5& __a5, _T6& __a6, _T7& __a7
#define _GLIBCXX_ARGS __a1, __a2, __a3, __a4, __a5, __a6, __a7
#define _GLIBCXX_COMMA_SHIFTED ,
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED _T1, _T2, _T3, _T4, _T5, _T6
#define _GLIBCXX_PARAMS_SHIFTED _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5, _T6 __a6
#define _GLIBCXX_ARGS_SHIFTED __a1, __a2, __a3, __a4, __a5, __a6
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1; _T2 _M_arg2; _T3 _M_arg3; _T4 _M_arg4; _T5 _M_arg5; _T6 _M_arg6; _T7 _M_arg7;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1), _M_arg2(__a2), _M_arg3(__a3), _M_arg4(__a4), _M_arg5(__a5), _M_arg6(__a6), _M_arg7(__a7)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple), ::std::tr1::get<1>(__tuple), ::std::tr1::get<2>(__tuple), ::std::tr1::get<3>(__tuple), ::std::tr1::get<4>(__tuple), ::std::tr1::get<5>(__tuple), ::std::tr1::get<6>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T2> _CV(_T2, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T3> _CV(_T3, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T4> _CV(_T4, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T5> _CV(_T5, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T6> _CV(_T6, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T7> _CV(_T7, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T2>()(_M_arg2, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T3>()(_M_arg3, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T4>()(_M_arg4, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T5>()(_M_arg5, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T6>()(_M_arg6, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T7>()(_M_arg7, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1, typename __add_c_ref<_T2>::type __a2, typename __add_c_ref<_T3>::type __a3, typename __add_c_ref<_T4>::type __a4, typename __add_c_ref<_T5>::type __a5, typename __add_c_ref<_T6>::type __a6, typename __add_c_ref<_T7>::type __a7
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1), _M_arg2(__in._M_arg2), _M_arg3(__in._M_arg3), _M_arg4(__in._M_arg4), _M_arg5(__in._M_arg5), _M_arg6(__in._M_arg6), _M_arg7(__in._M_arg7)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1; _M_arg2 = __in._M_arg2; _M_arg3 = __in._M_arg3; _M_arg4 = __in._M_arg4; _M_arg5 = __in._M_arg5; _M_arg6 = __in._M_arg6; _M_arg7 = __in._M_arg7;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass, typename _T2 = _NullClass, typename _T3 = _NullClass, typename _T4 = _NullClass, typename _T5 = _NullClass, typename _T6 = _NullClass, typename _T7 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type, typename __strip_reference_wrapper<_T2>::__type, typename __strip_reference_wrapper<_T3>::__type, typename __strip_reference_wrapper<_T4>::__type, typename __strip_reference_wrapper<_T5>::__type, typename __strip_reference_wrapper<_T6>::__type, typename __strip_reference_wrapper<_T7>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6, typename _U7
#define _GLIBCXX_TEMPLATE_ARGS_U _U1, _U2, _U3, _U4, _U5, _U6, _U7
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1), ref(__a2), ref(__a3), ref(__a4), ref(__a5), ref(__a6), ref(__a7)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&, _T2&, _T3&, _T4&, _T5&, _T6&, _T7&
#define _GLIBCXX_NUM_ARGS_PLUS_1 8
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T8
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS
#define _GLIBCXX_NUM_ARGS 8
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6, typename _T7, typename _T8
#define _GLIBCXX_TEMPLATE_ARGS _T1, _T2, _T3, _T4, _T5, _T6, _T7, _T8
#define _GLIBCXX_PARAMS _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5, _T6 __a6, _T7 __a7, _T8 __a8
#define _GLIBCXX_REF_PARAMS _T1& __a1, _T2& __a2, _T3& __a3, _T4& __a4, _T5& __a5, _T6& __a6, _T7& __a7, _T8& __a8
#define _GLIBCXX_ARGS __a1, __a2, __a3, __a4, __a5, __a6, __a7, __a8
#define _GLIBCXX_COMMA_SHIFTED ,
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6, typename _T7
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED _T1, _T2, _T3, _T4, _T5, _T6, _T7
#define _GLIBCXX_PARAMS_SHIFTED _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5, _T6 __a6, _T7 __a7
#define _GLIBCXX_ARGS_SHIFTED __a1, __a2, __a3, __a4, __a5, __a6, __a7
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1; _T2 _M_arg2; _T3 _M_arg3; _T4 _M_arg4; _T5 _M_arg5; _T6 _M_arg6; _T7 _M_arg7; _T8 _M_arg8;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1), _M_arg2(__a2), _M_arg3(__a3), _M_arg4(__a4), _M_arg5(__a5), _M_arg6(__a6), _M_arg7(__a7), _M_arg8(__a8)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple), ::std::tr1::get<1>(__tuple), ::std::tr1::get<2>(__tuple), ::std::tr1::get<3>(__tuple), ::std::tr1::get<4>(__tuple), ::std::tr1::get<5>(__tuple), ::std::tr1::get<6>(__tuple), ::std::tr1::get<7>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T2> _CV(_T2, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T3> _CV(_T3, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T4> _CV(_T4, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T5> _CV(_T5, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T6> _CV(_T6, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T7> _CV(_T7, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T8> _CV(_T8, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T2>()(_M_arg2, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T3>()(_M_arg3, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T4>()(_M_arg4, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T5>()(_M_arg5, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T6>()(_M_arg6, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T7>()(_M_arg7, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T8>()(_M_arg8, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1, typename __add_c_ref<_T2>::type __a2, typename __add_c_ref<_T3>::type __a3, typename __add_c_ref<_T4>::type __a4, typename __add_c_ref<_T5>::type __a5, typename __add_c_ref<_T6>::type __a6, typename __add_c_ref<_T7>::type __a7, typename __add_c_ref<_T8>::type __a8
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1), _M_arg2(__in._M_arg2), _M_arg3(__in._M_arg3), _M_arg4(__in._M_arg4), _M_arg5(__in._M_arg5), _M_arg6(__in._M_arg6), _M_arg7(__in._M_arg7), _M_arg8(__in._M_arg8)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1; _M_arg2 = __in._M_arg2; _M_arg3 = __in._M_arg3; _M_arg4 = __in._M_arg4; _M_arg5 = __in._M_arg5; _M_arg6 = __in._M_arg6; _M_arg7 = __in._M_arg7; _M_arg8 = __in._M_arg8;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass, typename _T2 = _NullClass, typename _T3 = _NullClass, typename _T4 = _NullClass, typename _T5 = _NullClass, typename _T6 = _NullClass, typename _T7 = _NullClass, typename _T8 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type, typename __strip_reference_wrapper<_T2>::__type, typename __strip_reference_wrapper<_T3>::__type, typename __strip_reference_wrapper<_T4>::__type, typename __strip_reference_wrapper<_T5>::__type, typename __strip_reference_wrapper<_T6>::__type, typename __strip_reference_wrapper<_T7>::__type, typename __strip_reference_wrapper<_T8>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6, typename _U7, typename _U8
#define _GLIBCXX_TEMPLATE_ARGS_U _U1, _U2, _U3, _U4, _U5, _U6, _U7, _U8
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1), ref(__a2), ref(__a3), ref(__a4), ref(__a5), ref(__a6), ref(__a7), ref(__a8)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&, _T2&, _T3&, _T4&, _T5&, _T6&, _T7&, _T8&
#define _GLIBCXX_NUM_ARGS_PLUS_1 9
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T9
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS
#define _GLIBCXX_NUM_ARGS 9
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6, typename _T7, typename _T8, typename _T9
#define _GLIBCXX_TEMPLATE_ARGS _T1, _T2, _T3, _T4, _T5, _T6, _T7, _T8, _T9
#define _GLIBCXX_PARAMS _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5, _T6 __a6, _T7 __a7, _T8 __a8, _T9 __a9
#define _GLIBCXX_REF_PARAMS _T1& __a1, _T2& __a2, _T3& __a3, _T4& __a4, _T5& __a5, _T6& __a6, _T7& __a7, _T8& __a8, _T9& __a9
#define _GLIBCXX_ARGS __a1, __a2, __a3, __a4, __a5, __a6, __a7, __a8, __a9
#define _GLIBCXX_COMMA_SHIFTED ,
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6, typename _T7, typename _T8
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED _T1, _T2, _T3, _T4, _T5, _T6, _T7, _T8
#define _GLIBCXX_PARAMS_SHIFTED _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5, _T6 __a6, _T7 __a7, _T8 __a8
#define _GLIBCXX_ARGS_SHIFTED __a1, __a2, __a3, __a4, __a5, __a6, __a7, __a8
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1; _T2 _M_arg2; _T3 _M_arg3; _T4 _M_arg4; _T5 _M_arg5; _T6 _M_arg6; _T7 _M_arg7; _T8 _M_arg8; _T9 _M_arg9;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1), _M_arg2(__a2), _M_arg3(__a3), _M_arg4(__a4), _M_arg5(__a5), _M_arg6(__a6), _M_arg7(__a7), _M_arg8(__a8), _M_arg9(__a9)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple), ::std::tr1::get<1>(__tuple), ::std::tr1::get<2>(__tuple), ::std::tr1::get<3>(__tuple), ::std::tr1::get<4>(__tuple), ::std::tr1::get<5>(__tuple), ::std::tr1::get<6>(__tuple), ::std::tr1::get<7>(__tuple), ::std::tr1::get<8>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T2> _CV(_T2, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T3> _CV(_T3, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T4> _CV(_T4, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T5> _CV(_T5, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T6> _CV(_T6, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T7> _CV(_T7, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T8> _CV(_T8, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T9> _CV(_T9, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T2>()(_M_arg2, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T3>()(_M_arg3, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T4>()(_M_arg4, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T5>()(_M_arg5, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T6>()(_M_arg6, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T7>()(_M_arg7, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T8>()(_M_arg8, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T9>()(_M_arg9, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1, typename __add_c_ref<_T2>::type __a2, typename __add_c_ref<_T3>::type __a3, typename __add_c_ref<_T4>::type __a4, typename __add_c_ref<_T5>::type __a5, typename __add_c_ref<_T6>::type __a6, typename __add_c_ref<_T7>::type __a7, typename __add_c_ref<_T8>::type __a8, typename __add_c_ref<_T9>::type __a9
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1), _M_arg2(__in._M_arg2), _M_arg3(__in._M_arg3), _M_arg4(__in._M_arg4), _M_arg5(__in._M_arg5), _M_arg6(__in._M_arg6), _M_arg7(__in._M_arg7), _M_arg8(__in._M_arg8), _M_arg9(__in._M_arg9)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1; _M_arg2 = __in._M_arg2; _M_arg3 = __in._M_arg3; _M_arg4 = __in._M_arg4; _M_arg5 = __in._M_arg5; _M_arg6 = __in._M_arg6; _M_arg7 = __in._M_arg7; _M_arg8 = __in._M_arg8; _M_arg9 = __in._M_arg9;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass, typename _T2 = _NullClass, typename _T3 = _NullClass, typename _T4 = _NullClass, typename _T5 = _NullClass, typename _T6 = _NullClass, typename _T7 = _NullClass, typename _T8 = _NullClass, typename _T9 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type, typename __strip_reference_wrapper<_T2>::__type, typename __strip_reference_wrapper<_T3>::__type, typename __strip_reference_wrapper<_T4>::__type, typename __strip_reference_wrapper<_T5>::__type, typename __strip_reference_wrapper<_T6>::__type, typename __strip_reference_wrapper<_T7>::__type, typename __strip_reference_wrapper<_T8>::__type, typename __strip_reference_wrapper<_T9>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6, typename _U7, typename _U8, typename _U9
#define _GLIBCXX_TEMPLATE_ARGS_U _U1, _U2, _U3, _U4, _U5, _U6, _U7, _U8, _U9
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1), ref(__a2), ref(__a3), ref(__a4), ref(__a5), ref(__a6), ref(__a7), ref(__a8), ref(__a9)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&, _T2&, _T3&, _T4&, _T5&, _T6&, _T7&, _T8&, _T9&
#define _GLIBCXX_NUM_ARGS_PLUS_1 10
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T10
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS
#define _GLIBCXX_LAST_INCLUDE
#define _GLIBCXX_NUM_ARGS 10
#define _GLIBCXX_COMMA ,
#define _GLIBCXX_TEMPLATE_PARAMS typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6, typename _T7, typename _T8, typename _T9, typename _T10
#define _GLIBCXX_TEMPLATE_ARGS _T1, _T2, _T3, _T4, _T5, _T6, _T7, _T8, _T9, _T10
#define _GLIBCXX_PARAMS _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5, _T6 __a6, _T7 __a7, _T8 __a8, _T9 __a9, _T10 __a10
#define _GLIBCXX_REF_PARAMS _T1& __a1, _T2& __a2, _T3& __a3, _T4& __a4, _T5& __a5, _T6& __a6, _T7& __a7, _T8& __a8, _T9& __a9, _T10& __a10
#define _GLIBCXX_ARGS __a1, __a2, __a3, __a4, __a5, __a6, __a7, __a8, __a9, __a10
#define _GLIBCXX_COMMA_SHIFTED ,
#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED typename _T1, typename _T2, typename _T3, typename _T4, typename _T5, typename _T6, typename _T7, typename _T8, typename _T9
#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED _T1, _T2, _T3, _T4, _T5, _T6, _T7, _T8, _T9
#define _GLIBCXX_PARAMS_SHIFTED _T1 __a1, _T2 __a2, _T3 __a3, _T4 __a4, _T5 __a5, _T6 __a6, _T7 __a7, _T8 __a8, _T9 __a9
#define _GLIBCXX_ARGS_SHIFTED __a1, __a2, __a3, __a4, __a5, __a6, __a7, __a8, __a9
#define _GLIBCXX_BIND_MEMBERS _T1 _M_arg1; _T2 _M_arg2; _T3 _M_arg3; _T4 _M_arg4; _T5 _M_arg5; _T6 _M_arg6; _T7 _M_arg7; _T8 _M_arg8; _T9 _M_arg9; _T10 _M_arg10;
#define _GLIBCXX_BIND_MEMBERS_INIT _M_arg1(__a1), _M_arg2(__a2), _M_arg3(__a3), _M_arg4(__a4), _M_arg5(__a5), _M_arg6(__a6), _M_arg7(__a7), _M_arg8(__a8), _M_arg9(__a9), _M_arg10(__a10)
#define _GLIBCXX_MU_GET_TUPLE_ARGS ::std::tr1::get<0>(__tuple), ::std::tr1::get<1>(__tuple), ::std::tr1::get<2>(__tuple), ::std::tr1::get<3>(__tuple), ::std::tr1::get<4>(__tuple), ::std::tr1::get<5>(__tuple), ::std::tr1::get<6>(__tuple), ::std::tr1::get<7>(__tuple), ::std::tr1::get<8>(__tuple), ::std::tr1::get<9>(__tuple)
#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T2> _CV(_T2, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T3> _CV(_T3, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T4> _CV(_T4, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T5> _CV(_T5, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T6> _CV(_T6, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T7> _CV(_T7, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T8> _CV(_T8, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T9> _CV(_T9, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type, typename result_of<_Mu<_T10> _CV(_T10, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type
#define _GLIBCXX_BIND_V_ARGS _Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T2>()(_M_arg2, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T3>()(_M_arg3, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T4>()(_M_arg4, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T5>()(_M_arg5, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T6>()(_M_arg6, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T7>()(_M_arg7, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T8>()(_M_arg8, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T9>()(_M_arg9, ::std::tr1::tie(_GLIBCXX_BIND_ARGS)), _Mu<_T10>()(_M_arg10, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))
#define _GLIBCXX_TUPLE_ADD_CREF typename __add_c_ref<_T1>::type __a1, typename __add_c_ref<_T2>::type __a2, typename __add_c_ref<_T3>::type __a3, typename __add_c_ref<_T4>::type __a4, typename __add_c_ref<_T5>::type __a5, typename __add_c_ref<_T6>::type __a6, typename __add_c_ref<_T7>::type __a7, typename __add_c_ref<_T8>::type __a8, typename __add_c_ref<_T9>::type __a9, typename __add_c_ref<_T10>::type __a10
#define _GLIBCXX_TUPLE_COPY_INIT _M_arg1(__in._M_arg1), _M_arg2(__in._M_arg2), _M_arg3(__in._M_arg3), _M_arg4(__in._M_arg4), _M_arg5(__in._M_arg5), _M_arg6(__in._M_arg6), _M_arg7(__in._M_arg7), _M_arg8(__in._M_arg8), _M_arg9(__in._M_arg9), _M_arg10(__in._M_arg10)
#define _GLIBCXX_TUPLE_ASSIGN _M_arg1 = __in._M_arg1; _M_arg2 = __in._M_arg2; _M_arg3 = __in._M_arg3; _M_arg4 = __in._M_arg4; _M_arg5 = __in._M_arg5; _M_arg6 = __in._M_arg6; _M_arg7 = __in._M_arg7; _M_arg8 = __in._M_arg8; _M_arg9 = __in._M_arg9; _M_arg10 = __in._M_arg10;
#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS typename _T1 = _NullClass, typename _T2 = _NullClass, typename _T3 = _NullClass, typename _T4 = _NullClass, typename _T5 = _NullClass, typename _T6 = _NullClass, typename _T7 = _NullClass, typename _T8 = _NullClass, typename _T9 = _NullClass, typename _T10 = _NullClass
#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED typename __strip_reference_wrapper<_T1>::__type, typename __strip_reference_wrapper<_T2>::__type, typename __strip_reference_wrapper<_T3>::__type, typename __strip_reference_wrapper<_T4>::__type, typename __strip_reference_wrapper<_T5>::__type, typename __strip_reference_wrapper<_T6>::__type, typename __strip_reference_wrapper<_T7>::__type, typename __strip_reference_wrapper<_T8>::__type, typename __strip_reference_wrapper<_T9>::__type, typename __strip_reference_wrapper<_T10>::__type
#define _GLIBCXX_TEMPLATE_PARAMS_U typename _U1, typename _U2, typename _U3, typename _U4, typename _U5, typename _U6, typename _U7, typename _U8, typename _U9, typename _U10
#define _GLIBCXX_TEMPLATE_ARGS_U _U1, _U2, _U3, _U4, _U5, _U6, _U7, _U8, _U9, _U10
#define _GLIBCXX_REF_WRAP_PARAMS ref(__a1), ref(__a2), ref(__a3), ref(__a4), ref(__a5), ref(__a6), ref(__a7), ref(__a8), ref(__a9), ref(__a10)
#define _GLIBCXX_REF_TEMPLATE_ARGS _T1&, _T2&, _T3&, _T4&, _T5&, _T6&, _T7&, _T8&, _T9&, _T10&
#define _GLIBCXX_NUM_ARGS_PLUS_1 11
#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T11
#include _GLIBCXX_REPEAT_HEADER
#undef _GLIBCXX_T_NUM_ARGS_PLUS_1
#undef _GLIBCXX_NUM_ARGS_PLUS_1
#undef _GLIBCXX_REF_TEMPLATE_ARGS
#undef _GLIBCXX_REF_WRAP_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS_U
#undef _GLIBCXX_TEMPLATE_PARAMS_U
#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED
#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS
#undef _GLIBCXX_TUPLE_ASSIGN
#undef _GLIBCXX_TUPLE_COPY_INIT
#undef _GLIBCXX_TUPLE_ADD_CREF
#undef _GLIBCXX_BIND_V_ARGS
#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS
#undef _GLIBCXX_MU_GET_TUPLE_ARGS
#undef _GLIBCXX_BIND_MEMBERS_INIT
#undef _GLIBCXX_BIND_MEMBERS
#undef _GLIBCXX_ARGS_SHIFTED
#undef _GLIBCXX_PARAMS_SHIFTED
#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED
#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED
#undef _GLIBCXX_COMMA_SHIFTED
#undef _GLIBCXX_ARGS
#undef _GLIBCXX_REF_PARAMS
#undef _GLIBCXX_PARAMS
#undef _GLIBCXX_TEMPLATE_ARGS
#undef _GLIBCXX_TEMPLATE_PARAMS
#undef _GLIBCXX_COMMA
#undef _GLIBCXX_NUM_ARGS
#undef _GLIBCXX_LAST_INCLUDE

