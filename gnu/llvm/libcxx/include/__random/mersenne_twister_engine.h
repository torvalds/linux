//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_MERSENNE_TWISTER_ENGINE_H
#define _LIBCPP___RANDOM_MERSENNE_TWISTER_ENGINE_H

#include <__algorithm/equal.h>
#include <__algorithm/min.h>
#include <__config>
#include <__random/is_seed_sequence.h>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
class _LIBCPP_TEMPLATE_VIS mersenne_twister_engine;

template <class _UInt,
          size_t _Wp,
          size_t _Np,
          size_t _Mp,
          size_t _Rp,
          _UInt _Ap,
          size_t _Up,
          _UInt _Dp,
          size_t _Sp,
          _UInt _Bp,
          size_t _Tp,
          _UInt _Cp,
          size_t _Lp,
          _UInt _Fp>
_LIBCPP_HIDE_FROM_ABI bool
operator==(const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x,
           const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __y);

template <class _UInt,
          size_t _Wp,
          size_t _Np,
          size_t _Mp,
          size_t _Rp,
          _UInt _Ap,
          size_t _Up,
          _UInt _Dp,
          size_t _Sp,
          _UInt _Bp,
          size_t _Tp,
          _UInt _Cp,
          size_t _Lp,
          _UInt _Fp>
_LIBCPP_HIDE_FROM_ABI bool
operator!=(const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x,
           const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __y);

template <class _CharT,
          class _Traits,
          class _UInt,
          size_t _Wp,
          size_t _Np,
          size_t _Mp,
          size_t _Rp,
          _UInt _Ap,
          size_t _Up,
          _UInt _Dp,
          size_t _Sp,
          _UInt _Bp,
          size_t _Tp,
          _UInt _Cp,
          size_t _Lp,
          _UInt _Fp>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os,
           const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x);

template <class _CharT,
          class _Traits,
          class _UInt,
          size_t _Wp,
          size_t _Np,
          size_t _Mp,
          size_t _Rp,
          _UInt _Ap,
          size_t _Up,
          _UInt _Dp,
          size_t _Sp,
          _UInt _Bp,
          size_t _Tp,
          _UInt _Cp,
          size_t _Lp,
          _UInt _Fp>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is,
           mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x);

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
class _LIBCPP_TEMPLATE_VIS mersenne_twister_engine {
public:
  // types
  typedef _UIntType result_type;

private:
  result_type __x_[__n];
  size_t __i_;

  static_assert(0 < __m, "mersenne_twister_engine invalid parameters");
  static_assert(__m <= __n, "mersenne_twister_engine invalid parameters");
  static _LIBCPP_CONSTEXPR const result_type _Dt = numeric_limits<result_type>::digits;
  static_assert(__w <= _Dt, "mersenne_twister_engine invalid parameters");
  static_assert(2 <= __w, "mersenne_twister_engine invalid parameters");
  static_assert(__r <= __w, "mersenne_twister_engine invalid parameters");
  static_assert(__u <= __w, "mersenne_twister_engine invalid parameters");
  static_assert(__s <= __w, "mersenne_twister_engine invalid parameters");
  static_assert(__t <= __w, "mersenne_twister_engine invalid parameters");
  static_assert(__l <= __w, "mersenne_twister_engine invalid parameters");

public:
  static _LIBCPP_CONSTEXPR const result_type _Min = 0;
  static _LIBCPP_CONSTEXPR const result_type _Max =
      __w == _Dt ? result_type(~0) : (result_type(1) << __w) - result_type(1);
  static_assert(_Min < _Max, "mersenne_twister_engine invalid parameters");
  static_assert(__a <= _Max, "mersenne_twister_engine invalid parameters");
  static_assert(__b <= _Max, "mersenne_twister_engine invalid parameters");
  static_assert(__c <= _Max, "mersenne_twister_engine invalid parameters");
  static_assert(__d <= _Max, "mersenne_twister_engine invalid parameters");
  static_assert(__f <= _Max, "mersenne_twister_engine invalid parameters");

