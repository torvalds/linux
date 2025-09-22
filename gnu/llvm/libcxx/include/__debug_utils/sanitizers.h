//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___LIBCXX_DEBUG_UTILS_SANITIZERS_H
#define _LIBCPP___LIBCXX_DEBUG_UTILS_SANITIZERS_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_constant_evaluated.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#ifndef _LIBCPP_HAS_NO_ASAN

extern "C" {
_LIBCPP_EXPORTED_FROM_ABI void
__sanitizer_annotate_contiguous_container(const void*, const void*, const void*, const void*);
_LIBCPP_EXPORTED_FROM_ABI void __sanitizer_annotate_double_ended_contiguous_container(
    const void*, const void*, const void*, const void*, const void*, const void*);
_LIBCPP_EXPORTED_FROM_ABI int
__sanitizer_verify_double_ended_contiguous_container(const void*, const void*, const void*, const void*);
}

#endif // _LIBCPP_HAS_NO_ASAN

_LIBCPP_BEGIN_NAMESPACE_STD

// ASan choices
#ifndef _LIBCPP_HAS_NO_ASAN
#  define _LIBCPP_HAS_ASAN_CONTAINER_ANNOTATIONS_FOR_ALL_ALLOCATORS 1
#endif

#ifdef _LIBCPP_HAS_ASAN_CONTAINER_ANNOTATIONS_FOR_ALL_ALLOCATORS
// __asan_annotate_container_with_allocator determines whether containers with custom allocators are annotated. This is
// a public customization point to disable annotations if the custom allocator assumes that the memory isn't poisoned.
// See the https://libcxx.llvm.org/UsingLibcxx.html#turning-off-asan-annotation-in-containers for more information.
template <class _Alloc>
struct __asan_annotate_container_with_allocator : true_type {};
#endif

// Annotate a double-ended contiguous range.
// - [__first_storage, __last_storage) is the allocated memory region,
// - [__first_old_contained, __last_old_contained) is the previously allowed (unpoisoned) range, and
// - [__first_new_contained, __last_new_contained) is the new allowed (unpoisoned) range.
template <class _Allocator>
_LIBCPP_HIDE_FROM_ABI void __annotate_double_ended_contiguous_container(
    const void* __first_storage,
    const void* __last_storage,
    const void* __first_old_contained,
    const void* __last_old_contained,
    const void* __first_new_contained,
    const void* __last_new_contained) {
#ifdef _LIBCPP_HAS_NO_ASAN
  (void)__first_storage;
  (void)__last_storage;
  (void)__first_old_contained;
  (void)__last_old_contained;
  (void)__first_new_contained;
  (void)__last_new_contained;
#else
  if (__asan_annotate_container_with_allocator<_Allocator>::value && __first_storage != nullptr)
    __sanitizer_annotate_double_ended_contiguous_container(
        __first_storage,
        __last_storage,
        __first_old_contained,
        __last_old_contained,
        __first_new_contained,
        __last_new_contained);
#endif
}

// Annotate a contiguous range.
// [__first_storage, __last_storage) is the allocated memory region,
// __old_last_contained is the previously last allowed (unpoisoned) element, and
// __new_last_contained is the new last allowed (unpoisoned) element.
template <class _Allocator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 void __annotate_contiguous_container(
    const void* __first_storage,
    const void* __last_storage,
    const void* __old_last_contained,
    const void* __new_last_contained) {
#ifdef _LIBCPP_HAS_NO_ASAN
  (void)__first_storage;
  (void)__last_storage;
  (void)__old_last_contained;
  (void)__new_last_contained;
#else
  if (!__libcpp_is_constant_evaluated() && __asan_annotate_container_with_allocator<_Allocator>::value &&
      __first_storage != nullptr)
    __sanitizer_annotate_contiguous_container(
        __first_storage, __last_storage, __old_last_contained, __new_last_contained);
#endif
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___LIBCXX_DEBUG_UTILS_SANITIZERS_H
