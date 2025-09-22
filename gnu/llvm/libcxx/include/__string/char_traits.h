//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STRING_CHAR_TRAITS_H
#define _LIBCPP___STRING_CHAR_TRAITS_H

#include <__algorithm/fill_n.h>
#include <__algorithm/find.h>
#include <__algorithm/find_end.h>
#include <__algorithm/find_first_of.h>
#include <__algorithm/min.h>
#include <__assert>
#include <__compare/ordering.h>
#include <__config>
#include <__functional/hash.h>
#include <__functional/identity.h>
#include <__iterator/iterator_traits.h>
#include <__string/constexpr_c_functions.h>
#include <__type_traits/is_constant_evaluated.h>
#include <__utility/is_pointer_in_range.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iosfwd>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
#  include <cwchar> // for wmemcpy
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _CharT>
struct char_traits;
/*
The Standard does not define the base template for char_traits because it is impossible to provide
a correct definition for arbitrary character types. Instead, it requires implementations to provide
specializations for predefined character types like `char`, `wchar_t` and others. We provide this as
exposition-only to document what members a char_traits specialization should provide:
{
    using char_type  = _CharT;
    using int_type   = ...;
    using off_type   = ...;
    using pos_type   = ...;
    using state_type = ...;

    static void assign(char_type&, const char_type&);
    static bool eq(char_type, char_type);
    static bool lt(char_type, char_type);

    static int              compare(const char_type*, const char_type*, size_t);
    static size_t           length(const char_type*);
    static const char_type* find(const char_type*, size_t, const char_type&);
    static char_type*       move(char_type*, const char_type*, size_t);
    static char_type*       copy(char_type*, const char_type*, size_t);
    static char_type*       assign(char_type*, size_t, char_type);

    static int_type  not_eof(int_type);
    static char_type to_char_type(int_type);
    static int_type  to_int_type(char_type);
    static bool      eq_int_type(int_type, int_type);
    static int_type  eof();
};
*/

// char_traits<char>

template <>
struct _LIBCPP_TEMPLATE_VIS char_traits<char> {
  using char_type  = char;
  using int_type   = int;
  using off_type   = streamoff;
  using pos_type   = streampos;
  using state_type = mbstate_t;
#if _LIBCPP_STD_VER >= 20
  using comparison_category = strong_ordering;
#endif

  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 void
  assign(char_type& __c1, const char_type& __c2) _NOEXCEPT {
    __c1 = __c2;
  }

  // TODO: Make this _LIBCPP_HIDE_FROM_ABI
  static inline _LIBCPP_HIDDEN _LIBCPP_CONSTEXPR bool eq(char_type __c1, char_type __c2) _NOEXCEPT {
    return __c1 == __c2;
  }
  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool lt(char_type __c1, char_type __c2) _NOEXCEPT {
    return (unsigned char)__c1 < (unsigned char)__c2;
  }

  // __constexpr_memcmp requires a trivially lexicographically comparable type, but char is not when char is a signed
  // type
  static _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 int
  compare(const char_type* __lhs, const char_type* __rhs, size_t __count) _NOEXCEPT {
    if (__libcpp_is_constant_evaluated()) {
#ifdef _LIBCPP_COMPILER_CLANG_BASED
      return __builtin_memcmp(__lhs, __rhs, __count);
#else
      while (__count != 0) {
        if (lt(*__lhs, *__rhs))
          return -1;
        if (lt(*__rhs, *__lhs))
          return 1;

        __count -= sizeof(char_type);
        ++__lhs;
        ++__rhs;
      }
      return 0;
#endif // _LIBCPP_COMPILER_CLANG_BASED
    } else {
      return __builtin_memcmp(__lhs, __rhs, __count);
    }
  }

  static inline _LIBCPP_HIDE_FROM_ABI size_t _LIBCPP_CONSTEXPR_SINCE_CXX17 length(const char_type* __s) _NOEXCEPT {
    return std::__constexpr_strlen(__s);
  }

  static _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 const char_type*
  find(const char_type* __s, size_t __n, const char_type& __a) _NOEXCEPT {
    if (__n == 0)
      return nullptr;
    return std::__constexpr_memchr(__s, __a, __n);
  }

  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 char_type*
  move(char_type* __s1, const char_type* __s2, size_t __n) _NOEXCEPT {
    return std::__constexpr_memmove(__s1, __s2, __element_count(__n));
  }

  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 char_type*
  copy(char_type* __s1, const char_type* __s2, size_t __n) _NOEXCEPT {
    _LIBCPP_ASSERT_NON_OVERLAPPING_RANGES(!std::__is_pointer_in_range(__s1, __s1 + __n, __s2),
                                          "char_traits::copy: source and destination ranges overlap");
    std::__constexpr_memmove(__s1, __s2, __element_count(__n));
    return __s1;
  }

  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 char_type*
  assign(char_type* __s, size_t __n, char_type __a) _NOEXCEPT {
    std::fill_n(__s, __n, __a);
    return __s;
  }

  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int_type not_eof(int_type __c) _NOEXCEPT {
    return eq_int_type(__c, eof()) ? ~eof() : __c;
  }
  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR char_type to_char_type(int_type __c) _NOEXCEPT {
    return char_type(__c);
  }
  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int_type to_int_type(char_type __c) _NOEXCEPT {
    return int_type((unsigned char)__c);
  }
  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool eq_int_type(int_type __c1, int_type __c2) _NOEXCEPT {
    return __c1 == __c2;
  }
  static inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int_type eof() _NOEXCEPT { return int_type(EOF); }
};

