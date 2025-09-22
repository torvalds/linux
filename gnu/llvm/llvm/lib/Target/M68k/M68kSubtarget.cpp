//===-- M68kSubtarget.cpp - M68k Subtarget Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the M68k specific subclass of TargetSubtargetInfo.
///
//===----------------------------------------------------------------------===//

#include "M68kSubtarget.h"
#include "GISel/M68kCallLowering.h"
#include "GISel/M68kLegalizerInfo.h"
#include "GISel/M68kRegisterBankInfo.h"

#include "M68k.h"
#include "M68kMachineFunction.h"
#include "M68kRegisterInfo.h"
#include "M68kTargetMachine.h"

#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "m68k-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "M68kGenSubtargetInfo.inc"

extern bool FixGlobalBaseReg;

/// Select the M68k CPU for the given triple and cpu name.
static StringRef selectM68kCPU(Triple TT, StringRef CPU) {
  if (CPU.empty() || CPU == "generic") {
    CPU = "M68000";
  }
  return CPU;
}

void M68kSubtarget::anchor() {}

M68kSubtarget::M68kSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                             const M68kTargetMachine &TM)
    : M68kGenSubtargetInfo(TT, CPU, /*TuneCPU*/ CPU, FS), TM(TM), TSInfo(),
      InstrInfo(initializeSubtargetDependencies(CPU, TT, FS, TM)),
      FrameLowering(*this, this->getStackAlignment()), TLInfo(TM, *this),
      TargetTriple(TT) {
  CallLoweringInfo.reset(new M68kCallLowering(*getTargetLowering()));
  Legalizer.reset(new M68kLegalizerInfo(*this));

  auto *RBI = new M68kRegisterBankInfo(*getRegisterInfo());
  RegBankInfo.reset(RBI);
  InstSelector.reset(createM68kInstructionSelector(TM, *this, *RBI));
}

const CallLowering *M68kSubtarget::getCallLowering() const {
  return CallLoweringInfo.get();
}

InstructionSelector *M68kSubtarget::getInstructionSelector() const {
  return InstSelector.get();
}

const LegalizerInfo *M68kSubtarget::getLegalizerInfo() const {
  return Legalizer.get();
}

const RegisterBankInfo *M68kSubtarget::getRegBankInfo() const {
  return RegBankInfo.get();
}

bool M68kSubtarget::isPositionIndependent() const {
  return TM.isPositionIndependent();
}

bool M68kSubtarget::isLegalToCallImmediateAddr() const { return true; }

M68kSubtarget &M68kSubtarget::initializeSubtargetDependencies(
    StringRef CPU, Triple TT, StringRef FS, const M68kTargetMachine &TM) {
  std::string CPUName = selectM68kCPU(TT, CPU).str();

  // Parse features string.
  ParseSubtargetFeatures(CPUName, CPUName, FS);

  // Initialize scheduling itinerary for the specified CPU.
  InstrItins = getInstrItineraryForCPU(CPUName);

  stackAlignment = 8;

  return *this;
}

//===----------------------------------------------------------------------===//
// Code Model
//
// Key assumptions:
//  - Whenever possible we use pc-rel encoding since it is smaller(16 bit) than
//    absolute(32 bit).
//  - GOT is reachable within 16 bit offset for both Small and Medium models.
//  - Code section is reachable within 16 bit offset for both models.
//
//  ---------------------+-------------------------+--------------------------
//                       |          Small          |          Medium
//                       +-------------------------+------------+-------------
//                       |   Static   |    PIC     |   Static   |    PIC
//  ---------------------+------------+------------+------------+-------------
//                branch |   pc-rel   |   pc-rel   |   pc-rel   |   pc-rel
//  ---------------------+------------+------------+------------+-------------
//           call global |    @PLT    |    @PLT    |    @PLT    |    @PLT
//  ---------------------+------------+------------+------------+-------------
//         call internal |   pc-rel   |   pc-rel   |   pc-rel   |   pc-rel
//  ---------------------+------------+------------+------------+-------------
//            data local |   pc-rel   |   pc-rel   |  ~pc-rel   |  ^pc-rel
//  ---------------------+------------+------------+------------+-------------
//       data local big* |   pc-rel   |   pc-rel   |  absolute  |  @GOTOFF
//  ---------------------+------------+------------+------------+-------------
//           data global |   pc-rel   |  @GOTPCREL |  ~pc-rel   |  @GOTPCREL
//  ---------------------+------------+------------+------------+-------------
//      data global big* |   pc-rel   |  @GOTPCREL |  absolute  |  @GOTPCREL
//  ---------------------+------------+------------+------------+-------------
//
// * Big data potentially cannot be reached within 16 bit offset and requires
//   special handling for old(x00 and x10) CPUs. Normally these symbols go into
//   separate .ldata section which mapped after normal .data and .text, but I
//   don't really know how this must be done for M68k atm... will try to dig
//   this info out from GCC. For now CPUs prior to M68020 will use static ref
//   for Static Model and @GOT based references for PIC.
//
// ~ These are absolute for older CPUs for now.
// ^ These are @GOTOFF for older CPUs for now.
//===----------------------------------------------------------------------===//

