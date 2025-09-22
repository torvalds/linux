//===- lib/CodeGen/MachineOperand.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Methods common to all machine operands.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/ADT/StableHashing.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/CodeGen/MIRFormatter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValueManager.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>

using namespace llvm;

static cl::opt<int>
    PrintRegMaskNumRegs("print-regmask-num-regs",
                        cl::desc("Number of registers to limit to when "
                                 "printing regmask operands in IR dumps. "
                                 "unlimited = -1"),
                        cl::init(32), cl::Hidden);

static const MachineFunction *getMFIfAvailable(const MachineOperand &MO) {
  if (const MachineInstr *MI = MO.getParent())
    if (const MachineBasicBlock *MBB = MI->getParent())
      if (const MachineFunction *MF = MBB->getParent())
        return MF;
  return nullptr;
}

static MachineFunction *getMFIfAvailable(MachineOperand &MO) {
  return const_cast<MachineFunction *>(
      getMFIfAvailable(const_cast<const MachineOperand &>(MO)));
}

unsigned MachineOperand::getOperandNo() const {
  assert(getParent() && "Operand does not belong to any instruction!");
  return getParent()->getOperandNo(this);
}

void MachineOperand::setReg(Register Reg) {
  if (getReg() == Reg)
    return; // No change.

  // Clear the IsRenamable bit to keep it conservatively correct.
  IsRenamable = false;

  // Otherwise, we have to change the register.  If this operand is embedded
  // into a machine function, we need to update the old and new register's
  // use/def lists.
  if (MachineFunction *MF = getMFIfAvailable(*this)) {
    MachineRegisterInfo &MRI = MF->getRegInfo();
    MRI.removeRegOperandFromUseList(this);
    SmallContents.RegNo = Reg;
    MRI.addRegOperandToUseList(this);
    return;
  }

  // Otherwise, just change the register, no problem.  :)
  SmallContents.RegNo = Reg;
}

void MachineOperand::substVirtReg(Register Reg, unsigned SubIdx,
                                  const TargetRegisterInfo &TRI) {
  assert(Reg.isVirtual());
  if (SubIdx && getSubReg())
    SubIdx = TRI.composeSubRegIndices(SubIdx, getSubReg());
  setReg(Reg);
  if (SubIdx)
    setSubReg(SubIdx);
}

void MachineOperand::substPhysReg(MCRegister Reg, const TargetRegisterInfo &TRI) {
  assert(Register::isPhysicalRegister(Reg));
  if (getSubReg()) {
    Reg = TRI.getSubReg(Reg, getSubReg());
    // Note that getSubReg() may return 0 if the sub-register doesn't exist.
    // That won't happen in legal code.
    setSubReg(0);
    if (isDef())
      setIsUndef(false);
  }
  setReg(Reg);
}

/// Change a def to a use, or a use to a def.
void MachineOperand::setIsDef(bool Val) {
  assert(isReg() && "Wrong MachineOperand accessor");
  assert((!Val || !isDebug()) && "Marking a debug operation as def");
  if (IsDef == Val)
    return;
  assert(!IsDeadOrKill && "Changing def/use with dead/kill set not supported");
  // MRI may keep uses and defs in different list positions.
  if (MachineFunction *MF = getMFIfAvailable(*this)) {
    MachineRegisterInfo &MRI = MF->getRegInfo();
    MRI.removeRegOperandFromUseList(this);
    IsDef = Val;
    MRI.addRegOperandToUseList(this);
    return;
  }
  IsDef = Val;
}

bool MachineOperand::isRenamable() const {
  assert(isReg() && "Wrong MachineOperand accessor");
  assert(getReg().isPhysical() &&
         "isRenamable should only be checked on physical registers");
  if (!IsRenamable)
    return false;

  const MachineInstr *MI = getParent();
  if (!MI)
    return true;

  if (isDef())
    return !MI->hasExtraDefRegAllocReq(MachineInstr::IgnoreBundle);

  assert(isUse() && "Reg is not def or use");
  return !MI->hasExtraSrcRegAllocReq(MachineInstr::IgnoreBundle);
}

void MachineOperand::setIsRenamable(bool Val) {
  assert(isReg() && "Wrong MachineOperand accessor");
  assert(getReg().isPhysical() &&
         "setIsRenamable should only be called on physical registers");
  IsRenamable = Val;
}

// If this operand is currently a register operand, and if this is in a
// function, deregister the operand from the register's use/def list.
void MachineOperand::removeRegFromUses() {
  if (!isReg() || !isOnRegUseList())
    return;

  if (MachineFunction *MF = getMFIfAvailable(*this))
    MF->getRegInfo().removeRegOperandFromUseList(this);
}

/// ChangeToImmediate - Replace this operand with a new immediate operand of
/// the specified value.  If an operand is known to be an immediate already,
/// the setImm method should be used.
void MachineOperand::ChangeToImmediate(int64_t ImmVal, unsigned TargetFlags) {
  assert((!isReg() || !isTied()) && "Cannot change a tied operand into an imm");

  removeRegFromUses();

  OpKind = MO_Immediate;
  Contents.ImmVal = ImmVal;
  setTargetFlags(TargetFlags);
}

void MachineOperand::ChangeToFPImmediate(const ConstantFP *FPImm,
                                         unsigned TargetFlags) {
  assert((!isReg() || !isTied()) && "Cannot change a tied operand into an imm");

  removeRegFromUses();

  OpKind = MO_FPImmediate;
  Contents.CFP = FPImm;
  setTargetFlags(TargetFlags);
}

void MachineOperand::ChangeToES(const char *SymName,
                                unsigned TargetFlags) {
  assert((!isReg() || !isTied()) &&
         "Cannot change a tied operand into an external symbol");

  removeRegFromUses();

  OpKind = MO_ExternalSymbol;
  Contents.OffsetedInfo.Val.SymbolName = SymName;
  setOffset(0); // Offset is always 0.
  setTargetFlags(TargetFlags);
}

