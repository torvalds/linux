//===- FuzzerInterface.h - Interface header for the Fuzzer ------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Define the interface between libFuzzer and the library being tested.
//===----------------------------------------------------------------------===//

// NOTE: the libFuzzer interface is thin and in the majority of cases
// you should not include this file into your target. In 95% of cases
// all you need is to define the following function in your file:
// extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);

// WARNING: keep the interface in C.

#ifndef LLVM_FUZZER_INTERFACE_H
#define LLVM_FUZZER_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Define FUZZER_INTERFACE_VISIBILITY to set default visibility in a way that
// doesn't break MSVC.
#if defined(_WIN32)
#define FUZZER_INTERFACE_VISIBILITY __declspec(dllexport)
#else
#define FUZZER_INTERFACE_VISIBILITY __attribute__((visibility("default")))
#endif

// Mandatory user-provided target function.
// Executes the code under test with [Data, Data+Size) as the input.
// libFuzzer will invoke this function *many* times with different inputs.
// Must return 0.
FUZZER_INTERFACE_VISIBILITY int
LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);

// Optional user-provided initialization function.
// If provided, this function will be called by libFuzzer once at startup.
// It may read and modify argc/argv.
// Must return 0.
FUZZER_INTERFACE_VISIBILITY int LLVMFuzzerInitialize(int *argc, char ***argv);

// Optional user-provided custom mutator.
// Mutates raw data in [Data, Data+Size) inplace.
// Returns the new size, which is not greater than MaxSize.
// Given the same Seed produces the same mutation.
FUZZER_INTERFACE_VISIBILITY size_t
LLVMFuzzerCustomMutator(uint8_t *Data, size_t Size, size_t MaxSize,
                        unsigned int Seed);

// Optional user-provided custom cross-over function.
// Combines pieces of Data1 & Data2 together into Out.
// Returns the new size, which is not greater than MaxOutSize.
// Should produce the same mutation given the same Seed.
FUZZER_INTERFACE_VISIBILITY size_t
LLVMFuzzerCustomCrossOver(const uint8_t *Data1, size_t Size1,
                          const uint8_t *Data2, size_t Size2, uint8_t *Out,
                          size_t MaxOutSize, unsigned int Seed);

// Experimental, may go away in future.
// libFuzzer-provided function to be used inside LLVMFuzzerCustomMutator.
// Mutates raw data in [Data, Data+Size) inplace.
// Returns the new size, which is not greater than MaxSize.
FUZZER_INTERFACE_VISIBILITY size_t
LLVMFuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize);

#undef FUZZER_INTERFACE_VISIBILITY

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // LLVM_FUZZER_INTERFACE_H
