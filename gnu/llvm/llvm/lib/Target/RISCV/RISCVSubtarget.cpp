//===-- RISCVSubtarget.cpp - RISC-V Subtarget Information -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RISC-V specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "RISCVSubtarget.h"
#include "GISel/RISCVCallLowering.h"
#include "GISel/RISCVLegalizerInfo.h"
#include "RISCV.h"
#include "RISCVFrameLowering.h"
#include "RISCVTargetMachine.h"
#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "RISCVGenSubtargetInfo.inc"

#define GET_RISCV_MACRO_FUSION_PRED_IMPL
#include "RISCVGenMacroFusion.inc"

namespace llvm::RISCVTuneInfoTable {

#define GET_RISCVTuneInfoTable_IMPL
#include "RISCVGenSearchableTables.inc"
} // namespace llvm::RISCVTuneInfoTable

static cl::opt<unsigned> RVVVectorLMULMax(
    "riscv-v-fixed-length-vector-lmul-max",
    cl::desc("The maximum LMUL value to use for fixed length vectors. "
             "Fractional LMUL values are not supported."),
    cl::init(8), cl::Hidden);

static cl::opt<bool> RISCVDisableUsingConstantPoolForLargeInts(
    "riscv-disable-using-constant-pool-for-large-ints",
    cl::desc("Disable using constant pool for large integers."),
    cl::init(false), cl::Hidden);

static cl::opt<unsigned> RISCVMaxBuildIntsCost(
    "riscv-max-build-ints-cost",
    cl::desc("The maximum cost used for building integers."), cl::init(0),
    cl::Hidden);

static cl::opt<bool> UseAA("riscv-use-aa", cl::init(true),
                           cl::desc("Enable the use of AA during codegen."));

static cl::opt<unsigned> RISCVMinimumJumpTableEntries(
    "riscv-min-jump-table-entries", cl::Hidden,
    cl::desc("Set minimum number of entries to use a jump table on RISCV"));

void RISCVSubtarget::anchor() {}

RISCVSubtarget &
RISCVSubtarget::initializeSubtargetDependencies(const Triple &TT, StringRef CPU,
                                                StringRef TuneCPU, StringRef FS,
                                                StringRef ABIName) {
  // Determine default and user-specified characteristics
  bool Is64Bit = TT.isArch64Bit();
  if (CPU.empty() || CPU == "generic")
    CPU = Is64Bit ? "generic-rv64" : "generic-rv32";

  if (TuneCPU.empty())
    TuneCPU = CPU;

  TuneInfo = RISCVTuneInfoTable::getRISCVTuneInfo(TuneCPU);
  // If there is no TuneInfo for this CPU, we fail back to generic.
  if (!TuneInfo)
    TuneInfo = RISCVTuneInfoTable::getRISCVTuneInfo("generic");
  assert(TuneInfo && "TuneInfo shouldn't be nullptr!");

  ParseSubtargetFeatures(CPU, TuneCPU, FS);
  TargetABI = RISCVABI::computeTargetABI(TT, getFeatureBits(), ABIName);
  RISCVFeatures::validate(TT, getFeatureBits());
  return *this;
}

RISCVSubtarget::RISCVSubtarget(const Triple &TT, StringRef CPU,
                               StringRef TuneCPU, StringRef FS,
                               StringRef ABIName, unsigned RVVVectorBitsMin,
                               unsigned RVVVectorBitsMax,
                               const TargetMachine &TM)
    : RISCVGenSubtargetInfo(TT, CPU, TuneCPU, FS),
      RVVVectorBitsMin(RVVVectorBitsMin), RVVVectorBitsMax(RVVVectorBitsMax),
      FrameLowering(
          initializeSubtargetDependencies(TT, CPU, TuneCPU, FS, ABIName)),
      InstrInfo(*this), RegInfo(getHwMode()), TLInfo(TM, *this) {}

const CallLowering *RISCVSubtarget::getCallLowering() const {
  if (!CallLoweringInfo)
    CallLoweringInfo.reset(new RISCVCallLowering(*getTargetLowering()));
  return CallLoweringInfo.get();
}

