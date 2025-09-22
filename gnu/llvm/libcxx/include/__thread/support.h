// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___THREAD_SUPPORT_H
#define _LIBCPP___THREAD_SUPPORT_H

#include <__config>

#ifndef _LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
#  pragma GCC system_header
#endif

/*

//
// The library supports multiple implementations of the basic threading functionality.
// The following functionality must be provided by any implementation:
//

_LIBCPP_BEGIN_NAMESPACE_STD

using __libcpp_timespec_t = ...;

//
// Mutex
//
using __libcpp_mutex_t = ...;
#define _LIBCPP_MUTEX_INITIALIZER ...

using __libcpp_recursive_mutex_t = ...;

int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t*);
_LIBCPP_NO_THREAD_SAFETY_ANALYSIS int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t*);
_LIBCPP_NO_THREAD_SAFETY_ANALYSIS bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t*);
_LIBCPP_NO_THREAD_SAFETY_ANALYSIS int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t*);
int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t*);

_LIBCPP_NO_THREAD_SAFETY_ANALYSIS int __libcpp_mutex_lock(__libcpp_mutex_t*);
_LIBCPP_NO_THREAD_SAFETY_ANALYSIS bool __libcpp_mutex_trylock(__libcpp_mutex_t*);
_LIBCPP_NO_THREAD_SAFETY_ANALYSIS int __libcpp_mutex_unlock(__libcpp_mutex_t*);
int __libcpp_mutex_destroy(__libcpp_mutex_t*);

//
// Condition Variable
//
using __libcpp_condvar_t = ...;
#define _LIBCPP_CONDVAR_INITIALIZER ...

int __libcpp_condvar_signal(__libcpp_condvar_t*);
int __libcpp_condvar_broadcast(__libcpp_condvar_t*);
_LIBCPP_NO_THREAD_SAFETY_ANALYSIS int __libcpp_condvar_wait(__libcpp_condvar_t*, __libcpp_mutex_t*);
_LIBCPP_NO_THREAD_SAFETY_ANALYSIS
int __libcpp_condvar_timedwait(__libcpp_condvar_t*, __libcpp_mutex_t*, __libcpp_timespec_t*);
int __libcpp_condvar_destroy(__libcpp_condvar_t*);

//
// Execute once
//
using __libcpp_exec_once_flag = ...;
#define _LIBCPP_EXEC_ONCE_INITIALIZER ...

int __libcpp_execute_once(__libcpp_exec_once_flag*, void (*__init_routine)());

//
// Thread id
//
using __libcpp_thread_id = ...;

bool __libcpp_thread_id_equal(__libcpp_thread_id, __libcpp_thread_id);
bool __libcpp_thread_id_less(__libcpp_thread_id, __libcpp_thread_id);

//
// Thread
//
#define _LIBCPP_NULL_THREAD ...
using __libcpp_thread_t = ...;

bool __libcpp_thread_isnull(const __libcpp_thread_t*);
int __libcpp_thread_create(__libcpp_thread_t*, void* (*__func)(void*), void* __arg);
__libcpp_thread_id __libcpp_thread_get_current_id();
__libcpp_thread_id __libcpp_thread_get_id(const __libcpp_thread_t*);
int __libcpp_thread_join(__libcpp_thread_t*);
int __libcpp_thread_detach(__libcpp_thread_t*);
void __libcpp_thread_yield();
void __libcpp_thread_sleep_for(const chrono::nanoseconds&);

//
// Thread local storage
//
#define _LIBCPP_TLS_DESTRUCTOR_CC ...
using __libcpp_tls_key = ...;

int __libcpp_tls_create(__libcpp_tls_key*, void (*__at_exit)(void*));
void* __libcpp_tls_get(__libcpp_tls_key);
int __libcpp_tls_set(__libcpp_tls_key, void*);

_LIBCPP_END_NAMESPACE_STD

*/

#if !defined(_LIBCPP_HAS_NO_THREADS)

#  if defined(_LIBCPP_HAS_THREAD_API_EXTERNAL)
#    include <__thread/support/external.h>
#  elif defined(_LIBCPP_HAS_THREAD_API_PTHREAD)
#    include <__thread/support/pthread.h>
#  elif defined(_LIBCPP_HAS_THREAD_API_C11)
#    include <__thread/support/c11.h>
#  elif defined(_LIBCPP_HAS_THREAD_API_WIN32)
#    include <__thread/support/windows.h>
#  else
#    error "No threading API was selected"
#  endif

#endif // !_LIBCPP_HAS_NO_THREADS

#endif // _LIBCPP___THREAD_SUPPORT_H