template <class _CharT, class _IntT, _IntT _EOFVal>
struct __char_traits_base {
  using char_type  = _CharT;
  using int_type   = _IntT;
  using off_type   = streamoff;
  using state_type = mbstate_t;
#if _LIBCPP_STD_VER >= 20
  using comparison_category = strong_ordering;
#endif

  // There are different aliases for the different char types, but they are all aliases to this type
  using pos_type = fpos<mbstate_t>;

  _LIBCPP_HIDE_FROM_ABI static inline _LIBCPP_CONSTEXPR_SINCE_CXX17 void
  assign(char_type& __lhs, const char_type& __rhs) _NOEXCEPT {
    __lhs = __rhs;
  }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR bool eq(char_type __lhs, char_type __rhs) _NOEXCEPT {
    return __lhs == __rhs;
  }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR bool lt(char_type __lhs, char_type __rhs) _NOEXCEPT {
    return __lhs < __rhs;
  }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX20 char_type*
  move(char_type* __dest, const char_type* __src, size_t __n) _NOEXCEPT {
    return std::__constexpr_memmove(__dest, __src, __element_count(__n));
  }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX20 char_type*
  copy(char_type* __dest, const char_type* __src, size_t __n) _NOEXCEPT {
    _LIBCPP_ASSERT_NON_OVERLAPPING_RANGES(!std::__is_pointer_in_range(__dest, __dest + __n, __src),
                                          "char_traits::copy: source and destination ranges overlap");
    return std::__constexpr_memmove(__dest, __src, __element_count(__n));
  }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX20 char_type*
  assign(char_type* __str, size_t __n, char_type __fill_char) _NOEXCEPT {
    std::fill_n(__str, __n, __fill_char);
    return __str;
  }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR char_type to_char_type(int_type __c) _NOEXCEPT {
    return char_type(__c);
  }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR int_type to_int_type(char_type __c) _NOEXCEPT { return int_type(__c); }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR bool eq_int_type(int_type __lhs, int_type __rhs) _NOEXCEPT {
    return __lhs == __rhs;
  }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR int_type eof() _NOEXCEPT { return _EOFVal; }

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR int_type not_eof(int_type __c) _NOEXCEPT {
    return eq_int_type(__c, eof()) ? static_cast<int_type>(~eof()) : __c;
  }
};

// char_traits<wchar_t>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
struct _LIBCPP_TEMPLATE_VIS char_traits<wchar_t> : __char_traits_base<wchar_t, wint_t, static_cast<wint_t>(WEOF)> {
  static _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 int
  compare(const char_type* __s1, const char_type* __s2, size_t __n) _NOEXCEPT {
    if (__n == 0)
      return 0;
    return std::__constexpr_wmemcmp(__s1, __s2, __n);
  }

  static _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 size_t length(const char_type* __s) _NOEXCEPT {
    return std::__constexpr_wcslen(__s);
  }

  static _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 const char_type*
  find(const char_type* __s, size_t __n, const char_type& __a) _NOEXCEPT {
    if (__n == 0)
      return nullptr;
    return std::__constexpr_wmemchr(__s, __a, __n);
  }
};
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

#ifndef _LIBCPP_HAS_NO_CHAR8_T

