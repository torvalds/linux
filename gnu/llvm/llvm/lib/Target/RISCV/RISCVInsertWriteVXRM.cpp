//===-- RISCVInsertWriteVXRM.cpp - Insert Write of RISC-V VXRM CSR --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass inserts writes to the VXRM CSR as needed by vector instructions.
// Each instruction that uses VXRM carries an operand that contains its required
// VXRM value. This pass tries to optimize placement to avoid redundant writes
// to VXRM.
//
// This is done using 2 dataflow algorithms. The first is a forward data flow
// to calculate where a VXRM value is available. The second is a backwards
// dataflow to determine where a VXRM value is anticipated.
//
// Finally, we use the results of these two dataflows to insert VXRM writes
// where a value is anticipated, but not available.
//
// FIXME: This pass does not split critical edges, so there can still be some
// redundancy.
//
// FIXME: If we are willing to have writes that aren't always needed, we could
// reduce the number of VXRM writes in some cases.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCVBaseInfo.h"
#include "RISCV.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "riscv-insert-write-vxrm"
#define RISCV_INSERT_WRITE_VXRM_NAME "RISC-V Insert Write VXRM Pass"

namespace {

class VXRMInfo {
  uint8_t VXRMImm = 0;

  enum : uint8_t {
    Uninitialized,
    Static,
    Unknown,
  } State = Uninitialized;

public:
  VXRMInfo() {}

  static VXRMInfo getUnknown() {
    VXRMInfo Info;
    Info.setUnknown();
    return Info;
  }

  bool isValid() const { return State != Uninitialized; }
  void setUnknown() { State = Unknown; }
  bool isUnknown() const { return State == Unknown; }

  bool isStatic() const { return State == Static; }

  void setVXRMImm(unsigned Imm) {
    assert(Imm <= 3 && "Unexpected VXRM value");
    VXRMImm = Imm;
    State = Static;
  }
  unsigned getVXRMImm() const {
    assert(isStatic() && VXRMImm <= 3 && "Unexpected state");
    return VXRMImm;
  }

  bool operator==(const VXRMInfo &Other) const {
    // Uninitialized is only equal to another Uninitialized.
    if (State != Other.State)
      return false;

    if (isStatic())
      return VXRMImm == Other.VXRMImm;

    assert((isValid() || isUnknown()) && "Unexpected state");
    return true;
  }

  bool operator!=(const VXRMInfo &Other) const { return !(*this == Other); }

  // Calculate the VXRMInfo visible to a block assuming this and Other are
  // both predecessors.
  VXRMInfo intersect(const VXRMInfo &Other) const {
    // If the new value isn't valid, ignore it.
    if (!Other.isValid())
      return *this;

    // If this value isn't valid, this must be the first predecessor, use it.
    if (!isValid())
      return Other;

    // If either is unknown, the result is unknown.
    if (isUnknown() || Other.isUnknown())
      return VXRMInfo::getUnknown();

    // If we have an exact match, return this.
    if (*this == Other)
      return *this;

    // Otherwise the result is unknown.
    return VXRMInfo::getUnknown();
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Support for debugging, callable in GDB: V->dump()
  LLVM_DUMP_METHOD void dump() const {
    print(dbgs());
    dbgs() << "\n";
  }

  void print(raw_ostream &OS) const {
    OS << '{';
    if (!isValid())
      OS << "Uninitialized";
    else if (isUnknown())
      OS << "Unknown";
    else
      OS << getVXRMImm();
    OS << '}';
  }
#endif
};

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_ATTRIBUTE_USED
inline raw_ostream &operator<<(raw_ostream &OS, const VXRMInfo &V) {
  V.print(OS);
  return OS;
}
#endif

struct BlockData {
  // Indicates if the block uses VXRM. Uninitialized means no use.
  VXRMInfo VXRMUse;

  // Indicates the VXRM output from the block. Unitialized means transparent.
  VXRMInfo VXRMOut;

  // Keeps track of the available VXRM value at the start of the basic bloc.
  VXRMInfo AvailableIn;

