//===-- memprof_init_version.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// This header defines a versioned __memprof_init function to be called at the
// startup of the instrumented program.
//===----------------------------------------------------------------------===//
#ifndef MEMPROF_INIT_VERSION_H
#define MEMPROF_INIT_VERSION_H

#include "sanitizer_common/sanitizer_platform.h"

extern "C" {
// Every time the Memprof ABI changes we also change the version number in the
// __memprof_init function name.  Objects built with incompatible Memprof ABI
// versions will not link with run-time.
#define __memprof_version_mismatch_check __memprof_version_mismatch_check_v1
}

#endif // MEMPROF_INIT_VERSION_H