void MachineOperand::ChangeToGA(const GlobalValue *GV, int64_t Offset,
                                unsigned TargetFlags) {
  assert((!isReg() || !isTied()) &&
         "Cannot change a tied operand into a global address");

  removeRegFromUses();

  OpKind = MO_GlobalAddress;
  Contents.OffsetedInfo.Val.GV = GV;
  setOffset(Offset);
  setTargetFlags(TargetFlags);
}

void MachineOperand::ChangeToBA(const BlockAddress *BA, int64_t Offset,
                                unsigned TargetFlags) {
  assert((!isReg() || !isTied()) &&
         "Cannot change a tied operand into a block address");

  removeRegFromUses();

  OpKind = MO_BlockAddress;
  Contents.OffsetedInfo.Val.BA = BA;
  setOffset(Offset);
  setTargetFlags(TargetFlags);
}

void MachineOperand::ChangeToMCSymbol(MCSymbol *Sym, unsigned TargetFlags) {
  assert((!isReg() || !isTied()) &&
         "Cannot change a tied operand into an MCSymbol");

  removeRegFromUses();

  OpKind = MO_MCSymbol;
  Contents.Sym = Sym;
  setTargetFlags(TargetFlags);
}

void MachineOperand::ChangeToFrameIndex(int Idx, unsigned TargetFlags) {
  assert((!isReg() || !isTied()) &&
         "Cannot change a tied operand into a FrameIndex");

  removeRegFromUses();

  OpKind = MO_FrameIndex;
  setIndex(Idx);
  setTargetFlags(TargetFlags);
}

void MachineOperand::ChangeToTargetIndex(unsigned Idx, int64_t Offset,
                                         unsigned TargetFlags) {
  assert((!isReg() || !isTied()) &&
         "Cannot change a tied operand into a FrameIndex");

  removeRegFromUses();

  OpKind = MO_TargetIndex;
  setIndex(Idx);
  setOffset(Offset);
  setTargetFlags(TargetFlags);
}

void MachineOperand::ChangeToDbgInstrRef(unsigned InstrIdx, unsigned OpIdx,
                                         unsigned TargetFlags) {
  assert((!isReg() || !isTied()) &&
         "Cannot change a tied operand into a DbgInstrRef");

  removeRegFromUses();

  OpKind = MO_DbgInstrRef;
  setInstrRefInstrIndex(InstrIdx);
  setInstrRefOpIndex(OpIdx);
  setTargetFlags(TargetFlags);
}

/// ChangeToRegister - Replace this operand with a new register operand of
/// the specified value.  If an operand is known to be an register already,
/// the setReg method should be used.
void MachineOperand::ChangeToRegister(Register Reg, bool isDef, bool isImp,
                                      bool isKill, bool isDead, bool isUndef,
                                      bool isDebug) {
  MachineRegisterInfo *RegInfo = nullptr;
  if (MachineFunction *MF = getMFIfAvailable(*this))
    RegInfo = &MF->getRegInfo();
  // If this operand is already a register operand, remove it from the
  // register's use/def lists.
  bool WasReg = isReg();
  if (RegInfo && WasReg)
    RegInfo->removeRegOperandFromUseList(this);

  // Ensure debug instructions set debug flag on register uses.
  const MachineInstr *MI = getParent();
  if (!isDef && MI && MI->isDebugInstr())
    isDebug = true;

  // Change this to a register and set the reg#.
  assert(!(isDead && !isDef) && "Dead flag on non-def");
  assert(!(isKill && isDef) && "Kill flag on def");
  OpKind = MO_Register;
  SmallContents.RegNo = Reg;
  SubReg_TargetFlags = 0;
  IsDef = isDef;
  IsImp = isImp;
  IsDeadOrKill = isKill | isDead;
  IsRenamable = false;
  IsUndef = isUndef;
  IsInternalRead = false;
  IsEarlyClobber = false;
  IsDebug = isDebug;
  // Ensure isOnRegUseList() returns false.
  Contents.Reg.Prev = nullptr;
  // Preserve the tie when the operand was already a register.
  if (!WasReg)
    TiedTo = 0;

  // If this operand is embedded in a function, add the operand to the
  // register's use/def list.
  if (RegInfo)
    RegInfo->addRegOperandToUseList(this);
}