  // Keeps track of the available VXRM value at the end of the basic block.
  VXRMInfo AvailableOut;

  // Keeps track of what VXRM is anticipated at the start of the basic block.
  VXRMInfo AnticipatedIn;

  // Keeps track of what VXRM is anticipated at the end of the basic block.
  VXRMInfo AnticipatedOut;

  // Keeps track of whether the block is already in the queue.
  bool InQueue;

  BlockData() = default;
};

class RISCVInsertWriteVXRM : public MachineFunctionPass {
  const TargetInstrInfo *TII;

  std::vector<BlockData> BlockInfo;
  std::queue<const MachineBasicBlock *> WorkList;

public:
  static char ID;

  RISCVInsertWriteVXRM() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override {
    return RISCV_INSERT_WRITE_VXRM_NAME;
  }

private:
  bool computeVXRMChanges(const MachineBasicBlock &MBB);
  void computeAvailable(const MachineBasicBlock &MBB);
  void computeAnticipated(const MachineBasicBlock &MBB);
  void emitWriteVXRM(MachineBasicBlock &MBB);
};

} // end anonymous namespace

char RISCVInsertWriteVXRM::ID = 0;

INITIALIZE_PASS(RISCVInsertWriteVXRM, DEBUG_TYPE, RISCV_INSERT_WRITE_VXRM_NAME,
                false, false)

static bool ignoresVXRM(const MachineInstr &MI) {
  switch (RISCV::getRVVMCOpcode(MI.getOpcode())) {
  default:
    return false;
  case RISCV::VNCLIP_WI:
  case RISCV::VNCLIPU_WI:
    return MI.getOperand(3).getImm() == 0;
  }
}

bool RISCVInsertWriteVXRM::computeVXRMChanges(const MachineBasicBlock &MBB) {
  BlockData &BBInfo = BlockInfo[MBB.getNumber()];

  bool NeedVXRMWrite = false;
  for (const MachineInstr &MI : MBB) {
    int VXRMIdx = RISCVII::getVXRMOpNum(MI.getDesc());
    if (VXRMIdx >= 0 && !ignoresVXRM(MI)) {
      unsigned NewVXRMImm = MI.getOperand(VXRMIdx).getImm();

      if (!BBInfo.VXRMUse.isValid())
        BBInfo.VXRMUse.setVXRMImm(NewVXRMImm);

      BBInfo.VXRMOut.setVXRMImm(NewVXRMImm);
      NeedVXRMWrite = true;
      continue;
    }

    if (MI.isCall() || MI.isInlineAsm() ||
        MI.modifiesRegister(RISCV::VXRM, /*TRI=*/nullptr)) {
      if (!BBInfo.VXRMUse.isValid())
        BBInfo.VXRMUse.setUnknown();

      BBInfo.VXRMOut.setUnknown();
    }
  }

  return NeedVXRMWrite;
}

void RISCVInsertWriteVXRM::computeAvailable(const MachineBasicBlock &MBB) {
  BlockData &BBInfo = BlockInfo[MBB.getNumber()];

  BBInfo.InQueue = false;

  VXRMInfo Available;
  if (MBB.pred_empty()) {
    Available.setUnknown();
  } else {
    for (const MachineBasicBlock *P : MBB.predecessors())
      Available = Available.intersect(BlockInfo[P->getNumber()].AvailableOut);
  }

  // If we don't have any valid available info, wait until we do.
  if (!Available.isValid())
    return;

  if (Available != BBInfo.AvailableIn) {
    BBInfo.AvailableIn = Available;
    LLVM_DEBUG(dbgs() << "AvailableIn state of " << printMBBReference(MBB)
                      << " changed to " << BBInfo.AvailableIn << "\n");
  }

  if (BBInfo.VXRMOut.isValid())
    Available = BBInfo.VXRMOut;

  if (Available == BBInfo.AvailableOut)
    return;

  BBInfo.AvailableOut = Available;
  LLVM_DEBUG(dbgs() << "AvailableOut state of " << printMBBReference(MBB)
                    << " changed to " << BBInfo.AvailableOut << "\n");

  // Add the successors to the work list so that we can propagate.
  for (MachineBasicBlock *S : MBB.successors()) {
    if (!BlockInfo[S->getNumber()].InQueue) {
      BlockInfo[S->getNumber()].InQueue = true;
      WorkList.push(S);
    }
  }
}

void RISCVInsertWriteVXRM::computeAnticipated(const MachineBasicBlock &MBB) {
  BlockData &BBInfo = BlockInfo[MBB.getNumber()];

  BBInfo.InQueue = false;

  VXRMInfo Anticipated;
  if (MBB.succ_empty()) {
    Anticipated.setUnknown();
  } else {
    for (const MachineBasicBlock *S : MBB.successors())
      Anticipated =
          Anticipated.intersect(BlockInfo[S->getNumber()].AnticipatedIn);
  }

  // If we don't have any valid anticipated info, wait until we do.
  if (!Anticipated.isValid())
    return;

  if (Anticipated != BBInfo.AnticipatedOut) {
    BBInfo.AnticipatedOut = Anticipated;
    LLVM_DEBUG(dbgs() << "AnticipatedOut state of " << printMBBReference(MBB)
                      << " changed to " << BBInfo.AnticipatedOut << "\n");
  }

  // If this block reads VXRM, copy it.
  if (BBInfo.VXRMUse.isValid())
    Anticipated = BBInfo.VXRMUse;

  if (Anticipated == BBInfo.AnticipatedIn)
    return;

  BBInfo.AnticipatedIn = Anticipated;
  LLVM_DEBUG(dbgs() << "AnticipatedIn state of " << printMBBReference(MBB)
                    << " changed to " << BBInfo.AnticipatedIn << "\n");

  // Add the predecessors to the work list so that we can propagate.
  for (MachineBasicBlock *P : MBB.predecessors()) {
    if (!BlockInfo[P->getNumber()].InQueue) {
      BlockInfo[P->getNumber()].InQueue = true;
      WorkList.push(P);
    }
  }
}

void RISCVInsertWriteVXRM::emitWriteVXRM(MachineBasicBlock &MBB) {
  const BlockData &BBInfo = BlockInfo[MBB.getNumber()];

  VXRMInfo Info = BBInfo.AvailableIn;

  // Flag to indicates we need to insert a VXRM write. We want to delay it as
  // late as possible in this block.
  bool PendingInsert = false;

  // Insert VXRM write if anticipated and not available.
  if (BBInfo.AnticipatedIn.isStatic()) {
    // If this is the entry block and the value is anticipated, insert.
    if (MBB.isEntryBlock()) {
      PendingInsert = true;
    } else {
      // Search for any predecessors that wouldn't satisfy our requirement and
      // insert a write VXRM if needed.
      // NOTE: If one predecessor is able to provide the requirement, but
      // another isn't, it means we have a critical edge. The better placement
      // would be to split the critical edge.
      for (MachineBasicBlock *P : MBB.predecessors()) {
        const BlockData &PInfo = BlockInfo[P->getNumber()];
        // If it's available out of the predecessor, then we're ok.
        if (PInfo.AvailableOut.isStatic() &&
            PInfo.AvailableOut.getVXRMImm() ==
                BBInfo.AnticipatedIn.getVXRMImm())
          continue;
        // If the predecessor anticipates this value for all its succesors,
        // then a write to VXRM would have already occured before this block is
        // executed.
        if (PInfo.AnticipatedOut.isStatic() &&
            PInfo.AnticipatedOut.getVXRMImm() ==
                BBInfo.AnticipatedIn.getVXRMImm())
          continue;
        PendingInsert = true;
        break;
      }
    }

    Info = BBInfo.AnticipatedIn;
  }

  for (MachineInstr &MI : MBB) {
    int VXRMIdx = RISCVII::getVXRMOpNum(MI.getDesc());
    if (VXRMIdx >= 0 && !ignoresVXRM(MI)) {
      unsigned NewVXRMImm = MI.getOperand(VXRMIdx).getImm();

      if (PendingInsert || !Info.isStatic() ||
          Info.getVXRMImm() != NewVXRMImm) {
        assert((!PendingInsert ||
                (Info.isStatic() && Info.getVXRMImm() == NewVXRMImm)) &&
               "Pending VXRM insertion mismatch");
        LLVM_DEBUG(dbgs() << "Inserting before "; MI.print(dbgs()));
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(RISCV::WriteVXRMImm))
            .addImm(NewVXRMImm);
        PendingInsert = false;
      }

      MI.addOperand(MachineOperand::CreateReg(RISCV::VXRM, /*IsDef*/ false,
                                              /*IsImp*/ true));
      Info.setVXRMImm(NewVXRMImm);
      continue;
    }

    if (MI.isCall() || MI.isInlineAsm() ||
        MI.modifiesRegister(RISCV::VXRM, /*TRI=*/nullptr))
      Info.setUnknown();
  }

