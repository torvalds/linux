//===-- AMDGPUMachineFunctionInfo.cpp ---------------------------------------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMachineFunction.h"
#include "AMDGPU.h"
#include "AMDGPUPerfHintAnalysis.h"
#include "AMDGPUSubtarget.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

static const GlobalVariable *
getKernelDynLDSGlobalFromFunction(const Function &F) {
  const Module *M = F.getParent();
  SmallString<64> KernelDynLDSName("llvm.amdgcn.");
  KernelDynLDSName += F.getName();
  KernelDynLDSName += ".dynlds";
  return M->getNamedGlobal(KernelDynLDSName);
}

static bool hasLDSKernelArgument(const Function &F) {
  for (const Argument &Arg : F.args()) {
    Type *ArgTy = Arg.getType();
    if (auto PtrTy = dyn_cast<PointerType>(ArgTy)) {
      if (PtrTy->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS)
        return true;
    }
  }
  return false;
}

AMDGPUMachineFunction::AMDGPUMachineFunction(const Function &F,
                                             const AMDGPUSubtarget &ST)
    : IsEntryFunction(AMDGPU::isEntryFunctionCC(F.getCallingConv())),
      IsModuleEntryFunction(
          AMDGPU::isModuleEntryFunctionCC(F.getCallingConv())),
      IsChainFunction(AMDGPU::isChainCC(F.getCallingConv())) {

  // FIXME: Should initialize KernArgSize based on ExplicitKernelArgOffset,
  // except reserved size is not correctly aligned.

  Attribute MemBoundAttr = F.getFnAttribute("amdgpu-memory-bound");
  MemoryBound = MemBoundAttr.getValueAsBool();

  Attribute WaveLimitAttr = F.getFnAttribute("amdgpu-wave-limiter");
  WaveLimiter = WaveLimitAttr.getValueAsBool();

  // FIXME: How is this attribute supposed to interact with statically known
  // global sizes?
  StringRef S = F.getFnAttribute("amdgpu-gds-size").getValueAsString();
  if (!S.empty())
    S.consumeInteger(0, GDSSize);

  // Assume the attribute allocates before any known GDS globals.
  StaticGDSSize = GDSSize;

  // Second value, if present, is the maximum value that can be assigned.
  // Useful in PromoteAlloca or for LDS spills. Could be used for diagnostics
  // during codegen.
  std::pair<unsigned, unsigned> LDSSizeRange = AMDGPU::getIntegerPairAttribute(
      F, "amdgpu-lds-size", {0, UINT32_MAX}, true);

  // The two separate variables are only profitable when the LDS module lowering
  // pass is disabled. If graphics does not use dynamic LDS, this is never
  // profitable. Leaving cleanup for a later change.
  LDSSize = LDSSizeRange.first;
  StaticLDSSize = LDSSize;

  CallingConv::ID CC = F.getCallingConv();
  if (CC == CallingConv::AMDGPU_KERNEL || CC == CallingConv::SPIR_KERNEL)
    ExplicitKernArgSize = ST.getExplicitKernArgSize(F, MaxKernArgAlign);

  // FIXME: Shouldn't be target specific
  Attribute NSZAttr = F.getFnAttribute("no-signed-zeros-fp-math");
  NoSignedZerosFPMath =
      NSZAttr.isStringAttribute() && NSZAttr.getValueAsString() == "true";

  const GlobalVariable *DynLdsGlobal = getKernelDynLDSGlobalFromFunction(F);
  if (DynLdsGlobal || hasLDSKernelArgument(F))
    UsesDynamicLDS = true;
}

