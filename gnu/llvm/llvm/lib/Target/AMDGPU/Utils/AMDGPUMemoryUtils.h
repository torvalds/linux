//===- AMDGPUMemoryUtils.h - Memory related helper functions -*- C++ -*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUMEMORYUTILS_H
#define LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUMEMORYUTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace llvm {

struct Align;
class AAResults;
class DataLayout;
class GlobalVariable;
class LoadInst;
class MemoryDef;
class MemorySSA;
class Value;
class Function;
class CallGraph;
class Module;

namespace AMDGPU {

using FunctionVariableMap = DenseMap<Function *, DenseSet<GlobalVariable *>>;
using VariableFunctionMap = DenseMap<GlobalVariable *, DenseSet<Function *>>;

Align getAlign(const DataLayout &DL, const GlobalVariable *GV);

bool isDynamicLDS(const GlobalVariable &GV);
bool isLDSVariableToLower(const GlobalVariable &GV);

struct LDSUsesInfoTy {
  FunctionVariableMap direct_access;
  FunctionVariableMap indirect_access;
};

bool eliminateConstantExprUsesOfLDSFromAllInstructions(Module &M);

void getUsesOfLDSByFunction(const CallGraph &CG, Module &M,
                            FunctionVariableMap &kernels,
                            FunctionVariableMap &functions);

bool isKernelLDS(const Function *F);

LDSUsesInfoTy getTransitiveUsesOfLDS(const CallGraph &CG, Module &M);

/// Strip FnAttr attribute from any functions where we may have
/// introduced its use.
void removeFnAttrFromReachable(CallGraph &CG, Function *KernelRoot,
                               ArrayRef<StringRef> FnAttrs);

/// Given a \p Def clobbering a load from \p Ptr according to the MSSA check
/// if this is actually a memory update or an artificial clobber to facilitate
/// ordering constraints.
bool isReallyAClobber(const Value *Ptr, MemoryDef *Def, AAResults *AA);

/// Check is a \p Load is clobbered in its function.
bool isClobberedInFunction(const LoadInst *Load, MemorySSA *MSSA,
                           AAResults *AA);

} // end namespace AMDGPU

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUMEMORYUTILS_H