  // engine characteristics
  static _LIBCPP_CONSTEXPR const size_t word_size                      = __w;
  static _LIBCPP_CONSTEXPR const size_t state_size                     = __n;
  static _LIBCPP_CONSTEXPR const size_t shift_size                     = __m;
  static _LIBCPP_CONSTEXPR const size_t mask_bits                      = __r;
  static _LIBCPP_CONSTEXPR const result_type xor_mask                  = __a;
  static _LIBCPP_CONSTEXPR const size_t tempering_u                    = __u;
  static _LIBCPP_CONSTEXPR const result_type tempering_d               = __d;
  static _LIBCPP_CONSTEXPR const size_t tempering_s                    = __s;
  static _LIBCPP_CONSTEXPR const result_type tempering_b               = __b;
  static _LIBCPP_CONSTEXPR const size_t tempering_t                    = __t;
  static _LIBCPP_CONSTEXPR const result_type tempering_c               = __c;
  static _LIBCPP_CONSTEXPR const size_t tempering_l                    = __l;
  static _LIBCPP_CONSTEXPR const result_type initialization_multiplier = __f;
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type min() { return _Min; }
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR result_type max() { return _Max; }
  static _LIBCPP_CONSTEXPR const result_type default_seed = 5489u;

  // constructors and seeding functions
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI mersenne_twister_engine() : mersenne_twister_engine(default_seed) {}
  _LIBCPP_HIDE_FROM_ABI explicit mersenne_twister_engine(result_type __sd) { seed(__sd); }
#else
  _LIBCPP_HIDE_FROM_ABI explicit mersenne_twister_engine(result_type __sd = default_seed) { seed(__sd); }
#endif
  template <class _Sseq, __enable_if_t<__is_seed_sequence<_Sseq, mersenne_twister_engine>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI explicit mersenne_twister_engine(_Sseq& __q) {
    seed(__q);
  }
  _LIBCPP_HIDE_FROM_ABI void seed(result_type __sd = default_seed);
  template <class _Sseq, __enable_if_t<__is_seed_sequence<_Sseq, mersenne_twister_engine>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI void seed(_Sseq& __q) {
    __seed(__q, integral_constant<unsigned, 1 + (__w - 1) / 32>());
  }

  // generating functions
  _LIBCPP_HIDE_FROM_ABI result_type operator()();
  _LIBCPP_HIDE_FROM_ABI void discard(unsigned long long __z) {
    for (; __z; --__z)
      operator()();
  }

  template <class _UInt,
            size_t _Wp,
            size_t _Np,
            size_t _Mp,
            size_t _Rp,
            _UInt _Ap,
            size_t _Up,
            _UInt _Dp,
            size_t _Sp,
            _UInt _Bp,
            size_t _Tp,
            _UInt _Cp,
            size_t _Lp,
            _UInt _Fp>
  friend bool operator==(
      const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x,
      const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __y);

  template <class _UInt,
            size_t _Wp,
            size_t _Np,
            size_t _Mp,
            size_t _Rp,
            _UInt _Ap,
            size_t _Up,
            _UInt _Dp,
            size_t _Sp,
            _UInt _Bp,
            size_t _Tp,
            _UInt _Cp,
            size_t _Lp,
            _UInt _Fp>
  friend bool operator!=(
      const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x,
      const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __y);

  template <class _CharT,
            class _Traits,
            class _UInt,
            size_t _Wp,
            size_t _Np,
            size_t _Mp,
            size_t _Rp,
            _UInt _Ap,
            size_t _Up,
            _UInt _Dp,
            size_t _Sp,
            _UInt _Bp,
            size_t _Tp,
            _UInt _Cp,
            size_t _Lp,
            _UInt _Fp>
  friend basic_ostream<_CharT, _Traits>& operator<<(
      basic_ostream<_CharT, _Traits>& __os,
      const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x);

