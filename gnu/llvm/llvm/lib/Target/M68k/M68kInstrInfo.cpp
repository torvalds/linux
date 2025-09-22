//===-- M68kInstrInfo.cpp - M68k Instruction Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the M68k declaration of the TargetInstrInfo class.
///
//===----------------------------------------------------------------------===//

#include "M68kInstrInfo.h"

#include "M68kInstrBuilder.h"
#include "M68kMachineFunction.h"
#include "M68kTargetMachine.h"
#include "MCTargetDesc/M68kMCCodeEmitter.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Regex.h"

#include <functional>

using namespace llvm;

#define DEBUG_TYPE "M68k-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "M68kGenInstrInfo.inc"

// Pin the vtable to this file.
void M68kInstrInfo::anchor() {}

M68kInstrInfo::M68kInstrInfo(const M68kSubtarget &STI)
    : M68kGenInstrInfo(M68k::ADJCALLSTACKDOWN, M68k::ADJCALLSTACKUP, 0,
                       M68k::RET),
      Subtarget(STI), RI(STI) {}

static M68k::CondCode getCondFromBranchOpc(unsigned BrOpc) {
  switch (BrOpc) {
  default:
    return M68k::COND_INVALID;
  case M68k::Beq8:
    return M68k::COND_EQ;
  case M68k::Bne8:
    return M68k::COND_NE;
  case M68k::Blt8:
    return M68k::COND_LT;
  case M68k::Ble8:
    return M68k::COND_LE;
  case M68k::Bgt8:
    return M68k::COND_GT;
  case M68k::Bge8:
    return M68k::COND_GE;
  case M68k::Bcs8:
    return M68k::COND_CS;
  case M68k::Bls8:
    return M68k::COND_LS;
  case M68k::Bhi8:
    return M68k::COND_HI;
  case M68k::Bcc8:
    return M68k::COND_CC;
  case M68k::Bmi8:
    return M68k::COND_MI;
  case M68k::Bpl8:
    return M68k::COND_PL;
  case M68k::Bvs8:
    return M68k::COND_VS;
  case M68k::Bvc8:
    return M68k::COND_VC;
  }
}

