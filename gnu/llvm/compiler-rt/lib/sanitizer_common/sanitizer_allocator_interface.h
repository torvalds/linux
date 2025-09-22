//===-- sanitizer_allocator_interface.h ------------------------- C++ -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Re-declaration of functions from public sanitizer allocator interface.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ALLOCATOR_INTERFACE_H
#define SANITIZER_ALLOCATOR_INTERFACE_H

#include "sanitizer_internal_defs.h"

using __sanitizer::uptr;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
uptr __sanitizer_get_estimated_allocated_size(uptr size);
SANITIZER_INTERFACE_ATTRIBUTE int __sanitizer_get_ownership(const void *p);
SANITIZER_INTERFACE_ATTRIBUTE const void *__sanitizer_get_allocated_begin(
    const void *p);
SANITIZER_INTERFACE_ATTRIBUTE uptr
__sanitizer_get_allocated_size(const void *p);
SANITIZER_INTERFACE_ATTRIBUTE uptr
__sanitizer_get_allocated_size_fast(const void *p);
SANITIZER_INTERFACE_ATTRIBUTE uptr __sanitizer_get_current_allocated_bytes();
SANITIZER_INTERFACE_ATTRIBUTE uptr __sanitizer_get_heap_size();
SANITIZER_INTERFACE_ATTRIBUTE uptr __sanitizer_get_free_bytes();
SANITIZER_INTERFACE_ATTRIBUTE uptr __sanitizer_get_unmapped_bytes();

SANITIZER_INTERFACE_ATTRIBUTE int __sanitizer_install_malloc_and_free_hooks(
    void (*malloc_hook)(const void *, uptr),
    void (*free_hook)(const void *));

SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
    void __sanitizer_malloc_hook(void *ptr, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
    void __sanitizer_free_hook(void *ptr);
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE int
__sanitizer_ignore_free_hook(void *ptr);

SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_purge_allocator();

SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_print_memory_profile(uptr top_percent, uptr max_number_of_contexts);
}  // extern "C"

#endif  // SANITIZER_ALLOCATOR_INTERFACE_H
