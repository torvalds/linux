// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___THREAD_SUPPORT_WINDOWS_H
#define _LIBCPP___THREAD_SUPPORT_WINDOWS_H

#include <__chrono/duration.h>
#include <__config>
#include <ctime>

#ifndef _LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

using __libcpp_timespec_t = ::timespec;

//
// Mutex
//
typedef void* __libcpp_mutex_t;
#define _LIBCPP_MUTEX_INITIALIZER 0

#if defined(_M_IX86) || defined(__i386__) || defined(_M_ARM) || defined(__arm__)
typedef void* __libcpp_recursive_mutex_t[6];
#elif defined(_M_AMD64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
typedef void* __libcpp_recursive_mutex_t[5];
#else
#  error Unsupported architecture
#endif

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t* __m);

_LIBCPP_EXPORTED_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int
__libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t* __m);

_LIBCPP_EXPORTED_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS bool
__libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t* __m);

_LIBCPP_EXPORTED_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int
__libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t* __m);

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t* __m);

_LIBCPP_EXPORTED_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int __libcpp_mutex_lock(__libcpp_mutex_t* __m);

_LIBCPP_EXPORTED_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS bool __libcpp_mutex_trylock(__libcpp_mutex_t* __m);

_LIBCPP_EXPORTED_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int __libcpp_mutex_unlock(__libcpp_mutex_t* __m);

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_mutex_destroy(__libcpp_mutex_t* __m);

//
// Condition variable
//
typedef void* __libcpp_condvar_t;
#define _LIBCPP_CONDVAR_INITIALIZER 0

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_condvar_signal(__libcpp_condvar_t* __cv);

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_condvar_broadcast(__libcpp_condvar_t* __cv);

_LIBCPP_EXPORTED_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int
__libcpp_condvar_wait(__libcpp_condvar_t* __cv, __libcpp_mutex_t* __m);

_LIBCPP_EXPORTED_FROM_ABI _LIBCPP_NO_THREAD_SAFETY_ANALYSIS int
__libcpp_condvar_timedwait(__libcpp_condvar_t* __cv, __libcpp_mutex_t* __m, __libcpp_timespec_t* __ts);

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_condvar_destroy(__libcpp_condvar_t* __cv);

//
// Execute once
//
typedef void* __libcpp_exec_once_flag;
#define _LIBCPP_EXEC_ONCE_INITIALIZER 0

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_execute_once(__libcpp_exec_once_flag* __flag, void (*__init_routine)());

//
// Thread id
//
typedef long __libcpp_thread_id;

_LIBCPP_EXPORTED_FROM_ABI bool __libcpp_thread_id_equal(__libcpp_thread_id __t1, __libcpp_thread_id __t2);

_LIBCPP_EXPORTED_FROM_ABI bool __libcpp_thread_id_less(__libcpp_thread_id __t1, __libcpp_thread_id __t2);

//
// Thread
//
#define _LIBCPP_NULL_THREAD 0U
typedef void* __libcpp_thread_t;

_LIBCPP_EXPORTED_FROM_ABI bool __libcpp_thread_isnull(const __libcpp_thread_t* __t);

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_thread_create(__libcpp_thread_t* __t, void* (*__func)(void*), void* __arg);

_LIBCPP_EXPORTED_FROM_ABI __libcpp_thread_id __libcpp_thread_get_current_id();

_LIBCPP_EXPORTED_FROM_ABI __libcpp_thread_id __libcpp_thread_get_id(const __libcpp_thread_t* __t);

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_thread_join(__libcpp_thread_t* __t);

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_thread_detach(__libcpp_thread_t* __t);

_LIBCPP_EXPORTED_FROM_ABI void __libcpp_thread_yield();

_LIBCPP_EXPORTED_FROM_ABI void __libcpp_thread_sleep_for(const chrono::nanoseconds& __ns);

//
// Thread local storage
//
typedef long __libcpp_tls_key;

#define _LIBCPP_TLS_DESTRUCTOR_CC __stdcall

_LIBCPP_EXPORTED_FROM_ABI int
__libcpp_tls_create(__libcpp_tls_key* __key, void(_LIBCPP_TLS_DESTRUCTOR_CC* __at_exit)(void*));

_LIBCPP_EXPORTED_FROM_ABI void* __libcpp_tls_get(__libcpp_tls_key __key);

_LIBCPP_EXPORTED_FROM_ABI int __libcpp_tls_set(__libcpp_tls_key __key, void* __p);

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___THREAD_SUPPORT_WINDOWS_H