bool M68kInstrInfo::AnalyzeBranchImpl(MachineBasicBlock &MBB,
                                      MachineBasicBlock *&TBB,
                                      MachineBasicBlock *&FBB,
                                      SmallVectorImpl<MachineOperand> &Cond,
                                      bool AllowModify) const {

  auto UncondBranch =
      std::pair<MachineBasicBlock::reverse_iterator, MachineBasicBlock *>{
          MBB.rend(), nullptr};

  // Erase any instructions if allowed at the end of the scope.
  std::vector<std::reference_wrapper<llvm::MachineInstr>> EraseList;
  auto FinalizeOnReturn = llvm::make_scope_exit([&EraseList] {
    std::for_each(EraseList.begin(), EraseList.end(),
                  [](auto &ref) { ref.get().eraseFromParent(); });
  });

  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  for (auto iter = MBB.rbegin(); iter != MBB.rend(); iter = std::next(iter)) {

    unsigned Opcode = iter->getOpcode();

    if (iter->isDebugInstr())
      continue;

    // Working from the bottom, when we see a non-terminator instruction, we're
    // done.
    if (!isUnpredicatedTerminator(*iter))
      break;

    // A terminator that isn't a branch can't easily be handled by this
    // analysis.
    if (!iter->isBranch())
      return true;

    // Handle unconditional branches.
    if (Opcode == M68k::BRA8 || Opcode == M68k::BRA16) {
      if (!iter->getOperand(0).isMBB())
        return true;
      UncondBranch = {iter, iter->getOperand(0).getMBB()};

      // TBB is used to indicate the unconditional destination.
      TBB = UncondBranch.second;

      if (!AllowModify)
        continue;

      // If the block has any instructions after a JMP, erase them.
      EraseList.insert(EraseList.begin(), MBB.rbegin(), iter);

      Cond.clear();
      FBB = nullptr;

      // Erase the JMP if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(UncondBranch.second)) {
        TBB = nullptr;
        EraseList.push_back(*iter);
        UncondBranch = {MBB.rend(), nullptr};
      }

      continue;
    }

    // Handle conditional branches.
    auto BranchCode = M68k::GetCondFromBranchOpc(Opcode);

    // Can't handle indirect branch.
    if (BranchCode == M68k::COND_INVALID)
      return true;

    // In practice we should never have an undef CCR operand, if we do
    // abort here as we are not prepared to preserve the flag.
    // ??? Is this required?
    // if (iter->getOperand(1).isUndef())
    //   return true;

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      if (!iter->getOperand(0).isMBB())
        return true;
      MachineBasicBlock *CondBranchTarget = iter->getOperand(0).getMBB();

      // If we see something like this:
      //
      //     bcc l1
      //     bra l2
      //     ...
      //   l1:
      //     ...
      //   l2:
      if (UncondBranch.first != MBB.rend()) {

        assert(std::next(UncondBranch.first) == iter && "Wrong block layout.");

        // And we are allowed to modify the block and the target block of the
        // conditional branch is the direct successor of this block:
        //
        //     bcc l1
        //     bra l2
        //   l1:
        //     ...
        //   l2:
        //
        // we change it to this if allowed:
        //
        //     bncc l2
        //   l1:
        //     ...
        //   l2:
        //
        // Which is a bit more efficient.
        if (AllowModify && MBB.isLayoutSuccessor(CondBranchTarget)) {

          BranchCode = GetOppositeBranchCondition(BranchCode);
          unsigned BNCC = GetCondBranchFromCond(BranchCode);

          BuildMI(MBB, *UncondBranch.first, MBB.rfindDebugLoc(iter), get(BNCC))
              .addMBB(UncondBranch.second);

          EraseList.push_back(*iter);
          EraseList.push_back(*UncondBranch.first);

          TBB = UncondBranch.second;
          FBB = nullptr;
          Cond.push_back(MachineOperand::CreateImm(BranchCode));

          // Otherwise preserve TBB, FBB and Cond as requested
        } else {
          TBB = CondBranchTarget;
          FBB = UncondBranch.second;
          Cond.push_back(MachineOperand::CreateImm(BranchCode));
        }

        UncondBranch = {MBB.rend(), nullptr};
        continue;
      }

      TBB = CondBranchTarget;
      FBB = nullptr;
      Cond.push_back(MachineOperand::CreateImm(BranchCode));

      continue;
    }

    // Handle subsequent conditional branches. Only handle the case where all
    // conditional branches branch to the same destination and their condition
    // opcodes fit one of the special multi-branch idioms.
    assert(Cond.size() == 1);
    assert(TBB);

    // If the conditions are the same, we can leave them alone.
    auto OldBranchCode = static_cast<M68k::CondCode>(Cond[0].getImm());
    if (!iter->getOperand(0).isMBB())
      return true;
    auto NewTBB = iter->getOperand(0).getMBB();
    if (OldBranchCode == BranchCode && TBB == NewTBB)
      continue;

    // If they differ we cannot do much here.
    return true;
  }

  return false;
}

bool M68kInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const {
  return AnalyzeBranchImpl(MBB, TBB, FBB, Cond, AllowModify);
}

unsigned M68kInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                     int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugValue())
      continue;
    if (I->getOpcode() != M68k::BRA8 &&
        getCondFromBranchOpc(I->getOpcode()) == M68k::COND_INVALID)
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}

unsigned M68kInstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  // Shouldn't be a fall through.
  assert(TBB && "InsertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 1 || Cond.size() == 0) &&
         "M68k branch conditions have one component!");
  assert(!BytesAdded && "code size not handled");

  if (Cond.empty()) {
    // Unconditional branch?
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(M68k::BRA8)).addMBB(TBB);
    return 1;
  }

  // If FBB is null, it is implied to be a fall-through block.
  bool FallThru = FBB == nullptr;

  // Conditional branch.
  unsigned Count = 0;
  M68k::CondCode CC = (M68k::CondCode)Cond[0].getImm();
  unsigned Opc = GetCondBranchFromCond(CC);
  BuildMI(&MBB, DL, get(Opc)).addMBB(TBB);
  ++Count;
  if (!FallThru) {
    // Two-way Conditional branch. Insert the second branch.
    BuildMI(&MBB, DL, get(M68k::BRA8)).addMBB(FBB);
    ++Count;
  }
  return Count;
}

