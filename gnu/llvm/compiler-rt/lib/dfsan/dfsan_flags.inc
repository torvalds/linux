//===-- dfsan_flags.inc -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DFSan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef DFSAN_FLAG
# error "Define DFSAN_FLAG prior to including this file!"
#endif

// DFSAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

DFSAN_FLAG(bool, warn_unimplemented, false,
           "Whether to warn on unimplemented functions.")
DFSAN_FLAG(bool, warn_nonzero_labels, false,
           "Whether to warn on unimplemented functions.")
DFSAN_FLAG(
    bool, strict_data_dependencies, true,
    "Whether to propagate labels only when there is an obvious data dependency"
    "(e.g., when comparing strings, ignore the fact that the output of the"
    "comparison might be data-dependent on the content of the strings). This"
    "applies only to the custom functions defined in 'custom.c'.")
DFSAN_FLAG(
     int, origin_history_size, Origin::kMaxDepth,
    "The limit of origin chain length. Non-positive values mean unlimited.")
DFSAN_FLAG(
     int, origin_history_per_stack_limit, 20000,
    "The limit of origin node's references count. "
    "Non-positive values mean unlimited.")
DFSAN_FLAG(int, store_context_size, 20,
           "The depth limit of origin tracking stack traces.")
DFSAN_FLAG(bool, check_origin_invariant, false,
           "Whether to check if the origin invariant holds.")
DFSAN_FLAG(bool, zero_in_malloc, true,
           "Whether to zero shadow space of new allocated memory.")
DFSAN_FLAG(bool, zero_in_free, true,
           "Whether to zero shadow space of deallocated memory.")
