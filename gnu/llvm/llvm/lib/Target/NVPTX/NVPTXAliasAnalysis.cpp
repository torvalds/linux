//===--------------------- NVPTXAliasAnalysis.cpp--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the NVPTX address space based alias analysis pass.
//===----------------------------------------------------------------------===//

#include "NVPTXAliasAnalysis.h"
#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "NVPTX.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

#define DEBUG_TYPE "NVPTX-aa"

AnalysisKey NVPTXAA::Key;

char NVPTXAAWrapperPass::ID = 0;
char NVPTXExternalAAWrapper::ID = 0;

INITIALIZE_PASS(NVPTXAAWrapperPass, "nvptx-aa",
                "NVPTX Address space based Alias Analysis", false, true)

INITIALIZE_PASS(NVPTXExternalAAWrapper, "nvptx-aa-wrapper",
                "NVPTX Address space based Alias Analysis Wrapper", false, true)

ImmutablePass *llvm::createNVPTXAAWrapperPass() {
  return new NVPTXAAWrapperPass();
}

ImmutablePass *llvm::createNVPTXExternalAAWrapperPass() {
  return new NVPTXExternalAAWrapper();
}

NVPTXAAWrapperPass::NVPTXAAWrapperPass() : ImmutablePass(ID) {
  initializeNVPTXAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

void NVPTXAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

static AliasResult::Kind getAliasResult(unsigned AS1, unsigned AS2) {
  if ((AS1 == ADDRESS_SPACE_GENERIC) || (AS2 == ADDRESS_SPACE_GENERIC))
    return AliasResult::MayAlias;

  // PTX s6.4.1.1. Generic Addressing:
  // A generic address maps to global memory unless it falls within
  // the window for const, local, or shared memory. The Kernel
  // Function Parameters (.param) window is contained within the
  // .global window.
  //
  // Therefore a global pointer may alias with a param pointer on some
  // GPUs via addrspacecast(param->generic->global) when cvta.param
  // instruction is used (PTX 7.7+ and SM_70+).
  //
  // TODO: cvta.param is not yet supported. We need to change aliasing
  // rules once it is added.

  return (AS1 == AS2 ? AliasResult::MayAlias : AliasResult::NoAlias);
}

AliasResult NVPTXAAResult::alias(const MemoryLocation &Loc1,
                                 const MemoryLocation &Loc2, AAQueryInfo &AAQI,
                                 const Instruction *) {
  unsigned AS1 = Loc1.Ptr->getType()->getPointerAddressSpace();
  unsigned AS2 = Loc2.Ptr->getType()->getPointerAddressSpace();

  return getAliasResult(AS1, AS2);
}

// TODO: .param address space may be writable in presence of cvta.param, but
// this instruction is currently not supported. NVPTXLowerArgs also does not
// allow any writes to .param pointers.
static bool isConstOrParam(unsigned AS) {
  return AS == AddressSpace::ADDRESS_SPACE_CONST ||
         AS == AddressSpace::ADDRESS_SPACE_PARAM;
}

ModRefInfo NVPTXAAResult::getModRefInfoMask(const MemoryLocation &Loc,
                                            AAQueryInfo &AAQI,
                                            bool IgnoreLocals) {
  if (isConstOrParam(Loc.Ptr->getType()->getPointerAddressSpace()))
    return ModRefInfo::NoModRef;

  const Value *Base = getUnderlyingObject(Loc.Ptr);
  if (isConstOrParam(Base->getType()->getPointerAddressSpace()))
    return ModRefInfo::NoModRef;

  return ModRefInfo::ModRef;
}
