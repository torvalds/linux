// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_DIRECTORY_OPTIONS_H
#define _LIBCPP___FILESYSTEM_DIRECTORY_OPTIONS_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

enum class directory_options : unsigned char { none = 0, follow_directory_symlink = 1, skip_permission_denied = 2 };

_LIBCPP_HIDE_FROM_ABI inline constexpr directory_options operator&(directory_options __lhs, directory_options __rhs) {
  return static_cast<directory_options>(static_cast<unsigned char>(__lhs) & static_cast<unsigned char>(__rhs));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr directory_options operator|(directory_options __lhs, directory_options __rhs) {
  return static_cast<directory_options>(static_cast<unsigned char>(__lhs) | static_cast<unsigned char>(__rhs));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr directory_options operator^(directory_options __lhs, directory_options __rhs) {
  return static_cast<directory_options>(static_cast<unsigned char>(__lhs) ^ static_cast<unsigned char>(__rhs));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr directory_options operator~(directory_options __lhs) {
  return static_cast<directory_options>(~static_cast<unsigned char>(__lhs));
}

_LIBCPP_HIDE_FROM_ABI inline directory_options& operator&=(directory_options& __lhs, directory_options __rhs) {
  return __lhs = __lhs & __rhs;
}

_LIBCPP_HIDE_FROM_ABI inline directory_options& operator|=(directory_options& __lhs, directory_options __rhs) {
  return __lhs = __lhs | __rhs;
}

_LIBCPP_HIDE_FROM_ABI inline directory_options& operator^=(directory_options& __lhs, directory_options __rhs) {
  return __lhs = __lhs ^ __rhs;
}

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___FILESYSTEM_DIRECTORY_OPTIONS_H
