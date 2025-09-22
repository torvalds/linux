// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_OPERATIONS_H
#define _LIBCPP___FILESYSTEM_OPERATIONS_H

#include <__chrono/time_point.h>
#include <__config>
#include <__filesystem/copy_options.h>
#include <__filesystem/file_status.h>
#include <__filesystem/file_time_type.h>
#include <__filesystem/file_type.h>
#include <__filesystem/path.h>
#include <__filesystem/perm_options.h>
#include <__filesystem/perms.h>
#include <__filesystem/space_info.h>
#include <__system_error/error_code.h>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17 && !defined(_LIBCPP_HAS_NO_FILESYSTEM)

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY_PUSH

_LIBCPP_EXPORTED_FROM_ABI path __absolute(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI path __canonical(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI bool
__copy_file(const path& __from, const path& __to, copy_options __opt, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI void
__copy_symlink(const path& __existing_symlink, const path& __new_symlink, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI void
__copy(const path& __from, const path& __to, copy_options __opt, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI bool __create_directories(const path&, error_code* = nullptr);
_LIBCPP_EXPORTED_FROM_ABI void
__create_directory_symlink(const path& __to, const path& __new_symlink, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI bool __create_directory(const path&, error_code* = nullptr);
_LIBCPP_EXPORTED_FROM_ABI bool __create_directory(const path&, const path& __attributes, error_code* = nullptr);
_LIBCPP_EXPORTED_FROM_ABI void
__create_hard_link(const path& __to, const path& __new_hard_link, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI void
__create_symlink(const path& __to, const path& __new_symlink, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI path __current_path(error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI void __current_path(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI bool __equivalent(const path&, const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI file_status __status(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI uintmax_t __file_size(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI uintmax_t __hard_link_count(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI file_status __symlink_status(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI file_time_type __last_write_time(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI void __last_write_time(const path&, file_time_type __new_time, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI path __weakly_canonical(path const& __p, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI path __read_symlink(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI uintmax_t __remove_all(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI bool __remove(const path&, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI void __rename(const path& __from, const path& __to, error_code* __ec = nullptr);
_LIBCPP_EXPORTED_FROM_ABI void __resize_file(const path&, uintmax_t __size, error_code* = nullptr);
_LIBCPP_EXPORTED_FROM_ABI path __temp_directory_path(error_code* __ec = nullptr);

inline _LIBCPP_HIDE_FROM_ABI path absolute(const path& __p) { return __absolute(__p); }
inline _LIBCPP_HIDE_FROM_ABI path absolute(const path& __p, error_code& __ec) { return __absolute(__p, &__ec); }
inline _LIBCPP_HIDE_FROM_ABI path canonical(const path& __p) { return __canonical(__p); }
inline _LIBCPP_HIDE_FROM_ABI path canonical(const path& __p, error_code& __ec) { return __canonical(__p, &__ec); }
inline _LIBCPP_HIDE_FROM_ABI bool copy_file(const path& __from, const path& __to) {
  return __copy_file(__from, __to, copy_options::none);
}
inline _LIBCPP_HIDE_FROM_ABI bool copy_file(const path& __from, const path& __to, error_code& __ec) {
  return __copy_file(__from, __to, copy_options::none, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI bool copy_file(const path& __from, const path& __to, copy_options __opt) {
  return __copy_file(__from, __to, __opt);
}
inline _LIBCPP_HIDE_FROM_ABI bool
copy_file(const path& __from, const path& __to, copy_options __opt, error_code& __ec) {
  return __copy_file(__from, __to, __opt, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI void copy_symlink(const path& __from, const path& __to) { __copy_symlink(__from, __to); }
inline _LIBCPP_HIDE_FROM_ABI void copy_symlink(const path& __from, const path& __to, error_code& __ec) noexcept {
  __copy_symlink(__from, __to, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI void copy(const path& __from, const path& __to) {
  __copy(__from, __to, copy_options::none);
}
inline _LIBCPP_HIDE_FROM_ABI void copy(const path& __from, const path& __to, error_code& __ec) {
  __copy(__from, __to, copy_options::none, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI void copy(const path& __from, const path& __to, copy_options __opt) {
  __copy(__from, __to, __opt);
}
inline _LIBCPP_HIDE_FROM_ABI void copy(const path& __from, const path& __to, copy_options __opt, error_code& __ec) {
  __copy(__from, __to, __opt, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI bool create_directories(const path& __p) { return __create_directories(__p); }
inline _LIBCPP_HIDE_FROM_ABI bool create_directories(const path& __p, error_code& __ec) {
  return __create_directories(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI void create_directory_symlink(const path& __target, const path& __link) {
  __create_directory_symlink(__target, __link);
}
inline _LIBCPP_HIDE_FROM_ABI void
create_directory_symlink(const path& __target, const path& __link, error_code& __ec) noexcept {
  __create_directory_symlink(__target, __link, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI bool create_directory(const path& __p) { return __create_directory(__p); }
inline _LIBCPP_HIDE_FROM_ABI bool create_directory(const path& __p, error_code& __ec) noexcept {
  return __create_directory(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI bool create_directory(const path& __p, const path& __attrs) {
  return __create_directory(__p, __attrs);
}
inline _LIBCPP_HIDE_FROM_ABI bool create_directory(const path& __p, const path& __attrs, error_code& __ec) noexcept {
  return __create_directory(__p, __attrs, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI void create_hard_link(const path& __target, const path& __link) {
  __create_hard_link(__target, __link);
}
inline _LIBCPP_HIDE_FROM_ABI void
create_hard_link(const path& __target, const path& __link, error_code& __ec) noexcept {
  __create_hard_link(__target, __link, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI void create_symlink(const path& __target, const path& __link) {
  __create_symlink(__target, __link);
}
inline _LIBCPP_HIDE_FROM_ABI void create_symlink(const path& __target, const path& __link, error_code& __ec) noexcept {
  return __create_symlink(__target, __link, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI path current_path() { return __current_path(); }
inline _LIBCPP_HIDE_FROM_ABI path current_path(error_code& __ec) { return __current_path(&__ec); }
inline _LIBCPP_HIDE_FROM_ABI void current_path(const path& __p) { __current_path(__p); }
inline _LIBCPP_HIDE_FROM_ABI void current_path(const path& __p, error_code& __ec) noexcept {
  __current_path(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI bool equivalent(const path& __p1, const path& __p2) { return __equivalent(__p1, __p2); }
inline _LIBCPP_HIDE_FROM_ABI bool equivalent(const path& __p1, const path& __p2, error_code& __ec) noexcept {
  return __equivalent(__p1, __p2, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI bool status_known(file_status __s) noexcept { return __s.type() != file_type::none; }
inline _LIBCPP_HIDE_FROM_ABI bool exists(file_status __s) noexcept {
  return status_known(__s) && __s.type() != file_type::not_found;
}
inline _LIBCPP_HIDE_FROM_ABI bool exists(const path& __p) { return exists(__status(__p)); }

inline _LIBCPP_HIDE_FROM_ABI bool exists(const path& __p, error_code& __ec) noexcept {
  auto __s = __status(__p, &__ec);
  if (status_known(__s))
    __ec.clear();
  return exists(__s);
}

inline _LIBCPP_HIDE_FROM_ABI uintmax_t file_size(const path& __p) { return __file_size(__p); }
inline _LIBCPP_HIDE_FROM_ABI uintmax_t file_size(const path& __p, error_code& __ec) noexcept {
  return __file_size(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI uintmax_t hard_link_count(const path& __p) { return __hard_link_count(__p); }
inline _LIBCPP_HIDE_FROM_ABI uintmax_t hard_link_count(const path& __p, error_code& __ec) noexcept {
  return __hard_link_count(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI bool is_block_file(file_status __s) noexcept { return __s.type() == file_type::block; }
inline _LIBCPP_HIDE_FROM_ABI bool is_block_file(const path& __p) { return is_block_file(__status(__p)); }
inline _LIBCPP_HIDE_FROM_ABI bool is_block_file(const path& __p, error_code& __ec) noexcept {
  return is_block_file(__status(__p, &__ec));
}
inline _LIBCPP_HIDE_FROM_ABI bool is_character_file(file_status __s) noexcept {
  return __s.type() == file_type::character;
}
inline _LIBCPP_HIDE_FROM_ABI bool is_character_file(const path& __p) { return is_character_file(__status(__p)); }
inline _LIBCPP_HIDE_FROM_ABI bool is_character_file(const path& __p, error_code& __ec) noexcept {
  return is_character_file(__status(__p, &__ec));
}
inline _LIBCPP_HIDE_FROM_ABI bool is_directory(file_status __s) noexcept { return __s.type() == file_type::directory; }
inline _LIBCPP_HIDE_FROM_ABI bool is_directory(const path& __p) { return is_directory(__status(__p)); }
inline _LIBCPP_HIDE_FROM_ABI bool is_directory(const path& __p, error_code& __ec) noexcept {
  return is_directory(__status(__p, &__ec));
}
_LIBCPP_EXPORTED_FROM_ABI bool __fs_is_empty(const path& __p, error_code* __ec = nullptr);
inline _LIBCPP_HIDE_FROM_ABI bool is_empty(const path& __p) { return __fs_is_empty(__p); }
inline _LIBCPP_HIDE_FROM_ABI bool is_empty(const path& __p, error_code& __ec) { return __fs_is_empty(__p, &__ec); }
inline _LIBCPP_HIDE_FROM_ABI bool is_fifo(file_status __s) noexcept { return __s.type() == file_type::fifo; }
inline _LIBCPP_HIDE_FROM_ABI bool is_fifo(const path& __p) { return is_fifo(__status(__p)); }
inline _LIBCPP_HIDE_FROM_ABI bool is_fifo(const path& __p, error_code& __ec) noexcept {
  return is_fifo(__status(__p, &__ec));
}
inline _LIBCPP_HIDE_FROM_ABI bool is_regular_file(file_status __s) noexcept { return __s.type() == file_type::regular; }
inline _LIBCPP_HIDE_FROM_ABI bool is_regular_file(const path& __p) { return is_regular_file(__status(__p)); }
inline _LIBCPP_HIDE_FROM_ABI bool is_regular_file(const path& __p, error_code& __ec) noexcept {
  return is_regular_file(__status(__p, &__ec));
}
inline _LIBCPP_HIDE_FROM_ABI bool is_symlink(file_status __s) noexcept { return __s.type() == file_type::symlink; }
inline _LIBCPP_HIDE_FROM_ABI bool is_symlink(const path& __p) { return is_symlink(__symlink_status(__p)); }
inline _LIBCPP_HIDE_FROM_ABI bool is_symlink(const path& __p, error_code& __ec) noexcept {
  return is_symlink(__symlink_status(__p, &__ec));
}
inline _LIBCPP_HIDE_FROM_ABI bool is_other(file_status __s) noexcept {
  return exists(__s) && !is_regular_file(__s) && !is_directory(__s) && !is_symlink(__s);
}
inline _LIBCPP_HIDE_FROM_ABI bool is_other(const path& __p) { return is_other(__status(__p)); }
inline _LIBCPP_HIDE_FROM_ABI bool is_other(const path& __p, error_code& __ec) noexcept {
  return is_other(__status(__p, &__ec));
}
inline _LIBCPP_HIDE_FROM_ABI bool is_socket(file_status __s) noexcept { return __s.type() == file_type::socket; }
inline _LIBCPP_HIDE_FROM_ABI bool is_socket(const path& __p) { return is_socket(__status(__p)); }
inline _LIBCPP_HIDE_FROM_ABI bool is_socket(const path& __p, error_code& __ec) noexcept {
  return is_socket(__status(__p, &__ec));
}
inline _LIBCPP_HIDE_FROM_ABI file_time_type last_write_time(const path& __p) { return __last_write_time(__p); }
inline _LIBCPP_HIDE_FROM_ABI file_time_type last_write_time(const path& __p, error_code& __ec) noexcept {
  return __last_write_time(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI void last_write_time(const path& __p, file_time_type __t) { __last_write_time(__p, __t); }
inline _LIBCPP_HIDE_FROM_ABI void last_write_time(const path& __p, file_time_type __t, error_code& __ec) noexcept {
  __last_write_time(__p, __t, &__ec);
}
_LIBCPP_EXPORTED_FROM_ABI void __permissions(const path&, perms, perm_options, error_code* = nullptr);
inline _LIBCPP_HIDE_FROM_ABI void
permissions(const path& __p, perms __prms, perm_options __opts = perm_options::replace) {
  __permissions(__p, __prms, __opts);
}
inline _LIBCPP_HIDE_FROM_ABI void permissions(const path& __p, perms __prms, error_code& __ec) noexcept {
  __permissions(__p, __prms, perm_options::replace, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI void permissions(const path& __p, perms __prms, perm_options __opts, error_code& __ec) {
  __permissions(__p, __prms, __opts, &__ec);
}

inline _LIBCPP_HIDE_FROM_ABI path proximate(const path& __p, const path& __base, error_code& __ec) {
  path __tmp = __weakly_canonical(__p, &__ec);
  if (__ec)
    return {};
  path __tmp_base = __weakly_canonical(__base, &__ec);
  if (__ec)
    return {};
  return __tmp.lexically_proximate(__tmp_base);
}

inline _LIBCPP_HIDE_FROM_ABI path proximate(const path& __p, error_code& __ec) {
  return proximate(__p, current_path(), __ec);
}
inline _LIBCPP_HIDE_FROM_ABI path proximate(const path& __p, const path& __base = current_path()) {
  return __weakly_canonical(__p).lexically_proximate(__weakly_canonical(__base));
}
inline _LIBCPP_HIDE_FROM_ABI path read_symlink(const path& __p) { return __read_symlink(__p); }
inline _LIBCPP_HIDE_FROM_ABI path read_symlink(const path& __p, error_code& __ec) { return __read_symlink(__p, &__ec); }

inline _LIBCPP_HIDE_FROM_ABI path relative(const path& __p, const path& __base, error_code& __ec) {
  path __tmp = __weakly_canonical(__p, &__ec);
  if (__ec)
    return path();
  path __tmpbase = __weakly_canonical(__base, &__ec);
  if (__ec)
    return path();
  return __tmp.lexically_relative(__tmpbase);
}

inline _LIBCPP_HIDE_FROM_ABI path relative(const path& __p, error_code& __ec) {
  return relative(__p, current_path(), __ec);
}
inline _LIBCPP_HIDE_FROM_ABI path relative(const path& __p, const path& __base = current_path()) {
  return __weakly_canonical(__p).lexically_relative(__weakly_canonical(__base));
}
inline _LIBCPP_HIDE_FROM_ABI uintmax_t remove_all(const path& __p) { return __remove_all(__p); }
inline _LIBCPP_HIDE_FROM_ABI uintmax_t remove_all(const path& __p, error_code& __ec) {
  return __remove_all(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI bool remove(const path& __p) { return __remove(__p); }
inline _LIBCPP_HIDE_FROM_ABI bool remove(const path& __p, error_code& __ec) noexcept { return __remove(__p, &__ec); }
inline _LIBCPP_HIDE_FROM_ABI void rename(const path& __from, const path& __to) { return __rename(__from, __to); }
inline _LIBCPP_HIDE_FROM_ABI void rename(const path& __from, const path& __to, error_code& __ec) noexcept {
  return __rename(__from, __to, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI void resize_file(const path& __p, uintmax_t __ns) { return __resize_file(__p, __ns); }
inline _LIBCPP_HIDE_FROM_ABI void resize_file(const path& __p, uintmax_t __ns, error_code& __ec) noexcept {
  return __resize_file(__p, __ns, &__ec);
}
_LIBCPP_EXPORTED_FROM_ABI space_info __space(const path&, error_code* __ec = nullptr);
inline _LIBCPP_HIDE_FROM_ABI space_info space(const path& __p) { return __space(__p); }
inline _LIBCPP_HIDE_FROM_ABI space_info space(const path& __p, error_code& __ec) noexcept {
  return __space(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI file_status status(const path& __p) { return __status(__p); }
inline _LIBCPP_HIDE_FROM_ABI file_status status(const path& __p, error_code& __ec) noexcept {
  return __status(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI file_status symlink_status(const path& __p) { return __symlink_status(__p); }
inline _LIBCPP_HIDE_FROM_ABI file_status symlink_status(const path& __p, error_code& __ec) noexcept {
  return __symlink_status(__p, &__ec);
}
inline _LIBCPP_HIDE_FROM_ABI path temp_directory_path() { return __temp_directory_path(); }
inline _LIBCPP_HIDE_FROM_ABI path temp_directory_path(error_code& __ec) { return __temp_directory_path(&__ec); }
inline _LIBCPP_HIDE_FROM_ABI path weakly_canonical(path const& __p) { return __weakly_canonical(__p); }
inline _LIBCPP_HIDE_FROM_ABI path weakly_canonical(path const& __p, error_code& __ec) {
  return __weakly_canonical(__p, &__ec);
}

_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY_POP

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // _LIBCPP_STD_VER >= 17 && !defined(_LIBCPP_HAS_NO_FILESYSTEM)

#endif // _LIBCPP___FILESYSTEM_OPERATIONS_H
