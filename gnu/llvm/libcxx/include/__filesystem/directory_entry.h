// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_DIRECTORY_ENTRY_H
#define _LIBCPP___FILESYSTEM_DIRECTORY_ENTRY_H

#include <__chrono/time_point.h>
#include <__compare/ordering.h>
#include <__config>
#include <__filesystem/file_status.h>
#include <__filesystem/file_time_type.h>
#include <__filesystem/file_type.h>
#include <__filesystem/filesystem_error.h>
#include <__filesystem/operations.h>
#include <__filesystem/path.h>
#include <__filesystem/perms.h>
#include <__system_error/errc.h>
#include <__system_error/error_code.h>
#include <__utility/move.h>
#include <__utility/unreachable.h>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 17 && !defined(_LIBCPP_HAS_NO_FILESYSTEM)

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY_PUSH

class directory_entry {
  typedef filesystem::path _Path;

public:
  // constructors and destructors
  _LIBCPP_HIDE_FROM_ABI directory_entry() noexcept                  = default;
  _LIBCPP_HIDE_FROM_ABI directory_entry(directory_entry const&)     = default;
  _LIBCPP_HIDE_FROM_ABI directory_entry(directory_entry&&) noexcept = default;

  _LIBCPP_HIDE_FROM_ABI explicit directory_entry(_Path const& __p) : __p_(__p) {
    error_code __ec;
    __refresh(&__ec);
  }

  _LIBCPP_HIDE_FROM_ABI directory_entry(_Path const& __p, error_code& __ec) : __p_(__p) { __refresh(&__ec); }

  _LIBCPP_HIDE_FROM_ABI ~directory_entry() {}

  _LIBCPP_HIDE_FROM_ABI directory_entry& operator=(directory_entry const&)     = default;
  _LIBCPP_HIDE_FROM_ABI directory_entry& operator=(directory_entry&&) noexcept = default;

  _LIBCPP_HIDE_FROM_ABI void assign(_Path const& __p) {
    __p_ = __p;
    error_code __ec;
    __refresh(&__ec);
  }

  _LIBCPP_HIDE_FROM_ABI void assign(_Path const& __p, error_code& __ec) {
    __p_ = __p;
    __refresh(&__ec);
  }

  _LIBCPP_HIDE_FROM_ABI void replace_filename(_Path const& __p) {
    __p_.replace_filename(__p);
    error_code __ec;
    __refresh(&__ec);
  }

  _LIBCPP_HIDE_FROM_ABI void replace_filename(_Path const& __p, error_code& __ec) {
    __p_ = __p_.parent_path() / __p;
    __refresh(&__ec);
  }

  _LIBCPP_HIDE_FROM_ABI void refresh() { __refresh(); }

  _LIBCPP_HIDE_FROM_ABI void refresh(error_code& __ec) noexcept { __refresh(&__ec); }

  _LIBCPP_HIDE_FROM_ABI _Path const& path() const noexcept { return __p_; }

  _LIBCPP_HIDE_FROM_ABI operator const _Path&() const noexcept { return __p_; }

  _LIBCPP_HIDE_FROM_ABI bool exists() const { return filesystem::exists(file_status{__get_ft()}); }

  _LIBCPP_HIDE_FROM_ABI bool exists(error_code& __ec) const noexcept {
    return filesystem::exists(file_status{__get_ft(&__ec)});
  }

  _LIBCPP_HIDE_FROM_ABI bool is_block_file() const { return __get_ft() == file_type::block; }

  _LIBCPP_HIDE_FROM_ABI bool is_block_file(error_code& __ec) const noexcept {
    return __get_ft(&__ec) == file_type::block;
  }

  _LIBCPP_HIDE_FROM_ABI bool is_character_file() const { return __get_ft() == file_type::character; }

  _LIBCPP_HIDE_FROM_ABI bool is_character_file(error_code& __ec) const noexcept {
    return __get_ft(&__ec) == file_type::character;
  }

  _LIBCPP_HIDE_FROM_ABI bool is_directory() const { return __get_ft() == file_type::directory; }

  _LIBCPP_HIDE_FROM_ABI bool is_directory(error_code& __ec) const noexcept {
    return __get_ft(&__ec) == file_type::directory;
  }

  _LIBCPP_HIDE_FROM_ABI bool is_fifo() const { return __get_ft() == file_type::fifo; }

