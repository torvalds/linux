// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_DURATION_H
#define _LIBCPP___CHRONO_DURATION_H

#include <__compare/ordering.h>
#include <__compare/three_way_comparable.h>
#include <__config>
#include <__type_traits/common_type.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_floating_point.h>
#include <limits>
#include <ratio>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

template <class _Rep, class _Period = ratio<1> >
class _LIBCPP_TEMPLATE_VIS duration;

template <class _Tp>
struct __is_duration : false_type {};

template <class _Rep, class _Period>
struct __is_duration<duration<_Rep, _Period> > : true_type {};

template <class _Rep, class _Period>
struct __is_duration<const duration<_Rep, _Period> > : true_type {};

template <class _Rep, class _Period>
struct __is_duration<volatile duration<_Rep, _Period> > : true_type {};

template <class _Rep, class _Period>
struct __is_duration<const volatile duration<_Rep, _Period> > : true_type {};

} // namespace chrono

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
struct _LIBCPP_TEMPLATE_VIS common_type<chrono::duration<_Rep1, _Period1>, chrono::duration<_Rep2, _Period2> > {
  typedef chrono::duration<typename common_type<_Rep1, _Rep2>::type, typename __ratio_gcd<_Period1, _Period2>::type>
      type;
};

namespace chrono {

// duration_cast

template <class _FromDuration,
          class _ToDuration,
          class _Period = typename ratio_divide<typename _FromDuration::period, typename _ToDuration::period>::type,
          bool          = _Period::num == 1,
          bool          = _Period::den == 1>
struct __duration_cast;

template <class _FromDuration, class _ToDuration, class _Period>
struct __duration_cast<_FromDuration, _ToDuration, _Period, true, true> {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _ToDuration operator()(const _FromDuration& __fd) const {
    return _ToDuration(static_cast<typename _ToDuration::rep>(__fd.count()));
  }
};

template <class _FromDuration, class _ToDuration, class _Period>
struct __duration_cast<_FromDuration, _ToDuration, _Period, true, false> {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _ToDuration operator()(const _FromDuration& __fd) const {
    typedef typename common_type<typename _ToDuration::rep, typename _FromDuration::rep, intmax_t>::type _Ct;
    return _ToDuration(
        static_cast<typename _ToDuration::rep>(static_cast<_Ct>(__fd.count()) / static_cast<_Ct>(_Period::den)));
  }
};

template <class _FromDuration, class _ToDuration, class _Period>
struct __duration_cast<_FromDuration, _ToDuration, _Period, false, true> {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _ToDuration operator()(const _FromDuration& __fd) const {
    typedef typename common_type<typename _ToDuration::rep, typename _FromDuration::rep, intmax_t>::type _Ct;
    return _ToDuration(
        static_cast<typename _ToDuration::rep>(static_cast<_Ct>(__fd.count()) * static_cast<_Ct>(_Period::num)));
  }
};

template <class _FromDuration, class _ToDuration, class _Period>
struct __duration_cast<_FromDuration, _ToDuration, _Period, false, false> {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _ToDuration operator()(const _FromDuration& __fd) const {
    typedef typename common_type<typename _ToDuration::rep, typename _FromDuration::rep, intmax_t>::type _Ct;
    return _ToDuration(static_cast<typename _ToDuration::rep>(
        static_cast<_Ct>(__fd.count()) * static_cast<_Ct>(_Period::num) / static_cast<_Ct>(_Period::den)));
  }
};

template <class _ToDuration, class _Rep, class _Period, __enable_if_t<__is_duration<_ToDuration>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _ToDuration duration_cast(const duration<_Rep, _Period>& __fd) {
  return __duration_cast<duration<_Rep, _Period>, _ToDuration>()(__fd);
}

template <class _Rep>
struct _LIBCPP_TEMPLATE_VIS treat_as_floating_point : is_floating_point<_Rep> {};

#if _LIBCPP_STD_VER >= 17
template <class _Rep>
inline constexpr bool treat_as_floating_point_v = treat_as_floating_point<_Rep>::value;
#endif

template <class _Rep>
struct _LIBCPP_TEMPLATE_VIS duration_values {
public:
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR _Rep zero() _NOEXCEPT { return _Rep(0); }
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR _Rep max() _NOEXCEPT { return numeric_limits<_Rep>::max(); }
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR _Rep min() _NOEXCEPT { return numeric_limits<_Rep>::lowest(); }
};

#if _LIBCPP_STD_VER >= 17
template <class _ToDuration, class _Rep, class _Period, enable_if_t<__is_duration<_ToDuration>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _ToDuration floor(const duration<_Rep, _Period>& __d) {
  _ToDuration __t = chrono::duration_cast<_ToDuration>(__d);
  if (__t > __d)
    __t = __t - _ToDuration{1};
  return __t;
}

template <class _ToDuration, class _Rep, class _Period, enable_if_t<__is_duration<_ToDuration>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _ToDuration ceil(const duration<_Rep, _Period>& __d) {
  _ToDuration __t = chrono::duration_cast<_ToDuration>(__d);
  if (__t < __d)
    __t = __t + _ToDuration{1};
  return __t;
}

template <class _ToDuration, class _Rep, class _Period, enable_if_t<__is_duration<_ToDuration>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _ToDuration round(const duration<_Rep, _Period>& __d) {
  _ToDuration __lower = chrono::floor<_ToDuration>(__d);
  _ToDuration __upper = __lower + _ToDuration{1};
  auto __lower_diff   = __d - __lower;
  auto __upper_diff   = __upper - __d;
  if (__lower_diff < __upper_diff)
    return __lower;
  if (__lower_diff > __upper_diff)
    return __upper;
  return __lower.count() & 1 ? __upper : __lower;
}
#endif

// duration

template <class _Rep, class _Period>
class _LIBCPP_TEMPLATE_VIS duration {
  static_assert(!__is_duration<_Rep>::value, "A duration representation can not be a duration");
  static_assert(__is_ratio<_Period>::value, "Second template parameter of duration must be a std::ratio");
  static_assert(_Period::num > 0, "duration period must be positive");

