// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___THREAD_SUPPORT_C11_H
#define _LIBCPP___THREAD_SUPPORT_C11_H

#include <__chrono/convert_to_timespec.h>
#include <__chrono/duration.h>
#include <__config>
#include <ctime>
#include <errno.h>
#include <threads.h>

#ifndef _LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

using __libcpp_timespec_t = ::timespec;

//
// Mutex
//
typedef mtx_t __libcpp_mutex_t;
// mtx_t is a struct so using {} for initialization is valid.
#define _LIBCPP_MUTEX_INITIALIZER                                                                                      \
  {}

typedef mtx_t __libcpp_recursive_mutex_t;

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t* __m) {
  return mtx_init(__m, mtx_plain | mtx_recursive) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int
__libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t* __m) {
  return mtx_lock(__m) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS bool
__libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t* __m) {
  return mtx_trylock(__m) == thrd_success;
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int
__libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t* __m) {
  return mtx_unlock(__m) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t* __m) {
  mtx_destroy(__m);
  return 0;
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int __libcpp_mutex_lock(__libcpp_mutex_t* __m) {
  return mtx_lock(__m) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS bool __libcpp_mutex_trylock(__libcpp_mutex_t* __m) {
  return mtx_trylock(__m) == thrd_success;
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int __libcpp_mutex_unlock(__libcpp_mutex_t* __m) {
  return mtx_unlock(__m) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_mutex_destroy(__libcpp_mutex_t* __m) {
  mtx_destroy(__m);
  return 0;
}

//
// Condition Variable
//
typedef cnd_t __libcpp_condvar_t;
// cnd_t is a struct so using {} for initialization is valid.
#define _LIBCPP_CONDVAR_INITIALIZER                                                                                    \
  {}

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_condvar_signal(__libcpp_condvar_t* __cv) {
  return cnd_signal(__cv) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_condvar_broadcast(__libcpp_condvar_t* __cv) {
  return cnd_broadcast(__cv) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int
__libcpp_condvar_wait(__libcpp_condvar_t* __cv, __libcpp_mutex_t* __m) {
  return cnd_wait(__cv, __m) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int
__libcpp_condvar_timedwait(__libcpp_condvar_t* __cv, __libcpp_mutex_t* __m, timespec* __ts) {
  int __ec = cnd_timedwait(__cv, __m, __ts);
  return __ec == thrd_timedout ? ETIMEDOUT : __ec;
}

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_condvar_destroy(__libcpp_condvar_t* __cv) {
  cnd_destroy(__cv);
  return 0;
}

//
// Execute once
//
typedef ::once_flag __libcpp_exec_once_flag;
#define _LIBCPP_EXEC_ONCE_INITIALIZER ONCE_FLAG_INIT

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_execute_once(__libcpp_exec_once_flag* flag, void (*init_routine)(void)) {
  ::call_once(flag, init_routine);
  return 0;
}

//
// Thread id
//
typedef thrd_t __libcpp_thread_id;

// Returns non-zero if the thread ids are equal, otherwise 0
inline _LIBCPP_HIDE_FROM_ABI bool __libcpp_thread_id_equal(__libcpp_thread_id t1, __libcpp_thread_id t2) {
  return thrd_equal(t1, t2) != 0;
}

// Returns non-zero if t1 < t2, otherwise 0
inline _LIBCPP_HIDE_FROM_ABI bool __libcpp_thread_id_less(__libcpp_thread_id t1, __libcpp_thread_id t2) {
  return t1 < t2;
}

//
// Thread
//
#define _LIBCPP_NULL_THREAD 0U

typedef thrd_t __libcpp_thread_t;

inline _LIBCPP_HIDE_FROM_ABI __libcpp_thread_id __libcpp_thread_get_id(const __libcpp_thread_t* __t) { return *__t; }

inline _LIBCPP_HIDE_FROM_ABI bool __libcpp_thread_isnull(const __libcpp_thread_t* __t) {
  return __libcpp_thread_get_id(__t) == 0;
}

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_thread_create(__libcpp_thread_t* __t, void* (*__func)(void*), void* __arg) {
  int __ec = thrd_create(__t, reinterpret_cast<thrd_start_t>(__func), __arg);
  return __ec == thrd_nomem ? ENOMEM : __ec;
}

inline _LIBCPP_HIDE_FROM_ABI __libcpp_thread_id __libcpp_thread_get_current_id() { return thrd_current(); }

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_thread_join(__libcpp_thread_t* __t) {
  return thrd_join(*__t, nullptr) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_thread_detach(__libcpp_thread_t* __t) {
  return thrd_detach(*__t) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI void __libcpp_thread_yield() { thrd_yield(); }

inline _LIBCPP_HIDE_FROM_ABI void __libcpp_thread_sleep_for(const chrono::nanoseconds& __ns) {
  __libcpp_timespec_t __ts = std::__convert_to_timespec<__libcpp_timespec_t>(__ns);
  thrd_sleep(&__ts, nullptr);
}

//
// Thread local storage
//
#define _LIBCPP_TLS_DESTRUCTOR_CC /* nothing */

typedef tss_t __libcpp_tls_key;

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_tls_create(__libcpp_tls_key* __key, void (*__at_exit)(void*)) {
  return tss_create(__key, __at_exit) == thrd_success ? 0 : EINVAL;
}

inline _LIBCPP_HIDE_FROM_ABI void* __libcpp_tls_get(__libcpp_tls_key __key) { return tss_get(__key); }

inline _LIBCPP_HIDE_FROM_ABI int __libcpp_tls_set(__libcpp_tls_key __key, void* __p) {
  return tss_set(__key, __p) == thrd_success ? 0 : EINVAL;
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___THREAD_SUPPORT_C11_H