/// isIdenticalTo - Return true if this operand is identical to the specified
/// operand. Note that this should stay in sync with the hash_value overload
/// below.
bool MachineOperand::isIdenticalTo(const MachineOperand &Other) const {
  if (getType() != Other.getType() ||
      getTargetFlags() != Other.getTargetFlags())
    return false;

  switch (getType()) {
  case MachineOperand::MO_Register:
    return getReg() == Other.getReg() && isDef() == Other.isDef() &&
           getSubReg() == Other.getSubReg();
  case MachineOperand::MO_Immediate:
    return getImm() == Other.getImm();
  case MachineOperand::MO_CImmediate:
    return getCImm() == Other.getCImm();
  case MachineOperand::MO_FPImmediate:
    return getFPImm() == Other.getFPImm();
  case MachineOperand::MO_MachineBasicBlock:
    return getMBB() == Other.getMBB();
  case MachineOperand::MO_FrameIndex:
    return getIndex() == Other.getIndex();
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_TargetIndex:
    return getIndex() == Other.getIndex() && getOffset() == Other.getOffset();
  case MachineOperand::MO_JumpTableIndex:
    return getIndex() == Other.getIndex();
  case MachineOperand::MO_GlobalAddress:
    return getGlobal() == Other.getGlobal() && getOffset() == Other.getOffset();
  case MachineOperand::MO_ExternalSymbol:
    return strcmp(getSymbolName(), Other.getSymbolName()) == 0 &&
           getOffset() == Other.getOffset();
  case MachineOperand::MO_BlockAddress:
    return getBlockAddress() == Other.getBlockAddress() &&
           getOffset() == Other.getOffset();
  case MachineOperand::MO_RegisterMask:
  case MachineOperand::MO_RegisterLiveOut: {
    // Shallow compare of the two RegMasks
    const uint32_t *RegMask = getRegMask();
    const uint32_t *OtherRegMask = Other.getRegMask();
    if (RegMask == OtherRegMask)
      return true;

    if (const MachineFunction *MF = getMFIfAvailable(*this)) {
      const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
      unsigned RegMaskSize = MachineOperand::getRegMaskSize(TRI->getNumRegs());
      // Deep compare of the two RegMasks
      return std::equal(RegMask, RegMask + RegMaskSize, OtherRegMask);
    }
    // We don't know the size of the RegMask, so we can't deep compare the two
    // reg masks.
    return false;
  }
  case MachineOperand::MO_MCSymbol:
    return getMCSymbol() == Other.getMCSymbol();
  case MachineOperand::MO_DbgInstrRef:
    return getInstrRefInstrIndex() == Other.getInstrRefInstrIndex() &&
           getInstrRefOpIndex() == Other.getInstrRefOpIndex();
  case MachineOperand::MO_CFIIndex:
    return getCFIIndex() == Other.getCFIIndex();
  case MachineOperand::MO_Metadata:
    return getMetadata() == Other.getMetadata();
  case MachineOperand::MO_IntrinsicID:
    return getIntrinsicID() == Other.getIntrinsicID();
  case MachineOperand::MO_Predicate:
    return getPredicate() == Other.getPredicate();
  case MachineOperand::MO_ShuffleMask:
    return getShuffleMask() == Other.getShuffleMask();
  }
  llvm_unreachable("Invalid machine operand type");
}

// Note: this must stay exactly in sync with isIdenticalTo above.
hash_code llvm::hash_value(const MachineOperand &MO) {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    // Register operands don't have target flags.
    return hash_combine(MO.getType(), (unsigned)MO.getReg(), MO.getSubReg(), MO.isDef());
  case MachineOperand::MO_Immediate:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getImm());
  case MachineOperand::MO_CImmediate:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getCImm());
  case MachineOperand::MO_FPImmediate:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getFPImm());
  case MachineOperand::MO_MachineBasicBlock:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getMBB());
  case MachineOperand::MO_FrameIndex:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getIndex());
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_TargetIndex:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getIndex(),
                        MO.getOffset());
  case MachineOperand::MO_JumpTableIndex:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getIndex());
  case MachineOperand::MO_ExternalSymbol:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getOffset(),
                        StringRef(MO.getSymbolName()));
  case MachineOperand::MO_GlobalAddress:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getGlobal(),
                        MO.getOffset());
  case MachineOperand::MO_BlockAddress:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getBlockAddress(),
                        MO.getOffset());
  case MachineOperand::MO_RegisterMask:
  case MachineOperand::MO_RegisterLiveOut: {
    if (const MachineFunction *MF = getMFIfAvailable(MO)) {
      const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
      unsigned RegMaskSize = MachineOperand::getRegMaskSize(TRI->getNumRegs());
      const uint32_t *RegMask = MO.getRegMask();
      std::vector<stable_hash> RegMaskHashes(RegMask, RegMask + RegMaskSize);
      return hash_combine(MO.getType(), MO.getTargetFlags(),
                          stable_hash_combine_array(RegMaskHashes.data(),
                                                    RegMaskHashes.size()));
    }

    assert(0 && "MachineOperand not associated with any MachineFunction");
    return hash_combine(MO.getType(), MO.getTargetFlags());
  }
  case MachineOperand::MO_Metadata:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getMetadata());
  case MachineOperand::MO_MCSymbol:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getMCSymbol());
  case MachineOperand::MO_DbgInstrRef:
    return hash_combine(MO.getType(), MO.getTargetFlags(),
                        MO.getInstrRefInstrIndex(), MO.getInstrRefOpIndex());
  case MachineOperand::MO_CFIIndex:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getCFIIndex());
  case MachineOperand::MO_IntrinsicID:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getIntrinsicID());
  case MachineOperand::MO_Predicate:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getPredicate());
  case MachineOperand::MO_ShuffleMask:
    return hash_combine(MO.getType(), MO.getTargetFlags(), MO.getShuffleMask());
  }
  llvm_unreachable("Invalid machine operand type");
}

// Try to crawl up to the machine function and get TRI and IntrinsicInfo from
// it.
static void tryToGetTargetInfo(const MachineOperand &MO,
                               const TargetRegisterInfo *&TRI,
                               const TargetIntrinsicInfo *&IntrinsicInfo) {
  if (const MachineFunction *MF = getMFIfAvailable(MO)) {
    TRI = MF->getSubtarget().getRegisterInfo();
    IntrinsicInfo = MF->getTarget().getIntrinsicInfo();
  }
}

static const char *getTargetIndexName(const MachineFunction &MF, int Index) {
  const auto *TII = MF.getSubtarget().getInstrInfo();
  assert(TII && "expected instruction info");
  auto Indices = TII->getSerializableTargetIndices();
  auto Found = find_if(Indices, [&](const std::pair<int, const char *> &I) {
    return I.first == Index;
  });
  if (Found != Indices.end())
    return Found->second;
  return nullptr;
}

const char *MachineOperand::getTargetIndexName() const {
  const MachineFunction *MF = getMFIfAvailable(*this);
  return MF ? ::getTargetIndexName(*MF, this->getIndex()) : nullptr;
}

static const char *getTargetFlagName(const TargetInstrInfo *TII, unsigned TF) {
  auto Flags = TII->getSerializableDirectMachineOperandTargetFlags();
  for (const auto &I : Flags) {
    if (I.first == TF) {
      return I.second;
    }
  }
  return nullptr;
}

