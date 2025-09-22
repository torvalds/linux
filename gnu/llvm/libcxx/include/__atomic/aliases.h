//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ATOMIC_ALIASES_H
#define _LIBCPP___ATOMIC_ALIASES_H

#include <__atomic/atomic.h>
#include <__atomic/atomic_lock_free.h>
#include <__atomic/contention_t.h>
#include <__atomic/is_always_lock_free.h>
#include <__config>
#include <__type_traits/conditional.h>
#include <__type_traits/make_unsigned.h>
#include <cstddef>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

using atomic_bool   = atomic<bool>;
using atomic_char   = atomic<char>;
using atomic_schar  = atomic<signed char>;
using atomic_uchar  = atomic<unsigned char>;
using atomic_short  = atomic<short>;
using atomic_ushort = atomic<unsigned short>;
using atomic_int    = atomic<int>;
using atomic_uint   = atomic<unsigned int>;
using atomic_long   = atomic<long>;
using atomic_ulong  = atomic<unsigned long>;
using atomic_llong  = atomic<long long>;
using atomic_ullong = atomic<unsigned long long>;
#ifndef _LIBCPP_HAS_NO_CHAR8_T
using atomic_char8_t = atomic<char8_t>;
#endif
using atomic_char16_t = atomic<char16_t>;
using atomic_char32_t = atomic<char32_t>;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
using atomic_wchar_t = atomic<wchar_t>;
#endif

using atomic_int_least8_t   = atomic<int_least8_t>;
using atomic_uint_least8_t  = atomic<uint_least8_t>;
using atomic_int_least16_t  = atomic<int_least16_t>;
using atomic_uint_least16_t = atomic<uint_least16_t>;
using atomic_int_least32_t  = atomic<int_least32_t>;
using atomic_uint_least32_t = atomic<uint_least32_t>;
using atomic_int_least64_t  = atomic<int_least64_t>;
using atomic_uint_least64_t = atomic<uint_least64_t>;

using atomic_int_fast8_t   = atomic<int_fast8_t>;
using atomic_uint_fast8_t  = atomic<uint_fast8_t>;
using atomic_int_fast16_t  = atomic<int_fast16_t>;
using atomic_uint_fast16_t = atomic<uint_fast16_t>;
using atomic_int_fast32_t  = atomic<int_fast32_t>;
using atomic_uint_fast32_t = atomic<uint_fast32_t>;
using atomic_int_fast64_t  = atomic<int_fast64_t>;
using atomic_uint_fast64_t = atomic<uint_fast64_t>;

using atomic_int8_t   = atomic< int8_t>;
using atomic_uint8_t  = atomic<uint8_t>;
using atomic_int16_t  = atomic< int16_t>;
using atomic_uint16_t = atomic<uint16_t>;
using atomic_int32_t  = atomic< int32_t>;
using atomic_uint32_t = atomic<uint32_t>;
using atomic_int64_t  = atomic< int64_t>;
using atomic_uint64_t = atomic<uint64_t>;

using atomic_intptr_t  = atomic<intptr_t>;
using atomic_uintptr_t = atomic<uintptr_t>;
using atomic_size_t    = atomic<size_t>;
using atomic_ptrdiff_t = atomic<ptrdiff_t>;
using atomic_intmax_t  = atomic<intmax_t>;
using atomic_uintmax_t = atomic<uintmax_t>;

// C++20 atomic_{signed,unsigned}_lock_free: prefer the contention type most highly, then the largest lock-free type
#if _LIBCPP_STD_VER >= 20
#  if ATOMIC_LLONG_LOCK_FREE == 2
using __largest_lock_free_type = long long;
#  elif ATOMIC_INT_LOCK_FREE == 2
using __largest_lock_free_type = int;
#  elif ATOMIC_SHORT_LOCK_FREE == 2
using __largest_lock_free_type = short;
#  elif ATOMIC_CHAR_LOCK_FREE == 2
using __largest_lock_free_type = char;
#  else
#    define _LIBCPP_NO_LOCK_FREE_TYPES // There are no lockfree types (this can happen on unusual platforms)
#  endif

#  ifndef _LIBCPP_NO_LOCK_FREE_TYPES
using __contention_t_or_largest =
    __conditional_t<__libcpp_is_always_lock_free<__cxx_contention_t>::__value,
                    __cxx_contention_t,
                    __largest_lock_free_type>;

using atomic_signed_lock_free   = atomic<__contention_t_or_largest>;
using atomic_unsigned_lock_free = atomic<make_unsigned_t<__contention_t_or_largest>>;
#  endif // !_LIBCPP_NO_LOCK_FREE_TYPES
#endif   // C++20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ATOMIC_ALIASES_H
