//===-- SystemZSubtarget.cpp - SystemZ subtarget information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemZSubtarget.h"
#include "MCTargetDesc/SystemZMCTargetDesc.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "systemz-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "SystemZGenSubtargetInfo.inc"

static cl::opt<bool> UseSubRegLiveness(
    "systemz-subreg-liveness",
    cl::desc("Enable subregister liveness tracking for SystemZ (experimental)"),
    cl::Hidden);

// Pin the vtable to this file.
void SystemZSubtarget::anchor() {}

SystemZSubtarget &SystemZSubtarget::initializeSubtargetDependencies(
    StringRef CPU, StringRef TuneCPU, StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  if (TuneCPU.empty())
    TuneCPU = CPU;
  // Parse features string.
  ParseSubtargetFeatures(CPU, TuneCPU, FS);

  // -msoft-float implies -mno-vx.
  if (HasSoftFloat)
    HasVector = false;

  // -mno-vx implicitly disables all vector-related features.
  if (!HasVector) {
    HasVectorEnhancements1 = false;
    HasVectorEnhancements2 = false;
    HasVectorPackedDecimal = false;
    HasVectorPackedDecimalEnhancement = false;
    HasVectorPackedDecimalEnhancement2 = false;
  }

  return *this;
}

SystemZCallingConventionRegisters *
SystemZSubtarget::initializeSpecialRegisters() {
  if (isTargetXPLINK64())
    return new SystemZXPLINK64Registers;
  else if (isTargetELF())
    return new SystemZELFRegisters;
  llvm_unreachable("Invalid Calling Convention. Cannot initialize Special "
                   "Call Registers!");
}

SystemZSubtarget::SystemZSubtarget(const Triple &TT, const std::string &CPU,
                                   const std::string &TuneCPU,
                                   const std::string &FS,
                                   const TargetMachine &TM)
    : SystemZGenSubtargetInfo(TT, CPU, TuneCPU, FS), TargetTriple(TT),
      SpecialRegisters(initializeSpecialRegisters()),
      InstrInfo(initializeSubtargetDependencies(CPU, TuneCPU, FS)),
      TLInfo(TM, *this), FrameLowering(SystemZFrameLowering::create(*this)) {}

bool SystemZSubtarget::enableSubRegLiveness() const {
  return UseSubRegLiveness;
}

bool SystemZSubtarget::isAddressedViaADA(const GlobalValue *GV) const {
  if (const auto *GO = dyn_cast<GlobalObject>(GV)) {
    // A R/O variable is placed in code section. If the R/O variable has as
    // least two byte alignment, then generated code can use relative
    // instructions to address the variable. Otherwise, use the ADA to address
    // the variable.
    if (GO->getAlignment() & 0x1) {
      return true;
    }

    // getKindForGlobal only works with definitions
    if (GO->isDeclaration()) {
      return true;
    }

    // check AvailableExternallyLinkage here as getKindForGlobal() asserts
    if (GO->hasAvailableExternallyLinkage()) {
      return true;
    }

    SectionKind GOKind = TargetLoweringObjectFile::getKindForGlobal(
        GO, TLInfo.getTargetMachine());
    if (!GOKind.isReadOnly()) {
      return true;
    }

    return false; // R/O variable with multiple of 2 byte alignment
  }
  return true;
}

bool SystemZSubtarget::isPC32DBLSymbol(const GlobalValue *GV,
                                       CodeModel::Model CM) const {
  if (isTargetzOS())
    return !isAddressedViaADA(GV);

  // PC32DBL accesses require the low bit to be clear.
  //
  // FIXME: Explicitly check for functions: the datalayout is currently
  // missing information about function pointers.
  const DataLayout &DL = GV->getDataLayout();
  if (GV->getPointerAlignment(DL) == 1 && !GV->getValueType()->isFunctionTy())
    return false;

  // For the small model, all locally-binding symbols are in range.
  if (CM == CodeModel::Small)
    return TLInfo.getTargetMachine().shouldAssumeDSOLocal(GV);

  // For Medium and above, assume that the symbol is not within the 4GB range.
  // Taking the address of locally-defined text would be OK, but that
  // case isn't easy to detect.
  return false;
}
