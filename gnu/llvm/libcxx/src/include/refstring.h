//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_REFSTRING_H
#define _LIBCPP_REFSTRING_H

#include "atomic_support.h"
#include <__config>
#include <cstddef>
#include <cstring>
#include <stdexcept>

// MacOS and iOS used to ship with libstdc++, and still support old applications
// linking against libstdc++. The libc++ and libstdc++ exceptions are supposed
// to be ABI compatible, such that they can be thrown from one library and caught
// in the other.
//
// For that reason, we must look for libstdc++ in the same process and if found,
// check the string stored in the exception object to see if it is the GCC empty
// string singleton before manipulating the reference count. This is done so that
// if an exception is created with a zero-length string in libstdc++, libc++abi
// won't try to delete the memory.
#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) || defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)
#  define _LIBCPP_CHECK_FOR_GCC_EMPTY_STRING_STORAGE
#  include <dlfcn.h>
#  include <mach-o/dyld.h>
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __refstring_imp {
namespace {
typedef int count_t;

struct _Rep_base {
  std::size_t len;
  std::size_t cap;
  count_t count;
};

inline _Rep_base* rep_from_data(const char* data_) noexcept {
  char* data = const_cast<char*>(data_);
  return reinterpret_cast<_Rep_base*>(data - sizeof(_Rep_base));
}

inline char* data_from_rep(_Rep_base* rep) noexcept {
  char* data = reinterpret_cast<char*>(rep);
  return data + sizeof(*rep);
}

#if defined(_LIBCPP_CHECK_FOR_GCC_EMPTY_STRING_STORAGE)
inline const char* compute_gcc_empty_string_storage() noexcept {
  void* handle = dlopen("/usr/lib/libstdc++.6.dylib", RTLD_NOLOAD);
  if (handle == nullptr)
    return nullptr;
  void* sym = dlsym(handle, "_ZNSs4_Rep20_S_empty_rep_storageE");
  if (sym == nullptr)
    return nullptr;
  return data_from_rep(reinterpret_cast<_Rep_base*>(sym));
}

inline const char* get_gcc_empty_string_storage() noexcept {
  static const char* p = compute_gcc_empty_string_storage();
  return p;
}
#endif

} // namespace
} // namespace __refstring_imp

using namespace __refstring_imp;

inline __libcpp_refstring::__libcpp_refstring(const char* msg) {
  std::size_t len = strlen(msg);
  _Rep_base* rep  = static_cast<_Rep_base*>(::operator new(sizeof(*rep) + len + 1));
  rep->len        = len;
  rep->cap        = len;
  rep->count      = 0;
  char* data      = data_from_rep(rep);
  std::memcpy(data, msg, len + 1);
  __imp_ = data;
}

inline __libcpp_refstring::__libcpp_refstring(const __libcpp_refstring& s) noexcept : __imp_(s.__imp_) {
  if (__uses_refcount())
    __libcpp_atomic_add(&rep_from_data(__imp_)->count, 1);
}

inline __libcpp_refstring& __libcpp_refstring::operator=(__libcpp_refstring const& s) noexcept {
  bool adjust_old_count     = __uses_refcount();
  struct _Rep_base* old_rep = rep_from_data(__imp_);
  __imp_                    = s.__imp_;
  if (__uses_refcount())
    __libcpp_atomic_add(&rep_from_data(__imp_)->count, 1);
  if (adjust_old_count) {
    if (__libcpp_atomic_add(&old_rep->count, count_t(-1)) < 0) {
      ::operator delete(old_rep);
    }
  }
  return *this;
}

inline __libcpp_refstring::~__libcpp_refstring() {
  if (__uses_refcount()) {
    _Rep_base* rep = rep_from_data(__imp_);
    if (__libcpp_atomic_add(&rep->count, count_t(-1)) < 0) {
      ::operator delete(rep);
    }
  }
}

inline bool __libcpp_refstring::__uses_refcount() const {
#if defined(_LIBCPP_CHECK_FOR_GCC_EMPTY_STRING_STORAGE)
  return __imp_ != get_gcc_empty_string_storage();
#else
  return true;
#endif
}

_LIBCPP_END_NAMESPACE_STD

#endif //_LIBCPP_REFSTRING_H
