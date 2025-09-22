// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_PERMS_H
#define _LIBCPP___FILESYSTEM_PERMS_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

// On Windows, these permission bits map to one single readonly flag per
// file, and the executable bit is always returned as set. When setting
// permissions, as long as the write bit is set for either owner, group or
// others, the readonly flag is cleared.
enum class perms : unsigned {
  none = 0,

  owner_read  = 0400,
  owner_write = 0200,
  owner_exec  = 0100,
  owner_all   = 0700,

  group_read  = 040,
  group_write = 020,
  group_exec  = 010,
  group_all   = 070,

  others_read  = 04,
  others_write = 02,
  others_exec  = 01,
  others_all   = 07,

  all = 0777,

  set_uid    = 04000,
  set_gid    = 02000,
  sticky_bit = 01000,
  mask       = 07777,
  unknown    = 0xFFFF,
};

_LIBCPP_HIDE_FROM_ABI inline constexpr perms operator&(perms __lhs, perms __rhs) {
  return static_cast<perms>(static_cast<unsigned>(__lhs) & static_cast<unsigned>(__rhs));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr perms operator|(perms __lhs, perms __rhs) {
  return static_cast<perms>(static_cast<unsigned>(__lhs) | static_cast<unsigned>(__rhs));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr perms operator^(perms __lhs, perms __rhs) {
  return static_cast<perms>(static_cast<unsigned>(__lhs) ^ static_cast<unsigned>(__rhs));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr perms operator~(perms __lhs) {
  return static_cast<perms>(~static_cast<unsigned>(__lhs));
}

_LIBCPP_HIDE_FROM_ABI inline perms& operator&=(perms& __lhs, perms __rhs) { return __lhs = __lhs & __rhs; }

_LIBCPP_HIDE_FROM_ABI inline perms& operator|=(perms& __lhs, perms __rhs) { return __lhs = __lhs | __rhs; }

_LIBCPP_HIDE_FROM_ABI inline perms& operator^=(perms& __lhs, perms __rhs) { return __lhs = __lhs ^ __rhs; }

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___FILESYSTEM_PERMS_H
