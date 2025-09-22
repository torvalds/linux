// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FILESYSTEM_FILESYSTEM_ERROR_H
#define _LIBCPP___FILESYSTEM_FILESYSTEM_ERROR_H

#include <__config>
#include <__filesystem/path.h>
#include <__memory/shared_ptr.h>
#include <__system_error/error_code.h>
#include <__system_error/system_error.h>
#include <__utility/forward.h>
#include <__verbose_abort>
#include <string>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

class _LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY _LIBCPP_EXPORTED_FROM_ABI filesystem_error : public system_error {
public:
  _LIBCPP_HIDE_FROM_ABI filesystem_error(const string& __what, error_code __ec)
      : system_error(__ec, __what), __storage_(make_shared<_Storage>(path(), path())) {
    __create_what(0);
  }

  _LIBCPP_HIDE_FROM_ABI filesystem_error(const string& __what, const path& __p1, error_code __ec)
      : system_error(__ec, __what), __storage_(make_shared<_Storage>(__p1, path())) {
    __create_what(1);
  }

  _LIBCPP_HIDE_FROM_ABI filesystem_error(const string& __what, const path& __p1, const path& __p2, error_code __ec)
      : system_error(__ec, __what), __storage_(make_shared<_Storage>(__p1, __p2)) {
    __create_what(2);
  }

  _LIBCPP_HIDE_FROM_ABI const path& path1() const noexcept { return __storage_->__p1_; }

  _LIBCPP_HIDE_FROM_ABI const path& path2() const noexcept { return __storage_->__p2_; }

  _LIBCPP_HIDE_FROM_ABI filesystem_error(const filesystem_error&) = default;
  ~filesystem_error() override; // key function

  _LIBCPP_HIDE_FROM_ABI_VIRTUAL
  const char* what() const noexcept override { return __storage_->__what_.c_str(); }

  void __create_what(int __num_paths);

private:
  struct _LIBCPP_HIDDEN _Storage {
    _LIBCPP_HIDE_FROM_ABI _Storage(const path& __p1, const path& __p2) : __p1_(__p1), __p2_(__p2) {}

    path __p1_;
    path __p2_;
    string __what_;
  };
  shared_ptr<_Storage> __storage_;
};

#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
template <class... _Args>
_LIBCPP_NORETURN inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY void
__throw_filesystem_error(_Args&&... __args) {
  throw filesystem_error(std::forward<_Args>(__args)...);
}
#  else
template <class... _Args>
_LIBCPP_NORETURN inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY void
__throw_filesystem_error(_Args&&...) {
  _LIBCPP_VERBOSE_ABORT("filesystem_error was thrown in -fno-exceptions mode");
}
#  endif

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___FILESYSTEM_FILESYSTEM_ERROR_H
