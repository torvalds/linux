//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_LINEAR_CONGRUENTIAL_ENGINE_H
#define _LIBCPP___RANDOM_LINEAR_CONGRUENTIAL_ENGINE_H

#include <__config>
#include <__random/is_seed_sequence.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_unsigned.h>
#include <cstdint>
#include <iosfwd>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

enum __lce_alg_type {
  _LCE_Full,
  _LCE_Part,
  _LCE_Schrage,
  _LCE_Promote,
};

template <unsigned long long __a,
          unsigned long long __c,
          unsigned long long __m,
          unsigned long long _Mp,
          bool _HasOverflow = (__a != 0ull && (__m & (__m - 1ull)) != 0ull),      // a != 0, m != 0, m != 2^n
          bool _Full        = (!_HasOverflow || __m - 1ull <= (_Mp - __c) / __a), // (a * x + c) % m works
          bool _Part        = (!_HasOverflow || __m - 1ull <= _Mp / __a),         // (a * x) % m works
          bool _Schrage     = (_HasOverflow && __m % __a <= __m / __a)>               // r <= q
struct __lce_alg_picker {
  static _LIBCPP_CONSTEXPR const __lce_alg_type __mode =
      _Full      ? _LCE_Full
      : _Part    ? _LCE_Part
      : _Schrage ? _LCE_Schrage
                 : _LCE_Promote;

#ifdef _LIBCPP_HAS_NO_INT128
  static_assert(_Mp != (unsigned long long)(-1) || _Full || _Part || _Schrage,
                "The current values for a, c, and m are not currently supported on platforms without __int128");
#endif
};

template <unsigned long long __a,
          unsigned long long __c,
          unsigned long long __m,
          unsigned long long _Mp,
          __lce_alg_type _Mode = __lce_alg_picker<__a, __c, __m, _Mp>::__mode>
struct __lce_ta;

// 64

#ifndef _LIBCPP_HAS_NO_INT128
template <unsigned long long _Ap, unsigned long long _Cp, unsigned long long _Mp>
struct __lce_ta<_Ap, _Cp, _Mp, (unsigned long long)(-1), _LCE_Promote> {
  typedef unsigned long long result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __xp) {
    __extension__ using __calc_type = unsigned __int128;
    const __calc_type __a           = static_cast<__calc_type>(_Ap);
    const __calc_type __c           = static_cast<__calc_type>(_Cp);
    const __calc_type __m           = static_cast<__calc_type>(_Mp);
    const __calc_type __x           = static_cast<__calc_type>(__xp);
    return static_cast<result_type>((__a * __x + __c) % __m);
  }
};
#endif

template <unsigned long long __a, unsigned long long __c, unsigned long long __m>
struct __lce_ta<__a, __c, __m, (unsigned long long)(-1), _LCE_Schrage> {
  typedef unsigned long long result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    // Schrage's algorithm
    const result_type __q  = __m / __a;
    const result_type __r  = __m % __a;
    const result_type __t0 = __a * (__x % __q);
    const result_type __t1 = __r * (__x / __q);
    __x                    = __t0 + (__t0 < __t1) * __m - __t1;
    __x += __c - (__x >= __m - __c) * __m;
    return __x;
  }
};

template <unsigned long long __a, unsigned long long __m>
struct __lce_ta<__a, 0ull, __m, (unsigned long long)(-1), _LCE_Schrage> {
  typedef unsigned long long result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    // Schrage's algorithm
    const result_type __q  = __m / __a;
    const result_type __r  = __m % __a;
    const result_type __t0 = __a * (__x % __q);
    const result_type __t1 = __r * (__x / __q);
    __x                    = __t0 + (__t0 < __t1) * __m - __t1;
    return __x;
  }
};

template <unsigned long long __a, unsigned long long __c, unsigned long long __m>
struct __lce_ta<__a, __c, __m, (unsigned long long)(-1), _LCE_Part> {
  typedef unsigned long long result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    // Use (((a*x) % m) + c) % m
    __x = (__a * __x) % __m;
    __x += __c - (__x >= __m - __c) * __m;
    return __x;
  }
};