void M68kInstrInfo::AddSExt(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator I, DebugLoc DL,
                            unsigned Reg, MVT From, MVT To) const {
  if (From == MVT::i8) {
    unsigned R = Reg;
    // EXT16 requires i16 register
    if (To == MVT::i32) {
      R = RI.getSubReg(Reg, M68k::MxSubRegIndex16Lo);
      assert(R && "No viable SUB register available");
    }
    BuildMI(MBB, I, DL, get(M68k::EXT16), R).addReg(R);
  }

  if (To == MVT::i32)
    BuildMI(MBB, I, DL, get(M68k::EXT32), Reg).addReg(Reg);
}

void M68kInstrInfo::AddZExt(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator I, DebugLoc DL,
                            unsigned Reg, MVT From, MVT To) const {

  unsigned Mask, And;
  if (From == MVT::i8)
    Mask = 0xFF;
  else
    Mask = 0xFFFF;

  if (To == MVT::i16)
    And = M68k::AND16di;
  else // i32
    And = M68k::AND32di;

  // TODO use xor r,r to decrease size
  BuildMI(MBB, I, DL, get(And), Reg).addReg(Reg).addImm(Mask);
}

// Convert MOVI to MOVQ if the target is a data register and the immediate
// fits in a sign-extended i8, otherwise emit a plain MOV.
bool M68kInstrInfo::ExpandMOVI(MachineInstrBuilder &MIB, MVT MVTSize) const {
  Register Reg = MIB->getOperand(0).getReg();
  int64_t Imm = MIB->getOperand(1).getImm();
  bool IsAddressReg = false;

  const auto *DR32 = RI.getRegClass(M68k::DR32RegClassID);
  const auto *AR32 = RI.getRegClass(M68k::AR32RegClassID);
  const auto *AR16 = RI.getRegClass(M68k::AR16RegClassID);

  if (AR16->contains(Reg) || AR32->contains(Reg))
    IsAddressReg = true;

  LLVM_DEBUG(dbgs() << "Expand " << *MIB.getInstr() << " to ");

  if (MVTSize == MVT::i8 || (!IsAddressReg && Imm >= -128 && Imm <= 127)) {
    LLVM_DEBUG(dbgs() << "MOVEQ\n");

    // We need to assign to the full register to make IV happy
    Register SReg =
        MVTSize == MVT::i32 ? Reg : Register(RI.getMatchingMegaReg(Reg, DR32));
    assert(SReg && "No viable MEGA register available");

    MIB->setDesc(get(M68k::MOVQ));
    MIB->getOperand(0).setReg(SReg);
  } else {
    LLVM_DEBUG(dbgs() << "MOVE\n");
    MIB->setDesc(get(MVTSize == MVT::i16 ? M68k::MOV16ri : M68k::MOV32ri));
  }

  return true;
}

bool M68kInstrInfo::ExpandMOVX_RR(MachineInstrBuilder &MIB, MVT MVTDst,
                                  MVT MVTSrc) const {
  unsigned Move = MVTDst == MVT::i16 ? M68k::MOV16rr : M68k::MOV32rr;
  Register Dst = MIB->getOperand(0).getReg();
  Register Src = MIB->getOperand(1).getReg();

  assert(Dst != Src && "You cannot use the same Regs with MOVX_RR");

  const auto &TRI = getRegisterInfo();

  const auto *RCDst = TRI.getMaximalPhysRegClass(Dst, MVTDst);
  const auto *RCSrc = TRI.getMaximalPhysRegClass(Src, MVTSrc);

  assert(RCDst && RCSrc && "Wrong use of MOVX_RR");
  assert(RCDst != RCSrc && "You cannot use the same Reg Classes with MOVX_RR");
  (void)RCSrc;

  // We need to find the super source register that matches the size of Dst
  unsigned SSrc = RI.getMatchingMegaReg(Src, RCDst);
  assert(SSrc && "No viable MEGA register available");

  DebugLoc DL = MIB->getDebugLoc();

  // If it happens to that super source register is the destination register
  // we do nothing
  if (Dst == SSrc) {
    LLVM_DEBUG(dbgs() << "Remove " << *MIB.getInstr() << '\n');
    MIB->eraseFromParent();
  } else { // otherwise we need to MOV
    LLVM_DEBUG(dbgs() << "Expand " << *MIB.getInstr() << " to MOV\n");
    MIB->setDesc(get(Move));
    MIB->getOperand(1).setReg(SSrc);
  }

  return true;
}

