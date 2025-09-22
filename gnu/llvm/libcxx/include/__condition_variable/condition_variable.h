//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONDITION_VARIABLE_CONDITION_VARIABLE_H
#define _LIBCPP___CONDITION_VARIABLE_CONDITION_VARIABLE_H

#include <__chrono/duration.h>
#include <__chrono/steady_clock.h>
#include <__chrono/system_clock.h>
#include <__chrono/time_point.h>
#include <__config>
#include <__mutex/mutex.h>
#include <__mutex/unique_lock.h>
#include <__system_error/system_error.h>
#include <__thread/support.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_floating_point.h>
#include <__utility/move.h>
#include <limits>
#include <ratio>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_HAS_NO_THREADS

// enum class cv_status
_LIBCPP_DECLARE_STRONG_ENUM(cv_status){no_timeout, timeout};
_LIBCPP_DECLARE_STRONG_ENUM_EPILOG(cv_status)

class _LIBCPP_EXPORTED_FROM_ABI condition_variable {
  __libcpp_condvar_t __cv_ = _LIBCPP_CONDVAR_INITIALIZER;

public:
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR condition_variable() _NOEXCEPT = default;

#  ifdef _LIBCPP_HAS_TRIVIAL_CONDVAR_DESTRUCTION
  ~condition_variable() = default;
#  else
  ~condition_variable();
#  endif

  condition_variable(const condition_variable&)            = delete;
  condition_variable& operator=(const condition_variable&) = delete;

  void notify_one() _NOEXCEPT;
  void notify_all() _NOEXCEPT;

  void wait(unique_lock<mutex>& __lk) _NOEXCEPT;
  template <class _Predicate>
  _LIBCPP_METHOD_TEMPLATE_IMPLICIT_INSTANTIATION_VIS void wait(unique_lock<mutex>& __lk, _Predicate __pred);

  template <class _Clock, class _Duration>
  _LIBCPP_METHOD_TEMPLATE_IMPLICIT_INSTANTIATION_VIS cv_status
  wait_until(unique_lock<mutex>& __lk, const chrono::time_point<_Clock, _Duration>& __t);

  template <class _Clock, class _Duration, class _Predicate>
  _LIBCPP_METHOD_TEMPLATE_IMPLICIT_INSTANTIATION_VIS bool
  wait_until(unique_lock<mutex>& __lk, const chrono::time_point<_Clock, _Duration>& __t, _Predicate __pred);

  template <class _Rep, class _Period>
  _LIBCPP_METHOD_TEMPLATE_IMPLICIT_INSTANTIATION_VIS cv_status
  wait_for(unique_lock<mutex>& __lk, const chrono::duration<_Rep, _Period>& __d);

  template <class _Rep, class _Period, class _Predicate>
  bool _LIBCPP_HIDE_FROM_ABI
  wait_for(unique_lock<mutex>& __lk, const chrono::duration<_Rep, _Period>& __d, _Predicate __pred);

  typedef __libcpp_condvar_t* native_handle_type;
  _LIBCPP_HIDE_FROM_ABI native_handle_type native_handle() { return &__cv_; }

private:
  void
  __do_timed_wait(unique_lock<mutex>& __lk, chrono::time_point<chrono::system_clock, chrono::nanoseconds>) _NOEXCEPT;
#  if defined(_LIBCPP_HAS_COND_CLOCKWAIT)
  _LIBCPP_HIDE_FROM_ABI void
  __do_timed_wait(unique_lock<mutex>& __lk, chrono::time_point<chrono::steady_clock, chrono::nanoseconds>) _NOEXCEPT;
#  endif
  template <class _Clock>
  _LIBCPP_HIDE_FROM_ABI void
  __do_timed_wait(unique_lock<mutex>& __lk, chrono::time_point<_Clock, chrono::nanoseconds>) _NOEXCEPT;
};
#endif // !_LIBCPP_HAS_NO_THREADS

template <class _Rep, class _Period, __enable_if_t<is_floating_point<_Rep>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI chrono::nanoseconds __safe_nanosecond_cast(chrono::duration<_Rep, _Period> __d) {
  using namespace chrono;
  using __ratio       = ratio_divide<_Period, nano>;
  using __ns_rep      = nanoseconds::rep;
  _Rep __result_float = __d.count() * __ratio::num / __ratio::den;

  _Rep __result_max = numeric_limits<__ns_rep>::max();
  if (__result_float >= __result_max) {
    return nanoseconds::max();
  }

  _Rep __result_min = numeric_limits<__ns_rep>::min();
  if (__result_float <= __result_min) {
    return nanoseconds::min();
  }

  return nanoseconds(static_cast<__ns_rep>(__result_float));
}

template <class _Rep, class _Period, __enable_if_t<!is_floating_point<_Rep>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI chrono::nanoseconds __safe_nanosecond_cast(chrono::duration<_Rep, _Period> __d) {
  using namespace chrono;
  if (__d.count() == 0) {
    return nanoseconds(0);
  }

  using __ratio         = ratio_divide<_Period, nano>;
  using __ns_rep        = nanoseconds::rep;
  __ns_rep __result_max = numeric_limits<__ns_rep>::max();
  if (__d.count() > 0 && __d.count() > __result_max / __ratio::num) {
    return nanoseconds::max();
  }

  __ns_rep __result_min = numeric_limits<__ns_rep>::min();
  if (__d.count() < 0 && __d.count() < __result_min / __ratio::num) {
    return nanoseconds::min();
  }

  __ns_rep __result = __d.count() * __ratio::num / __ratio::den;
  if (__result == 0) {
    return nanoseconds(1);
  }

  return nanoseconds(__result);
}