static void printCFIRegister(unsigned DwarfReg, raw_ostream &OS,
                             const TargetRegisterInfo *TRI) {
  if (!TRI) {
    OS << "%dwarfreg." << DwarfReg;
    return;
  }

  if (std::optional<unsigned> Reg = TRI->getLLVMRegNum(DwarfReg, true))
    OS << printReg(*Reg, TRI);
  else
    OS << "<badreg>";
}

static void printIRBlockReference(raw_ostream &OS, const BasicBlock &BB,
                                  ModuleSlotTracker &MST) {
  OS << "%ir-block.";
  if (BB.hasName()) {
    printLLVMNameWithoutPrefix(OS, BB.getName());
    return;
  }
  std::optional<int> Slot;
  if (const Function *F = BB.getParent()) {
    if (F == MST.getCurrentFunction()) {
      Slot = MST.getLocalSlot(&BB);
    } else if (const Module *M = F->getParent()) {
      ModuleSlotTracker CustomMST(M, /*ShouldInitializeAllMetadata=*/false);
      CustomMST.incorporateFunction(*F);
      Slot = CustomMST.getLocalSlot(&BB);
    }
  }
  if (Slot)
    MachineOperand::printIRSlotNumber(OS, *Slot);
  else
    OS << "<unknown>";
}

static void printSyncScope(raw_ostream &OS, const LLVMContext &Context,
                           SyncScope::ID SSID,
                           SmallVectorImpl<StringRef> &SSNs) {
  switch (SSID) {
  case SyncScope::System:
    break;
  default:
    if (SSNs.empty())
      Context.getSyncScopeNames(SSNs);

    OS << "syncscope(\"";
    printEscapedString(SSNs[SSID], OS);
    OS << "\") ";
    break;
  }
}

static const char *getTargetMMOFlagName(const TargetInstrInfo &TII,
                                        unsigned TMMOFlag) {
  auto Flags = TII.getSerializableMachineMemOperandTargetFlags();
  for (const auto &I : Flags) {
    if (I.first == TMMOFlag) {
      return I.second;
    }
  }
  return nullptr;
}

static void printFrameIndex(raw_ostream& OS, int FrameIndex, bool IsFixed,
                            const MachineFrameInfo *MFI) {
  StringRef Name;
  if (MFI) {
    IsFixed = MFI->isFixedObjectIndex(FrameIndex);
    if (const AllocaInst *Alloca = MFI->getObjectAllocation(FrameIndex))
      if (Alloca->hasName())
        Name = Alloca->getName();
    if (IsFixed)
      FrameIndex -= MFI->getObjectIndexBegin();
  }
  MachineOperand::printStackObjectReference(OS, FrameIndex, IsFixed, Name);
}

void MachineOperand::printSubRegIdx(raw_ostream &OS, uint64_t Index,
                                    const TargetRegisterInfo *TRI) {
  OS << "%subreg.";
  if (TRI && Index != 0 && Index < TRI->getNumSubRegIndices())
    OS << TRI->getSubRegIndexName(Index);
  else
    OS << Index;
}

void MachineOperand::printTargetFlags(raw_ostream &OS,
                                      const MachineOperand &Op) {
  if (!Op.getTargetFlags())
    return;
  const MachineFunction *MF = getMFIfAvailable(Op);
  if (!MF)
    return;

  const auto *TII = MF->getSubtarget().getInstrInfo();
  assert(TII && "expected instruction info");
  auto Flags = TII->decomposeMachineOperandsTargetFlags(Op.getTargetFlags());
  OS << "target-flags(";
  const bool HasDirectFlags = Flags.first;
  const bool HasBitmaskFlags = Flags.second;
  if (!HasDirectFlags && !HasBitmaskFlags) {
    OS << "<unknown>) ";
    return;
  }
  if (HasDirectFlags) {
    if (const auto *Name = getTargetFlagName(TII, Flags.first))
      OS << Name;
    else
      OS << "<unknown target flag>";
  }
  if (!HasBitmaskFlags) {
    OS << ") ";
    return;
  }
  bool IsCommaNeeded = HasDirectFlags;
  unsigned BitMask = Flags.second;
  auto BitMasks = TII->getSerializableBitmaskMachineOperandTargetFlags();
  for (const auto &Mask : BitMasks) {
    // Check if the flag's bitmask has the bits of the current mask set.
    if ((BitMask & Mask.first) == Mask.first) {
      if (IsCommaNeeded)
        OS << ", ";
      IsCommaNeeded = true;
      OS << Mask.second;
      // Clear the bits which were serialized from the flag's bitmask.
      BitMask &= ~(Mask.first);
    }
  }
  if (BitMask) {
    // When the resulting flag's bitmask isn't zero, we know that we didn't
    // serialize all of the bit flags.
    if (IsCommaNeeded)
      OS << ", ";
    OS << "<unknown bitmask target flag>";
  }
  OS << ") ";
}

void MachineOperand::printSymbol(raw_ostream &OS, MCSymbol &Sym) {
  OS << "<mcsymbol " << Sym << ">";
}

void MachineOperand::printStackObjectReference(raw_ostream &OS,
                                               unsigned FrameIndex,
                                               bool IsFixed, StringRef Name) {
  if (IsFixed) {
    OS << "%fixed-stack." << FrameIndex;
    return;
  }

  OS << "%stack." << FrameIndex;
  if (!Name.empty())
    OS << '.' << Name;
}

void MachineOperand::printOperandOffset(raw_ostream &OS, int64_t Offset) {
  if (Offset == 0)
    return;
  if (Offset < 0) {
    OS << " - " << -Offset;
    return;
  }
  OS << " + " << Offset;
}

void MachineOperand::printIRSlotNumber(raw_ostream &OS, int Slot) {
  if (Slot == -1)
    OS << "<badref>";
  else
    OS << Slot;
}