/// Expand SExt MOVE pseudos into a MOV and a EXT if the operands are two
/// different registers or just EXT if it is the same register
bool M68kInstrInfo::ExpandMOVSZX_RR(MachineInstrBuilder &MIB, bool IsSigned,
                                    MVT MVTDst, MVT MVTSrc) const {
  LLVM_DEBUG(dbgs() << "Expand " << *MIB.getInstr() << " to ");

  unsigned Move;

  if (MVTDst == MVT::i16)
    Move = M68k::MOV16rr;
  else // i32
    Move = M68k::MOV32rr;

  Register Dst = MIB->getOperand(0).getReg();
  Register Src = MIB->getOperand(1).getReg();

  assert(Dst != Src && "You cannot use the same Regs with MOVSX_RR");

  const auto &TRI = getRegisterInfo();

  const auto *RCDst = TRI.getMaximalPhysRegClass(Dst, MVTDst);
  const auto *RCSrc = TRI.getMaximalPhysRegClass(Src, MVTSrc);

  assert(RCDst && RCSrc && "Wrong use of MOVSX_RR");
  assert(RCDst != RCSrc && "You cannot use the same Reg Classes with MOVSX_RR");
  (void)RCSrc;

  // We need to find the super source register that matches the size of Dst
  unsigned SSrc = RI.getMatchingMegaReg(Src, RCDst);
  assert(SSrc && "No viable MEGA register available");

  MachineBasicBlock &MBB = *MIB->getParent();
  DebugLoc DL = MIB->getDebugLoc();

  if (Dst != SSrc) {
    LLVM_DEBUG(dbgs() << "Move and " << '\n');
    BuildMI(MBB, MIB.getInstr(), DL, get(Move), Dst).addReg(SSrc);
  }

  if (IsSigned) {
    LLVM_DEBUG(dbgs() << "Sign Extend" << '\n');
    AddSExt(MBB, MIB.getInstr(), DL, Dst, MVTSrc, MVTDst);
  } else {
    LLVM_DEBUG(dbgs() << "Zero Extend" << '\n');
    AddZExt(MBB, MIB.getInstr(), DL, Dst, MVTSrc, MVTDst);
  }

  MIB->eraseFromParent();

  return true;
}

bool M68kInstrInfo::ExpandMOVSZX_RM(MachineInstrBuilder &MIB, bool IsSigned,
                                    const MCInstrDesc &Desc, MVT MVTDst,
                                    MVT MVTSrc) const {
  LLVM_DEBUG(dbgs() << "Expand " << *MIB.getInstr() << " to LOAD and ");

  Register Dst = MIB->getOperand(0).getReg();

  // We need the subreg of Dst to make instruction verifier happy because the
  // real machine instruction consumes and produces values of the same size and
  // the registers the will be used here fall into different classes and this
  // makes IV cry. We could use a bigger operation, but this will put some
  // pressure on cache and memory, so no.
  unsigned SubDst =
      RI.getSubReg(Dst, MVTSrc == MVT::i8 ? M68k::MxSubRegIndex8Lo
                                          : M68k::MxSubRegIndex16Lo);
  assert(SubDst && "No viable SUB register available");

  // Make this a plain move
  MIB->setDesc(Desc);
  MIB->getOperand(0).setReg(SubDst);

  MachineBasicBlock::iterator I = MIB.getInstr();
  I++;
  MachineBasicBlock &MBB = *MIB->getParent();
  DebugLoc DL = MIB->getDebugLoc();

  if (IsSigned) {
    LLVM_DEBUG(dbgs() << "Sign Extend" << '\n');
    AddSExt(MBB, I, DL, Dst, MVTSrc, MVTDst);
  } else {
    LLVM_DEBUG(dbgs() << "Zero Extend" << '\n');
    AddZExt(MBB, I, DL, Dst, MVTSrc, MVTDst);
  }

  return true;
}

