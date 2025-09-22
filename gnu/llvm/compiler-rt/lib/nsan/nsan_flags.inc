//===-- nsan_flags.inc ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// NSan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef NSAN_FLAG
#error "Define NSAN_FLAG prior to including this file!"
#endif

// NSAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

NSAN_FLAG(bool, halt_on_error, true, "If true, halt after the first error.")
NSAN_FLAG(bool, resume_after_warning, true,
          "If true, we resume resume the computation from the original "
          "application floating-point value after a warning. If false, "
          "computations continue with the shadow value.")
NSAN_FLAG(const char *, suppressions, "", "Suppressions file name.")
NSAN_FLAG(bool, resume_after_suppression, true,
          "If true, a suppression will also resume the computation from the FT"
          " domain. If false, output is suppressed but the shadow value is"
          " retained.")
// FIXME: should this be specified in units of epsilon instead?
NSAN_FLAG(int, log2_max_relative_error, 19,
          "Log2 maximum admissible relative error, e.g. 19 means max relative "
          "error of 1/2^19 ~= 0.000002.")
NSAN_FLAG(int, log2_absolute_error_threshold, 32,
          "Log2 maximum admissible absolute error. Any numbers closer than "
          "1/2^n are considered to be the same.")
NSAN_FLAG(bool, disable_warnings, false,
          "If true, disable warning printing. This is useful to only compute "
          "stats.")
NSAN_FLAG(bool, enable_check_stats, false,
          "If true, compute check stats, i.e. for each line, the number of "
          "times a check was performed on this line.")
NSAN_FLAG(bool, enable_warning_stats, false,
          "If true, compute warning stats, i.e. for each line, the number of "
          "times a warning was emitted for this line.")
NSAN_FLAG(bool, enable_loadtracking_stats, false,
          "If true, compute load tracking stats, i.e. for each load from "
          "memory, the number of times nsan resumed from the original value "
          "due to invalid or unknown types.")
NSAN_FLAG(bool, print_stats_on_exit, false, "If true, print stats on exit.")
