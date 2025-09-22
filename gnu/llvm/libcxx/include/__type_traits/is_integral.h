//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_INTEGRAL_H
#define _LIBCPP___TYPE_TRAITS_IS_INTEGRAL_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/remove_cv.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// clang-format off
template <class _Tp> struct __libcpp_is_integral                     { enum { value = 0 }; };
template <>          struct __libcpp_is_integral<bool>               { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<char>               { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<signed char>        { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<unsigned char>      { enum { value = 1 }; };
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>          struct __libcpp_is_integral<wchar_t>            { enum { value = 1 }; };
#endif
#ifndef _LIBCPP_HAS_NO_CHAR8_T
template <>          struct __libcpp_is_integral<char8_t>            { enum { value = 1 }; };
#endif
template <>          struct __libcpp_is_integral<char16_t>           { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<char32_t>           { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<short>              { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<unsigned short>     { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<int>                { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<unsigned int>       { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<long>               { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<unsigned long>      { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<long long>          { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<unsigned long long> { enum { value = 1 }; };
#ifndef _LIBCPP_HAS_NO_INT128
template <>          struct __libcpp_is_integral<__int128_t>         { enum { value = 1 }; };
template <>          struct __libcpp_is_integral<__uint128_t>        { enum { value = 1 }; };
#endif
// clang-format on

#if __has_builtin(__is_integral)

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_integral : _BoolConstant<__is_integral(_Tp)> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_integral_v = __is_integral(_Tp);
#  endif

#else

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_integral : public _BoolConstant<__libcpp_is_integral<__remove_cv_t<_Tp> >::value> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_integral_v = is_integral<_Tp>::value;
#  endif

#endif // __has_builtin(__is_integral)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_INTEGRAL_H