  template <class _R1, class _R2>
  struct __no_overflow {
  private:
    static const intmax_t __gcd_n1_n2 = __static_gcd<_R1::num, _R2::num>::value;
    static const intmax_t __gcd_d1_d2 = __static_gcd<_R1::den, _R2::den>::value;
    static const intmax_t __n1        = _R1::num / __gcd_n1_n2;
    static const intmax_t __d1        = _R1::den / __gcd_d1_d2;
    static const intmax_t __n2        = _R2::num / __gcd_n1_n2;
    static const intmax_t __d2        = _R2::den / __gcd_d1_d2;
    static const intmax_t max         = -((intmax_t(1) << (sizeof(intmax_t) * CHAR_BIT - 1)) + 1);

    template <intmax_t _Xp, intmax_t _Yp, bool __overflow>
    struct __mul // __overflow == false
    {
      static const intmax_t value = _Xp * _Yp;
    };

    template <intmax_t _Xp, intmax_t _Yp>
    struct __mul<_Xp, _Yp, true> {
      static const intmax_t value = 1;
    };

  public:
    static const bool value = (__n1 <= max / __d2) && (__n2 <= max / __d1);
    typedef ratio<__mul<__n1, __d2, !value>::value, __mul<__n2, __d1, !value>::value> type;
  };

public:
  typedef _Rep rep;
  typedef typename _Period::type period;

private:
  rep __rep_;

public:
#ifndef _LIBCPP_CXX03_LANG
  constexpr duration() = default;
#else
  _LIBCPP_HIDE_FROM_ABI duration() {}
#endif

  template <class _Rep2,
            __enable_if_t<is_convertible<const _Rep2&, rep>::value &&
                              (treat_as_floating_point<rep>::value || !treat_as_floating_point<_Rep2>::value),
                          int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit duration(const _Rep2& __r) : __rep_(__r) {}

  // conversions
  template <class _Rep2,
            class _Period2,
            __enable_if_t<__no_overflow<_Period2, period>::value && (treat_as_floating_point<rep>::value ||
                                                                     (__no_overflow<_Period2, period>::type::den == 1 &&
                                                                      !treat_as_floating_point<_Rep2>::value)),
                          int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR duration(const duration<_Rep2, _Period2>& __d)
      : __rep_(chrono::duration_cast<duration>(__d).count()) {}

  // observer

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR rep count() const { return __rep_; }