bool M68kInstrInfo::ExpandPUSH_POP(MachineInstrBuilder &MIB,
                                   const MCInstrDesc &Desc, bool IsPush) const {
  MachineBasicBlock::iterator I = MIB.getInstr();
  I++;
  MachineBasicBlock &MBB = *MIB->getParent();
  MachineOperand MO = MIB->getOperand(0);
  DebugLoc DL = MIB->getDebugLoc();
  if (IsPush)
    BuildMI(MBB, I, DL, Desc).addReg(RI.getStackRegister()).add(MO);
  else
    BuildMI(MBB, I, DL, Desc, MO.getReg()).addReg(RI.getStackRegister());

  MIB->eraseFromParent();
  return true;
}

bool M68kInstrInfo::ExpandCCR(MachineInstrBuilder &MIB, bool IsToCCR) const {

  // Replace the pseudo instruction with the real one
  if (IsToCCR)
    MIB->setDesc(get(M68k::MOV16cd));
  else
    // FIXME M68010 or later is required
    MIB->setDesc(get(M68k::MOV16dc));

  // Promote used register to the next class
  auto &Opd = MIB->getOperand(1);
  Opd.setReg(getRegisterInfo().getMatchingSuperReg(
      Opd.getReg(), M68k::MxSubRegIndex8Lo, &M68k::DR16RegClass));

  return true;
}

bool M68kInstrInfo::ExpandMOVEM(MachineInstrBuilder &MIB,
                                const MCInstrDesc &Desc, bool IsRM) const {
  int Reg = 0, Offset = 0, Base = 0;
  auto XR32 = RI.getRegClass(M68k::XR32RegClassID);
  auto DL = MIB->getDebugLoc();
  auto MI = MIB.getInstr();
  auto &MBB = *MIB->getParent();

  if (IsRM) {
    Reg = MIB->getOperand(0).getReg();
    Offset = MIB->getOperand(1).getImm();
    Base = MIB->getOperand(2).getReg();
  } else {
    Offset = MIB->getOperand(0).getImm();
    Base = MIB->getOperand(1).getReg();
    Reg = MIB->getOperand(2).getReg();
  }

  // If the register is not in XR32 then it is smaller than 32 bit, we
  // implicitly promote it to 32
  if (!XR32->contains(Reg)) {
    Reg = RI.getMatchingMegaReg(Reg, XR32);
    assert(Reg && "Has not meaningful MEGA register");
  }

  unsigned Mask = 1 << RI.getSpillRegisterOrder(Reg);
  if (IsRM) {
    BuildMI(MBB, MI, DL, Desc)
        .addImm(Mask)
        .addImm(Offset)
        .addReg(Base)
        .addReg(Reg, RegState::ImplicitDefine)
        .copyImplicitOps(*MIB);
  } else {
    BuildMI(MBB, MI, DL, Desc)
        .addImm(Offset)
        .addReg(Base)
        .addImm(Mask)
        .addReg(Reg, RegState::Implicit)
        .copyImplicitOps(*MIB);
  }

  MIB->eraseFromParent();

  return true;
}

/// Expand a single-def pseudo instruction to a two-addr
/// instruction with two undef reads of the register being defined.
/// This is used for mapping:
///   %d0 = SETCS_C32d
/// to:
///   %d0 = SUBX32dd %d0<undef>, %d0<undef>
///
static bool Expand2AddrUndef(MachineInstrBuilder &MIB,
                             const MCInstrDesc &Desc) {
  assert(Desc.getNumOperands() == 3 && "Expected two-addr instruction.");
  Register Reg = MIB->getOperand(0).getReg();
  MIB->setDesc(Desc);

  // MachineInstr::addOperand() will insert explicit operands before any
  // implicit operands.
  MIB.addReg(Reg, RegState::Undef).addReg(Reg, RegState::Undef);
  // But we don't trust that.
  assert(MIB->getOperand(1).getReg() == Reg &&
         MIB->getOperand(2).getReg() == Reg && "Misplaced operand");
  return true;
}

