//===-- AMDGPUMachineFunctionInfo.h -------------------------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUMACHINEFUNCTION_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUMACHINEFUNCTION_H

#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"

namespace llvm {

class AMDGPUSubtarget;

class AMDGPUMachineFunction : public MachineFunctionInfo {
  /// A map to keep track of local memory objects and their offsets within the
  /// local memory space.
  SmallDenseMap<const GlobalValue *, unsigned, 4> LocalMemoryObjects;

protected:
  uint64_t ExplicitKernArgSize = 0; // Cache for this.
  Align MaxKernArgAlign;        // Cache for this.

  /// Number of bytes in the LDS that are being used.
  uint32_t LDSSize = 0;
  uint32_t GDSSize = 0;

  /// Number of bytes in the LDS allocated statically. This field is only used
  /// in the instruction selector and not part of the machine function info.
  uint32_t StaticLDSSize = 0;
  uint32_t StaticGDSSize = 0;

  /// Align for dynamic shared memory if any. Dynamic shared memory is
  /// allocated directly after the static one, i.e., LDSSize. Need to pad
  /// LDSSize to ensure that dynamic one is aligned accordingly.
  /// The maximal alignment is updated during IR translation or lowering
  /// stages.
  Align DynLDSAlign;

  // Flag to check dynamic LDS usage by kernel.
  bool UsesDynamicLDS = false;

  // Kernels + shaders. i.e. functions called by the hardware and not called
  // by other functions.
  bool IsEntryFunction = false;

  // Entry points called by other functions instead of directly by the hardware.
  bool IsModuleEntryFunction = false;

  // Functions with the amdgpu_cs_chain or amdgpu_cs_chain_preserve CC.
  bool IsChainFunction = false;

  bool NoSignedZerosFPMath = false;

  // Function may be memory bound.
  bool MemoryBound = false;

  // Kernel may need limited waves per EU for better performance.
  bool WaveLimiter = false;

public:
  AMDGPUMachineFunction(const Function &F, const AMDGPUSubtarget &ST);

  uint64_t getExplicitKernArgSize() const {
    return ExplicitKernArgSize;
  }

  Align getMaxKernArgAlign() const { return MaxKernArgAlign; }

  uint32_t getLDSSize() const {
    return LDSSize;
  }

  uint32_t getGDSSize() const {
    return GDSSize;
  }

  bool isEntryFunction() const {
    return IsEntryFunction;
  }

  bool isModuleEntryFunction() const { return IsModuleEntryFunction; }

  bool isChainFunction() const { return IsChainFunction; }

  // The stack is empty upon entry to this function.
  bool isBottomOfStack() const {
    return isEntryFunction() || isChainFunction();
  }

  bool hasNoSignedZerosFPMath() const {
    return NoSignedZerosFPMath;
  }

  bool isMemoryBound() const {
    return MemoryBound;
  }

  bool needsWaveLimiter() const {
    return WaveLimiter;
  }

  unsigned allocateLDSGlobal(const DataLayout &DL, const GlobalVariable &GV) {
    return allocateLDSGlobal(DL, GV, DynLDSAlign);
  }

  unsigned allocateLDSGlobal(const DataLayout &DL, const GlobalVariable &GV,
                             Align Trailing);

  static std::optional<uint32_t> getLDSKernelIdMetadata(const Function &F);
  static std::optional<uint32_t> getLDSAbsoluteAddress(const GlobalValue &GV);

  Align getDynLDSAlign() const { return DynLDSAlign; }

  void setDynLDSAlign(const Function &F, const GlobalVariable &GV);

  void setUsesDynamicLDS(bool DynLDS);

  bool isDynamicLDSUsed() const;
};

}
#endif
