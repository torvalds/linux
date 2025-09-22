//===------- X86InsertPrefetch.cpp - Insert cache prefetch hints ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass applies cache prefetch instructions based on a profile. The pass
// assumes DiscriminateMemOps ran immediately before, to ensure debug info
// matches the one used at profile generation time. The profile is encoded in
// afdo format (text or binary). It contains prefetch hints recommendations.
// Each recommendation is made in terms of debug info locations, a type (i.e.
// nta, t{0|1|2}) and a delta. The debug info identifies an instruction with a
// memory operand (see X86DiscriminateMemOps). The prefetch will be made for
// a location at that memory operand + the delta specified in the
// recommendation.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Module.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/ProfileData/SampleProfReader.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Transforms/IPO/SampleProfile.h"
using namespace llvm;
using namespace sampleprof;

static cl::opt<std::string>
    PrefetchHintsFile("prefetch-hints-file",
                      cl::desc("Path to the prefetch hints profile. See also "
                               "-x86-discriminate-memops"),
                      cl::Hidden);
namespace {

class X86InsertPrefetch : public MachineFunctionPass {
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool doInitialization(Module &) override;

  bool runOnMachineFunction(MachineFunction &MF) override;
  struct PrefetchInfo {
    unsigned InstructionID;
    int64_t Delta;
  };
  typedef SmallVectorImpl<PrefetchInfo> Prefetches;
  bool findPrefetchInfo(const FunctionSamples *Samples, const MachineInstr &MI,
                        Prefetches &prefetches) const;

public:
  static char ID;
  X86InsertPrefetch(const std::string &PrefetchHintsFilename);
  StringRef getPassName() const override {
    return "X86 Insert Cache Prefetches";
  }

private:
  std::string Filename;
  std::unique_ptr<SampleProfileReader> Reader;
};

using PrefetchHints = SampleRecord::CallTargetMap;

// Return any prefetching hints for the specified MachineInstruction. The hints
// are returned as pairs (name, delta).
ErrorOr<const PrefetchHints &>
getPrefetchHints(const FunctionSamples *TopSamples, const MachineInstr &MI) {
  if (const auto &Loc = MI.getDebugLoc())
    if (const auto *Samples = TopSamples->findFunctionSamples(Loc))
      return Samples->findCallTargetMapAt(FunctionSamples::getOffset(Loc),
                                          Loc->getBaseDiscriminator());
  return std::error_code();
}

// The prefetch instruction can't take memory operands involving vector
// registers.
bool IsMemOpCompatibleWithPrefetch(const MachineInstr &MI, int Op) {
  Register BaseReg = MI.getOperand(Op + X86::AddrBaseReg).getReg();
  Register IndexReg = MI.getOperand(Op + X86::AddrIndexReg).getReg();
  return (BaseReg == 0 ||
          X86MCRegisterClasses[X86::GR64RegClassID].contains(BaseReg) ||
          X86MCRegisterClasses[X86::GR32RegClassID].contains(BaseReg)) &&
         (IndexReg == 0 ||
          X86MCRegisterClasses[X86::GR64RegClassID].contains(IndexReg) ||
          X86MCRegisterClasses[X86::GR32RegClassID].contains(IndexReg));
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//            Implementation
//===----------------------------------------------------------------------===//

char X86InsertPrefetch::ID = 0;

X86InsertPrefetch::X86InsertPrefetch(const std::string &PrefetchHintsFilename)
    : MachineFunctionPass(ID), Filename(PrefetchHintsFilename) {}

/// Return true if the provided MachineInstruction has cache prefetch hints. In
/// that case, the prefetch hints are stored, in order, in the Prefetches
/// vector.
bool X86InsertPrefetch::findPrefetchInfo(const FunctionSamples *TopSamples,
                                         const MachineInstr &MI,
                                         Prefetches &Prefetches) const {
  assert(Prefetches.empty() &&
         "Expected caller passed empty PrefetchInfo vector.");

  // There is no point to match prefetch hints if the profile is using MD5.
  if (FunctionSamples::UseMD5)
    return false;

  static constexpr std::pair<StringLiteral, unsigned> HintTypes[] = {
      {"_nta_", X86::PREFETCHNTA},
      {"_t0_", X86::PREFETCHT0},
      {"_t1_", X86::PREFETCHT1},
      {"_t2_", X86::PREFETCHT2},
  };
  static const char *SerializedPrefetchPrefix = "__prefetch";

  auto T = getPrefetchHints(TopSamples, MI);
  if (!T)
    return false;
  int16_t max_index = -1;
  // Convert serialized prefetch hints into PrefetchInfo objects, and populate
  // the Prefetches vector.
  for (const auto &S_V : *T) {
    StringRef Name = S_V.first.stringRef();
    if (Name.consume_front(SerializedPrefetchPrefix)) {
      int64_t D = static_cast<int64_t>(S_V.second);
      unsigned IID = 0;
      for (const auto &HintType : HintTypes) {
        if (Name.consume_front(HintType.first)) {
          IID = HintType.second;
          break;
        }
      }
      if (IID == 0)
        return false;
      uint8_t index = 0;
      Name.consumeInteger(10, index);

      if (index >= Prefetches.size())
        Prefetches.resize(index + 1);
      Prefetches[index] = {IID, D};
      max_index = std::max(max_index, static_cast<int16_t>(index));
    }
  }
  assert(max_index + 1 >= 0 &&
         "Possible overflow: max_index + 1 should be positive.");
  assert(static_cast<size_t>(max_index + 1) == Prefetches.size() &&
         "The number of prefetch hints received should match the number of "
         "PrefetchInfo objects returned");
  return !Prefetches.empty();
}

bool X86InsertPrefetch::doInitialization(Module &M) {
  if (Filename.empty())
    return false;

  LLVMContext &Ctx = M.getContext();
  // TODO: Propagate virtual file system into LLVM targets.
  auto FS = vfs::getRealFileSystem();
  ErrorOr<std::unique_ptr<SampleProfileReader>> ReaderOrErr =
      SampleProfileReader::create(Filename, Ctx, *FS);
  if (std::error_code EC = ReaderOrErr.getError()) {
    std::string Msg = "Could not open profile: " + EC.message();
    Ctx.diagnose(DiagnosticInfoSampleProfile(Filename, Msg,
                                             DiagnosticSeverity::DS_Warning));
    return false;
  }
  Reader = std::move(ReaderOrErr.get());
  Reader->read();
  return true;
}

void X86InsertPrefetch::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool X86InsertPrefetch::runOnMachineFunction(MachineFunction &MF) {
  if (!Reader)
    return false;
  const FunctionSamples *Samples = Reader->getSamplesFor(MF.getFunction());
  if (!Samples)
    return false;

  bool Changed = false;

  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SmallVector<PrefetchInfo, 4> Prefetches;
  for (auto &MBB : MF) {
    for (auto MI = MBB.instr_begin(); MI != MBB.instr_end();) {
      auto Current = MI;
      ++MI;

      int Offset = X86II::getMemoryOperandNo(Current->getDesc().TSFlags);
      if (Offset < 0)
        continue;
      unsigned Bias = X86II::getOperandBias(Current->getDesc());
      int MemOpOffset = Offset + Bias;
      // FIXME(mtrofin): ORE message when the recommendation cannot be taken.
      if (!IsMemOpCompatibleWithPrefetch(*Current, MemOpOffset))
        continue;
      Prefetches.clear();
      if (!findPrefetchInfo(Samples, *Current, Prefetches))
        continue;
      assert(!Prefetches.empty() &&
             "The Prefetches vector should contain at least a value if "
             "findPrefetchInfo returned true.");
      for (auto &PrefInfo : Prefetches) {
        unsigned PFetchInstrID = PrefInfo.InstructionID;
        int64_t Delta = PrefInfo.Delta;
        const MCInstrDesc &Desc = TII->get(PFetchInstrID);
        MachineInstr *PFetch =
            MF.CreateMachineInstr(Desc, Current->getDebugLoc(), true);
        MachineInstrBuilder MIB(MF, PFetch);

        static_assert(X86::AddrBaseReg == 0 && X86::AddrScaleAmt == 1 &&
                          X86::AddrIndexReg == 2 && X86::AddrDisp == 3 &&
                          X86::AddrSegmentReg == 4,
                      "Unexpected change in X86 operand offset order.");

        // This assumes X86::AddBaseReg = 0, {...}ScaleAmt = 1, etc.
        // FIXME(mtrofin): consider adding a:
        //     MachineInstrBuilder::set(unsigned offset, op).
        MIB.addReg(Current->getOperand(MemOpOffset + X86::AddrBaseReg).getReg())
            .addImm(
                Current->getOperand(MemOpOffset + X86::AddrScaleAmt).getImm())
            .addReg(
                Current->getOperand(MemOpOffset + X86::AddrIndexReg).getReg())
            .addImm(Current->getOperand(MemOpOffset + X86::AddrDisp).getImm() +
                    Delta)
            .addReg(Current->getOperand(MemOpOffset + X86::AddrSegmentReg)
                        .getReg());

        if (!Current->memoperands_empty()) {
          MachineMemOperand *CurrentOp = *(Current->memoperands_begin());
          MIB.addMemOperand(MF.getMachineMemOperand(
              CurrentOp, CurrentOp->getOffset() + Delta, CurrentOp->getSize()));
        }

        // Insert before Current. This is because Current may clobber some of
        // the registers used to describe the input memory operand.
        MBB.insert(Current, PFetch);
        Changed = true;
      }
    }
  }
  return Changed;
}

FunctionPass *llvm::createX86InsertPrefetchPass() {
  return new X86InsertPrefetch(PrefetchHintsFile);
}