template <>
struct _LIBCPP_TEMPLATE_VIS char_traits<char8_t>
    : __char_traits_base<char8_t, unsigned int, static_cast<unsigned int>(EOF)> {
  static _LIBCPP_HIDE_FROM_ABI constexpr int
  compare(const char_type* __s1, const char_type* __s2, size_t __n) noexcept {
    return std::__constexpr_memcmp(__s1, __s2, __element_count(__n));
  }

  static _LIBCPP_HIDE_FROM_ABI constexpr size_t length(const char_type* __str) noexcept {
    return std::__constexpr_strlen(__str);
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr const char_type*
  find(const char_type* __s, size_t __n, const char_type& __a) noexcept {
    return std::__constexpr_memchr(__s, __a, __n);
  }
};

#endif // _LIBCPP_HAS_NO_CHAR8_T

template <>
struct _LIBCPP_TEMPLATE_VIS char_traits<char16_t>
    : __char_traits_base<char16_t, uint_least16_t, static_cast<uint_least16_t>(0xFFFF)> {
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX17 int
  compare(const char_type* __s1, const char_type* __s2, size_t __n) _NOEXCEPT;
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX17 size_t length(const char_type* __s) _NOEXCEPT;

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX17 const char_type*
  find(const char_type* __s, size_t __n, const char_type& __a) _NOEXCEPT {
    __identity __proj;
    const char_type* __match = std::__find(__s, __s + __n, __a, __proj);
    if (__match == __s + __n)
      return nullptr;
    return __match;
  }
};

inline _LIBCPP_CONSTEXPR_SINCE_CXX17 int
char_traits<char16_t>::compare(const char_type* __s1, const char_type* __s2, size_t __n) _NOEXCEPT {
  for (; __n; --__n, ++__s1, ++__s2) {
    if (lt(*__s1, *__s2))
      return -1;
    if (lt(*__s2, *__s1))
      return 1;
  }
  return 0;
}

inline _LIBCPP_CONSTEXPR_SINCE_CXX17 size_t char_traits<char16_t>::length(const char_type* __s) _NOEXCEPT {
  size_t __len = 0;
  for (; !eq(*__s, char_type(0)); ++__s)
    ++__len;
  return __len;
}

template <>
struct _LIBCPP_TEMPLATE_VIS char_traits<char32_t>
    : __char_traits_base<char32_t, uint_least32_t, static_cast<uint_least32_t>(0xFFFFFFFF)> {
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX17 int
  compare(const char_type* __s1, const char_type* __s2, size_t __n) _NOEXCEPT;
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX17 size_t length(const char_type* __s) _NOEXCEPT;

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX17 const char_type*
  find(const char_type* __s, size_t __n, const char_type& __a) _NOEXCEPT {
    __identity __proj;
    const char_type* __match = std::__find(__s, __s + __n, __a, __proj);
    if (__match == __s + __n)
      return nullptr;
    return __match;
  }
};

inline _LIBCPP_CONSTEXPR_SINCE_CXX17 int
char_traits<char32_t>::compare(const char_type* __s1, const char_type* __s2, size_t __n) _NOEXCEPT {
  for (; __n; --__n, ++__s1, ++__s2) {
    if (lt(*__s1, *__s2))
      return -1;
    if (lt(*__s2, *__s1))
      return 1;
  }
  return 0;
}

inline _LIBCPP_CONSTEXPR_SINCE_CXX17 size_t char_traits<char32_t>::length(const char_type* __s) _NOEXCEPT {
  size_t __len = 0;
  for (; !eq(*__s, char_type(0)); ++__s)
    ++__len;
  return __len;
}

// helper fns for basic_string and string_view

// __str_find
template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_find(const _CharT* __p, _SizeT __sz, _CharT __c, _SizeT __pos) _NOEXCEPT {
  if (__pos >= __sz)
    return __npos;
  const _CharT* __r = _Traits::find(__p + __pos, __sz - __pos, __c);
  if (__r == nullptr)
    return __npos;
  return static_cast<_SizeT>(__r - __p);
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI inline _LIBCPP_CONSTEXPR_SINCE_CXX14 const _CharT* __search_substring(
    const _CharT* __first1, const _CharT* __last1, const _CharT* __first2, const _CharT* __last2) _NOEXCEPT {
  // Take advantage of knowing source and pattern lengths.
  // Stop short when source is smaller than pattern.
  const ptrdiff_t __len2 = __last2 - __first2;
  if (__len2 == 0)
    return __first1;

  ptrdiff_t __len1 = __last1 - __first1;
  if (__len1 < __len2)
    return __last1;

  // First element of __first2 is loop invariant.
  _CharT __f2 = *__first2;
  while (true) {
    __len1 = __last1 - __first1;
    // Check whether __first1 still has at least __len2 bytes.
    if (__len1 < __len2)
      return __last1;

    // Find __f2 the first byte matching in __first1.
    __first1 = _Traits::find(__first1, __len1 - __len2 + 1, __f2);
    if (__first1 == nullptr)
      return __last1;

    // It is faster to compare from the first byte of __first1 even if we
    // already know that it matches the first byte of __first2: this is because
    // __first2 is most likely aligned, as it is user's "pattern" string, and
    // __first1 + 1 is most likely not aligned, as the match is in the middle of
    // the string.
    if (_Traits::compare(__first1, __first2, __len2) == 0)
      return __first1;

    ++__first1;
  }
}

template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_find(const _CharT* __p, _SizeT __sz, const _CharT* __s, _SizeT __pos, _SizeT __n) _NOEXCEPT {
  if (__pos > __sz)
    return __npos;

  if (__n == 0) // There is nothing to search, just return __pos.
    return __pos;

  const _CharT* __r = std::__search_substring<_CharT, _Traits>(__p + __pos, __p + __sz, __s, __s + __n);

  if (__r == __p + __sz)
    return __npos;
  return static_cast<_SizeT>(__r - __p);
}

// __str_rfind

template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_rfind(const _CharT* __p, _SizeT __sz, _CharT __c, _SizeT __pos) _NOEXCEPT {
  if (__sz < 1)
    return __npos;
  if (__pos < __sz)
    ++__pos;
  else
    __pos = __sz;
  for (const _CharT* __ps = __p + __pos; __ps != __p;) {
    if (_Traits::eq(*--__ps, __c))
      return static_cast<_SizeT>(__ps - __p);
  }
  return __npos;
}

template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_rfind(const _CharT* __p, _SizeT __sz, const _CharT* __s, _SizeT __pos, _SizeT __n) _NOEXCEPT {
  __pos = std::min(__pos, __sz);
  if (__n < __sz - __pos)
    __pos += __n;
  else
    __pos = __sz;
  const _CharT* __r = std::__find_end_classic(__p, __p + __pos, __s, __s + __n, _Traits::eq);
  if (__n > 0 && __r == __p + __pos)
    return __npos;
  return static_cast<_SizeT>(__r - __p);
}

// __str_find_first_of
template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_find_first_of(const _CharT* __p, _SizeT __sz, const _CharT* __s, _SizeT __pos, _SizeT __n) _NOEXCEPT {
  if (__pos >= __sz || __n == 0)
    return __npos;
  const _CharT* __r = std::__find_first_of_ce(__p + __pos, __p + __sz, __s, __s + __n, _Traits::eq);
  if (__r == __p + __sz)
    return __npos;
  return static_cast<_SizeT>(__r - __p);
}

// __str_find_last_of
template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_find_last_of(const _CharT* __p, _SizeT __sz, const _CharT* __s, _SizeT __pos, _SizeT __n) _NOEXCEPT {
  if (__n != 0) {
    if (__pos < __sz)
      ++__pos;
    else
      __pos = __sz;
    for (const _CharT* __ps = __p + __pos; __ps != __p;) {
      const _CharT* __r = _Traits::find(__s, __n, *--__ps);
      if (__r)
        return static_cast<_SizeT>(__ps - __p);
    }
  }
  return __npos;
}

// __str_find_first_not_of
template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_find_first_not_of(const _CharT* __p, _SizeT __sz, const _CharT* __s, _SizeT __pos, _SizeT __n) _NOEXCEPT {
  if (__pos < __sz) {
    const _CharT* __pe = __p + __sz;
    for (const _CharT* __ps = __p + __pos; __ps != __pe; ++__ps)
      if (_Traits::find(__s, __n, *__ps) == nullptr)
        return static_cast<_SizeT>(__ps - __p);
  }
  return __npos;
}

template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_find_first_not_of(const _CharT* __p, _SizeT __sz, _CharT __c, _SizeT __pos) _NOEXCEPT {
  if (__pos < __sz) {
    const _CharT* __pe = __p + __sz;
    for (const _CharT* __ps = __p + __pos; __ps != __pe; ++__ps)
      if (!_Traits::eq(*__ps, __c))
        return static_cast<_SizeT>(__ps - __p);
  }
  return __npos;
}

// __str_find_last_not_of
template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_find_last_not_of(const _CharT* __p, _SizeT __sz, const _CharT* __s, _SizeT __pos, _SizeT __n) _NOEXCEPT {
  if (__pos < __sz)
    ++__pos;
  else
    __pos = __sz;
  for (const _CharT* __ps = __p + __pos; __ps != __p;)
    if (_Traits::find(__s, __n, *--__ps) == nullptr)
      return static_cast<_SizeT>(__ps - __p);
  return __npos;
}

template <class _CharT, class _SizeT, class _Traits, _SizeT __npos>
inline _SizeT _LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI
__str_find_last_not_of(const _CharT* __p, _SizeT __sz, _CharT __c, _SizeT __pos) _NOEXCEPT {
  if (__pos < __sz)
    ++__pos;
  else
    __pos = __sz;
  for (const _CharT* __ps = __p + __pos; __ps != __p;)
    if (!_Traits::eq(*--__ps, __c))
      return static_cast<_SizeT>(__ps - __p);
  return __npos;
}

template <class _Ptr>
inline _LIBCPP_HIDE_FROM_ABI size_t __do_string_hash(_Ptr __p, _Ptr __e) {
  typedef typename iterator_traits<_Ptr>::value_type value_type;
  return __murmur2_or_cityhash<size_t>()(__p, (__e - __p) * sizeof(value_type));
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___STRING_CHAR_TRAITS_H