#ifndef _LIBCPP_HAS_NO_THREADS
template <class _Predicate>
void condition_variable::wait(unique_lock<mutex>& __lk, _Predicate __pred) {
  while (!__pred())
    wait(__lk);
}

template <class _Clock, class _Duration>
cv_status condition_variable::wait_until(unique_lock<mutex>& __lk, const chrono::time_point<_Clock, _Duration>& __t) {
  using namespace chrono;
  using __clock_tp_ns = time_point<_Clock, nanoseconds>;

  typename _Clock::time_point __now = _Clock::now();
  if (__t <= __now)
    return cv_status::timeout;

  __clock_tp_ns __t_ns = __clock_tp_ns(std::__safe_nanosecond_cast(__t.time_since_epoch()));

  __do_timed_wait(__lk, __t_ns);
  return _Clock::now() < __t ? cv_status::no_timeout : cv_status::timeout;
}

template <class _Clock, class _Duration, class _Predicate>
bool condition_variable::wait_until(
    unique_lock<mutex>& __lk, const chrono::time_point<_Clock, _Duration>& __t, _Predicate __pred) {
  while (!__pred()) {
    if (wait_until(__lk, __t) == cv_status::timeout)
      return __pred();
  }
  return true;
}

template <class _Rep, class _Period>
cv_status condition_variable::wait_for(unique_lock<mutex>& __lk, const chrono::duration<_Rep, _Period>& __d) {
  using namespace chrono;
  if (__d <= __d.zero())
    return cv_status::timeout;
  using __ns_rep                   = nanoseconds::rep;
  steady_clock::time_point __c_now = steady_clock::now();

#  if defined(_LIBCPP_HAS_COND_CLOCKWAIT)
  using __clock_tp_ns     = time_point<steady_clock, nanoseconds>;
  __ns_rep __now_count_ns = std::__safe_nanosecond_cast(__c_now.time_since_epoch()).count();
#  else
  using __clock_tp_ns     = time_point<system_clock, nanoseconds>;
  __ns_rep __now_count_ns = std::__safe_nanosecond_cast(system_clock::now().time_since_epoch()).count();
#  endif

  __ns_rep __d_ns_count = std::__safe_nanosecond_cast(__d).count();

  if (__now_count_ns > numeric_limits<__ns_rep>::max() - __d_ns_count) {
    __do_timed_wait(__lk, __clock_tp_ns::max());
  } else {
    __do_timed_wait(__lk, __clock_tp_ns(nanoseconds(__now_count_ns + __d_ns_count)));
  }

  return steady_clock::now() - __c_now < __d ? cv_status::no_timeout : cv_status::timeout;
}

template <class _Rep, class _Period, class _Predicate>
inline bool
condition_variable::wait_for(unique_lock<mutex>& __lk, const chrono::duration<_Rep, _Period>& __d, _Predicate __pred) {
  return wait_until(__lk, chrono::steady_clock::now() + __d, std::move(__pred));
}

#  if defined(_LIBCPP_HAS_COND_CLOCKWAIT)
inline void condition_variable::__do_timed_wait(
    unique_lock<mutex>& __lk, chrono::time_point<chrono::steady_clock, chrono::nanoseconds> __tp) _NOEXCEPT {
  using namespace chrono;
  if (!__lk.owns_lock())
    __throw_system_error(EPERM, "condition_variable::timed wait: mutex not locked");
  nanoseconds __d = __tp.time_since_epoch();
  timespec __ts;
  seconds __s                 = duration_cast<seconds>(__d);
  using __ts_sec              = decltype(__ts.tv_sec);
  const __ts_sec __ts_sec_max = numeric_limits<__ts_sec>::max();
  if (__s.count() < __ts_sec_max) {
    __ts.tv_sec  = static_cast<__ts_sec>(__s.count());
    __ts.tv_nsec = (__d - __s).count();
  } else {
    __ts.tv_sec  = __ts_sec_max;
    __ts.tv_nsec = giga::num - 1;
  }
  int __ec = pthread_cond_clockwait(&__cv_, __lk.mutex()->native_handle(), CLOCK_MONOTONIC, &__ts);
  if (__ec != 0 && __ec != ETIMEDOUT)
    __throw_system_error(__ec, "condition_variable timed_wait failed");
}
#  endif // _LIBCPP_HAS_COND_CLOCKWAIT

template <class _Clock>
inline void condition_variable::__do_timed_wait(unique_lock<mutex>& __lk,
                                                chrono::time_point<_Clock, chrono::nanoseconds> __tp) _NOEXCEPT {
  wait_for(__lk, __tp - _Clock::now());
}

#endif // _LIBCPP_HAS_NO_THREADS

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___CONDITION_VARIABLE_CONDITION_VARIABLE_H
