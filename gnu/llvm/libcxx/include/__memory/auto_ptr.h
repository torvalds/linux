// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_AUTO_PTR_H
#define _LIBCPP___MEMORY_AUTO_PTR_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR)

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct _LIBCPP_DEPRECATED_IN_CXX11 auto_ptr_ref {
  _Tp* __ptr_;
};

template <class _Tp>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 auto_ptr {
private:
  _Tp* __ptr_;

public:
  typedef _Tp element_type;

  _LIBCPP_HIDE_FROM_ABI explicit auto_ptr(_Tp* __p = 0) _NOEXCEPT : __ptr_(__p) {}
  _LIBCPP_HIDE_FROM_ABI auto_ptr(auto_ptr& __p) _NOEXCEPT : __ptr_(__p.release()) {}
  template <class _Up>
  _LIBCPP_HIDE_FROM_ABI auto_ptr(auto_ptr<_Up>& __p) _NOEXCEPT : __ptr_(__p.release()) {}
  _LIBCPP_HIDE_FROM_ABI auto_ptr& operator=(auto_ptr& __p) _NOEXCEPT {
    reset(__p.release());
    return *this;
  }
  template <class _Up>
  _LIBCPP_HIDE_FROM_ABI auto_ptr& operator=(auto_ptr<_Up>& __p) _NOEXCEPT {
    reset(__p.release());
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI auto_ptr& operator=(auto_ptr_ref<_Tp> __p) _NOEXCEPT {
    reset(__p.__ptr_);
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI ~auto_ptr() _NOEXCEPT { delete __ptr_; }

  _LIBCPP_HIDE_FROM_ABI _Tp& operator*() const _NOEXCEPT { return *__ptr_; }
  _LIBCPP_HIDE_FROM_ABI _Tp* operator->() const _NOEXCEPT { return __ptr_; }
  _LIBCPP_HIDE_FROM_ABI _Tp* get() const _NOEXCEPT { return __ptr_; }
  _LIBCPP_HIDE_FROM_ABI _Tp* release() _NOEXCEPT {
    _Tp* __t = __ptr_;
    __ptr_   = nullptr;
    return __t;
  }
  _LIBCPP_HIDE_FROM_ABI void reset(_Tp* __p = 0) _NOEXCEPT {
    if (__ptr_ != __p)
      delete __ptr_;
    __ptr_ = __p;
  }

  _LIBCPP_HIDE_FROM_ABI auto_ptr(auto_ptr_ref<_Tp> __p) _NOEXCEPT : __ptr_(__p.__ptr_) {}
  template <class _Up>
  _LIBCPP_HIDE_FROM_ABI operator auto_ptr_ref<_Up>() _NOEXCEPT {
    auto_ptr_ref<_Up> __t;
    __t.__ptr_ = release();
    return __t;
  }
  template <class _Up>
  _LIBCPP_HIDE_FROM_ABI operator auto_ptr<_Up>() _NOEXCEPT {
    return auto_ptr<_Up>(release());
  }
};

template <>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 auto_ptr<void> {
public:
  typedef void element_type;
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR)

#endif // _LIBCPP___MEMORY_AUTO_PTR_H