template <unsigned long long __a, unsigned long long __c, unsigned long long __m>
struct __lce_ta<__a, __c, __m, (unsigned long long)(-1), _LCE_Full> {
  typedef unsigned long long result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) { return (__a * __x + __c) % __m; }
};

template <unsigned long long __a, unsigned long long __c>
struct __lce_ta<__a, __c, 0ull, (unsigned long long)(-1), _LCE_Full> {
  typedef unsigned long long result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) { return __a * __x + __c; }
};

// 32

template <unsigned long long __a, unsigned long long __c, unsigned long long __m>
struct __lce_ta<__a, __c, __m, unsigned(-1), _LCE_Promote> {
  typedef unsigned result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    return static_cast<result_type>(__lce_ta<__a, __c, __m, (unsigned long long)(-1)>::next(__x));
  }
};

template <unsigned long long _Ap, unsigned long long _Cp, unsigned long long _Mp>
struct __lce_ta<_Ap, _Cp, _Mp, unsigned(-1), _LCE_Schrage> {
  typedef unsigned result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    const result_type __a = static_cast<result_type>(_Ap);
    const result_type __c = static_cast<result_type>(_Cp);
    const result_type __m = static_cast<result_type>(_Mp);
    // Schrage's algorithm
    const result_type __q  = __m / __a;
    const result_type __r  = __m % __a;
    const result_type __t0 = __a * (__x % __q);
    const result_type __t1 = __r * (__x / __q);
    __x                    = __t0 + (__t0 < __t1) * __m - __t1;
    __x += __c - (__x >= __m - __c) * __m;
    return __x;
  }
};

template <unsigned long long _Ap, unsigned long long _Mp>
struct __lce_ta<_Ap, 0ull, _Mp, unsigned(-1), _LCE_Schrage> {
  typedef unsigned result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    const result_type __a = static_cast<result_type>(_Ap);
    const result_type __m = static_cast<result_type>(_Mp);
    // Schrage's algorithm
    const result_type __q  = __m / __a;
    const result_type __r  = __m % __a;
    const result_type __t0 = __a * (__x % __q);
    const result_type __t1 = __r * (__x / __q);
    __x                    = __t0 + (__t0 < __t1) * __m - __t1;
    return __x;
  }
};

template <unsigned long long _Ap, unsigned long long _Cp, unsigned long long _Mp>
struct __lce_ta<_Ap, _Cp, _Mp, unsigned(-1), _LCE_Part> {
  typedef unsigned result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    const result_type __a = static_cast<result_type>(_Ap);
    const result_type __c = static_cast<result_type>(_Cp);
    const result_type __m = static_cast<result_type>(_Mp);
    // Use (((a*x) % m) + c) % m
    __x = (__a * __x) % __m;
    __x += __c - (__x >= __m - __c) * __m;
    return __x;
  }
};

template <unsigned long long _Ap, unsigned long long _Cp, unsigned long long _Mp>
struct __lce_ta<_Ap, _Cp, _Mp, unsigned(-1), _LCE_Full> {
  typedef unsigned result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    const result_type __a = static_cast<result_type>(_Ap);
    const result_type __c = static_cast<result_type>(_Cp);
    const result_type __m = static_cast<result_type>(_Mp);
    return (__a * __x + __c) % __m;
  }
};

template <unsigned long long _Ap, unsigned long long _Cp>
struct __lce_ta<_Ap, _Cp, 0ull, unsigned(-1), _LCE_Full> {
  typedef unsigned result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    const result_type __a = static_cast<result_type>(_Ap);
    const result_type __c = static_cast<result_type>(_Cp);
    return __a * __x + __c;
  }
};

// 16

template <unsigned long long __a, unsigned long long __c, unsigned long long __m, __lce_alg_type __mode>
struct __lce_ta<__a, __c, __m, (unsigned short)(-1), __mode> {
  typedef unsigned short result_type;
  _LIBCPP_HIDE_FROM_ABI static result_type next(result_type __x) {
    return static_cast<result_type>(__lce_ta<__a, __c, __m, unsigned(-1)>::next(__x));
  }
};