  _LIBCPP_HIDE_FROM_ABI bool is_fifo(error_code& __ec) const noexcept { return __get_ft(&__ec) == file_type::fifo; }

  _LIBCPP_HIDE_FROM_ABI bool is_other() const { return filesystem::is_other(file_status{__get_ft()}); }

  _LIBCPP_HIDE_FROM_ABI bool is_other(error_code& __ec) const noexcept {
    return filesystem::is_other(file_status{__get_ft(&__ec)});
  }

  _LIBCPP_HIDE_FROM_ABI bool is_regular_file() const { return __get_ft() == file_type::regular; }

  _LIBCPP_HIDE_FROM_ABI bool is_regular_file(error_code& __ec) const noexcept {
    return __get_ft(&__ec) == file_type::regular;
  }

  _LIBCPP_HIDE_FROM_ABI bool is_socket() const { return __get_ft() == file_type::socket; }

  _LIBCPP_HIDE_FROM_ABI bool is_socket(error_code& __ec) const noexcept { return __get_ft(&__ec) == file_type::socket; }

  _LIBCPP_HIDE_FROM_ABI bool is_symlink() const { return __get_sym_ft() == file_type::symlink; }

  _LIBCPP_HIDE_FROM_ABI bool is_symlink(error_code& __ec) const noexcept {
    return __get_sym_ft(&__ec) == file_type::symlink;
  }
  _LIBCPP_HIDE_FROM_ABI uintmax_t file_size() const { return __get_size(); }

  _LIBCPP_HIDE_FROM_ABI uintmax_t file_size(error_code& __ec) const noexcept { return __get_size(&__ec); }

  _LIBCPP_HIDE_FROM_ABI uintmax_t hard_link_count() const { return __get_nlink(); }

  _LIBCPP_HIDE_FROM_ABI uintmax_t hard_link_count(error_code& __ec) const noexcept { return __get_nlink(&__ec); }

  _LIBCPP_HIDE_FROM_ABI file_time_type last_write_time() const { return __get_write_time(); }

  _LIBCPP_HIDE_FROM_ABI file_time_type last_write_time(error_code& __ec) const noexcept {
    return __get_write_time(&__ec);
  }

  _LIBCPP_HIDE_FROM_ABI file_status status() const { return __get_status(); }

  _LIBCPP_HIDE_FROM_ABI file_status status(error_code& __ec) const noexcept { return __get_status(&__ec); }

  _LIBCPP_HIDE_FROM_ABI file_status symlink_status() const { return __get_symlink_status(); }

  _LIBCPP_HIDE_FROM_ABI file_status symlink_status(error_code& __ec) const noexcept {
    return __get_symlink_status(&__ec);
  }

  _LIBCPP_HIDE_FROM_ABI bool operator==(directory_entry const& __rhs) const noexcept { return __p_ == __rhs.__p_; }

#  if _LIBCPP_STD_VER <= 17
  _LIBCPP_HIDE_FROM_ABI bool operator!=(directory_entry const& __rhs) const noexcept { return __p_ != __rhs.__p_; }

  _LIBCPP_HIDE_FROM_ABI bool operator<(directory_entry const& __rhs) const noexcept { return __p_ < __rhs.__p_; }

  _LIBCPP_HIDE_FROM_ABI bool operator<=(directory_entry const& __rhs) const noexcept { return __p_ <= __rhs.__p_; }

  _LIBCPP_HIDE_FROM_ABI bool operator>(directory_entry const& __rhs) const noexcept { return __p_ > __rhs.__p_; }

  _LIBCPP_HIDE_FROM_ABI bool operator>=(directory_entry const& __rhs) const noexcept { return __p_ >= __rhs.__p_; }

#  else // _LIBCPP_STD_VER <= 17

  _LIBCPP_HIDE_FROM_ABI strong_ordering operator<=>(const directory_entry& __rhs) const noexcept {
    return __p_ <=> __rhs.__p_;
  }

#  endif // _LIBCPP_STD_VER <= 17

  template <class _CharT, class _Traits>
  _LIBCPP_HIDE_FROM_ABI friend basic_ostream<_CharT, _Traits>&
  operator<<(basic_ostream<_CharT, _Traits>& __os, const directory_entry& __d) {
    return __os << __d.path();
  }

private:
  friend class directory_iterator;
  friend class recursive_directory_iterator;
  friend class _LIBCPP_HIDDEN __dir_stream;