static void printCFI(raw_ostream &OS, const MCCFIInstruction &CFI,
                     const TargetRegisterInfo *TRI) {
  switch (CFI.getOperation()) {
  case MCCFIInstruction::OpSameValue:
    OS << "same_value ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    printCFIRegister(CFI.getRegister(), OS, TRI);
    break;
  case MCCFIInstruction::OpRememberState:
    OS << "remember_state ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    break;
  case MCCFIInstruction::OpRestoreState:
    OS << "restore_state ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    break;
  case MCCFIInstruction::OpOffset:
    OS << "offset ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    printCFIRegister(CFI.getRegister(), OS, TRI);
    OS << ", " << CFI.getOffset();
    break;
  case MCCFIInstruction::OpDefCfaRegister:
    OS << "def_cfa_register ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    printCFIRegister(CFI.getRegister(), OS, TRI);
    break;
  case MCCFIInstruction::OpDefCfaOffset:
    OS << "def_cfa_offset ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    OS << CFI.getOffset();
    break;
  case MCCFIInstruction::OpDefCfa:
    OS << "def_cfa ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    printCFIRegister(CFI.getRegister(), OS, TRI);
    OS << ", " << CFI.getOffset();
    break;
  case MCCFIInstruction::OpLLVMDefAspaceCfa:
    OS << "llvm_def_aspace_cfa ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    printCFIRegister(CFI.getRegister(), OS, TRI);
    OS << ", " << CFI.getOffset();
    OS << ", " << CFI.getAddressSpace();
    break;
  case MCCFIInstruction::OpRelOffset:
    OS << "rel_offset ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    printCFIRegister(CFI.getRegister(), OS, TRI);
    OS << ", " << CFI.getOffset();
    break;
  case MCCFIInstruction::OpAdjustCfaOffset:
    OS << "adjust_cfa_offset ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    OS << CFI.getOffset();
    break;
  case MCCFIInstruction::OpRestore:
    OS << "restore ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    printCFIRegister(CFI.getRegister(), OS, TRI);
    break;
  case MCCFIInstruction::OpEscape: {
    OS << "escape ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    if (!CFI.getValues().empty()) {
      size_t e = CFI.getValues().size() - 1;
      for (size_t i = 0; i < e; ++i)
        OS << format("0x%02x", uint8_t(CFI.getValues()[i])) << ", ";
      OS << format("0x%02x", uint8_t(CFI.getValues()[e]));
    }
    break;
  }
  case MCCFIInstruction::OpUndefined:
    OS << "undefined ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    printCFIRegister(CFI.getRegister(), OS, TRI);
    break;
  case MCCFIInstruction::OpRegister:
    OS << "register ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    printCFIRegister(CFI.getRegister(), OS, TRI);
    OS << ", ";
    printCFIRegister(CFI.getRegister2(), OS, TRI);
    break;
  case MCCFIInstruction::OpWindowSave:
    OS << "window_save ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    break;
  case MCCFIInstruction::OpNegateRAState:
    OS << "negate_ra_sign_state ";
    if (MCSymbol *Label = CFI.getLabel())
      MachineOperand::printSymbol(OS, *Label);
    break;
  default:
    // TODO: Print the other CFI Operations.
    OS << "<unserializable cfi directive>";
    break;
  }
}

void MachineOperand::print(raw_ostream &OS, const TargetRegisterInfo *TRI,
                           const TargetIntrinsicInfo *IntrinsicInfo) const {
  print(OS, LLT{}, TRI, IntrinsicInfo);
}

void MachineOperand::print(raw_ostream &OS, LLT TypeToPrint,
                           const TargetRegisterInfo *TRI,
                           const TargetIntrinsicInfo *IntrinsicInfo) const {
  tryToGetTargetInfo(*this, TRI, IntrinsicInfo);
  ModuleSlotTracker DummyMST(nullptr);
  print(OS, DummyMST, TypeToPrint, std::nullopt, /*PrintDef=*/false,
        /*IsStandalone=*/true,
        /*ShouldPrintRegisterTies=*/true,
        /*TiedOperandIdx=*/0, TRI, IntrinsicInfo);
}