template <class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
class _LIBCPP_TEMPLATE_VIS linear_congruential_engine;

template <class _CharT, class _Traits, class _Up, _Up _Ap, _Up _Cp, _Up _Np>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const linear_congruential_engine<_Up, _Ap, _Cp, _Np>&);

template <class _CharT, class _Traits, class _Up, _Up _Ap, _Up _Cp, _Up _Np>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, linear_congruential_engine<_Up, _Ap, _Cp, _Np>& __x);

template <class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
class _LIBCPP_TEMPLATE_VIS linear_congruential_engine {
public:
  // types
  typedef _UIntType result_type;

private:
  result_type __x_;

  static _LIBCPP_CONSTEXPR const result_type _Mp = result_type(-1);

  static_assert(__m == 0 || __a < __m, "linear_congruential_engine invalid parameters");
  static_assert(__m == 0 || __c < __m, "linear_congruential_engine invalid parameters");
  static_assert(is_unsigned<_UIntType>::value, "_UIntType must be unsigned type");

public:
  static _LIBCPP_CONSTEXPR const result_type _Min = __c == 0u ? 1u : 0u;
  static _LIBCPP_CONSTEXPR const result_type _Max = __m - _UIntType(1u);
  static_assert(_Min < _Max, "linear_congruential_engine invalid parameters");

  // engine characteristics
  static _LIBCPP_CONSTEXPR const result_type multiplier = __a;
  static _LIBCPP_CONSTEXPR const result_type increment  = __c;
  static _LIBCPP_CONSTEXPR const result_type modulus    = __m;
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type min() { return _Min; }
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type max() { return _Max; }
  static _LIBCPP_CONSTEXPR const result_type default_seed = 1u;

  // constructors and seeding functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI linear_congruential_engine() : linear_congruential_engine(default_seed) {}
  _LIBCPP_HIDE_FROM_ABI explicit linear_congruential_engine(result_type __s) { seed(__s); }
#else
  _LIBCPP_HIDE_FROM_ABI explicit linear_congruential_engine(result_type __s = default_seed) { seed(__s); }
#endif
  template <class _Sseq, __enable_if_t<__is_seed_sequence<_Sseq, linear_congruential_engine>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI explicit linear_congruential_engine(_Sseq& __q) {
    seed(__q);
  }
  _LIBCPP_HIDE_FROM_ABI void seed(result_type __s = default_seed) {
    seed(integral_constant<bool, __m == 0>(), integral_constant<bool, __c == 0>(), __s);
  }
  template <class _Sseq, __enable_if_t<__is_seed_sequence<_Sseq, linear_congruential_engine>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI void seed(_Sseq& __q) {
    __seed(
        __q,
        integral_constant<unsigned,
                          1 + (__m == 0 ? (sizeof(result_type) * __CHAR_BIT__ - 1) / 32 : (__m > 0x100000000ull))>());
  }

  // generating functions
  _LIBCPP_HIDE_FROM_ABI result_type operator()() {
    return __x_ = static_cast<result_type>(__lce_ta<__a, __c, __m, _Mp>::next(__x_));
  }
  _LIBCPP_HIDE_FROM_ABI void discard(unsigned long long __z) {
    for (; __z; --__z)
      operator()();
  }

  friend _LIBCPP_HIDE_FROM_ABI bool
  operator==(const linear_congruential_engine& __x, const linear_congruential_engine& __y) {
    return __x.__x_ == __y.__x_;
  }
  friend _LIBCPP_HIDE_FROM_ABI bool
  operator!=(const linear_congruential_engine& __x, const linear_congruential_engine& __y) {
    return !(__x == __y);
  }

private:
  _LIBCPP_HIDE_FROM_ABI void seed(true_type, true_type, result_type __s) { __x_ = __s == 0 ? 1 : __s; }
  _LIBCPP_HIDE_FROM_ABI void seed(true_type, false_type, result_type __s) { __x_ = __s; }
  _LIBCPP_HIDE_FROM_ABI void seed(false_type, true_type, result_type __s) { __x_ = __s % __m == 0 ? 1 : __s % __m; }
  _LIBCPP_HIDE_FROM_ABI void seed(false_type, false_type, result_type __s) { __x_ = __s % __m; }

