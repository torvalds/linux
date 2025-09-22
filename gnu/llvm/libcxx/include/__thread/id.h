// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___THREAD_ID_H
#define _LIBCPP___THREAD_ID_H

#include <__compare/ordering.h>
#include <__config>
#include <__fwd/functional.h>
#include <__fwd/ostream.h>
#include <__thread/support.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_HAS_NO_THREADS
class _LIBCPP_EXPORTED_FROM_ABI __thread_id;

namespace this_thread {

_LIBCPP_HIDE_FROM_ABI __thread_id get_id() _NOEXCEPT;

} // namespace this_thread

template <>
struct hash<__thread_id>;

class _LIBCPP_TEMPLATE_VIS __thread_id {
  // FIXME: pthread_t is a pointer on Darwin but a long on Linux.
  // NULL is the no-thread value on Darwin.  Someone needs to check
  // on other platforms.  We assume 0 works everywhere for now.
  __libcpp_thread_id __id_;

  static _LIBCPP_HIDE_FROM_ABI bool
  __lt_impl(__thread_id __x, __thread_id __y) _NOEXCEPT { // id==0 is always less than any other thread_id
    if (__x.__id_ == 0)
      return __y.__id_ != 0;
    if (__y.__id_ == 0)
      return false;
    return __libcpp_thread_id_less(__x.__id_, __y.__id_);
  }

public:
  _LIBCPP_HIDE_FROM_ABI __thread_id() _NOEXCEPT : __id_(0) {}

  _LIBCPP_HIDE_FROM_ABI void __reset() { __id_ = 0; }

  friend _LIBCPP_HIDE_FROM_ABI bool operator==(__thread_id __x, __thread_id __y) _NOEXCEPT;
#  if _LIBCPP_STD_VER <= 17
  friend _LIBCPP_HIDE_FROM_ABI bool operator<(__thread_id __x, __thread_id __y) _NOEXCEPT;
#  else  // _LIBCPP_STD_VER <= 17
  friend _LIBCPP_HIDE_FROM_ABI strong_ordering operator<=>(__thread_id __x, __thread_id __y) noexcept;
#  endif // _LIBCPP_STD_VER <= 17

  template <class _CharT, class _Traits>
  friend _LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, __thread_id __id);

private:
  _LIBCPP_HIDE_FROM_ABI __thread_id(__libcpp_thread_id __id) : __id_(__id) {}

  _LIBCPP_HIDE_FROM_ABI friend __libcpp_thread_id __get_underlying_id(const __thread_id __id) { return __id.__id_; }

  friend __thread_id this_thread::get_id() _NOEXCEPT;
  friend class _LIBCPP_EXPORTED_FROM_ABI thread;
  friend struct _LIBCPP_TEMPLATE_VIS hash<__thread_id>;
};

inline _LIBCPP_HIDE_FROM_ABI bool operator==(__thread_id __x, __thread_id __y) _NOEXCEPT {
  // Don't pass id==0 to underlying routines
  if (__x.__id_ == 0)
    return __y.__id_ == 0;
  if (__y.__id_ == 0)
    return false;
  return __libcpp_thread_id_equal(__x.__id_, __y.__id_);
}

#  if _LIBCPP_STD_VER <= 17

inline _LIBCPP_HIDE_FROM_ABI bool operator!=(__thread_id __x, __thread_id __y) _NOEXCEPT { return !(__x == __y); }

inline _LIBCPP_HIDE_FROM_ABI bool operator<(__thread_id __x, __thread_id __y) _NOEXCEPT {
  return __thread_id::__lt_impl(__x.__id_, __y.__id_);
}

inline _LIBCPP_HIDE_FROM_ABI bool operator<=(__thread_id __x, __thread_id __y) _NOEXCEPT { return !(__y < __x); }
inline _LIBCPP_HIDE_FROM_ABI bool operator>(__thread_id __x, __thread_id __y) _NOEXCEPT { return __y < __x; }
inline _LIBCPP_HIDE_FROM_ABI bool operator>=(__thread_id __x, __thread_id __y) _NOEXCEPT { return !(__x < __y); }

#  else // _LIBCPP_STD_VER <= 17

inline _LIBCPP_HIDE_FROM_ABI strong_ordering operator<=>(__thread_id __x, __thread_id __y) noexcept {
  if (__x == __y)
    return strong_ordering::equal;
  if (__thread_id::__lt_impl(__x, __y))
    return strong_ordering::less;
  return strong_ordering::greater;
}

#  endif // _LIBCPP_STD_VER <= 17

namespace this_thread {

inline _LIBCPP_HIDE_FROM_ABI __thread_id get_id() _NOEXCEPT { return __libcpp_thread_get_current_id(); }

} // namespace this_thread

#endif // !_LIBCPP_HAS_NO_THREADS

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___THREAD_ID_H
