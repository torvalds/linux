//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_SEED_SEQ_H
#define _LIBCPP___RANDOM_SEED_SEQ_H

#include <__algorithm/copy.h>
#include <__algorithm/fill.h>
#include <__algorithm/max.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__type_traits/is_unsigned.h>
#include <cstdint>
#include <initializer_list>
#include <vector>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

class _LIBCPP_TEMPLATE_VIS seed_seq {
public:
  // types
  typedef uint32_t result_type;

  // constructors
  _LIBCPP_HIDE_FROM_ABI seed_seq() _NOEXCEPT {}
#ifndef _LIBCPP_CXX03_LANG
  template <class _Tp, __enable_if_t<is_integral<_Tp>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI seed_seq(initializer_list<_Tp> __il) {
    __init(__il.begin(), __il.end());
  }
#endif // _LIBCPP_CXX03_LANG

  template <class _InputIterator>
  _LIBCPP_HIDE_FROM_ABI seed_seq(_InputIterator __first, _InputIterator __last) {
    static_assert(is_integral<typename iterator_traits<_InputIterator>::value_type>::value,
                  "Mandates: iterator_traits<InputIterator>::value_type is an integer type");
    __init(__first, __last);
  }

  // generating functions
  template <class _RandomAccessIterator>
  _LIBCPP_HIDE_FROM_ABI void generate(_RandomAccessIterator __first, _RandomAccessIterator __last);

  // property functions
  _LIBCPP_HIDE_FROM_ABI size_t size() const _NOEXCEPT { return __v_.size(); }
  template <class _OutputIterator>
  _LIBCPP_HIDE_FROM_ABI void param(_OutputIterator __dest) const {
    std::copy(__v_.begin(), __v_.end(), __dest);
  }

  seed_seq(const seed_seq&)       = delete;
  void operator=(const seed_seq&) = delete;

  _LIBCPP_HIDE_FROM_ABI static result_type _Tp(result_type __x) { return __x ^ (__x >> 27); }

private:
  template <class _InputIterator>
  _LIBCPP_HIDE_FROM_ABI void __init(_InputIterator __first, _InputIterator __last);

  vector<result_type> __v_;
};

template <class _InputIterator>
void seed_seq::__init(_InputIterator __first, _InputIterator __last) {
  for (_InputIterator __s = __first; __s != __last; ++__s)
    __v_.push_back(*__s & 0xFFFFFFFF);
}

template <class _RandomAccessIterator>
void seed_seq::generate(_RandomAccessIterator __first, _RandomAccessIterator __last) {
  using _ValueType = typename iterator_traits<_RandomAccessIterator>::value_type;
  static_assert(is_unsigned<_ValueType>::value && sizeof(_ValueType) >= sizeof(uint32_t),
                "[rand.util.seedseq]/7 requires the value_type of the iterator to be an unsigned "
                "integer capable of accommodating 32-bit quantities.");

  if (__first != __last) {
    std::fill(__first, __last, 0x8b8b8b8b);
    const size_t __n = static_cast<size_t>(__last - __first);
    const size_t __s = __v_.size();
    const size_t __t = (__n >= 623) ? 11 : (__n >= 68) ? 7 : (__n >= 39) ? 5 : (__n >= 7) ? 3 : (__n - 1) / 2;
    const size_t __p = (__n - __t) / 2;
    const size_t __q = __p + __t;
    const size_t __m = std::max(__s + 1, __n);
    // __k = 0;
    {
      result_type __r = 1664525 * _Tp(__first[0] ^ __first[__p] ^ __first[__n - 1]);
      __first[__p] += __r;
      __r += __s;
      __first[__q] += __r;
      __first[0] = __r;
    }
    // Initialize indexing terms used with if statements as an optimization to
    // avoid calculating modulo n on every loop iteration for each term.
    size_t __kmodn  = 0;         // __k % __n
    size_t __k1modn = __n - 1;   // (__k - 1) % __n
    size_t __kpmodn = __p % __n; // (__k + __p) % __n
    size_t __kqmodn = __q % __n; // (__k + __q) % __n

    for (size_t __k = 1; __k <= __s; ++__k) {
      if (++__kmodn == __n)
        __kmodn = 0;
      if (++__k1modn == __n)
        __k1modn = 0;
      if (++__kpmodn == __n)
        __kpmodn = 0;
      if (++__kqmodn == __n)
        __kqmodn = 0;

      result_type __r = 1664525 * _Tp(__first[__kmodn] ^ __first[__kpmodn] ^ __first[__k1modn]);
      __first[__kpmodn] += __r;
      __r += __kmodn + __v_[__k - 1];
      __first[__kqmodn] += __r;
      __first[__kmodn] = __r;
    }
    for (size_t __k = __s + 1; __k < __m; ++__k) {
      if (++__kmodn == __n)
        __kmodn = 0;
      if (++__k1modn == __n)
        __k1modn = 0;
      if (++__kpmodn == __n)
        __kpmodn = 0;
      if (++__kqmodn == __n)
        __kqmodn = 0;

      result_type __r = 1664525 * _Tp(__first[__kmodn] ^ __first[__kpmodn] ^ __first[__k1modn]);
      __first[__kpmodn] += __r;
      __r += __kmodn;
      __first[__kqmodn] += __r;
      __first[__kmodn] = __r;
    }
    for (size_t __k = __m; __k < __m + __n; ++__k) {
      if (++__kmodn == __n)
        __kmodn = 0;
      if (++__k1modn == __n)
        __k1modn = 0;
      if (++__kpmodn == __n)
        __kpmodn = 0;
      if (++__kqmodn == __n)
        __kqmodn = 0;

      result_type __r = 1566083941 * _Tp(__first[__kmodn] + __first[__kpmodn] + __first[__k1modn]);
      __first[__kpmodn] ^= __r;
      __r -= __kmodn;
      __first[__kqmodn] ^= __r;
      __first[__kmodn] = __r;
    }
  }
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_SEED_SEQ_H