  template <class _Sseq>
  _LIBCPP_HIDE_FROM_ABI void __seed(_Sseq& __q, integral_constant<unsigned, 1>);
  template <class _Sseq>
  _LIBCPP_HIDE_FROM_ABI void __seed(_Sseq& __q, integral_constant<unsigned, 2>);

  template <class _CharT, class _Traits, class _Up, _Up _Ap, _Up _Cp, _Up _Np>
  friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const linear_congruential_engine<_Up, _Ap, _Cp, _Np>&);

  template <class _CharT, class _Traits, class _Up, _Up _Ap, _Up _Cp, _Up _Np>
  friend basic_istream<_CharT, _Traits>&
  operator>>(basic_istream<_CharT, _Traits>& __is, linear_congruential_engine<_Up, _Ap, _Cp, _Np>& __x);
};

template <class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
_LIBCPP_CONSTEXPR const typename linear_congruential_engine<_UIntType, __a, __c, __m>::result_type
    linear_congruential_engine<_UIntType, __a, __c, __m>::multiplier;

template <class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
_LIBCPP_CONSTEXPR const typename linear_congruential_engine<_UIntType, __a, __c, __m>::result_type
    linear_congruential_engine<_UIntType, __a, __c, __m>::increment;

template <class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
_LIBCPP_CONSTEXPR const typename linear_congruential_engine<_UIntType, __a, __c, __m>::result_type
    linear_congruential_engine<_UIntType, __a, __c, __m>::modulus;

template <class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
_LIBCPP_CONSTEXPR const typename linear_congruential_engine<_UIntType, __a, __c, __m>::result_type
    linear_congruential_engine<_UIntType, __a, __c, __m>::default_seed;

template <class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
template <class _Sseq>
void linear_congruential_engine<_UIntType, __a, __c, __m>::__seed(_Sseq& __q, integral_constant<unsigned, 1>) {
  const unsigned __k = 1;
  uint32_t __ar[__k + 3];
  __q.generate(__ar, __ar + __k + 3);
  result_type __s = static_cast<result_type>(__ar[3] % __m);
  __x_            = __c == 0 && __s == 0 ? result_type(1) : __s;
}

template <class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
template <class _Sseq>
void linear_congruential_engine<_UIntType, __a, __c, __m>::__seed(_Sseq& __q, integral_constant<unsigned, 2>) {
  const unsigned __k = 2;
  uint32_t __ar[__k + 3];
  __q.generate(__ar, __ar + __k + 3);
  result_type __s = static_cast<result_type>((__ar[3] + ((uint64_t)__ar[4] << 32)) % __m);
  __x_            = __c == 0 && __s == 0 ? result_type(1) : __s;
}

template <class _CharT, class _Traits, class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
inline _LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const linear_congruential_engine<_UIntType, __a, __c, __m>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _Ostream;
  __os.flags(_Ostream::dec | _Ostream::left);
  __os.fill(__os.widen(' '));
  return __os << __x.__x_;
}

template <class _CharT, class _Traits, class _UIntType, _UIntType __a, _UIntType __c, _UIntType __m>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, linear_congruential_engine<_UIntType, __a, __c, __m>& __x) {
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  _UIntType __t;
  __is >> __t;
  if (!__is.fail())
    __x.__x_ = __t;
  return __is;
}

typedef linear_congruential_engine<uint_fast32_t, 16807, 0, 2147483647> minstd_rand0;
typedef linear_congruential_engine<uint_fast32_t, 48271, 0, 2147483647> minstd_rand;

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_LINEAR_CONGRUENTIAL_ENGINE_H
