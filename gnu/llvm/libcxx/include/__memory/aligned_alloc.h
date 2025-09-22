//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_ALIGNED_ALLOC_H
#define _LIBCPP___MEMORY_ALIGNED_ALLOC_H

#include <__config>
#include <cstddef>
#include <cstdlib>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_HAS_NO_LIBRARY_ALIGNED_ALLOCATION

// Low-level helpers to call the aligned allocation and deallocation functions
// on the target platform. This is used to implement libc++'s own memory
// allocation routines -- if you need to allocate memory inside the library,
// chances are that you want to use `__libcpp_allocate` instead.
//
// Returns the allocated memory, or `nullptr` on failure.
inline _LIBCPP_HIDE_FROM_ABI void* __libcpp_aligned_alloc(std::size_t __alignment, std::size_t __size) {
#  if defined(_LIBCPP_MSVCRT_LIKE)
  return ::_aligned_malloc(__size, __alignment);
#  elif _LIBCPP_STD_VER >= 17 && !defined(_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC)
  // aligned_alloc() requires that __size is a multiple of __alignment,
  // but for C++ [new.delete.general], only states "if the value of an
  // alignment argument passed to any of these functions is not a valid
  // alignment value, the behavior is undefined".
  // To handle calls such as ::operator new(1, std::align_val_t(128)), we
  // round __size up to the next multiple of __alignment.
  size_t __rounded_size = (__size + __alignment - 1) & ~(__alignment - 1);
  // Rounding up could have wrapped around to zero, so we have to add another
  // max() ternary to the actual call site to avoid succeeded in that case.
  return ::aligned_alloc(__alignment, __size > __rounded_size ? __size : __rounded_size);
#  else
  void* __result = nullptr;
  (void)::posix_memalign(&__result, __alignment, __size);
  // If posix_memalign fails, __result is unmodified so we still return `nullptr`.
  return __result;
#  endif
}

inline _LIBCPP_HIDE_FROM_ABI void __libcpp_aligned_free(void* __ptr) {
#  if defined(_LIBCPP_MSVCRT_LIKE)
  ::_aligned_free(__ptr);
#  else
  ::free(__ptr);
#  endif
}

#endif // !_LIBCPP_HAS_NO_LIBRARY_ALIGNED_ALLOCATION

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_ALIGNED_ALLOC_H
