//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//
// These shims implement symbols that are present in the system libc++ on Apple platforms
// but are not implemented in upstream libc++. This allows testing libc++ under a system
// library configuration, which requires the just-built libc++ to be ABI compatible with
// the system library it is replacing.
//

#include <cstddef>
#include <new>

namespace std { // purposefully not versioned, like align_val_t
enum class __type_descriptor_t : unsigned long long;
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void* operator new(std::size_t __sz, std::__type_descriptor_t) {
  return ::operator new(__sz);
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void* operator new(std::size_t __sz, const std::nothrow_t& __nt,
                                                std::__type_descriptor_t) noexcept {
  return ::operator new(__sz, __nt);
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void* operator new[](std::size_t __sz, std::__type_descriptor_t) {
  return ::operator new[](__sz);
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void* operator new[](std::size_t __sz, const std::nothrow_t& __nt,
                                                  std::__type_descriptor_t) noexcept {
  return ::operator new(__sz, __nt);
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void operator delete(void* __p, std::__type_descriptor_t) noexcept {
  return ::operator delete(__p);
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void operator delete(void* __p, const std::nothrow_t& __nt,
                                                  std::__type_descriptor_t) noexcept {
  return ::operator delete(__p, __nt);
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void operator delete[](void* __p, std::__type_descriptor_t) noexcept {
  return ::operator delete[](__p);
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void operator delete[](void* __p, const std::nothrow_t& __nt,
                                                    std::__type_descriptor_t) noexcept {
  return ::operator delete[](__p, __nt);
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void operator delete(void* __p, std::size_t __sz, std::__type_descriptor_t) noexcept {
  return ::operator delete(__p, __sz);
}

_LIBCPP_OVERRIDABLE_FUNC_VIS void operator delete[](void* __p, std::size_t __sz, std::__type_descriptor_t) noexcept {
  return ::operator delete[](__p, __sz);
}