/// Classify a blockaddress reference for the current subtarget according to how
/// we should reference it in a non-pcrel context.
unsigned char M68kSubtarget::classifyBlockAddressReference() const {
  // Unless we start to support Large Code Model branching is always pc-rel
  return M68kII::MO_PC_RELATIVE_ADDRESS;
}

unsigned char
M68kSubtarget::classifyLocalReference(const GlobalValue *GV) const {
  switch (TM.getCodeModel()) {
  default:
    llvm_unreachable("Unsupported code model");
  case CodeModel::Small:
  case CodeModel::Kernel: {
    return M68kII::MO_PC_RELATIVE_ADDRESS;
  }
  case CodeModel::Medium: {
    if (isPositionIndependent()) {
      // On M68020 and better we can fit big any data offset into dips field.
      if (atLeastM68020()) {
        return M68kII::MO_PC_RELATIVE_ADDRESS;
      }
      // Otherwise we could check the data size and make sure it will fit into
      // 16 bit offset. For now we will be conservative and go with @GOTOFF
      return M68kII::MO_GOTOFF;
    } else {
      if (atLeastM68020()) {
        return M68kII::MO_PC_RELATIVE_ADDRESS;
      }
      return M68kII::MO_ABSOLUTE_ADDRESS;
    }
  }
  }
}

unsigned char M68kSubtarget::classifyExternalReference(const Module &M) const {
  if (TM.shouldAssumeDSOLocal(nullptr))
    return classifyLocalReference(nullptr);

  if (isPositionIndependent())
    return M68kII::MO_GOTPCREL;

  return M68kII::MO_GOT;
}

unsigned char
M68kSubtarget::classifyGlobalReference(const GlobalValue *GV) const {
  return classifyGlobalReference(GV, *GV->getParent());
}

unsigned char M68kSubtarget::classifyGlobalReference(const GlobalValue *GV,
                                                     const Module &M) const {
  if (TM.shouldAssumeDSOLocal(GV))
    return classifyLocalReference(GV);

  switch (TM.getCodeModel()) {
  default:
    llvm_unreachable("Unsupported code model");
  case CodeModel::Small:
  case CodeModel::Kernel: {
    if (isPositionIndependent())
      return M68kII::MO_GOTPCREL;
    return M68kII::MO_PC_RELATIVE_ADDRESS;
  }
  case CodeModel::Medium: {
    if (isPositionIndependent())
      return M68kII::MO_GOTPCREL;

    if (atLeastM68020())
      return M68kII::MO_PC_RELATIVE_ADDRESS;

    return M68kII::MO_ABSOLUTE_ADDRESS;
  }
  }
}

unsigned M68kSubtarget::getJumpTableEncoding() const {
  if (isPositionIndependent()) {
    // The only time we want to use GOTOFF(used when with EK_Custom32) is when
    // the potential delta between the jump target and table base can be larger
    // than displacement field, which is True for older CPUs(16 bit disp)
    // in Medium model(can have large data way beyond 16 bit).
    if (TM.getCodeModel() == CodeModel::Medium && !atLeastM68020())
      return MachineJumpTableInfo::EK_Custom32;

    return MachineJumpTableInfo::EK_LabelDifference32;
  }

  // In non-pic modes, just use the address of a block.
  return MachineJumpTableInfo::EK_BlockAddress;
}

unsigned char
M68kSubtarget::classifyGlobalFunctionReference(const GlobalValue *GV) const {
  return classifyGlobalFunctionReference(GV, *GV->getParent());
}

unsigned char
M68kSubtarget::classifyGlobalFunctionReference(const GlobalValue *GV,
                                               const Module &M) const {
  // local always use pc-rel referencing
  if (TM.shouldAssumeDSOLocal(GV))
    return M68kII::MO_NO_FLAG;

  // If the function is marked as non-lazy, generate an indirect call
  // which loads from the GOT directly. This avoids run-time overhead
  // at the cost of eager binding.
  auto *F = dyn_cast_or_null<Function>(GV);
  if (F && F->hasFnAttribute(Attribute::NonLazyBind)) {
    return M68kII::MO_GOTPCREL;
  }

  // Ensure that we don't emit PLT relocations when in non-pic modes.
  return isPositionIndependent() ? M68kII::MO_PLT : M68kII::MO_ABSOLUTE_ADDRESS;
}