  enum _CacheType : unsigned char {
    _Empty,
    _IterSymlink,
    _IterNonSymlink,
    _RefreshSymlink,
    _RefreshSymlinkUnresolved,
    _RefreshNonSymlink
  };

  struct __cached_data {
    uintmax_t __size_;
    uintmax_t __nlink_;
    file_time_type __write_time_;
    perms __sym_perms_;
    perms __non_sym_perms_;
    file_type __type_;
    _CacheType __cache_type_;

    _LIBCPP_HIDE_FROM_ABI __cached_data() noexcept { __reset(); }

    _LIBCPP_HIDE_FROM_ABI void __reset() {
      __cache_type_ = _Empty;
      __type_       = file_type::none;
      __sym_perms_ = __non_sym_perms_ = perms::unknown;
      __size_ = __nlink_ = uintmax_t(-1);
      __write_time_      = file_time_type::min();
    }
  };

  _LIBCPP_HIDE_FROM_ABI static __cached_data __create_iter_result(file_type __ft) {
    __cached_data __data;
    __data.__type_       = __ft;
    __data.__cache_type_ = [&]() {
      switch (__ft) {
      case file_type::none:
        return _Empty;
      case file_type::symlink:
        return _IterSymlink;
      default:
        return _IterNonSymlink;
      }
    }();
    return __data;
  }

  _LIBCPP_HIDE_FROM_ABI void __assign_iter_entry(_Path&& __p, __cached_data __dt) {
    __p_    = std::move(__p);
    __data_ = __dt;
  }

  _LIBCPP_EXPORTED_FROM_ABI error_code __do_refresh() noexcept;

  _LIBCPP_HIDE_FROM_ABI static bool __is_dne_error(error_code const& __ec) {
    if (!__ec)
      return true;
    switch (static_cast<errc>(__ec.value())) {
    case errc::no_such_file_or_directory:
    case errc::not_a_directory:
      return true;
    default:
      return false;
    }
  }

  _LIBCPP_HIDE_FROM_ABI void
  __handle_error(const char* __msg, error_code* __dest_ec, error_code const& __ec, bool __allow_dne = false) const {
    if (__dest_ec) {
      *__dest_ec = __ec;
      return;
    }
    if (__ec && (!__allow_dne || !__is_dne_error(__ec)))
      __throw_filesystem_error(__msg, __p_, __ec);
  }

  _LIBCPP_HIDE_FROM_ABI void __refresh(error_code* __ec = nullptr) {
    __handle_error("in directory_entry::refresh",
                   __ec,
                   __do_refresh(),
                   /*allow_dne*/ true);
  }

  _LIBCPP_HIDE_FROM_ABI file_type __get_sym_ft(error_code* __ec = nullptr) const {
    switch (__data_.__cache_type_) {
    case _Empty:
      return __symlink_status(__p_, __ec).type();
    case _IterSymlink:
    case _RefreshSymlink:
    case _RefreshSymlinkUnresolved:
      if (__ec)
        __ec->clear();
      return file_type::symlink;
    case _IterNonSymlink:
    case _RefreshNonSymlink:
      file_status __st(__data_.__type_);
      if (__ec && !filesystem::exists(__st))
        *__ec = make_error_code(errc::no_such_file_or_directory);
      else if (__ec)
        __ec->clear();
      return __data_.__type_;
    }
    __libcpp_unreachable();
  }

  _LIBCPP_HIDE_FROM_ABI file_type __get_ft(error_code* __ec = nullptr) const {
    switch (__data_.__cache_type_) {
    case _Empty:
    case _IterSymlink:
    case _RefreshSymlinkUnresolved:
      return __status(__p_, __ec).type();
    case _IterNonSymlink:
    case _RefreshNonSymlink:
    case _RefreshSymlink: {
      file_status __st(__data_.__type_);
      if (__ec && !filesystem::exists(__st))
        *__ec = make_error_code(errc::no_such_file_or_directory);
      else if (__ec)
        __ec->clear();
      return __data_.__type_;
    }
    }
    __libcpp_unreachable();
  }

