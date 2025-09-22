//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_MAKE_SIGNED_H
#define _LIBCPP___TYPE_TRAITS_MAKE_SIGNED_H

#include <__config>
#include <__type_traits/copy_cv.h>
#include <__type_traits/is_enum.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/nat.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/type_list.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__make_signed)

template <class _Tp>
using __make_signed_t = __make_signed(_Tp);

#else
// clang-format off
typedef __type_list<signed char,
        __type_list<signed short,
        __type_list<signed int,
        __type_list<signed long,
        __type_list<signed long long,
#  ifndef _LIBCPP_HAS_NO_INT128
        __type_list<__int128_t,
#  endif
        __nat
#  ifndef _LIBCPP_HAS_NO_INT128
        >
#  endif
        > > > > > __signed_types;
// clang-format on

template <class _Tp, bool = is_integral<_Tp>::value || is_enum<_Tp>::value>
struct __make_signed{};

template <class _Tp>
struct __make_signed<_Tp, true> {
  typedef typename __find_first<__signed_types, sizeof(_Tp)>::type type;
};

// clang-format off
template <> struct __make_signed<bool,               true> {};
template <> struct __make_signed<  signed short,     true> {typedef short     type;};
template <> struct __make_signed<unsigned short,     true> {typedef short     type;};
template <> struct __make_signed<  signed int,       true> {typedef int       type;};
template <> struct __make_signed<unsigned int,       true> {typedef int       type;};
template <> struct __make_signed<  signed long,      true> {typedef long      type;};
template <> struct __make_signed<unsigned long,      true> {typedef long      type;};
template <> struct __make_signed<  signed long long, true> {typedef long long type;};
template <> struct __make_signed<unsigned long long, true> {typedef long long type;};
#  ifndef _LIBCPP_HAS_NO_INT128
template <> struct __make_signed<__int128_t,         true> {typedef __int128_t type;};
template <> struct __make_signed<__uint128_t,        true> {typedef __int128_t type;};
#  endif
// clang-format on

template <class _Tp>
using __make_signed_t = __copy_cv_t<_Tp, typename __make_signed<__remove_cv_t<_Tp> >::type>;

#endif // __has_builtin(__make_signed)

template <class _Tp>
struct make_signed {
  using type _LIBCPP_NODEBUG = __make_signed_t<_Tp>;
};

#if _LIBCPP_STD_VER >= 14
template <class _Tp>
using make_signed_t = __make_signed_t<_Tp>;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_MAKE_SIGNED_H