InstructionSelector *RISCVSubtarget::getInstructionSelector() const {
  if (!InstSelector) {
    InstSelector.reset(createRISCVInstructionSelector(
        *static_cast<const RISCVTargetMachine *>(&TLInfo.getTargetMachine()),
        *this, *getRegBankInfo()));
  }
  return InstSelector.get();
}

const LegalizerInfo *RISCVSubtarget::getLegalizerInfo() const {
  if (!Legalizer)
    Legalizer.reset(new RISCVLegalizerInfo(*this));
  return Legalizer.get();
}

const RISCVRegisterBankInfo *RISCVSubtarget::getRegBankInfo() const {
  if (!RegBankInfo)
    RegBankInfo.reset(new RISCVRegisterBankInfo(getHwMode()));
  return RegBankInfo.get();
}

bool RISCVSubtarget::useConstantPoolForLargeInts() const {
  return !RISCVDisableUsingConstantPoolForLargeInts;
}

unsigned RISCVSubtarget::getMaxBuildIntsCost() const {
  // Loading integer from constant pool needs two instructions (the reason why
  // the minimum cost is 2): an address calculation instruction and a load
  // instruction. Usually, address calculation and instructions used for
  // building integers (addi, slli, etc.) can be done in one cycle, so here we
  // set the default cost to (LoadLatency + 1) if no threshold is provided.
  return RISCVMaxBuildIntsCost == 0
             ? getSchedModel().LoadLatency + 1
             : std::max<unsigned>(2, RISCVMaxBuildIntsCost);
}

unsigned RISCVSubtarget::getMaxRVVVectorSizeInBits() const {
  assert(hasVInstructions() &&
         "Tried to get vector length without Zve or V extension support!");

  // ZvlLen specifies the minimum required vlen. The upper bound provided by
  // riscv-v-vector-bits-max should be no less than it.
  if (RVVVectorBitsMax != 0 && RVVVectorBitsMax < ZvlLen)
    report_fatal_error("riscv-v-vector-bits-max specified is lower "
                       "than the Zvl*b limitation");

  return RVVVectorBitsMax;
}

unsigned RISCVSubtarget::getMinRVVVectorSizeInBits() const {
  assert(hasVInstructions() &&
         "Tried to get vector length without Zve or V extension support!");

  if (RVVVectorBitsMin == -1U)
    return ZvlLen;

  // ZvlLen specifies the minimum required vlen. The lower bound provided by
  // riscv-v-vector-bits-min should be no less than it.
  if (RVVVectorBitsMin != 0 && RVVVectorBitsMin < ZvlLen)
    report_fatal_error("riscv-v-vector-bits-min specified is lower "
                       "than the Zvl*b limitation");

  return RVVVectorBitsMin;
}

unsigned RISCVSubtarget::getMaxLMULForFixedLengthVectors() const {
  assert(hasVInstructions() &&
         "Tried to get vector length without Zve or V extension support!");
  assert(RVVVectorLMULMax <= 8 &&
         llvm::has_single_bit<uint32_t>(RVVVectorLMULMax) &&
         "V extension requires a LMUL to be at most 8 and a power of 2!");
  return llvm::bit_floor(std::clamp<unsigned>(RVVVectorLMULMax, 1, 8));
}

bool RISCVSubtarget::useRVVForFixedLengthVectors() const {
  return hasVInstructions() && getMinRVVVectorSizeInBits() != 0;
}

bool RISCVSubtarget::enableSubRegLiveness() const { return true; }

void RISCVSubtarget::getPostRAMutations(
    std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  Mutations.push_back(createMacroFusionDAGMutation(getMacroFusions()));
}

  /// Enable use of alias analysis during code generation (during MI
  /// scheduling, DAGCombine, etc.).
bool RISCVSubtarget::useAA() const { return UseAA; }

unsigned RISCVSubtarget::getMinimumJumpTableEntries() const {
  return RISCVMinimumJumpTableEntries.getNumOccurrences() > 0
             ? RISCVMinimumJumpTableEntries
             : TuneInfo->MinimumJumpTableEntries;
}
