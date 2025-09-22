//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "__cxxabi_config.h"
#include "abort_message.h"
#include "include/overridable_function.h" // from libc++
#include <__memory/aligned_alloc.h>
#include <cstddef>
#include <cstdlib>
#include <new>

// Perform a few sanity checks on libc++ and libc++abi macros to ensure that
// the code below can be an exact copy of the code in libcxx/src/new.cpp.
#if !defined(_THROW_BAD_ALLOC)
#  error The _THROW_BAD_ALLOC macro should be already defined by libc++
#endif

#ifndef _LIBCPP_WEAK
#  error The _LIBCPP_WEAK macro should be already defined by libc++
#endif

#if defined(_LIBCXXABI_NO_EXCEPTIONS) != defined(_LIBCPP_HAS_NO_EXCEPTIONS)
#  error libc++ and libc++abi seem to disagree on whether exceptions are enabled
#endif

inline void __throw_bad_alloc_shim() {
#ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  throw std::bad_alloc();
#else
  abort_message("bad_alloc was thrown in -fno-exceptions mode");
#endif
}

#define _LIBCPP_ASSERT_SHIM(expr, str)                                                                                 \
  do {                                                                                                                 \
    if (!expr)                                                                                                         \
      abort_message(str);                                                                                              \
  } while (false)

// ------------------ BEGIN COPY ------------------
// Implement all new and delete operators as weak definitions
// in this shared library, so that they can be overridden by programs
// that define non-weak copies of the functions.

static void* operator_new_impl(std::size_t size) {
  if (size == 0)
    size = 1;
  void* p;
  while ((p = std::malloc(size)) == nullptr) {
    // If malloc fails and there is a new_handler,
    // call it to try free up memory.
    std::new_handler nh = std::get_new_handler();
    if (nh)
      nh();
    else
      break;
  }
  return p;
}

_LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE _LIBCPP_WEAK void* operator new(std::size_t size) _THROW_BAD_ALLOC {
  void* p = operator_new_impl(size);
  if (p == nullptr)
    __throw_bad_alloc_shim();
  return p;
}

_LIBCPP_WEAK void* operator new(size_t size, const std::nothrow_t&) noexcept {
#ifdef _LIBCPP_HAS_NO_EXCEPTIONS
#  if _LIBCPP_CAN_DETECT_OVERRIDDEN_FUNCTION
  _LIBCPP_ASSERT_SHIM(
      !std::__is_function_overridden(static_cast<void* (*)(std::size_t)>(&operator new)),
      "libc++ was configured with exceptions disabled and `operator new(size_t)` has been overridden, "
      "but `operator new(size_t, nothrow_t)` has not been overridden. This is problematic because "
      "`operator new(size_t, nothrow_t)` must call `operator new(size_t)`, which will terminate in case "
      "it fails to allocate, making it impossible for `operator new(size_t, nothrow_t)` to fulfill its "
      "contract (since it should return nullptr upon failure). Please make sure you override "
      "`operator new(size_t, nothrow_t)` as well.");
#  endif

  return operator_new_impl(size);
#else
  void* p = nullptr;
  try {
    p = ::operator new(size);
  } catch (...) {
  }
  return p;
#endif
}

_LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE _LIBCPP_WEAK void* operator new[](size_t size) _THROW_BAD_ALLOC {
  return ::operator new(size);
}

_LIBCPP_WEAK void* operator new[](size_t size, const std::nothrow_t&) noexcept {
#ifdef _LIBCPP_HAS_NO_EXCEPTIONS
#  if _LIBCPP_CAN_DETECT_OVERRIDDEN_FUNCTION
  _LIBCPP_ASSERT_SHIM(
      !std::__is_function_overridden(static_cast<void* (*)(std::size_t)>(&operator new[])),
      "libc++ was configured with exceptions disabled and `operator new[](size_t)` has been overridden, "
      "but `operator new[](size_t, nothrow_t)` has not been overridden. This is problematic because "
      "`operator new[](size_t, nothrow_t)` must call `operator new[](size_t)`, which will terminate in case "
      "it fails to allocate, making it impossible for `operator new[](size_t, nothrow_t)` to fulfill its "
      "contract (since it should return nullptr upon failure). Please make sure you override "
      "`operator new[](size_t, nothrow_t)` as well.");
#  endif

  return operator_new_impl(size);
#else
  void* p = nullptr;
  try {
    p = ::operator new[](size);
  } catch (...) {
  }
  return p;
#endif
}

_LIBCPP_WEAK void operator delete(void* ptr) noexcept { std::free(ptr); }

_LIBCPP_WEAK void operator delete(void* ptr, const std::nothrow_t&) noexcept { ::operator delete(ptr); }

_LIBCPP_WEAK void operator delete(void* ptr, size_t) noexcept { ::operator delete(ptr); }

_LIBCPP_WEAK void operator delete[](void* ptr) noexcept { ::operator delete(ptr); }

