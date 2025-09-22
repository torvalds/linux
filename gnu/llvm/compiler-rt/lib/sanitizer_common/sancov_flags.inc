//===-- sancov_flags.inc ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Sanitizer Coverage runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef SANCOV_FLAG
#error "Defnine SANCOV_FLAG prior to including this file!"
#endif

SANCOV_FLAG(bool, symbolize, true,
            "If set, coverage information will be symbolized by sancov tool "
            "after dumping.")

SANCOV_FLAG(bool, help, false, "Print flags help.")
