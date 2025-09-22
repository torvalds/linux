// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_FILE_STATUS_H
#define _LIBCPP___FILESYSTEM_FILE_STATUS_H

#include <__config>
#include <__filesystem/file_type.h>
#include <__filesystem/perms.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

class _LIBCPP_EXPORTED_FROM_ABI file_status {
public:
  // constructors
  _LIBCPP_HIDE_FROM_ABI file_status() noexcept : file_status(file_type::none) {}
  _LIBCPP_HIDE_FROM_ABI explicit file_status(file_type __ft, perms __prms = perms::unknown) noexcept
      : __ft_(__ft), __prms_(__prms) {}

  _LIBCPP_HIDE_FROM_ABI file_status(const file_status&) noexcept = default;
  _LIBCPP_HIDE_FROM_ABI file_status(file_status&&) noexcept      = default;

  _LIBCPP_HIDE_FROM_ABI ~file_status() {}

  _LIBCPP_HIDE_FROM_ABI file_status& operator=(const file_status&) noexcept = default;
  _LIBCPP_HIDE_FROM_ABI file_status& operator=(file_status&&) noexcept      = default;

  // observers
  _LIBCPP_HIDE_FROM_ABI file_type type() const noexcept { return __ft_; }

  _LIBCPP_HIDE_FROM_ABI perms permissions() const noexcept { return __prms_; }

  // modifiers
  _LIBCPP_HIDE_FROM_ABI void type(file_type __ft) noexcept { __ft_ = __ft; }

  _LIBCPP_HIDE_FROM_ABI void permissions(perms __p) noexcept { __prms_ = __p; }

#  if _LIBCPP_STD_VER >= 20

  _LIBCPP_HIDE_FROM_ABI friend bool operator==(const file_status& __lhs, const file_status& __rhs) noexcept {
    return __lhs.type() == __rhs.type() && __lhs.permissions() == __rhs.permissions();
  }

#  endif

private:
  file_type __ft_;
  perms __prms_;
};

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___FILESYSTEM_FILE_STATUS_H
