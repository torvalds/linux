//===-- sanitizer_interface_internal.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between run-time libraries of sanitizers.
//
// This header declares the sanitizer runtime interface functions.
// The runtime library has to define these functions so the instrumented program
// could call them.
//
// See also include/sanitizer/common_interface_defs.h
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_INTERFACE_INTERNAL_H
#define SANITIZER_INTERFACE_INTERNAL_H

#include "sanitizer_internal_defs.h"

extern "C" {
// Tell the tools to write their reports to "path.<pid>" instead of stderr.
// The special values are "stdout" and "stderr".
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_set_report_path(const char *path);
// Tell the tools to write their reports to the provided file descriptor
// (casted to void *).
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_set_report_fd(void *fd);
// Get the current full report file path, if a path was specified by
// an earlier call to __sanitizer_set_report_path. Returns null otherwise.
SANITIZER_INTERFACE_ATTRIBUTE
const char *__sanitizer_get_report_path();

typedef struct {
  int coverage_sandboxed;
  __sanitizer::sptr coverage_fd;
  unsigned int coverage_max_block_size;
} __sanitizer_sandbox_arguments;

// Notify the tools that the sandbox is going to be turned on.
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_sandbox_on_notify(__sanitizer_sandbox_arguments *args);

// This function is called by the tool when it has just finished reporting
// an error. 'error_summary' is a one-line string that summarizes
// the error message. This function can be overridden by the client.
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_report_error_summary(const char *error_summary);

SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_cov_dump();
SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_dump_coverage(
    const __sanitizer::uptr *pcs, const __sanitizer::uptr len);
SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_dump_trace_pc_guard_coverage();

SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_cov(__sanitizer::u32 *guard);

// Returns 1 on the first call, then returns 0 thereafter.  Called by the tool
// to ensure only one report is printed when multiple errors occur
// simultaneously.
SANITIZER_INTERFACE_ATTRIBUTE int __sanitizer_acquire_crash_state();

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_annotate_contiguous_container(const void *beg, const void *end,
                                               const void *old_mid,
                                               const void *new_mid);
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_annotate_double_ended_contiguous_container(
    const void *storage_beg, const void *storage_end,
    const void *old_container_beg, const void *old_container_end,
    const void *new_container_beg, const void *new_container_end);
SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_verify_contiguous_container(const void *beg, const void *mid,
                                            const void *end);
SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_verify_double_ended_contiguous_container(
    const void *storage_beg, const void *container_beg,
    const void *container_end, const void *storage_end);
SANITIZER_INTERFACE_ATTRIBUTE
const void *__sanitizer_contiguous_container_find_bad_address(const void *beg,
                                                              const void *mid,
                                                              const void *end);
SANITIZER_INTERFACE_ATTRIBUTE
const void *__sanitizer_double_ended_contiguous_container_find_bad_address(
    const void *storage_beg, const void *container_beg,
    const void *container_end, const void *storage_end);

SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_get_module_and_offset_for_pc(void *pc, char *module_path,
                                             __sanitizer::uptr module_path_len,
                                             void **pc_offset);
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_cmp();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_cmp1();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_cmp2();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_cmp4();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_cmp8();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_const_cmp1();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_const_cmp2();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_const_cmp4();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_const_cmp8();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_switch();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_div4();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_div8();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_gep();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_pc_indir();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_load1();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_load2();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_load4();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_load8();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_load16();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_store1();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_store2();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_store4();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_store8();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_store16();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_pc_guard(__sanitizer::u32 *);
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_trace_pc_guard_init(__sanitizer::u32 *, __sanitizer::u32 *);
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_8bit_counters_init(char *, char *);
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_bool_flag_init();
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void
__sanitizer_cov_pcs_init(const __sanitizer::uptr *, const __sanitizer::uptr *);
}  // extern "C"

#endif  // SANITIZER_INTERFACE_INTERNAL_H
