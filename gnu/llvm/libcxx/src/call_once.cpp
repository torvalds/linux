//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__mutex/once_flag.h>
#include <__utility/exception_guard.h>

#ifndef _LIBCPP_HAS_NO_THREADS
#  include <__thread/support.h>
#endif

#include "include/atomic_support.h"

_LIBCPP_BEGIN_NAMESPACE_STD

// If dispatch_once_f ever handles C++ exceptions, and if one can get to it
// without illegal macros (unexpected macros not beginning with _UpperCase or
// __lowercase), and if it stops spinning waiting threads, then call_once should
// call into dispatch_once_f instead of here. Relevant radar this code needs to
// keep in sync with:  7741191.

#ifndef _LIBCPP_HAS_NO_THREADS
static constinit __libcpp_mutex_t mut  = _LIBCPP_MUTEX_INITIALIZER;
static constinit __libcpp_condvar_t cv = _LIBCPP_CONDVAR_INITIALIZER;
#endif

void __call_once(volatile once_flag::_State_type& flag, void* arg, void (*func)(void*)) {
#if defined(_LIBCPP_HAS_NO_THREADS)

  if (flag == once_flag::_Unset) {
    auto guard = std::__make_exception_guard([&flag] { flag = once_flag::_Unset; });
    flag       = once_flag::_Pending;
    func(arg);
    flag = once_flag::_Complete;
    guard.__complete();
  }

#else // !_LIBCPP_HAS_NO_THREADS

  __libcpp_mutex_lock(&mut);
  while (flag == once_flag::_Pending)
    __libcpp_condvar_wait(&cv, &mut);
  if (flag == once_flag::_Unset) {
    auto guard = std::__make_exception_guard([&flag] {
      __libcpp_mutex_lock(&mut);
      __libcpp_relaxed_store(&flag, once_flag::_Unset);
      __libcpp_mutex_unlock(&mut);
      __libcpp_condvar_broadcast(&cv);
    });

    __libcpp_relaxed_store(&flag, once_flag::_Pending);
    __libcpp_mutex_unlock(&mut);
    func(arg);
    __libcpp_mutex_lock(&mut);
    __libcpp_atomic_store(&flag, once_flag::_Complete, _AO_Release);
    __libcpp_mutex_unlock(&mut);
    __libcpp_condvar_broadcast(&cv);
    guard.__complete();
  } else {
    __libcpp_mutex_unlock(&mut);
  }

#endif // !_LIBCPP_HAS_NO_THREADS
}

_LIBCPP_END_NAMESPACE_STD
