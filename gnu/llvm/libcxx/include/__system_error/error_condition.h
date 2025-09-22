// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___SYSTEM_ERROR_ERROR_CONDITION_H
#define _LIBCPP___SYSTEM_ERROR_ERROR_CONDITION_H

#include <__compare/ordering.h>
#include <__config>
#include <__functional/hash.h>
#include <__functional/unary_function.h>
#include <__system_error/errc.h>
#include <__system_error/error_category.h>
#include <cstddef>
#include <string>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_error_condition_enum : public false_type {};

#if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_error_condition_enum_v = is_error_condition_enum<_Tp>::value;
#endif

template <>
struct _LIBCPP_TEMPLATE_VIS is_error_condition_enum<errc> : true_type {};

#ifdef _LIBCPP_CXX03_LANG
template <>
struct _LIBCPP_TEMPLATE_VIS is_error_condition_enum<errc::__lx> : true_type {};
#endif

namespace __adl_only {
// Those cause ADL to trigger but they are not viable candidates,
// so they are never actually selected.
void make_error_condition() = delete;
} // namespace __adl_only

class _LIBCPP_EXPORTED_FROM_ABI error_condition {
  int __val_;
  const error_category* __cat_;

public:
  _LIBCPP_HIDE_FROM_ABI error_condition() _NOEXCEPT : __val_(0), __cat_(&generic_category()) {}

  _LIBCPP_HIDE_FROM_ABI error_condition(int __val, const error_category& __cat) _NOEXCEPT
      : __val_(__val),
        __cat_(&__cat) {}

  template <class _Ep, __enable_if_t<is_error_condition_enum<_Ep>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI error_condition(_Ep __e) _NOEXCEPT {
    using __adl_only::make_error_condition;
    *this = make_error_condition(__e);
  }

  _LIBCPP_HIDE_FROM_ABI void assign(int __val, const error_category& __cat) _NOEXCEPT {
    __val_ = __val;
    __cat_ = &__cat;
  }

  template <class _Ep, __enable_if_t<is_error_condition_enum<_Ep>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI error_condition& operator=(_Ep __e) _NOEXCEPT {
    using __adl_only::make_error_condition;
    *this = make_error_condition(__e);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI void clear() _NOEXCEPT {
    __val_ = 0;
    __cat_ = &generic_category();
  }

  _LIBCPP_HIDE_FROM_ABI int value() const _NOEXCEPT { return __val_; }

  _LIBCPP_HIDE_FROM_ABI const error_category& category() const _NOEXCEPT { return *__cat_; }
  string message() const;

  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const _NOEXCEPT { return __val_ != 0; }
};

inline _LIBCPP_HIDE_FROM_ABI error_condition make_error_condition(errc __e) _NOEXCEPT {
  return error_condition(static_cast<int>(__e), generic_category());
}

inline _LIBCPP_HIDE_FROM_ABI bool operator==(const error_condition& __x, const error_condition& __y) _NOEXCEPT {
  return __x.category() == __y.category() && __x.value() == __y.value();
}

#if _LIBCPP_STD_VER <= 17

inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const error_condition& __x, const error_condition& __y) _NOEXCEPT {
  return !(__x == __y);
}

inline _LIBCPP_HIDE_FROM_ABI bool operator<(const error_condition& __x, const error_condition& __y) _NOEXCEPT {
  return __x.category() < __y.category() || (__x.category() == __y.category() && __x.value() < __y.value());
}

#else // _LIBCPP_STD_VER <= 17

inline _LIBCPP_HIDE_FROM_ABI strong_ordering
operator<=>(const error_condition& __x, const error_condition& __y) noexcept {
  if (auto __c = __x.category() <=> __y.category(); __c != 0)
    return __c;
  return __x.value() <=> __y.value();
}

#endif // _LIBCPP_STD_VER <= 17

template <>
struct _LIBCPP_TEMPLATE_VIS hash<error_condition> : public __unary_function<error_condition, size_t> {
  _LIBCPP_HIDE_FROM_ABI size_t operator()(const error_condition& __ec) const _NOEXCEPT {
    return static_cast<size_t>(__ec.value());
  }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___SYSTEM_ERROR_ERROR_CONDITION_H
