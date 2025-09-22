// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef _LIBCPP___EXPECTED_BAD_EXPECTED_ACCESS_H
#define _LIBCPP___EXPECTED_BAD_EXPECTED_ACCESS_H

#include <__config>
#include <__exception/exception.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 23

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Err>
class bad_expected_access;

_LIBCPP_DIAGNOSTIC_PUSH
#  if !_LIBCPP_AVAILABILITY_HAS_BAD_EXPECTED_ACCESS_KEY_FUNCTION
_LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wweak-vtables")
#  endif
template <>
class _LIBCPP_EXPORTED_FROM_ABI bad_expected_access<void> : public exception {
protected:
  _LIBCPP_HIDE_FROM_ABI bad_expected_access() noexcept                                      = default;
  _LIBCPP_HIDE_FROM_ABI bad_expected_access(const bad_expected_access&) noexcept            = default;
  _LIBCPP_HIDE_FROM_ABI bad_expected_access(bad_expected_access&&) noexcept                 = default;
  _LIBCPP_HIDE_FROM_ABI bad_expected_access& operator=(const bad_expected_access&) noexcept = default;
  _LIBCPP_HIDE_FROM_ABI bad_expected_access& operator=(bad_expected_access&&) noexcept      = default;
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL ~bad_expected_access() override                             = default;

public:
#  if _LIBCPP_AVAILABILITY_HAS_BAD_EXPECTED_ACCESS_KEY_FUNCTION
  const char* what() const noexcept override;
#  else
  _LIBCPP_HIDE_FROM_ABI_VIRTUAL const char* what() const noexcept override { return "bad access to std::expected"; }
#  endif
};
_LIBCPP_DIAGNOSTIC_POP

template <class _Err>
class bad_expected_access : public bad_expected_access<void> {
public:
  _LIBCPP_HIDE_FROM_ABI explicit bad_expected_access(_Err __e) : __unex_(std::move(__e)) {}

  _LIBCPP_HIDE_FROM_ABI _Err& error() & noexcept { return __unex_; }
  _LIBCPP_HIDE_FROM_ABI const _Err& error() const& noexcept { return __unex_; }
  _LIBCPP_HIDE_FROM_ABI _Err&& error() && noexcept { return std::move(__unex_); }
  _LIBCPP_HIDE_FROM_ABI const _Err&& error() const&& noexcept { return std::move(__unex_); }

private:
  _Err __unex_;
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_POP_MACROS

#endif // _LIBCPP___EXPECTED_BAD_EXPECTED_ACCESS_H
