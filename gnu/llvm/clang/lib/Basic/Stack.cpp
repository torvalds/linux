//===--- Stack.cpp - Utilities for dealing with stack space ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines utilities for dealing with stack allocation and stack space.
///
//===----------------------------------------------------------------------===//

#include "clang/Basic/Stack.h"
#include "llvm/Support/CrashRecoveryContext.h"

#ifdef _MSC_VER
#include <intrin.h>  // for _AddressOfReturnAddress
#endif

static LLVM_THREAD_LOCAL void *BottomOfStack = nullptr;

static void *getStackPointer() {
#if __GNUC__ || __has_builtin(__builtin_frame_address)
  return __builtin_frame_address(0);
#elif defined(_MSC_VER)
  return _AddressOfReturnAddress();
#else
  char CharOnStack = 0;
  // The volatile store here is intended to escape the local variable, to
  // prevent the compiler from optimizing CharOnStack into anything other
  // than a char on the stack.
  //
  // Tested on: MSVC 2015 - 2019, GCC 4.9 - 9, Clang 3.2 - 9, ICC 13 - 19.
  char *volatile Ptr = &CharOnStack;
  return Ptr;
#endif
}

void clang::noteBottomOfStack() {
  if (!BottomOfStack)
    BottomOfStack = getStackPointer();
}

bool clang::isStackNearlyExhausted() {
  // We consider 256 KiB to be sufficient for any code that runs between checks
  // for stack size.
  constexpr size_t SufficientStack = 256 << 10;

  // If we don't know where the bottom of the stack is, hope for the best.
  if (!BottomOfStack)
    return false;

  intptr_t StackDiff = (intptr_t)getStackPointer() - (intptr_t)BottomOfStack;
  size_t StackUsage = (size_t)std::abs(StackDiff);

  // If the stack pointer has a surprising value, we do not understand this
  // stack usage scheme. (Perhaps the target allocates new stack regions on
  // demand for us.) Don't try to guess what's going on.
  if (StackUsage > DesiredStackSize)
    return false;

  return StackUsage >= DesiredStackSize - SufficientStack;
}

void clang::runWithSufficientStackSpaceSlow(llvm::function_ref<void()> Diag,
                                            llvm::function_ref<void()> Fn) {
  llvm::CrashRecoveryContext CRC;
  CRC.RunSafelyOnThread([&] {
    noteBottomOfStack();
    Diag();
    Fn();
  }, DesiredStackSize);
}
