// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___SYSTEM_ERROR_ERROR_CODE_H
#define _LIBCPP___SYSTEM_ERROR_ERROR_CODE_H

#include <__compare/ordering.h>
#include <__config>
#include <__functional/hash.h>
#include <__functional/unary_function.h>
#include <__system_error/errc.h>
#include <__system_error/error_category.h>
#include <__system_error/error_condition.h>
#include <cstddef>
#include <string>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_error_code_enum : public false_type {};

#if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_error_code_enum_v = is_error_code_enum<_Tp>::value;
#endif

namespace __adl_only {
// Those cause ADL to trigger but they are not viable candidates,
// so they are never actually selected.
void make_error_code() = delete;
} // namespace __adl_only

class _LIBCPP_EXPORTED_FROM_ABI error_code {
  int __val_;
  const error_category* __cat_;

public:
  _LIBCPP_HIDE_FROM_ABI error_code() _NOEXCEPT : __val_(0), __cat_(&system_category()) {}

  _LIBCPP_HIDE_FROM_ABI error_code(int __val, const error_category& __cat) _NOEXCEPT : __val_(__val), __cat_(&__cat) {}

  template <class _Ep, __enable_if_t<is_error_code_enum<_Ep>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI error_code(_Ep __e) _NOEXCEPT {
    using __adl_only::make_error_code;
    *this = make_error_code(__e);
  }

  _LIBCPP_HIDE_FROM_ABI void assign(int __val, const error_category& __cat) _NOEXCEPT {
    __val_ = __val;
    __cat_ = &__cat;
  }

  template <class _Ep, __enable_if_t<is_error_code_enum<_Ep>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI error_code& operator=(_Ep __e) _NOEXCEPT {
    using __adl_only::make_error_code;
    *this = make_error_code(__e);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI void clear() _NOEXCEPT {
    __val_ = 0;
    __cat_ = &system_category();
  }

  _LIBCPP_HIDE_FROM_ABI int value() const _NOEXCEPT { return __val_; }

  _LIBCPP_HIDE_FROM_ABI const error_category& category() const _NOEXCEPT { return *__cat_; }

  _LIBCPP_HIDE_FROM_ABI error_condition default_error_condition() const _NOEXCEPT {
    return __cat_->default_error_condition(__val_);
  }

  string message() const;

  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const _NOEXCEPT { return __val_ != 0; }
};

inline _LIBCPP_HIDE_FROM_ABI error_code make_error_code(errc __e) _NOEXCEPT {
  return error_code(static_cast<int>(__e), generic_category());
}

inline _LIBCPP_HIDE_FROM_ABI bool operator==(const error_code& __x, const error_code& __y) _NOEXCEPT {
  return __x.category() == __y.category() && __x.value() == __y.value();
}

inline _LIBCPP_HIDE_FROM_ABI bool operator==(const error_code& __x, const error_condition& __y) _NOEXCEPT {
  return __x.category().equivalent(__x.value(), __y) || __y.category().equivalent(__x, __y.value());
}

#if _LIBCPP_STD_VER <= 17
inline _LIBCPP_HIDE_FROM_ABI bool operator==(const error_condition& __x, const error_code& __y) _NOEXCEPT {
  return __y == __x;
}
#endif

#if _LIBCPP_STD_VER <= 17

inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const error_code& __x, const error_code& __y) _NOEXCEPT {
  return !(__x == __y);
}

inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const error_code& __x, const error_condition& __y) _NOEXCEPT {
  return !(__x == __y);
}

inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const error_condition& __x, const error_code& __y) _NOEXCEPT {
  return !(__x == __y);
}

inline _LIBCPP_HIDE_FROM_ABI bool operator<(const error_code& __x, const error_code& __y) _NOEXCEPT {
  return __x.category() < __y.category() || (__x.category() == __y.category() && __x.value() < __y.value());
}

#else // _LIBCPP_STD_VER <= 17

inline _LIBCPP_HIDE_FROM_ABI strong_ordering operator<=>(const error_code& __x, const error_code& __y) noexcept {
  if (auto __c = __x.category() <=> __y.category(); __c != 0)
    return __c;
  return __x.value() <=> __y.value();
}

#endif // _LIBCPP_STD_VER <= 17

template <>
struct _LIBCPP_TEMPLATE_VIS hash<error_code> : public __unary_function<error_code, size_t> {
  _LIBCPP_HIDE_FROM_ABI size_t operator()(const error_code& __ec) const _NOEXCEPT {
    return static_cast<size_t>(__ec.value());
  }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___SYSTEM_ERROR_ERROR_CODE_H
