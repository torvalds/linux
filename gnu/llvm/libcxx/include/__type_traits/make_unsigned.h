//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_MAKE_UNSIGNED_H
#define _LIBCPP___TYPE_TRAITS_MAKE_UNSIGNED_H

#include <__config>
#include <__type_traits/conditional.h>
#include <__type_traits/copy_cv.h>
#include <__type_traits/is_enum.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_unsigned.h>
#include <__type_traits/nat.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/type_list.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__make_unsigned)

template <class _Tp>
using __make_unsigned_t = __make_unsigned(_Tp);

#else
// clang-format off
typedef __type_list<unsigned char,
        __type_list<unsigned short,
        __type_list<unsigned int,
        __type_list<unsigned long,
        __type_list<unsigned long long,
#  ifndef _LIBCPP_HAS_NO_INT128
        __type_list<__uint128_t,
#  endif
        __nat
#  ifndef _LIBCPP_HAS_NO_INT128
        >
#  endif
        > > > > > __unsigned_types;
// clang-format on

template <class _Tp, bool = is_integral<_Tp>::value || is_enum<_Tp>::value>
struct __make_unsigned{};

template <class _Tp>
struct __make_unsigned<_Tp, true> {
  typedef typename __find_first<__unsigned_types, sizeof(_Tp)>::type type;
};

// clang-format off
template <> struct __make_unsigned<bool,               true> {};
template <> struct __make_unsigned<  signed short,     true> {typedef unsigned short     type;};
template <> struct __make_unsigned<unsigned short,     true> {typedef unsigned short     type;};
template <> struct __make_unsigned<  signed int,       true> {typedef unsigned int       type;};
template <> struct __make_unsigned<unsigned int,       true> {typedef unsigned int       type;};
template <> struct __make_unsigned<  signed long,      true> {typedef unsigned long      type;};
template <> struct __make_unsigned<unsigned long,      true> {typedef unsigned long      type;};
template <> struct __make_unsigned<  signed long long, true> {typedef unsigned long long type;};
template <> struct __make_unsigned<unsigned long long, true> {typedef unsigned long long type;};
#  ifndef _LIBCPP_HAS_NO_INT128
template <> struct __make_unsigned<__int128_t,         true> {typedef __uint128_t        type;};
template <> struct __make_unsigned<__uint128_t,        true> {typedef __uint128_t        type;};
#  endif
// clang-format on

template <class _Tp>
using __make_unsigned_t = __copy_cv_t<_Tp, typename __make_unsigned<__remove_cv_t<_Tp> >::type>;

#endif // __has_builtin(__make_unsigned)

template <class _Tp>
struct make_unsigned {
  using type _LIBCPP_NODEBUG = __make_unsigned_t<_Tp>;
};

#if _LIBCPP_STD_VER >= 14
template <class _Tp>
using make_unsigned_t = __make_unsigned_t<_Tp>;
#endif

#ifndef _LIBCPP_CXX03_LANG
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr __make_unsigned_t<_Tp> __to_unsigned_like(_Tp __x) noexcept {
  return static_cast<__make_unsigned_t<_Tp> >(__x);
}
#endif

template <class _Tp, class _Up>
using __copy_unsigned_t = __conditional_t<is_unsigned<_Tp>::value, __make_unsigned_t<_Up>, _Up>;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_MAKE_UNSIGNED_H