void MachineOperand::print(raw_ostream &OS, ModuleSlotTracker &MST,
                           LLT TypeToPrint, std::optional<unsigned> OpIdx,
                           bool PrintDef, bool IsStandalone,
                           bool ShouldPrintRegisterTies,
                           unsigned TiedOperandIdx,
                           const TargetRegisterInfo *TRI,
                           const TargetIntrinsicInfo *IntrinsicInfo) const {
  printTargetFlags(OS, *this);
  switch (getType()) {
  case MachineOperand::MO_Register: {
    Register Reg = getReg();
    if (isImplicit())
      OS << (isDef() ? "implicit-def " : "implicit ");
    else if (PrintDef && isDef())
      // Print the 'def' flag only when the operand is defined after '='.
      OS << "def ";
    if (isInternalRead())
      OS << "internal ";
    if (isDead())
      OS << "dead ";
    if (isKill())
      OS << "killed ";
    if (isUndef())
      OS << "undef ";
    if (isEarlyClobber())
      OS << "early-clobber ";
    if (getReg().isPhysical() && isRenamable())
      OS << "renamable ";
    // isDebug() is exactly true for register operands of a DBG_VALUE. So we
    // simply infer it when parsing and do not need to print it.

    const MachineRegisterInfo *MRI = nullptr;
    if (Reg.isVirtual()) {
      if (const MachineFunction *MF = getMFIfAvailable(*this)) {
        MRI = &MF->getRegInfo();
      }
    }

    OS << printReg(Reg, TRI, 0, MRI);
    // Print the sub register.
    if (unsigned SubReg = getSubReg()) {
      if (TRI)
        OS << '.' << TRI->getSubRegIndexName(SubReg);
      else
        OS << ".subreg" << SubReg;
    }
    // Print the register class / bank.
    if (Reg.isVirtual()) {
      if (const MachineFunction *MF = getMFIfAvailable(*this)) {
        const MachineRegisterInfo &MRI = MF->getRegInfo();
        if (IsStandalone || !PrintDef || MRI.def_empty(Reg)) {
          OS << ':';
          OS << printRegClassOrBank(Reg, MRI, TRI);
        }
      }
    }
    // Print ties.
    if (ShouldPrintRegisterTies && isTied() && !isDef())
      OS << "(tied-def " << TiedOperandIdx << ")";
    // Print types.
    if (TypeToPrint.isValid())
      OS << '(' << TypeToPrint << ')';
    break;
  }
  case MachineOperand::MO_Immediate: {
    const MIRFormatter *Formatter = nullptr;
    if (const MachineFunction *MF = getMFIfAvailable(*this)) {
      const auto *TII = MF->getSubtarget().getInstrInfo();
      assert(TII && "expected instruction info");
      Formatter = TII->getMIRFormatter();
    }
    if (Formatter)
      Formatter->printImm(OS, *getParent(), OpIdx, getImm());
    else
      OS << getImm();
    break;
  }
  case MachineOperand::MO_CImmediate:
    getCImm()->printAsOperand(OS, /*PrintType=*/true, MST);
    break;
  case MachineOperand::MO_FPImmediate:
    getFPImm()->printAsOperand(OS, /*PrintType=*/true, MST);
    break;
  case MachineOperand::MO_MachineBasicBlock:
    OS << printMBBReference(*getMBB());
    break;
  case MachineOperand::MO_FrameIndex: {
    int FrameIndex = getIndex();
    bool IsFixed = false;
    const MachineFrameInfo *MFI = nullptr;
    if (const MachineFunction *MF = getMFIfAvailable(*this))
      MFI = &MF->getFrameInfo();
    printFrameIndex(OS, FrameIndex, IsFixed, MFI);
    break;
  }
  case MachineOperand::MO_ConstantPoolIndex:
    OS << "%const." << getIndex();
    printOperandOffset(OS, getOffset());
    break;
  case MachineOperand::MO_TargetIndex: {
    OS << "target-index(";
    const char *Name = "<unknown>";
    if (const MachineFunction *MF = getMFIfAvailable(*this))
      if (const auto *TargetIndexName = ::getTargetIndexName(*MF, getIndex()))
        Name = TargetIndexName;
    OS << Name << ')';
    printOperandOffset(OS, getOffset());
    break;
  }
  case MachineOperand::MO_JumpTableIndex:
    OS << printJumpTableEntryReference(getIndex());
    break;
  case MachineOperand::MO_GlobalAddress:
    getGlobal()->printAsOperand(OS, /*PrintType=*/false, MST);
    printOperandOffset(OS, getOffset());
    break;
  case MachineOperand::MO_ExternalSymbol: {
    StringRef Name = getSymbolName();
    OS << '&';
    if (Name.empty()) {
      OS << "\"\"";
    } else {
      printLLVMNameWithoutPrefix(OS, Name);
    }
    printOperandOffset(OS, getOffset());
    break;
  }
  case MachineOperand::MO_BlockAddress: {
    OS << "blockaddress(";
    getBlockAddress()->getFunction()->printAsOperand(OS, /*PrintType=*/false,
                                                     MST);
    OS << ", ";
    printIRBlockReference(OS, *getBlockAddress()->getBasicBlock(), MST);
    OS << ')';
    MachineOperand::printOperandOffset(OS, getOffset());
    break;
  }
  case MachineOperand::MO_RegisterMask: {
    OS << "<regmask";
    if (TRI) {
      unsigned NumRegsInMask = 0;
      unsigned NumRegsEmitted = 0;
      for (unsigned i = 0; i < TRI->getNumRegs(); ++i) {
        unsigned MaskWord = i / 32;
        unsigned MaskBit = i % 32;
        if (getRegMask()[MaskWord] & (1 << MaskBit)) {
          if (PrintRegMaskNumRegs < 0 ||
              NumRegsEmitted <= static_cast<unsigned>(PrintRegMaskNumRegs)) {
            OS << " " << printReg(i, TRI);
            NumRegsEmitted++;
          }
          NumRegsInMask++;
        }
      }
      if (NumRegsEmitted != NumRegsInMask)
        OS << " and " << (NumRegsInMask - NumRegsEmitted) << " more...";
    } else {
      OS << " ...";
    }
    OS << ">";
    break;
  }
  case MachineOperand::MO_RegisterLiveOut: {
    const uint32_t *RegMask = getRegLiveOut();
    OS << "liveout(";
    if (!TRI) {
      OS << "<unknown>";
    } else {
      bool IsCommaNeeded = false;
      for (unsigned Reg = 0, E = TRI->getNumRegs(); Reg < E; ++Reg) {
        if (RegMask[Reg / 32] & (1U << (Reg % 32))) {
          if (IsCommaNeeded)
            OS << ", ";
          OS << printReg(Reg, TRI);
          IsCommaNeeded = true;
        }
      }
    }
    OS << ")";
    break;
  }
  case MachineOperand::MO_Metadata:
    getMetadata()->printAsOperand(OS, MST);
    break;
  case MachineOperand::MO_MCSymbol:
    printSymbol(OS, *getMCSymbol());
    break;
  case MachineOperand::MO_DbgInstrRef: {
    OS << "dbg-instr-ref(" << getInstrRefInstrIndex() << ", "
       << getInstrRefOpIndex() << ')';
    break;
  }
  case MachineOperand::MO_CFIIndex: {
    if (const MachineFunction *MF = getMFIfAvailable(*this))
      printCFI(OS, MF->getFrameInstructions()[getCFIIndex()], TRI);
    else
      OS << "<cfi directive>";
    break;
  }
  case MachineOperand::MO_IntrinsicID: {
    Intrinsic::ID ID = getIntrinsicID();
    if (ID < Intrinsic::num_intrinsics)
      OS << "intrinsic(@" << Intrinsic::getBaseName(ID) << ')';
    else if (IntrinsicInfo)
      OS << "intrinsic(@" << IntrinsicInfo->getName(ID) << ')';
    else
      OS << "intrinsic(" << ID << ')';
    break;
  }
  case MachineOperand::MO_Predicate: {
    auto Pred = static_cast<CmpInst::Predicate>(getPredicate());
    OS << (CmpInst::isIntPredicate(Pred) ? "int" : "float") << "pred("
       << Pred << ')';
    break;
  }
  case MachineOperand::MO_ShuffleMask:
    OS << "shufflemask(";
    ArrayRef<int> Mask = getShuffleMask();
    StringRef Separator;
    for (int Elt : Mask) {
      if (Elt == -1)
        OS << Separator << "undef";
      else
        OS << Separator << Elt;
      Separator = ", ";
    }

    OS << ')';
    break;
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineOperand::dump() const { dbgs() << *this << '\n'; }
#endif

//===----------------------------------------------------------------------===//
// MachineMemOperand Implementation
//===----------------------------------------------------------------------===//

/// getAddrSpace - Return the LLVM IR address space number that this pointer
/// points into.
unsigned MachinePointerInfo::getAddrSpace() const { return AddrSpace; }

/// isDereferenceable - Return true if V is always dereferenceable for
/// Offset + Size byte.
bool MachinePointerInfo::isDereferenceable(unsigned Size, LLVMContext &C,
                                           const DataLayout &DL) const {
  if (!isa<const Value *>(V))
    return false;

  const Value *BasePtr = cast<const Value *>(V);
  if (BasePtr == nullptr)
    return false;

  return isDereferenceableAndAlignedPointer(
      BasePtr, Align(1), APInt(DL.getPointerSizeInBits(), Offset + Size), DL);
}

/// getConstantPool - Return a MachinePointerInfo record that refers to the
/// constant pool.
MachinePointerInfo MachinePointerInfo::getConstantPool(MachineFunction &MF) {
  return MachinePointerInfo(MF.getPSVManager().getConstantPool());
}

/// getFixedStack - Return a MachinePointerInfo record that refers to the
/// the specified FrameIndex.
MachinePointerInfo MachinePointerInfo::getFixedStack(MachineFunction &MF,
                                                     int FI, int64_t Offset) {
  return MachinePointerInfo(MF.getPSVManager().getFixedStack(FI), Offset);
}

MachinePointerInfo MachinePointerInfo::getJumpTable(MachineFunction &MF) {
  return MachinePointerInfo(MF.getPSVManager().getJumpTable());
}

MachinePointerInfo MachinePointerInfo::getGOT(MachineFunction &MF) {
  return MachinePointerInfo(MF.getPSVManager().getGOT());
}

MachinePointerInfo MachinePointerInfo::getStack(MachineFunction &MF,
                                                int64_t Offset, uint8_t ID) {
  return MachinePointerInfo(MF.getPSVManager().getStack(), Offset, ID);
}

MachinePointerInfo MachinePointerInfo::getUnknownStack(MachineFunction &MF) {
  return MachinePointerInfo(MF.getDataLayout().getAllocaAddrSpace());
}

MachineMemOperand::MachineMemOperand(MachinePointerInfo ptrinfo, Flags f,
                                     LLT type, Align a, const AAMDNodes &AAInfo,
                                     const MDNode *Ranges, SyncScope::ID SSID,
                                     AtomicOrdering Ordering,
                                     AtomicOrdering FailureOrdering)
    : PtrInfo(ptrinfo), MemoryType(type), FlagVals(f), BaseAlign(a),
      AAInfo(AAInfo), Ranges(Ranges) {
  assert((PtrInfo.V.isNull() || isa<const PseudoSourceValue *>(PtrInfo.V) ||
          isa<PointerType>(cast<const Value *>(PtrInfo.V)->getType())) &&
         "invalid pointer value");
  assert((isLoad() || isStore()) && "Not a load/store!");

  AtomicInfo.SSID = static_cast<unsigned>(SSID);
  assert(getSyncScopeID() == SSID && "Value truncated");
  AtomicInfo.Ordering = static_cast<unsigned>(Ordering);
  assert(getSuccessOrdering() == Ordering && "Value truncated");
  AtomicInfo.FailureOrdering = static_cast<unsigned>(FailureOrdering);
  assert(getFailureOrdering() == FailureOrdering && "Value truncated");
}

MachineMemOperand::MachineMemOperand(MachinePointerInfo ptrinfo, Flags F,
                                     LocationSize TS, Align BaseAlignment,
                                     const AAMDNodes &AAInfo,
                                     const MDNode *Ranges, SyncScope::ID SSID,
                                     AtomicOrdering Ordering,
                                     AtomicOrdering FailureOrdering)
    : MachineMemOperand(
          ptrinfo, F,
          !TS.hasValue() ? LLT()
          : TS.isScalable()
              ? LLT::scalable_vector(1, 8 * TS.getValue().getKnownMinValue())
              : LLT::scalar(8 * TS.getValue().getKnownMinValue()),
          BaseAlignment, AAInfo, Ranges, SSID, Ordering, FailureOrdering) {}

void MachineMemOperand::refineAlignment(const MachineMemOperand *MMO) {
  // The Value and Offset may differ due to CSE. But the flags and size
  // should be the same.
  assert(MMO->getFlags() == getFlags() && "Flags mismatch!");
  assert((!MMO->getSize().hasValue() || !getSize().hasValue() ||
          MMO->getSize() == getSize()) &&
         "Size mismatch!");
  if (MMO->getBaseAlign() >= getBaseAlign()) {
    // Update the alignment value.
    BaseAlign = MMO->getBaseAlign();
    // Also update the base and offset, because the new alignment may
    // not be applicable with the old ones.
    PtrInfo = MMO->PtrInfo;
  }
}

/// getAlign - Return the minimum known alignment in bytes of the
/// actual memory reference.
Align MachineMemOperand::getAlign() const {
  return commonAlignment(getBaseAlign(), getOffset());
}

void MachineMemOperand::print(raw_ostream &OS, ModuleSlotTracker &MST,
                              SmallVectorImpl<StringRef> &SSNs,
                              const LLVMContext &Context,
                              const MachineFrameInfo *MFI,
                              const TargetInstrInfo *TII) const {
  OS << '(';
  if (isVolatile())
    OS << "volatile ";
  if (isNonTemporal())
    OS << "non-temporal ";
  if (isDereferenceable())
    OS << "dereferenceable ";
  if (isInvariant())
    OS << "invariant ";
  if (TII) {
    if (getFlags() & MachineMemOperand::MOTargetFlag1)
      OS << '"' << getTargetMMOFlagName(*TII, MachineMemOperand::MOTargetFlag1)
         << "\" ";
    if (getFlags() & MachineMemOperand::MOTargetFlag2)
      OS << '"' << getTargetMMOFlagName(*TII, MachineMemOperand::MOTargetFlag2)
         << "\" ";
    if (getFlags() & MachineMemOperand::MOTargetFlag3)
      OS << '"' << getTargetMMOFlagName(*TII, MachineMemOperand::MOTargetFlag3)
         << "\" ";
  } else {
    if (getFlags() & MachineMemOperand::MOTargetFlag1)
      OS << "\"MOTargetFlag1\" ";
    if (getFlags() & MachineMemOperand::MOTargetFlag2)
      OS << "\"MOTargetFlag2\" ";
    if (getFlags() & MachineMemOperand::MOTargetFlag3)
      OS << "\"MOTargetFlag3\" ";
  }

  assert((isLoad() || isStore()) &&
         "machine memory operand must be a load or store (or both)");
  if (isLoad())
    OS << "load ";
  if (isStore())
    OS << "store ";

  printSyncScope(OS, Context, getSyncScopeID(), SSNs);

  if (getSuccessOrdering() != AtomicOrdering::NotAtomic)
    OS << toIRString(getSuccessOrdering()) << ' ';
  if (getFailureOrdering() != AtomicOrdering::NotAtomic)
    OS << toIRString(getFailureOrdering()) << ' ';

  if (getMemoryType().isValid())
    OS << '(' << getMemoryType() << ')';
  else
    OS << "unknown-size";

  if (const Value *Val = getValue()) {
    OS << ((isLoad() && isStore()) ? " on " : isLoad() ? " from " : " into ");
    MIRFormatter::printIRValue(OS, *Val, MST);
  } else if (const PseudoSourceValue *PVal = getPseudoValue()) {
    OS << ((isLoad() && isStore()) ? " on " : isLoad() ? " from " : " into ");
    assert(PVal && "Expected a pseudo source value");
    switch (PVal->kind()) {
    case PseudoSourceValue::Stack:
      OS << "stack";
      break;
    case PseudoSourceValue::GOT:
      OS << "got";
      break;
    case PseudoSourceValue::JumpTable:
      OS << "jump-table";
      break;
    case PseudoSourceValue::ConstantPool:
      OS << "constant-pool";
      break;
    case PseudoSourceValue::FixedStack: {
      int FrameIndex = cast<FixedStackPseudoSourceValue>(PVal)->getFrameIndex();
      bool IsFixed = true;
      printFrameIndex(OS, FrameIndex, IsFixed, MFI);
      break;
    }
    case PseudoSourceValue::GlobalValueCallEntry:
      OS << "call-entry ";
      cast<GlobalValuePseudoSourceValue>(PVal)->getValue()->printAsOperand(
          OS, /*PrintType=*/false, MST);
      break;
    case PseudoSourceValue::ExternalSymbolCallEntry:
      OS << "call-entry &";
      printLLVMNameWithoutPrefix(
          OS, cast<ExternalSymbolPseudoSourceValue>(PVal)->getSymbol());
      break;
    default: {
      const MIRFormatter *Formatter = TII->getMIRFormatter();
      // FIXME: This is not necessarily the correct MIR serialization format for
      // a custom pseudo source value, but at least it allows
      // MIR printing to work on a target with custom pseudo source
      // values.
      OS << "custom \"";
      Formatter->printCustomPseudoSourceValue(OS, MST, *PVal);
      OS << '\"';
      break;
    }
    }
  } else if (getOpaqueValue() == nullptr && getOffset() != 0) {
    OS << ((isLoad() && isStore()) ? " on "
           : isLoad()              ? " from "
                                   : " into ")
       << "unknown-address";
  }
  MachineOperand::printOperandOffset(OS, getOffset());
  if (!getSize().hasValue() ||
      getAlign() != getSize().getValue().getKnownMinValue())
    OS << ", align " << getAlign().value();
  if (getAlign() != getBaseAlign())
    OS << ", basealign " << getBaseAlign().value();
  auto AAInfo = getAAInfo();
  if (AAInfo.TBAA) {
    OS << ", !tbaa ";
    AAInfo.TBAA->printAsOperand(OS, MST);
  }
  if (AAInfo.Scope) {
    OS << ", !alias.scope ";
    AAInfo.Scope->printAsOperand(OS, MST);
  }
  if (AAInfo.NoAlias) {
    OS << ", !noalias ";
    AAInfo.NoAlias->printAsOperand(OS, MST);
  }
  if (getRanges()) {
    OS << ", !range ";
    getRanges()->printAsOperand(OS, MST);
  }
  // FIXME: Implement addrspace printing/parsing in MIR.
  // For now, print this even though parsing it is not available in MIR.
  if (unsigned AS = getAddrSpace())
    OS << ", addrspace " << AS;

  OS << ')';
}
