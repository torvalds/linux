//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_MAKE_32_64_OR_128_BIT_H
#define _LIBCPP___TYPE_TRAITS_MAKE_32_64_OR_128_BIT_H

#include <__config>
#include <__type_traits/conditional.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_signed.h>
#include <__type_traits/is_unsigned.h>
#include <__type_traits/make_unsigned.h>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

/// Helper to promote an integral to smallest 32, 64, or 128 bit representation.
///
/// The restriction is the same as the integral version of to_char.
template <class _Tp>
#if _LIBCPP_STD_VER >= 20
  requires(is_signed_v<_Tp> || is_unsigned_v<_Tp> || is_same_v<_Tp, char>)
#endif
// clang-format off
using __make_32_64_or_128_bit_t =
    __copy_unsigned_t<_Tp,
        __conditional_t<sizeof(_Tp) <= sizeof(int32_t),    int32_t,
        __conditional_t<sizeof(_Tp) <= sizeof(int64_t),    int64_t,
#ifndef _LIBCPP_HAS_NO_INT128
        __conditional_t<sizeof(_Tp) <= sizeof(__int128_t), __int128_t,
        /* else */                                         void>
#else
        /* else */                                         void
#endif
    > > >;
// clang-format on

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_MAKE_32_64_OR_128_BIT_H
