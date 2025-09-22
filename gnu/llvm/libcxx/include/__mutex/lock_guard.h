//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MUTEX_LOCK_GUARD_H
#define _LIBCPP___MUTEX_LOCK_GUARD_H

#include <__config>
#include <__mutex/tag_types.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Mutex>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_THREAD_SAFETY_ANNOTATION(scoped_lockable) lock_guard {
public:
  typedef _Mutex mutex_type;

private:
  mutex_type& __m_;

public:
  _LIBCPP_NODISCARD
  _LIBCPP_HIDE_FROM_ABI explicit lock_guard(mutex_type& __m) _LIBCPP_THREAD_SAFETY_ANNOTATION(acquire_capability(__m))
      : __m_(__m) {
    __m_.lock();
  }

  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI lock_guard(mutex_type& __m, adopt_lock_t)
      _LIBCPP_THREAD_SAFETY_ANNOTATION(requires_capability(__m))
      : __m_(__m) {}
  _LIBCPP_HIDE_FROM_ABI ~lock_guard() _LIBCPP_THREAD_SAFETY_ANNOTATION(release_capability()) { __m_.unlock(); }

  lock_guard(lock_guard const&)            = delete;
  lock_guard& operator=(lock_guard const&) = delete;
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(lock_guard);

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MUTEX_LOCK_GUARD_H