  _LIBCPP_HIDE_FROM_ABI file_status __get_status(error_code* __ec = nullptr) const {
    switch (__data_.__cache_type_) {
    case _Empty:
    case _IterNonSymlink:
    case _IterSymlink:
    case _RefreshSymlinkUnresolved:
      return __status(__p_, __ec);
    case _RefreshNonSymlink:
    case _RefreshSymlink:
      return file_status(__get_ft(__ec), __data_.__non_sym_perms_);
    }
    __libcpp_unreachable();
  }

  _LIBCPP_HIDE_FROM_ABI file_status __get_symlink_status(error_code* __ec = nullptr) const {
    switch (__data_.__cache_type_) {
    case _Empty:
    case _IterNonSymlink:
    case _IterSymlink:
      return __symlink_status(__p_, __ec);
    case _RefreshNonSymlink:
      return file_status(__get_sym_ft(__ec), __data_.__non_sym_perms_);
    case _RefreshSymlink:
    case _RefreshSymlinkUnresolved:
      return file_status(__get_sym_ft(__ec), __data_.__sym_perms_);
    }
    __libcpp_unreachable();
  }

  _LIBCPP_HIDE_FROM_ABI uintmax_t __get_size(error_code* __ec = nullptr) const {
    switch (__data_.__cache_type_) {
    case _Empty:
    case _IterNonSymlink:
    case _IterSymlink:
    case _RefreshSymlinkUnresolved:
      return filesystem::__file_size(__p_, __ec);
    case _RefreshSymlink:
    case _RefreshNonSymlink: {
      error_code __m_ec;
      file_status __st(__get_ft(&__m_ec));
      __handle_error("in directory_entry::file_size", __ec, __m_ec);
      if (filesystem::exists(__st) && !filesystem::is_regular_file(__st)) {
        errc __err_kind = filesystem::is_directory(__st) ? errc::is_a_directory : errc::not_supported;
        __handle_error("in directory_entry::file_size", __ec, make_error_code(__err_kind));
      }
      return __data_.__size_;
    }
    }
    __libcpp_unreachable();
  }

  _LIBCPP_HIDE_FROM_ABI uintmax_t __get_nlink(error_code* __ec = nullptr) const {
    switch (__data_.__cache_type_) {
    case _Empty:
    case _IterNonSymlink:
    case _IterSymlink:
    case _RefreshSymlinkUnresolved:
      return filesystem::__hard_link_count(__p_, __ec);
    case _RefreshSymlink:
    case _RefreshNonSymlink: {
      error_code __m_ec;
      (void)__get_ft(&__m_ec);
      __handle_error("in directory_entry::hard_link_count", __ec, __m_ec);
      return __data_.__nlink_;
    }
    }
    __libcpp_unreachable();
  }

  _LIBCPP_HIDE_FROM_ABI file_time_type __get_write_time(error_code* __ec = nullptr) const {
    switch (__data_.__cache_type_) {
    case _Empty:
    case _IterNonSymlink:
    case _IterSymlink:
    case _RefreshSymlinkUnresolved:
      return filesystem::__last_write_time(__p_, __ec);
    case _RefreshSymlink:
    case _RefreshNonSymlink: {
      error_code __m_ec;
      file_status __st(__get_ft(&__m_ec));
      __handle_error("in directory_entry::last_write_time", __ec, __m_ec);
      if (filesystem::exists(__st) && __data_.__write_time_ == file_time_type::min())
        __handle_error("in directory_entry::last_write_time", __ec, make_error_code(errc::value_too_large));
      return __data_.__write_time_;
    }
    }
    __libcpp_unreachable();
  }

private:
  _Path __p_;
  __cached_data __data_;
};

class __dir_element_proxy {
public:
  inline _LIBCPP_HIDE_FROM_ABI directory_entry operator*() { return std::move(__elem_); }

private:
  friend class directory_iterator;
  friend class recursive_directory_iterator;
  _LIBCPP_HIDE_FROM_ABI explicit __dir_element_proxy(directory_entry const& __e) : __elem_(__e) {}
  _LIBCPP_HIDE_FROM_ABI __dir_element_proxy(__dir_element_proxy&& __o) : __elem_(std::move(__o.__elem_)) {}
  directory_entry __elem_;
};

_LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY_POP

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // _LIBCPP_STD_VER >= 17 && !defined(_LIBCPP_HAS_NO_FILESYSTEM)

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FILESYSTEM_DIRECTORY_ENTRY_H