bool M68kInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineInstrBuilder MIB(*MI.getParent()->getParent(), MI);
  switch (MI.getOpcode()) {
  case M68k::PUSH8d:
    return ExpandPUSH_POP(MIB, get(M68k::MOV8ed), true);
  case M68k::PUSH16d:
    return ExpandPUSH_POP(MIB, get(M68k::MOV16er), true);
  case M68k::PUSH32r:
    return ExpandPUSH_POP(MIB, get(M68k::MOV32er), true);

  case M68k::POP8d:
    return ExpandPUSH_POP(MIB, get(M68k::MOV8do), false);
  case M68k::POP16d:
    return ExpandPUSH_POP(MIB, get(M68k::MOV16ro), false);
  case M68k::POP32r:
    return ExpandPUSH_POP(MIB, get(M68k::MOV32ro), false);

  case M68k::SETCS_C8d:
    return Expand2AddrUndef(MIB, get(M68k::SUBX8dd));
  case M68k::SETCS_C16d:
    return Expand2AddrUndef(MIB, get(M68k::SUBX16dd));
  case M68k::SETCS_C32d:
    return Expand2AddrUndef(MIB, get(M68k::SUBX32dd));
  }
  return false;
}

bool M68kInstrInfo::isPCRelRegisterOperandLegal(
    const MachineOperand &MO) const {
  assert(MO.isReg());

  // Check whether this MO belongs to an instruction with addressing mode 'k',
  // Refer to TargetInstrInfo.h for more information about this function.

  const MachineInstr *MI = MO.getParent();
  const unsigned NameIndices = M68kInstrNameIndices[MI->getOpcode()];
  StringRef InstrName(&M68kInstrNameData[NameIndices]);
  const unsigned OperandNo = MO.getOperandNo();

  // If this machine operand is the 2nd operand, then check
  // whether the instruction has destination addressing mode 'k'.
  if (OperandNo == 1)
    return Regex("[A-Z]+(8|16|32)k[a-z](_TC)?$").match(InstrName);

  // If this machine operand is the last one, then check
  // whether the instruction has source addressing mode 'k'.
  if (OperandNo == MI->getNumExplicitOperands() - 1)
    return Regex("[A-Z]+(8|16|32)[a-z]k(_TC)?$").match(InstrName);

  return false;
}