  template <class _CharT,
            class _Traits,
            class _UInt,
            size_t _Wp,
            size_t _Np,
            size_t _Mp,
            size_t _Rp,
            _UInt _Ap,
            size_t _Up,
            _UInt _Dp,
            size_t _Sp,
            _UInt _Bp,
            size_t _Tp,
            _UInt _Cp,
            size_t _Lp,
            _UInt _Fp>
  friend basic_istream<_CharT, _Traits>&
  operator>>(basic_istream<_CharT, _Traits>& __is,
             mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x);

private:
  template <class _Sseq>
  _LIBCPP_HIDE_FROM_ABI void __seed(_Sseq& __q, integral_constant<unsigned, 1>);
  template <class _Sseq>
  _LIBCPP_HIDE_FROM_ABI void __seed(_Sseq& __q, integral_constant<unsigned, 2>);

  template <size_t __count,
            __enable_if_t<__count< __w, int> = 0> _LIBCPP_HIDE_FROM_ABI static result_type __lshift(result_type __x) {
    return (__x << __count) & _Max;
  }

  template <size_t __count, __enable_if_t<(__count >= __w), int> = 0>
  _LIBCPP_HIDE_FROM_ABI static result_type __lshift(result_type) {
    return result_type(0);
  }

  template <size_t __count,
            __enable_if_t<__count< _Dt, int> = 0> _LIBCPP_HIDE_FROM_ABI static result_type __rshift(result_type __x) {
    return __x >> __count;
  }

  template <size_t __count, __enable_if_t<(__count >= _Dt), int> = 0>
  _LIBCPP_HIDE_FROM_ABI static result_type __rshift(result_type) {
    return result_type(0);
  }
};

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const size_t
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::word_size;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const size_t
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::state_size;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const size_t
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::shift_size;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const size_t
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::mask_bits;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const typename mersenne_twister_engine<
    _UIntType,
    __w,
    __n,
    __m,
    __r,
    __a,
    __u,
    __d,
    __s,
    __b,
    __t,
    __c,
    __l,
    __f>::result_type
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::xor_mask;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const size_t
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::tempering_u;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const typename mersenne_twister_engine<
    _UIntType,
    __w,
    __n,
    __m,
    __r,
    __a,
    __u,
    __d,
    __s,
    __b,
    __t,
    __c,
    __l,
    __f>::result_type
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::tempering_d;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const size_t
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::tempering_s;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const typename mersenne_twister_engine<
    _UIntType,
    __w,
    __n,
    __m,
    __r,
    __a,
    __u,
    __d,
    __s,
    __b,
    __t,
    __c,
    __l,
    __f>::result_type
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::tempering_b;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const size_t
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::tempering_t;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const typename mersenne_twister_engine<
    _UIntType,
    __w,
    __n,
    __m,
    __r,
    __a,
    __u,
    __d,
    __s,
    __b,
    __t,
    __c,
    __l,
    __f>::result_type
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::tempering_c;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const size_t
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::tempering_l;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const typename mersenne_twister_engine<
    _UIntType,
    __w,
    __n,
    __m,
    __r,
    __a,
    __u,
    __d,
    __s,
    __b,
    __t,
    __c,
    __l,
    __f>::result_type
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::
        initialization_multiplier;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_LIBCPP_CONSTEXPR const typename mersenne_twister_engine<
    _UIntType,
    __w,
    __n,
    __m,
    __r,
    __a,
    __u,
    __d,
    __s,
    __b,
    __t,
    __c,
    __l,
    __f>::result_type
    mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::default_seed;

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
void mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::seed(
    result_type __sd) _LIBCPP_DISABLE_UBSAN_UNSIGNED_INTEGER_CHECK { // __w >= 2
  __x_[0] = __sd & _Max;
  for (size_t __i = 1; __i < __n; ++__i)
    __x_[__i] = (__f * (__x_[__i - 1] ^ __rshift<__w - 2>(__x_[__i - 1])) + __i) & _Max;
  __i_ = 0;
}

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
template <class _Sseq>
void mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::__seed(
    _Sseq& __q, integral_constant<unsigned, 1>) {
  const unsigned __k = 1;
  uint32_t __ar[__n * __k];
  __q.generate(__ar, __ar + __n * __k);
  for (size_t __i = 0; __i < __n; ++__i)
    __x_[__i] = static_cast<result_type>(__ar[__i] & _Max);
  const result_type __mask = __r == _Dt ? result_type(~0) : (result_type(1) << __r) - result_type(1);
  __i_                     = 0;
  if ((__x_[0] & ~__mask) == 0) {
    for (size_t __i = 1; __i < __n; ++__i)
      if (__x_[__i] != 0)
        return;
    __x_[0] = result_type(1) << (__w - 1);
  }
}

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
template <class _Sseq>
void mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::__seed(
    _Sseq& __q, integral_constant<unsigned, 2>) {
  const unsigned __k = 2;
  uint32_t __ar[__n * __k];
  __q.generate(__ar, __ar + __n * __k);
  for (size_t __i = 0; __i < __n; ++__i)
    __x_[__i] = static_cast<result_type>((__ar[2 * __i] + ((uint64_t)__ar[2 * __i + 1] << 32)) & _Max);
  const result_type __mask = __r == _Dt ? result_type(~0) : (result_type(1) << __r) - result_type(1);
  __i_                     = 0;
  if ((__x_[0] & ~__mask) == 0) {
    for (size_t __i = 1; __i < __n; ++__i)
      if (__x_[__i] != 0)
        return;
    __x_[0] = result_type(1) << (__w - 1);
  }
}