  // If all our successors anticipate a value, do the insert.
  // NOTE: It's possible that not all predecessors of our successor provide the
  // correct value. This can occur on critical edges. If we don't split the
  // critical edge we'll also have a write vxrm in the succesor that is
  // redundant with this one.
  if (PendingInsert ||
      (BBInfo.AnticipatedOut.isStatic() &&
       (!Info.isStatic() ||
        Info.getVXRMImm() != BBInfo.AnticipatedOut.getVXRMImm()))) {
    assert((!PendingInsert ||
            (Info.isStatic() && BBInfo.AnticipatedOut.isStatic() &&
             Info.getVXRMImm() == BBInfo.AnticipatedOut.getVXRMImm())) &&
           "Pending VXRM insertion mismatch");
    LLVM_DEBUG(dbgs() << "Inserting at end of " << printMBBReference(MBB)
                      << " changing to " << BBInfo.AnticipatedOut << "\n");
    BuildMI(MBB, MBB.getFirstTerminator(), DebugLoc(),
            TII->get(RISCV::WriteVXRMImm))
        .addImm(BBInfo.AnticipatedOut.getVXRMImm());
  }
}

bool RISCVInsertWriteVXRM::runOnMachineFunction(MachineFunction &MF) {
  // Skip if the vector extension is not enabled.
  const RISCVSubtarget &ST = MF.getSubtarget<RISCVSubtarget>();
  if (!ST.hasVInstructions())
    return false;

  TII = ST.getInstrInfo();

  assert(BlockInfo.empty() && "Expect empty block infos");
  BlockInfo.resize(MF.getNumBlockIDs());

  // Phase 1 - collect block information.
  bool NeedVXRMChange = false;
  for (const MachineBasicBlock &MBB : MF)
    NeedVXRMChange |= computeVXRMChanges(MBB);

  if (!NeedVXRMChange) {
    BlockInfo.clear();
    return false;
  }

  // Phase 2 - Compute available VXRM using a forward walk.
  for (const MachineBasicBlock &MBB : MF) {
    WorkList.push(&MBB);
    BlockInfo[MBB.getNumber()].InQueue = true;
  }
  while (!WorkList.empty()) {
    const MachineBasicBlock &MBB = *WorkList.front();
    WorkList.pop();
    computeAvailable(MBB);
  }

  // Phase 3 - Compute anticipated VXRM using a backwards walk.
  for (const MachineBasicBlock &MBB : llvm::reverse(MF)) {
    WorkList.push(&MBB);
    BlockInfo[MBB.getNumber()].InQueue = true;
  }
  while (!WorkList.empty()) {
    const MachineBasicBlock &MBB = *WorkList.front();
    WorkList.pop();
    computeAnticipated(MBB);
  }

  // Phase 4 - Emit VXRM writes at the earliest place possible.
  for (MachineBasicBlock &MBB : MF)
    emitWriteVXRM(MBB);

  BlockInfo.clear();

  return true;
}

FunctionPass *llvm::createRISCVInsertWriteVXRMPass() {
  return new RISCVInsertWriteVXRM();
}
