//===- AMDGPUEmitPrintf.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utility function to lower a printf call into a series of device
// library calls on the AMDGPU target.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_AMDGPUEMITPRINTF_H
#define LLVM_TRANSFORMS_UTILS_AMDGPUEMITPRINTF_H

#include "llvm/IR/IRBuilder.h"

namespace llvm {

Value *emitAMDGPUPrintfCall(IRBuilder<> &Builder, ArrayRef<Value *> Args,
                            bool isBuffered);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_AMDGPUEMITPRINTF_H