void M68kInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                const DebugLoc &DL, MCRegister DstReg,
                                MCRegister SrcReg, bool KillSrc) const {
  unsigned Opc = 0;

  // First deal with the normal symmetric copies.
  if (M68k::XR32RegClass.contains(DstReg, SrcReg))
    Opc = M68k::MOV32rr;
  else if (M68k::XR16RegClass.contains(DstReg, SrcReg))
    Opc = M68k::MOV16rr;
  else if (M68k::DR8RegClass.contains(DstReg, SrcReg))
    Opc = M68k::MOV8dd;

  if (Opc) {
    BuildMI(MBB, MI, DL, get(Opc), DstReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  // Now deal with asymmetrically sized copies. The cases that follow are upcast
  // moves.
  //
  // NOTE
  // These moves are not aware of type nature of these values and thus
  // won't do any SExt or ZExt and upper bits will basically contain garbage.
  MachineInstrBuilder MIB(*MBB.getParent(), MI);
  if (M68k::DR8RegClass.contains(SrcReg)) {
    if (M68k::XR16RegClass.contains(DstReg))
      Opc = M68k::MOVXd16d8;
    else if (M68k::XR32RegClass.contains(DstReg))
      Opc = M68k::MOVXd32d8;
  } else if (M68k::XR16RegClass.contains(SrcReg) &&
             M68k::XR32RegClass.contains(DstReg))
    Opc = M68k::MOVXd32d16;

  if (Opc) {
    BuildMI(MBB, MI, DL, get(Opc), DstReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  bool FromCCR = SrcReg == M68k::CCR;
  bool FromSR = SrcReg == M68k::SR;
  bool ToCCR = DstReg == M68k::CCR;
  bool ToSR = DstReg == M68k::SR;

  if (FromCCR) {
    assert(M68k::DR8RegClass.contains(DstReg) &&
           "Need DR8 register to copy CCR");
    Opc = M68k::MOV8dc;
  } else if (ToCCR) {
    assert(M68k::DR8RegClass.contains(SrcReg) &&
           "Need DR8 register to copy CCR");
    Opc = M68k::MOV8cd;
  } else if (FromSR || ToSR)
    llvm_unreachable("Cannot emit SR copy instruction");

  if (Opc) {
    BuildMI(MBB, MI, DL, get(Opc), DstReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  LLVM_DEBUG(dbgs() << "Cannot copy " << RI.getName(SrcReg) << " to "
                    << RI.getName(DstReg) << '\n');
  llvm_unreachable("Cannot emit physreg copy instruction");
}

namespace {
unsigned getLoadStoreRegOpcode(unsigned Reg, const TargetRegisterClass *RC,
                               const TargetRegisterInfo *TRI,
                               const M68kSubtarget &STI, bool load) {
  switch (TRI->getRegSizeInBits(*RC)) {
  default:
    llvm_unreachable("Unknown spill size");
  case 8:
    if (M68k::DR8RegClass.hasSubClassEq(RC))
      return load ? M68k::MOV8dp : M68k::MOV8pd;
    if (M68k::CCRCRegClass.hasSubClassEq(RC))
      return load ? M68k::MOV16cp : M68k::MOV16pc;

    llvm_unreachable("Unknown 1-byte regclass");
  case 16:
    assert(M68k::XR16RegClass.hasSubClassEq(RC) && "Unknown 2-byte regclass");
    return load ? M68k::MOVM16mp_P : M68k::MOVM16pm_P;
  case 32:
    assert(M68k::XR32RegClass.hasSubClassEq(RC) && "Unknown 4-byte regclass");
    return load ? M68k::MOVM32mp_P : M68k::MOVM32pm_P;
  }
}

unsigned getStoreRegOpcode(unsigned SrcReg, const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI,
                           const M68kSubtarget &STI) {
  return getLoadStoreRegOpcode(SrcReg, RC, TRI, STI, false);
}

unsigned getLoadRegOpcode(unsigned DstReg, const TargetRegisterClass *RC,
                          const TargetRegisterInfo *TRI,
                          const M68kSubtarget &STI) {
  return getLoadStoreRegOpcode(DstReg, RC, TRI, STI, true);
}
} // end anonymous namespace

bool M68kInstrInfo::getStackSlotRange(const TargetRegisterClass *RC,
                                      unsigned SubIdx, unsigned &Size,
                                      unsigned &Offset,
                                      const MachineFunction &MF) const {
  // The slot size must be the maximum size so we can easily use MOVEM.L
  Size = 4;
  Offset = 0;
  return true;
}

void M68kInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {
  const MachineFrameInfo &MFI = MBB.getParent()->getFrameInfo();
  assert(MFI.getObjectSize(FrameIndex) >= TRI->getSpillSize(*RC) &&
         "Stack slot is too small to store");
  (void)MFI;

  unsigned Opc = getStoreRegOpcode(SrcReg, RC, TRI, Subtarget);
  DebugLoc DL = MBB.findDebugLoc(MI);
  // (0,FrameIndex) <- $reg
  M68k::addFrameReference(BuildMI(MBB, MI, DL, get(Opc)), FrameIndex)
      .addReg(SrcReg, getKillRegState(IsKill));
}

void M68kInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register DstReg, int FrameIndex,
                                         const TargetRegisterClass *RC,
                                         const TargetRegisterInfo *TRI,
                                         Register VReg) const {
  const MachineFrameInfo &MFI = MBB.getParent()->getFrameInfo();
  assert(MFI.getObjectSize(FrameIndex) >= TRI->getSpillSize(*RC) &&
         "Stack slot is too small to load");
  (void)MFI;

  unsigned Opc = getLoadRegOpcode(DstReg, RC, TRI, Subtarget);
  DebugLoc DL = MBB.findDebugLoc(MI);
  M68k::addFrameReference(BuildMI(MBB, MI, DL, get(Opc), DstReg), FrameIndex);
}

/// Return a virtual register initialized with the global base register
/// value. Output instructions required to initialize the register in the
/// function entry block, if necessary.
///
/// TODO Move this function to M68kMachineFunctionInfo.
unsigned M68kInstrInfo::getGlobalBaseReg(MachineFunction *MF) const {
  M68kMachineFunctionInfo *MxFI = MF->getInfo<M68kMachineFunctionInfo>();
  unsigned GlobalBaseReg = MxFI->getGlobalBaseReg();
  if (GlobalBaseReg != 0)
    return GlobalBaseReg;

  // Create the register. The code to initialize it is inserted later,
  // by the M68kGlobalBaseReg pass (below).
  //
  // NOTE
  // Normally M68k uses A5 register as global base pointer but this will
  // create unnecessary spill if we use less then 4 registers in code; since A5
  // is callee-save anyway we could try to allocate caller-save first and if
  // lucky get one, otherwise it does not really matter which callee-save to
  // use.
  MachineRegisterInfo &RegInfo = MF->getRegInfo();
  GlobalBaseReg = RegInfo.createVirtualRegister(&M68k::AR32_NOSPRegClass);
  MxFI->setGlobalBaseReg(GlobalBaseReg);
  return GlobalBaseReg;
}

std::pair<unsigned, unsigned>
M68kInstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  return std::make_pair(TF, 0u);
}

ArrayRef<std::pair<unsigned, const char *>>
M68kInstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace M68kII;
  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_ABSOLUTE_ADDRESS, "m68k-absolute"},
      {MO_PC_RELATIVE_ADDRESS, "m68k-pcrel"},
      {MO_GOT, "m68k-got"},
      {MO_GOTOFF, "m68k-gotoff"},
      {MO_GOTPCREL, "m68k-gotpcrel"},
      {MO_PLT, "m68k-plt"},
      {MO_TLSGD, "m68k-tlsgd"},
      {MO_TLSLD, "m68k-tlsld"},
      {MO_TLSLDM, "m68k-tlsldm"},
      {MO_TLSIE, "m68k-tlsie"},
      {MO_TLSLE, "m68k-tlsle"}};
  return ArrayRef(TargetFlags);
}

#undef DEBUG_TYPE
#define DEBUG_TYPE "m68k-create-global-base-reg"

#define PASS_NAME "M68k PIC Global Base Reg Initialization"

namespace {
/// This initializes the PIC global base register
struct M68kGlobalBaseReg : public MachineFunctionPass {
  static char ID;
  M68kGlobalBaseReg() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    const M68kSubtarget &STI = MF.getSubtarget<M68kSubtarget>();
    M68kMachineFunctionInfo *MxFI = MF.getInfo<M68kMachineFunctionInfo>();

    unsigned GlobalBaseReg = MxFI->getGlobalBaseReg();

    // If we didn't need a GlobalBaseReg, don't insert code.
    if (GlobalBaseReg == 0)
      return false;

    // Insert the set of GlobalBaseReg into the first MBB of the function
    MachineBasicBlock &FirstMBB = MF.front();
    MachineBasicBlock::iterator MBBI = FirstMBB.begin();
    DebugLoc DL = FirstMBB.findDebugLoc(MBBI);
    const M68kInstrInfo *TII = STI.getInstrInfo();

    // Generate lea (__GLOBAL_OFFSET_TABLE_,%PC), %A5
    BuildMI(FirstMBB, MBBI, DL, TII->get(M68k::LEA32q), GlobalBaseReg)
        .addExternalSymbol("_GLOBAL_OFFSET_TABLE_", M68kII::MO_GOTPCREL);

    return true;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
char M68kGlobalBaseReg::ID = 0;
} // namespace

INITIALIZE_PASS(M68kGlobalBaseReg, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createM68kGlobalBaseRegPass() {
  return new M68kGlobalBaseReg();
}