_LIBCPP_WEAK void operator delete[](void* ptr, const std::nothrow_t&) noexcept { ::operator delete[](ptr); }

_LIBCPP_WEAK void operator delete[](void* ptr, size_t) noexcept { ::operator delete[](ptr); }

#if !defined(_LIBCPP_HAS_NO_LIBRARY_ALIGNED_ALLOCATION)

static void* operator_new_aligned_impl(std::size_t size, std::align_val_t alignment) {
  if (size == 0)
    size = 1;
  if (static_cast<size_t>(alignment) < sizeof(void*))
    alignment = std::align_val_t(sizeof(void*));

  // Try allocating memory. If allocation fails and there is a new_handler,
  // call it to try free up memory, and try again until it succeeds, or until
  // the new_handler decides to terminate.
  void* p;
  while ((p = std::__libcpp_aligned_alloc(static_cast<std::size_t>(alignment), size)) == nullptr) {
    std::new_handler nh = std::get_new_handler();
    if (nh)
      nh();
    else
      break;
  }
  return p;
}

_LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE _LIBCPP_WEAK void*
operator new(std::size_t size, std::align_val_t alignment) _THROW_BAD_ALLOC {
  void* p = operator_new_aligned_impl(size, alignment);
  if (p == nullptr)
    __throw_bad_alloc_shim();
  return p;
}

_LIBCPP_WEAK void* operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
#  ifdef _LIBCPP_HAS_NO_EXCEPTIONS
#    if _LIBCPP_CAN_DETECT_OVERRIDDEN_FUNCTION
  _LIBCPP_ASSERT_SHIM(
      !std::__is_function_overridden(static_cast<void* (*)(std::size_t, std::align_val_t)>(&operator new)),
      "libc++ was configured with exceptions disabled and `operator new(size_t, align_val_t)` has been overridden, "
      "but `operator new(size_t, align_val_t, nothrow_t)` has not been overridden. This is problematic because "
      "`operator new(size_t, align_val_t, nothrow_t)` must call `operator new(size_t, align_val_t)`, which will "
      "terminate in case it fails to allocate, making it impossible for `operator new(size_t, align_val_t, nothrow_t)` "
      "to fulfill its contract (since it should return nullptr upon failure). Please make sure you override "
      "`operator new(size_t, align_val_t, nothrow_t)` as well.");
#    endif

  return operator_new_aligned_impl(size, alignment);
#  else
  void* p = nullptr;
  try {
    p = ::operator new(size, alignment);
  } catch (...) {
  }
  return p;
#  endif
}

_LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE _LIBCPP_WEAK void*
operator new[](size_t size, std::align_val_t alignment) _THROW_BAD_ALLOC {
  return ::operator new(size, alignment);
}

_LIBCPP_WEAK void* operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
#  ifdef _LIBCPP_HAS_NO_EXCEPTIONS
#    if _LIBCPP_CAN_DETECT_OVERRIDDEN_FUNCTION
  _LIBCPP_ASSERT_SHIM(
      !std::__is_function_overridden(static_cast<void* (*)(std::size_t, std::align_val_t)>(&operator new[])),
      "libc++ was configured with exceptions disabled and `operator new[](size_t, align_val_t)` has been overridden, "
      "but `operator new[](size_t, align_val_t, nothrow_t)` has not been overridden. This is problematic because "
      "`operator new[](size_t, align_val_t, nothrow_t)` must call `operator new[](size_t, align_val_t)`, which will "
      "terminate in case it fails to allocate, making it impossible for `operator new[](size_t, align_val_t, "
      "nothrow_t)` to fulfill its contract (since it should return nullptr upon failure). Please make sure you "
      "override "
      "`operator new[](size_t, align_val_t, nothrow_t)` as well.");
#    endif

  return operator_new_aligned_impl(size, alignment);
#  else
  void* p = nullptr;
  try {
    p = ::operator new[](size, alignment);
  } catch (...) {
  }
  return p;
#  endif
}

_LIBCPP_WEAK void operator delete(void* ptr, std::align_val_t) noexcept { std::__libcpp_aligned_free(ptr); }

_LIBCPP_WEAK void operator delete(void* ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  ::operator delete(ptr, alignment);
}

_LIBCPP_WEAK void operator delete(void* ptr, size_t, std::align_val_t alignment) noexcept {
  ::operator delete(ptr, alignment);
}

_LIBCPP_WEAK void operator delete[](void* ptr, std::align_val_t alignment) noexcept {
  ::operator delete(ptr, alignment);
}

_LIBCPP_WEAK void operator delete[](void* ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  ::operator delete[](ptr, alignment);
}

_LIBCPP_WEAK void operator delete[](void* ptr, size_t, std::align_val_t alignment) noexcept {
  ::operator delete[](ptr, alignment);
}

#endif // !_LIBCPP_HAS_NO_LIBRARY_ALIGNED_ALLOCATION
// ------------------ END COPY ------------------
