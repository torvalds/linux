//===--------- LLJITUtilsCBindings.cpp - Advanced LLJIT features ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm-c/LLJIT.h"
#include "llvm-c/LLJITUtils.h"

#include "llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"

using namespace llvm;
using namespace llvm::orc;

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(LLJIT, LLVMOrcLLJITRef)

LLVMErrorRef LLVMOrcLLJITEnableDebugSupport(LLVMOrcLLJITRef J) {
  return wrap(llvm::orc::enableDebuggerSupport(*unwrap(J)));
}
