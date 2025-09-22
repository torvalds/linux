//===-- sanitizer_common_interface.inc ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Sanitizer Common interface list.
//===----------------------------------------------------------------------===//
INTERFACE_FUNCTION(__sanitizer_acquire_crash_state)
INTERFACE_FUNCTION(__sanitizer_annotate_contiguous_container)
INTERFACE_FUNCTION(__sanitizer_annotate_double_ended_contiguous_container)
INTERFACE_FUNCTION(__sanitizer_contiguous_container_find_bad_address)
INTERFACE_FUNCTION(
    __sanitizer_double_ended_contiguous_container_find_bad_address)
INTERFACE_FUNCTION(__sanitizer_set_death_callback)
INTERFACE_FUNCTION(__sanitizer_set_report_path)
INTERFACE_FUNCTION(__sanitizer_set_report_fd)
INTERFACE_FUNCTION(__sanitizer_get_report_path)
INTERFACE_FUNCTION(__sanitizer_verify_contiguous_container)
INTERFACE_FUNCTION(__sanitizer_verify_double_ended_contiguous_container)
INTERFACE_WEAK_FUNCTION(__sanitizer_on_print)
INTERFACE_WEAK_FUNCTION(__sanitizer_report_error_summary)
INTERFACE_WEAK_FUNCTION(__sanitizer_sandbox_on_notify)
// Sanitizer weak hooks
INTERFACE_WEAK_FUNCTION(__sanitizer_weak_hook_memcmp)
INTERFACE_WEAK_FUNCTION(__sanitizer_weak_hook_strcmp)
INTERFACE_WEAK_FUNCTION(__sanitizer_weak_hook_strncmp)
INTERFACE_WEAK_FUNCTION(__sanitizer_weak_hook_strstr)
// Stacktrace interface.
INTERFACE_FUNCTION(__sanitizer_get_module_and_offset_for_pc)
INTERFACE_FUNCTION(__sanitizer_symbolize_global)
INTERFACE_FUNCTION(__sanitizer_symbolize_pc)
// Allocator interface.
INTERFACE_FUNCTION(__sanitizer_get_allocated_begin)
INTERFACE_FUNCTION(__sanitizer_get_allocated_size)
INTERFACE_FUNCTION(__sanitizer_get_allocated_size_fast)
INTERFACE_FUNCTION(__sanitizer_get_current_allocated_bytes)
INTERFACE_FUNCTION(__sanitizer_get_estimated_allocated_size)
INTERFACE_FUNCTION(__sanitizer_get_free_bytes)
INTERFACE_FUNCTION(__sanitizer_get_heap_size)
INTERFACE_FUNCTION(__sanitizer_get_ownership)
INTERFACE_FUNCTION(__sanitizer_get_unmapped_bytes)
INTERFACE_FUNCTION(__sanitizer_install_malloc_and_free_hooks)
INTERFACE_FUNCTION(__sanitizer_purge_allocator)
INTERFACE_FUNCTION(__sanitizer_print_memory_profile)
INTERFACE_WEAK_FUNCTION(__sanitizer_free_hook)
INTERFACE_WEAK_FUNCTION(__sanitizer_malloc_hook)
INTERFACE_WEAK_FUNCTION(__sanitizer_ignore_free_hook)
// Memintrinsic functions.
INTERFACE_FUNCTION(__sanitizer_internal_memcpy)
INTERFACE_FUNCTION(__sanitizer_internal_memmove)
INTERFACE_FUNCTION(__sanitizer_internal_memset)
