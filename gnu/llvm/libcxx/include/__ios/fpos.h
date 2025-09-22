// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___IOS_FPOS_H
#define _LIBCPP___IOS_FPOS_H

#include <__config>
#include <__fwd/ios.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _StateT>
class _LIBCPP_TEMPLATE_VIS fpos {
private:
  _StateT __st_;
  streamoff __off_;

public:
  _LIBCPP_HIDE_FROM_ABI fpos(streamoff __off = streamoff()) : __st_(), __off_(__off) {}

  _LIBCPP_HIDE_FROM_ABI operator streamoff() const { return __off_; }

  _LIBCPP_HIDE_FROM_ABI _StateT state() const { return __st_; }
  _LIBCPP_HIDE_FROM_ABI void state(_StateT __st) { __st_ = __st; }

  _LIBCPP_HIDE_FROM_ABI fpos& operator+=(streamoff __off) {
    __off_ += __off;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI fpos operator+(streamoff __off) const {
    fpos __t(*this);
    __t += __off;
    return __t;
  }

  _LIBCPP_HIDE_FROM_ABI fpos& operator-=(streamoff __off) {
    __off_ -= __off;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI fpos operator-(streamoff __off) const {
    fpos __t(*this);
    __t -= __off;
    return __t;
  }
};

template <class _StateT>
inline _LIBCPP_HIDE_FROM_ABI streamoff operator-(const fpos<_StateT>& __x, const fpos<_StateT>& __y) {
  return streamoff(__x) - streamoff(__y);
}

template <class _StateT>
inline _LIBCPP_HIDE_FROM_ABI bool operator==(const fpos<_StateT>& __x, const fpos<_StateT>& __y) {
  return streamoff(__x) == streamoff(__y);
}

template <class _StateT>
inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const fpos<_StateT>& __x, const fpos<_StateT>& __y) {
  return streamoff(__x) != streamoff(__y);
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___IOS_FPOS_H