unsigned AMDGPUMachineFunction::allocateLDSGlobal(const DataLayout &DL,
                                                  const GlobalVariable &GV,
                                                  Align Trailing) {
  auto Entry = LocalMemoryObjects.insert(std::pair(&GV, 0));
  if (!Entry.second)
    return Entry.first->second;

  Align Alignment =
      DL.getValueOrABITypeAlignment(GV.getAlign(), GV.getValueType());

  unsigned Offset;
  if (GV.getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS) {

    std::optional<uint32_t> MaybeAbs = getLDSAbsoluteAddress(GV);
    if (MaybeAbs) {
      // Absolute address LDS variables that exist prior to the LDS lowering
      // pass raise a fatal error in that pass. These failure modes are only
      // reachable if that lowering pass is disabled or broken. If/when adding
      // support for absolute addresses on user specified variables, the
      // alignment check moves to the lowering pass and the frame calculation
      // needs to take the user variables into consideration.

      uint32_t ObjectStart = *MaybeAbs;

      if (ObjectStart != alignTo(ObjectStart, Alignment)) {
        report_fatal_error("Absolute address LDS variable inconsistent with "
                           "variable alignment");
      }

      if (isModuleEntryFunction()) {
        // If this is a module entry function, we can also sanity check against
        // the static frame. Strictly it would be better to check against the
        // attribute, i.e. that the variable is within the always-allocated
        // section, and not within some other non-absolute-address object
        // allocated here, but the extra error detection is minimal and we would
        // have to pass the Function around or cache the attribute value.
        uint32_t ObjectEnd =
            ObjectStart + DL.getTypeAllocSize(GV.getValueType());
        if (ObjectEnd > StaticLDSSize) {
          report_fatal_error(
              "Absolute address LDS variable outside of static frame");
        }
      }

      Entry.first->second = ObjectStart;
      return ObjectStart;
    }

    /// TODO: We should sort these to minimize wasted space due to alignment
    /// padding. Currently the padding is decided by the first encountered use
    /// during lowering.
    Offset = StaticLDSSize = alignTo(StaticLDSSize, Alignment);

    StaticLDSSize += DL.getTypeAllocSize(GV.getValueType());

    // Align LDS size to trailing, e.g. for aligning dynamic shared memory
    LDSSize = alignTo(StaticLDSSize, Trailing);
  } else {
    assert(GV.getAddressSpace() == AMDGPUAS::REGION_ADDRESS &&
           "expected region address space");

    Offset = StaticGDSSize = alignTo(StaticGDSSize, Alignment);
    StaticGDSSize += DL.getTypeAllocSize(GV.getValueType());

    // FIXME: Apply alignment of dynamic GDS
    GDSSize = StaticGDSSize;
  }

  Entry.first->second = Offset;
  return Offset;
}

std::optional<uint32_t>
AMDGPUMachineFunction::getLDSKernelIdMetadata(const Function &F) {
  // TODO: Would be more consistent with the abs symbols to use a range
  MDNode *MD = F.getMetadata("llvm.amdgcn.lds.kernel.id");
  if (MD && MD->getNumOperands() == 1) {
    if (ConstantInt *KnownSize =
            mdconst::extract<ConstantInt>(MD->getOperand(0))) {
      uint64_t ZExt = KnownSize->getZExtValue();
      if (ZExt <= UINT32_MAX) {
        return ZExt;
      }
    }
  }
  return {};
}

std::optional<uint32_t>
AMDGPUMachineFunction::getLDSAbsoluteAddress(const GlobalValue &GV) {
  if (GV.getAddressSpace() != AMDGPUAS::LOCAL_ADDRESS)
    return {};

  std::optional<ConstantRange> AbsSymRange = GV.getAbsoluteSymbolRange();
  if (!AbsSymRange)
    return {};

  if (const APInt *V = AbsSymRange->getSingleElement()) {
    std::optional<uint64_t> ZExt = V->tryZExtValue();
    if (ZExt && (*ZExt <= UINT32_MAX)) {
      return *ZExt;
    }
  }

  return {};
}

void AMDGPUMachineFunction::setDynLDSAlign(const Function &F,
                                           const GlobalVariable &GV) {
  const Module *M = F.getParent();
  const DataLayout &DL = M->getDataLayout();
  assert(DL.getTypeAllocSize(GV.getValueType()).isZero());

  Align Alignment =
      DL.getValueOrABITypeAlignment(GV.getAlign(), GV.getValueType());
  if (Alignment <= DynLDSAlign)
    return;

  LDSSize = alignTo(StaticLDSSize, Alignment);
  DynLDSAlign = Alignment;

  // If there is a dynamic LDS variable associated with this function F, every
  // further dynamic LDS instance (allocated by calling setDynLDSAlign) must
  // map to the same address. This holds because no LDS is allocated after the
  // lowering pass if there are dynamic LDS variables present.
  const GlobalVariable *Dyn = getKernelDynLDSGlobalFromFunction(F);
  if (Dyn) {
    unsigned Offset = LDSSize; // return this?
    std::optional<uint32_t> Expect = getLDSAbsoluteAddress(*Dyn);
    if (!Expect || (Offset != *Expect)) {
      report_fatal_error("Inconsistent metadata on dynamic LDS variable");
    }
  }
}

void AMDGPUMachineFunction::setUsesDynamicLDS(bool DynLDS) {
  UsesDynamicLDS = DynLDS;
}

bool AMDGPUMachineFunction::isDynamicLDSUsed() const { return UsesDynamicLDS; }
