// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMAT_ARGS_H
#define _LIBCPP___FORMAT_FORMAT_ARGS_H

#include <__config>
#include <__format/format_arg.h>
#include <__format/format_arg_store.h>
#include <__fwd/format.h>
#include <cstddef>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _Context>
class _LIBCPP_TEMPLATE_VIS basic_format_args {
public:
  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI basic_format_args(const __format_arg_store<_Context, _Args...>& __store) noexcept
      : __size_(sizeof...(_Args)) {
    if constexpr (sizeof...(_Args) != 0) {
      if constexpr (__format::__use_packed_format_arg_store(sizeof...(_Args))) {
        __values_ = __store.__storage.__values_;
        __types_  = __store.__storage.__types_;
      } else
        __args_ = __store.__storage.__args_;
    }
  }

  _LIBCPP_HIDE_FROM_ABI basic_format_arg<_Context> get(size_t __id) const noexcept {
    if (__id >= __size_)
      return basic_format_arg<_Context>{};

    if (__format::__use_packed_format_arg_store(__size_))
      return basic_format_arg<_Context>{__format::__get_packed_type(__types_, __id), __values_[__id]};

    return __args_[__id];
  }

  _LIBCPP_HIDE_FROM_ABI size_t __size() const noexcept { return __size_; }

private:
  size_t __size_{0};
  // [format.args]/5
  // [Note 1: Implementations are encouraged to optimize the representation of
  // basic_format_args for small number of formatting arguments by storing
  // indices of type alternatives separately from values and packing the
  // former. - end note]
  union {
    struct {
      const __basic_format_arg_value<_Context>* __values_;
      uint64_t __types_;
    };
    const basic_format_arg<_Context>* __args_;
  };
};

template <class _Context, class... _Args>
basic_format_args(__format_arg_store<_Context, _Args...>) -> basic_format_args<_Context>;

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_FORMAT_ARGS_H
