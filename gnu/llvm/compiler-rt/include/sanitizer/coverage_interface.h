//===-- sanitizer/coverage_interface.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Public interface for sanitizer coverage.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_COVERAG_INTERFACE_H
#define SANITIZER_COVERAG_INTERFACE_H

#include <sanitizer/common_interface_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

// Record and dump coverage info.
void SANITIZER_CDECL __sanitizer_cov_dump(void);

// Clear collected coverage info.
void SANITIZER_CDECL __sanitizer_cov_reset(void);

// Dump collected coverage info. Sorts pcs by module into individual .sancov
// files.
void SANITIZER_CDECL __sanitizer_dump_coverage(const uintptr_t *pcs,
                                               uintptr_t len);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SANITIZER_COVERAG_INTERFACE_H
