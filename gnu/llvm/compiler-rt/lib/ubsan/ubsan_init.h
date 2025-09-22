//===-- ubsan_init.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Initialization function for UBSan runtime.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_INIT_H
#define UBSAN_INIT_H

namespace __ubsan {

// Get the full tool name for UBSan.
const char *GetSanititizerToolName();

// Initialize UBSan as a standalone tool. Typically should be called early
// during initialization.
void InitAsStandalone();

// Initialize UBSan as a standalone tool, if it hasn't been initialized before.
void InitAsStandaloneIfNecessary();

// Initializes UBSan as a plugin tool. This function should be called once
// from "parent tool" (e.g. ASan) initialization.
void InitAsPlugin();

}  // namespace __ubsan

#endif  // UBSAN_INIT_H
