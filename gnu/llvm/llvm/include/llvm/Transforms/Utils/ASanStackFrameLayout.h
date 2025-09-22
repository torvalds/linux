//===- ASanStackFrameLayout.h - ComputeASanStackFrameLayout -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines ComputeASanStackFrameLayout and auxiliary data structs.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_ASANSTACKFRAMELAYOUT_H
#define LLVM_TRANSFORMS_UTILS_ASANSTACKFRAMELAYOUT_H
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

class AllocaInst;

// These magic constants should be the same as in
// in asan_internal.h from ASan runtime in compiler-rt.
static const int kAsanStackLeftRedzoneMagic = 0xf1;
static const int kAsanStackMidRedzoneMagic = 0xf2;
static const int kAsanStackRightRedzoneMagic = 0xf3;
static const int kAsanStackUseAfterReturnMagic = 0xf5;
static const int kAsanStackUseAfterScopeMagic = 0xf8;

// Input/output data struct for ComputeASanStackFrameLayout.
struct ASanStackVariableDescription {
  const char *Name;    // Name of the variable that will be displayed by asan
                       // if a stack-related bug is reported.
  uint64_t Size;       // Size of the variable in bytes.
  size_t LifetimeSize; // Size in bytes to use for lifetime analysis check.
                       // Will be rounded up to Granularity.
  uint64_t Alignment;  // Alignment of the variable (power of 2).
  AllocaInst *AI;      // The actual AllocaInst.
  size_t Offset;       // Offset from the beginning of the frame;
                       // set by ComputeASanStackFrameLayout.
  unsigned Line;       // Line number.
};

// Output data struct for ComputeASanStackFrameLayout.
struct ASanStackFrameLayout {
  uint64_t Granularity;     // Shadow granularity.
  uint64_t FrameAlignment;  // Alignment for the entire frame.
  uint64_t FrameSize;       // Size of the frame in bytes.
};

ASanStackFrameLayout ComputeASanStackFrameLayout(
    // The array of stack variables. The elements may get reordered and changed.
    SmallVectorImpl<ASanStackVariableDescription> &Vars,
    // AddressSanitizer's shadow granularity. Usually 8, may also be 16, 32, 64.
    uint64_t Granularity,
    // The minimal size of the left-most redzone (header).
    // At least 4 pointer sizes, power of 2, and >= Granularity.
    // The resulting FrameSize should be multiple of MinHeaderSize.
    uint64_t MinHeaderSize);

// Compute frame description, see DescribeAddressIfStack in ASan runtime.
SmallString<64> ComputeASanStackFrameDescription(
    const SmallVectorImpl<ASanStackVariableDescription> &Vars);

// Returns shadow bytes with marked red zones. This shadow represents the state
// if the stack frame when all local variables are inside of the own scope.
SmallVector<uint8_t, 64>
GetShadowBytes(const SmallVectorImpl<ASanStackVariableDescription> &Vars,
               const ASanStackFrameLayout &Layout);

// Returns shadow bytes with marked red zones and after scope. This shadow
// represents the state if the stack frame when all local variables are outside
// of the own scope.
SmallVector<uint8_t, 64> GetShadowBytesAfterScope(
    // The array of stack variables. The elements may get reordered and changed.
    const SmallVectorImpl<ASanStackVariableDescription> &Vars,
    const ASanStackFrameLayout &Layout);

} // llvm namespace

#endif  // LLVM_TRANSFORMS_UTILS_ASANSTACKFRAMELAYOUT_H