template <class _UIntType,
          size_t __w,
          size_t __n,
          size_t __m,
          size_t __r,
          _UIntType __a,
          size_t __u,
          _UIntType __d,
          size_t __s,
          _UIntType __b,
          size_t __t,
          _UIntType __c,
          size_t __l,
          _UIntType __f>
_UIntType
mersenne_twister_engine<_UIntType, __w, __n, __m, __r, __a, __u, __d, __s, __b, __t, __c, __l, __f>::operator()() {
  const size_t __j         = (__i_ + 1) % __n;
  const result_type __mask = __r == _Dt ? result_type(~0) : (result_type(1) << __r) - result_type(1);
  const result_type __yp   = (__x_[__i_] & ~__mask) | (__x_[__j] & __mask);
  const size_t __k         = (__i_ + __m) % __n;
  __x_[__i_]               = __x_[__k] ^ __rshift<1>(__yp) ^ (__a * (__yp & 1));
  result_type __z          = __x_[__i_] ^ (__rshift<__u>(__x_[__i_]) & __d);
  __i_                     = __j;
  __z ^= __lshift<__s>(__z) & __b;
  __z ^= __lshift<__t>(__z) & __c;
  return __z ^ __rshift<__l>(__z);
}

template <class _UInt,
          size_t _Wp,
          size_t _Np,
          size_t _Mp,
          size_t _Rp,
          _UInt _Ap,
          size_t _Up,
          _UInt _Dp,
          size_t _Sp,
          _UInt _Bp,
          size_t _Tp,
          _UInt _Cp,
          size_t _Lp,
          _UInt _Fp>
_LIBCPP_HIDE_FROM_ABI bool
operator==(const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x,
           const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __y) {
  if (__x.__i_ == __y.__i_)
    return std::equal(__x.__x_, __x.__x_ + _Np, __y.__x_);
  if (__x.__i_ == 0 || __y.__i_ == 0) {
    size_t __j = std::min(_Np - __x.__i_, _Np - __y.__i_);
    if (!std::equal(__x.__x_ + __x.__i_, __x.__x_ + __x.__i_ + __j, __y.__x_ + __y.__i_))
      return false;
    if (__x.__i_ == 0)
      return std::equal(__x.__x_ + __j, __x.__x_ + _Np, __y.__x_);
    return std::equal(__x.__x_, __x.__x_ + (_Np - __j), __y.__x_ + __j);
  }
  if (__x.__i_ < __y.__i_) {
    size_t __j = _Np - __y.__i_;
    if (!std::equal(__x.__x_ + __x.__i_, __x.__x_ + (__x.__i_ + __j), __y.__x_ + __y.__i_))
      return false;
    if (!std::equal(__x.__x_ + (__x.__i_ + __j), __x.__x_ + _Np, __y.__x_))
      return false;
    return std::equal(__x.__x_, __x.__x_ + __x.__i_, __y.__x_ + (_Np - (__x.__i_ + __j)));
  }
  size_t __j = _Np - __x.__i_;
  if (!std::equal(__y.__x_ + __y.__i_, __y.__x_ + (__y.__i_ + __j), __x.__x_ + __x.__i_))
    return false;
  if (!std::equal(__y.__x_ + (__y.__i_ + __j), __y.__x_ + _Np, __x.__x_))
    return false;
  return std::equal(__y.__x_, __y.__x_ + __y.__i_, __x.__x_ + (_Np - (__y.__i_ + __j)));
}