  // arithmetic

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR typename common_type<duration>::type operator+() const {
    return typename common_type<duration>::type(*this);
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR typename common_type<duration>::type operator-() const {
    return typename common_type<duration>::type(-__rep_);
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration& operator++() {
    ++__rep_;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration operator++(int) { return duration(__rep_++); }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration& operator--() {
    --__rep_;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration operator--(int) { return duration(__rep_--); }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration& operator+=(const duration& __d) {
    __rep_ += __d.count();
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration& operator-=(const duration& __d) {
    __rep_ -= __d.count();
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration& operator*=(const rep& __rhs) {
    __rep_ *= __rhs;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration& operator/=(const rep& __rhs) {
    __rep_ /= __rhs;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration& operator%=(const rep& __rhs) {
    __rep_ %= __rhs;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 duration& operator%=(const duration& __rhs) {
    __rep_ %= __rhs.count();
    return *this;
  }

  // special values

  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR duration zero() _NOEXCEPT {
    return duration(duration_values<rep>::zero());
  }
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR duration min() _NOEXCEPT {
    return duration(duration_values<rep>::min());
  }
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR duration max() _NOEXCEPT {
    return duration(duration_values<rep>::max());
  }
};

typedef duration<long long, nano> nanoseconds;
typedef duration<long long, micro> microseconds;
typedef duration<long long, milli> milliseconds;
typedef duration<long long > seconds;
typedef duration< long, ratio< 60> > minutes;
typedef duration< long, ratio<3600> > hours;
#if _LIBCPP_STD_VER >= 20
typedef duration< int, ratio_multiply<ratio<24>, hours::period>> days;
typedef duration< int, ratio_multiply<ratio<7>, days::period>> weeks;
typedef duration< int, ratio_multiply<ratio<146097, 400>, days::period>> years;
typedef duration< int, ratio_divide<years::period, ratio<12>>> months;
#endif
// Duration ==

template <class _LhsDuration, class _RhsDuration>
struct __duration_eq {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool operator()(const _LhsDuration& __lhs, const _RhsDuration& __rhs) const {
    typedef typename common_type<_LhsDuration, _RhsDuration>::type _Ct;
    return _Ct(__lhs).count() == _Ct(__rhs).count();
  }
};

template <class _LhsDuration>
struct __duration_eq<_LhsDuration, _LhsDuration> {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool operator()(const _LhsDuration& __lhs, const _LhsDuration& __rhs) const {
    return __lhs.count() == __rhs.count();
  }
};

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool
operator==(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  return __duration_eq<duration<_Rep1, _Period1>, duration<_Rep2, _Period2> >()(__lhs, __rhs);
}

#if _LIBCPP_STD_VER <= 17

// Duration !=

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool
operator!=(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  return !(__lhs == __rhs);
}

#endif // _LIBCPP_STD_VER <= 17

// Duration <

template <class _LhsDuration, class _RhsDuration>
struct __duration_lt {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool operator()(const _LhsDuration& __lhs, const _RhsDuration& __rhs) const {
    typedef typename common_type<_LhsDuration, _RhsDuration>::type _Ct;
    return _Ct(__lhs).count() < _Ct(__rhs).count();
  }
};

template <class _LhsDuration>
struct __duration_lt<_LhsDuration, _LhsDuration> {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool operator()(const _LhsDuration& __lhs, const _LhsDuration& __rhs) const {
    return __lhs.count() < __rhs.count();
  }
};

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool
operator<(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  return __duration_lt<duration<_Rep1, _Period1>, duration<_Rep2, _Period2> >()(__lhs, __rhs);
}

// Duration >

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool
operator>(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  return __rhs < __lhs;
}

// Duration <=

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool
operator<=(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  return !(__rhs < __lhs);
}

// Duration >=

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool
operator>=(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  return !(__lhs < __rhs);
}

#if _LIBCPP_STD_VER >= 20

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
  requires three_way_comparable<common_type_t<_Rep1, _Rep2>>
_LIBCPP_HIDE_FROM_ABI constexpr auto
operator<=>(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  using _Ct = common_type_t<duration<_Rep1, _Period1>, duration<_Rep2, _Period2>>;
  return _Ct(__lhs).count() <=> _Ct(__rhs).count();
}

#endif // _LIBCPP_STD_VER >= 20

// Duration +

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR
typename common_type<duration<_Rep1, _Period1>, duration<_Rep2, _Period2> >::type
operator+(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  typedef typename common_type<duration<_Rep1, _Period1>, duration<_Rep2, _Period2> >::type _Cd;
  return _Cd(_Cd(__lhs).count() + _Cd(__rhs).count());
}

// Duration -

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR
typename common_type<duration<_Rep1, _Period1>, duration<_Rep2, _Period2> >::type
operator-(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  typedef typename common_type<duration<_Rep1, _Period1>, duration<_Rep2, _Period2> >::type _Cd;
  return _Cd(_Cd(__lhs).count() - _Cd(__rhs).count());
}

// Duration *

template <class _Rep1,
          class _Period,
          class _Rep2,
          __enable_if_t<is_convertible<const _Rep2&, typename common_type<_Rep1, _Rep2>::type>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR duration<typename common_type<_Rep1, _Rep2>::type, _Period>
operator*(const duration<_Rep1, _Period>& __d, const _Rep2& __s) {
  typedef typename common_type<_Rep1, _Rep2>::type _Cr;
  typedef duration<_Cr, _Period> _Cd;
  return _Cd(_Cd(__d).count() * static_cast<_Cr>(__s));
}

template <class _Rep1,
          class _Period,
          class _Rep2,
          __enable_if_t<is_convertible<const _Rep1&, typename common_type<_Rep1, _Rep2>::type>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR duration<typename common_type<_Rep1, _Rep2>::type, _Period>
operator*(const _Rep1& __s, const duration<_Rep2, _Period>& __d) {
  return __d * __s;
}

// Duration /

template <class _Rep1,
          class _Period,
          class _Rep2,
          __enable_if_t<!__is_duration<_Rep2>::value &&
                            is_convertible<const _Rep2&, typename common_type<_Rep1, _Rep2>::type>::value,
                        int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR duration<typename common_type<_Rep1, _Rep2>::type, _Period>
operator/(const duration<_Rep1, _Period>& __d, const _Rep2& __s) {
  typedef typename common_type<_Rep1, _Rep2>::type _Cr;
  typedef duration<_Cr, _Period> _Cd;
  return _Cd(_Cd(__d).count() / static_cast<_Cr>(__s));
}

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR typename common_type<_Rep1, _Rep2>::type
operator/(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  typedef typename common_type<duration<_Rep1, _Period1>, duration<_Rep2, _Period2> >::type _Ct;
  return _Ct(__lhs).count() / _Ct(__rhs).count();
}

// Duration %

template <class _Rep1,
          class _Period,
          class _Rep2,
          __enable_if_t<!__is_duration<_Rep2>::value &&
                            is_convertible<const _Rep2&, typename common_type<_Rep1, _Rep2>::type>::value,
                        int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR duration<typename common_type<_Rep1, _Rep2>::type, _Period>
operator%(const duration<_Rep1, _Period>& __d, const _Rep2& __s) {
  typedef typename common_type<_Rep1, _Rep2>::type _Cr;
  typedef duration<_Cr, _Period> _Cd;
  return _Cd(_Cd(__d).count() % static_cast<_Cr>(__s));
}

template <class _Rep1, class _Period1, class _Rep2, class _Period2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR
typename common_type<duration<_Rep1, _Period1>, duration<_Rep2, _Period2> >::type
operator%(const duration<_Rep1, _Period1>& __lhs, const duration<_Rep2, _Period2>& __rhs) {
  typedef typename common_type<_Rep1, _Rep2>::type _Cr;
  typedef typename common_type<duration<_Rep1, _Period1>, duration<_Rep2, _Period2> >::type _Cd;
  return _Cd(static_cast<_Cr>(_Cd(__lhs).count()) % static_cast<_Cr>(_Cd(__rhs).count()));
}

} // namespace chrono

#if _LIBCPP_STD_VER >= 14
// Suffixes for duration literals [time.duration.literals]
inline namespace literals {
inline namespace chrono_literals {

_LIBCPP_HIDE_FROM_ABI constexpr chrono::hours operator""h(unsigned long long __h) {
  return chrono::hours(static_cast<chrono::hours::rep>(__h));
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::duration<long double, ratio<3600, 1>> operator""h(long double __h) {
  return chrono::duration<long double, ratio<3600, 1>>(__h);
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::minutes operator""min(unsigned long long __m) {
  return chrono::minutes(static_cast<chrono::minutes::rep>(__m));
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::duration<long double, ratio<60, 1>> operator""min(long double __m) {
  return chrono::duration<long double, ratio<60, 1>>(__m);
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::seconds operator""s(unsigned long long __s) {
  return chrono::seconds(static_cast<chrono::seconds::rep>(__s));
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::duration<long double> operator""s(long double __s) {
  return chrono::duration<long double>(__s);
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::milliseconds operator""ms(unsigned long long __ms) {
  return chrono::milliseconds(static_cast<chrono::milliseconds::rep>(__ms));
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::duration<long double, milli> operator""ms(long double __ms) {
  return chrono::duration<long double, milli>(__ms);
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::microseconds operator""us(unsigned long long __us) {
  return chrono::microseconds(static_cast<chrono::microseconds::rep>(__us));
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::duration<long double, micro> operator""us(long double __us) {
  return chrono::duration<long double, micro>(__us);
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::nanoseconds operator""ns(unsigned long long __ns) {
  return chrono::nanoseconds(static_cast<chrono::nanoseconds::rep>(__ns));
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::duration<long double, nano> operator""ns(long double __ns) {
  return chrono::duration<long double, nano>(__ns);
}

} // namespace chrono_literals
} // namespace literals

namespace chrono { // hoist the literals into namespace std::chrono
using namespace literals::chrono_literals;
} // namespace chrono

#endif // _LIBCPP_STD_VER >= 14

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#if !defined(_LIBCPP_REMOVE_TRANSITIVE_INCLUDES) && _LIBCPP_STD_VER <= 20
#  include <type_traits>
#endif

#endif // _LIBCPP___CHRONO_DURATION_H
