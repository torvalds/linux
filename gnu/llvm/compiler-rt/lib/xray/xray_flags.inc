//===-- xray_flags.inc ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// XRay runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_FLAG
#error "Define XRAY_FLAG prior to including this file!"
#endif

XRAY_FLAG(bool, patch_premain, false,
          "Whether to patch instrumentation points before main.")
XRAY_FLAG(const char *, xray_logfile_base, "xray-log.",
          "Filename base for the xray logfile.")
XRAY_FLAG(const char *, xray_mode, "", "Mode to install by default.")
XRAY_FLAG(uptr, xray_page_size_override, 0,
          "Override the default page size for the system, in bytes. The size "
          "should be a power-of-two.")

// Basic (Naive) Mode logging options.
XRAY_FLAG(bool, xray_naive_log, false,
          "DEPRECATED: Use xray_mode=xray-basic instead.")
XRAY_FLAG(int, xray_naive_log_func_duration_threshold_us, 5,
          "DEPRECATED: use the environment variable XRAY_BASIC_OPTIONS and set "
          "func_duration_threshold_us instead.")
XRAY_FLAG(int, xray_naive_log_max_stack_depth, 64,
          "DEPRECATED: use the environment variable XRAY_BASIC_OPTIONS and set "
          "max_stack_depth instead.")
XRAY_FLAG(int, xray_naive_log_thread_buffer_size, 1024,
          "DEPRECATED: use the environment variable XRAY_BASIC_OPTIONS and set "
          "thread_buffer_size instead.")

// FDR (Flight Data Recorder) Mode logging options.
XRAY_FLAG(bool, xray_fdr_log, false,
          "DEPRECATED: Use xray_mode=xray-fdr instead.")
XRAY_FLAG(int, xray_fdr_log_func_duration_threshold_us, 5,
          "DEPRECATED: use the environment variable XRAY_FDR_OPTIONS and set "
          "func_duration_threshold_us instead.")
XRAY_FLAG(int, xray_fdr_log_grace_period_us, 0,
          "DEPRECATED: use the environment variable XRAY_FDR_OPTIONS and set "
          "grace_period_ms instead.")
XRAY_FLAG(int, xray_fdr_log_grace_period_ms, 100,
          "DEPRECATED: use the environment variable XRAY_FDR_OPTIONS and set "
          "grace_period_ms instead.")