template <class _UInt,
          size_t _Wp,
          size_t _Np,
          size_t _Mp,
          size_t _Rp,
          _UInt _Ap,
          size_t _Up,
          _UInt _Dp,
          size_t _Sp,
          _UInt _Bp,
          size_t _Tp,
          _UInt _Cp,
          size_t _Lp,
          _UInt _Fp>
inline _LIBCPP_HIDE_FROM_ABI bool
operator!=(const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x,
           const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __y) {
  return !(__x == __y);
}

template <class _CharT,
          class _Traits,
          class _UInt,
          size_t _Wp,
          size_t _Np,
          size_t _Mp,
          size_t _Rp,
          _UInt _Ap,
          size_t _Up,
          _UInt _Dp,
          size_t _Sp,
          _UInt _Bp,
          size_t _Tp,
          _UInt _Cp,
          size_t _Lp,
          _UInt _Fp>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os,
           const mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x) {
  __save_flags<_CharT, _Traits> __lx(__os);
  typedef basic_ostream<_CharT, _Traits> _Ostream;
  __os.flags(_Ostream::dec | _Ostream::left);
  _CharT __sp = __os.widen(' ');
  __os.fill(__sp);
  __os << __x.__x_[__x.__i_];
  for (size_t __j = __x.__i_ + 1; __j < _Np; ++__j)
    __os << __sp << __x.__x_[__j];
  for (size_t __j = 0; __j < __x.__i_; ++__j)
    __os << __sp << __x.__x_[__j];
  return __os;
}

template <class _CharT,
          class _Traits,
          class _UInt,
          size_t _Wp,
          size_t _Np,
          size_t _Mp,
          size_t _Rp,
          _UInt _Ap,
          size_t _Up,
          _UInt _Dp,
          size_t _Sp,
          _UInt _Bp,
          size_t _Tp,
          _UInt _Cp,
          size_t _Lp,
          _UInt _Fp>
_LIBCPP_HIDE_FROM_ABI basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is,
           mersenne_twister_engine<_UInt, _Wp, _Np, _Mp, _Rp, _Ap, _Up, _Dp, _Sp, _Bp, _Tp, _Cp, _Lp, _Fp>& __x) {
  __save_flags<_CharT, _Traits> __lx(__is);
  typedef basic_istream<_CharT, _Traits> _Istream;
  __is.flags(_Istream::dec | _Istream::skipws);
  _UInt __t[_Np];
  for (size_t __i = 0; __i < _Np; ++__i)
    __is >> __t[__i];
  if (!__is.fail()) {
    for (size_t __i = 0; __i < _Np; ++__i)
      __x.__x_[__i] = __t[__i];
    __x.__i_ = 0;
  }
  return __is;
}

typedef mersenne_twister_engine<
    uint_fast32_t,
    32,
    624,
    397,
    31,
    0x9908b0df,
    11,
    0xffffffff,
    7,
    0x9d2c5680,
    15,
    0xefc60000,
    18,
    1812433253>
    mt19937;
typedef mersenne_twister_engine<
    uint_fast64_t,
    64,
    312,
    156,
    31,
    0xb5026f5aa96619e9ULL,
    29,
    0x5555555555555555ULL,
    17,
    0x71d67fffeda60000ULL,
    37,
    0xfff7eee000000000ULL,
    43,
    6364136223846793005ULL>
    mt19937_64;

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANDOM_MERSENNE_TWISTER_ENGINE_H
