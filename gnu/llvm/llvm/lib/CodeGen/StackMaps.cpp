//===- StackMaps.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/StackMaps.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "stackmaps"

static cl::opt<int> StackMapVersion(
    "stackmap-version", cl::init(3), cl::Hidden,
    cl::desc("Specify the stackmap encoding version (default = 3)"));

const char *StackMaps::WSMP = "Stack Maps: ";

static uint64_t getConstMetaVal(const MachineInstr &MI, unsigned Idx) {
  assert(MI.getOperand(Idx).isImm() &&
         MI.getOperand(Idx).getImm() == StackMaps::ConstantOp);
  const auto &MO = MI.getOperand(Idx + 1);
  assert(MO.isImm());
  return MO.getImm();
}

StackMapOpers::StackMapOpers(const MachineInstr *MI)
  : MI(MI) {
  assert(getVarIdx() <= MI->getNumOperands() &&
         "invalid stackmap definition");
}

PatchPointOpers::PatchPointOpers(const MachineInstr *MI)
    : MI(MI), HasDef(MI->getOperand(0).isReg() && MI->getOperand(0).isDef() &&
                     !MI->getOperand(0).isImplicit()) {
#ifndef NDEBUG
  unsigned CheckStartIdx = 0, e = MI->getNumOperands();
  while (CheckStartIdx < e && MI->getOperand(CheckStartIdx).isReg() &&
         MI->getOperand(CheckStartIdx).isDef() &&
         !MI->getOperand(CheckStartIdx).isImplicit())
    ++CheckStartIdx;

  assert(getMetaIdx() == CheckStartIdx &&
         "Unexpected additional definition in Patchpoint intrinsic.");
#endif
}

unsigned PatchPointOpers::getNextScratchIdx(unsigned StartIdx) const {
  if (!StartIdx)
    StartIdx = getVarIdx();

  // Find the next scratch register (implicit def and early clobber)
  unsigned ScratchIdx = StartIdx, e = MI->getNumOperands();
  while (ScratchIdx < e &&
         !(MI->getOperand(ScratchIdx).isReg() &&
           MI->getOperand(ScratchIdx).isDef() &&
           MI->getOperand(ScratchIdx).isImplicit() &&
           MI->getOperand(ScratchIdx).isEarlyClobber()))
    ++ScratchIdx;

  assert(ScratchIdx != e && "No scratch register available");
  return ScratchIdx;
}

unsigned StatepointOpers::getNumGcMapEntriesIdx() {
  // Take index of num of allocas and skip all allocas records.
  unsigned CurIdx = getNumAllocaIdx();
  unsigned NumAllocas = getConstMetaVal(*MI, CurIdx - 1);
  CurIdx++;
  while (NumAllocas--)
    CurIdx = StackMaps::getNextMetaArgIdx(MI, CurIdx);
  return CurIdx + 1; // skip <StackMaps::ConstantOp>
}

unsigned StatepointOpers::getNumAllocaIdx() {
  // Take index of num of gc ptrs and skip all gc ptr records.
  unsigned CurIdx = getNumGCPtrIdx();
  unsigned NumGCPtrs = getConstMetaVal(*MI, CurIdx - 1);
  CurIdx++;
  while (NumGCPtrs--)
    CurIdx = StackMaps::getNextMetaArgIdx(MI, CurIdx);
  return CurIdx + 1; // skip <StackMaps::ConstantOp>
}

unsigned StatepointOpers::getNumGCPtrIdx() {
  // Take index of num of deopt args and skip all deopt records.
  unsigned CurIdx = getNumDeoptArgsIdx();
  unsigned NumDeoptArgs = getConstMetaVal(*MI, CurIdx - 1);
  CurIdx++;
  while (NumDeoptArgs--) {
    CurIdx = StackMaps::getNextMetaArgIdx(MI, CurIdx);
  }
  return CurIdx + 1; // skip <StackMaps::ConstantOp>
}

int StatepointOpers::getFirstGCPtrIdx() {
  unsigned NumGCPtrsIdx = getNumGCPtrIdx();
  unsigned NumGCPtrs = getConstMetaVal(*MI, NumGCPtrsIdx - 1);
  if (NumGCPtrs == 0)
    return -1;
  ++NumGCPtrsIdx; // skip <num gc ptrs>
  assert(NumGCPtrsIdx < MI->getNumOperands());
  return (int)NumGCPtrsIdx;
}

unsigned StatepointOpers::getGCPointerMap(
    SmallVectorImpl<std::pair<unsigned, unsigned>> &GCMap) {
  unsigned CurIdx = getNumGcMapEntriesIdx();
  unsigned GCMapSize = getConstMetaVal(*MI, CurIdx - 1);
  CurIdx++;
  for (unsigned N = 0; N < GCMapSize; ++N) {
    unsigned B = MI->getOperand(CurIdx++).getImm();
    unsigned D = MI->getOperand(CurIdx++).getImm();
    GCMap.push_back(std::make_pair(B, D));
  }

  return GCMapSize;
}

bool StatepointOpers::isFoldableReg(Register Reg) const {
  unsigned FoldableAreaStart = getVarIdx();
  for (const MachineOperand &MO : MI->uses()) {
    if (MO.getOperandNo() >= FoldableAreaStart)
      break;
    if (MO.isReg() && MO.getReg() == Reg)
      return false;
  }
  return true;
}

bool StatepointOpers::isFoldableReg(const MachineInstr *MI, Register Reg) {
  if (MI->getOpcode() != TargetOpcode::STATEPOINT)
    return false;
  return StatepointOpers(MI).isFoldableReg(Reg);
}

StackMaps::StackMaps(AsmPrinter &AP) : AP(AP) {
  if (StackMapVersion != 3)
    llvm_unreachable("Unsupported stackmap version!");
}

unsigned StackMaps::getNextMetaArgIdx(const MachineInstr *MI, unsigned CurIdx) {
  assert(CurIdx < MI->getNumOperands() && "Bad meta arg index");
  const auto &MO = MI->getOperand(CurIdx);
  if (MO.isImm()) {
    switch (MO.getImm()) {
    default:
      llvm_unreachable("Unrecognized operand type.");
    case StackMaps::DirectMemRefOp:
      CurIdx += 2;
      break;
    case StackMaps::IndirectMemRefOp:
      CurIdx += 3;
      break;
    case StackMaps::ConstantOp:
      ++CurIdx;
      break;
    }
  }
  ++CurIdx;
  assert(CurIdx < MI->getNumOperands() && "points past operand list");
  return CurIdx;
}

/// Go up the super-register chain until we hit a valid dwarf register number.
static unsigned getDwarfRegNum(unsigned Reg, const TargetRegisterInfo *TRI) {
  int RegNum;
  for (MCPhysReg SR : TRI->superregs_inclusive(Reg)) {
    RegNum = TRI->getDwarfRegNum(SR, false);
    if (RegNum >= 0)
      break;
  }

  assert(RegNum >= 0 && isUInt<16>(RegNum) && "Invalid Dwarf register number.");
  return (unsigned)RegNum;
}

MachineInstr::const_mop_iterator
StackMaps::parseOperand(MachineInstr::const_mop_iterator MOI,
                        MachineInstr::const_mop_iterator MOE, LocationVec &Locs,
                        LiveOutVec &LiveOuts) {
  const TargetRegisterInfo *TRI = AP.MF->getSubtarget().getRegisterInfo();
  if (MOI->isImm()) {
    switch (MOI->getImm()) {
    default:
      llvm_unreachable("Unrecognized operand type.");
    case StackMaps::DirectMemRefOp: {
      auto &DL = AP.MF->getDataLayout();

      unsigned Size = DL.getPointerSizeInBits();
      assert((Size % 8) == 0 && "Need pointer size in bytes.");
      Size /= 8;
      Register Reg = (++MOI)->getReg();
      int64_t Imm = (++MOI)->getImm();
      Locs.emplace_back(StackMaps::Location::Direct, Size,
                        getDwarfRegNum(Reg, TRI), Imm);
      break;
    }
    case StackMaps::IndirectMemRefOp: {
      int64_t Size = (++MOI)->getImm();
      assert(Size > 0 && "Need a valid size for indirect memory locations.");
      Register Reg = (++MOI)->getReg();
      int64_t Imm = (++MOI)->getImm();
      Locs.emplace_back(StackMaps::Location::Indirect, Size,
                        getDwarfRegNum(Reg, TRI), Imm);
      break;
    }
    case StackMaps::ConstantOp: {
      ++MOI;
      assert(MOI->isImm() && "Expected constant operand.");
      int64_t Imm = MOI->getImm();
      if (isInt<32>(Imm)) {
        Locs.emplace_back(Location::Constant, sizeof(int64_t), 0, Imm);
      } else {
        // ConstPool is intentionally a MapVector of 'uint64_t's (as
        // opposed to 'int64_t's).  We should never be in a situation
        // where we have to insert either the tombstone or the empty
        // keys into a map, and for a DenseMap<uint64_t, T> these are
        // (uint64_t)0 and (uint64_t)-1.  They can be and are
        // represented using 32 bit integers.
        assert((uint64_t)Imm != DenseMapInfo<uint64_t>::getEmptyKey() &&
               (uint64_t)Imm != DenseMapInfo<uint64_t>::getTombstoneKey() &&
               "empty and tombstone keys should fit in 32 bits!");
        auto Result = ConstPool.insert(std::make_pair(Imm, Imm));
        Locs.emplace_back(Location::ConstantIndex, sizeof(int64_t), 0,
                          Result.first - ConstPool.begin());
      }
      break;
    }
    }
    return ++MOI;
  }

  // The physical register number will ultimately be encoded as a DWARF regno.
  // The stack map also records the size of a spill slot that can hold the
  // register content. (The runtime can track the actual size of the data type
  // if it needs to.)
  if (MOI->isReg()) {
    // Skip implicit registers (this includes our scratch registers)
    if (MOI->isImplicit())
      return ++MOI;

    if (MOI->isUndef()) {
      // Record `undef` register as constant. Use same value as ISel uses.
      Locs.emplace_back(Location::Constant, sizeof(int64_t), 0, 0xFEFEFEFE);
      return ++MOI;
    }

    assert(MOI->getReg().isPhysical() &&
           "Virtreg operands should have been rewritten before now.");
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(MOI->getReg());
    assert(!MOI->getSubReg() && "Physical subreg still around.");

    unsigned Offset = 0;
    unsigned DwarfRegNum = getDwarfRegNum(MOI->getReg(), TRI);
    unsigned LLVMRegNum = *TRI->getLLVMRegNum(DwarfRegNum, false);
    unsigned SubRegIdx = TRI->getSubRegIndex(LLVMRegNum, MOI->getReg());
    if (SubRegIdx)
      Offset = TRI->getSubRegIdxOffset(SubRegIdx);

    Locs.emplace_back(Location::Register, TRI->getSpillSize(*RC),
                      DwarfRegNum, Offset);
    return ++MOI;
  }

  if (MOI->isRegLiveOut())
    LiveOuts = parseRegisterLiveOutMask(MOI->getRegLiveOut());

  return ++MOI;
}

void StackMaps::print(raw_ostream &OS) {
  const TargetRegisterInfo *TRI =
      AP.MF ? AP.MF->getSubtarget().getRegisterInfo() : nullptr;
  OS << WSMP << "callsites:\n";
  for (const auto &CSI : CSInfos) {
    const LocationVec &CSLocs = CSI.Locations;
    const LiveOutVec &LiveOuts = CSI.LiveOuts;

    OS << WSMP << "callsite " << CSI.ID << "\n";
    OS << WSMP << "  has " << CSLocs.size() << " locations\n";

    unsigned Idx = 0;
    for (const auto &Loc : CSLocs) {
      OS << WSMP << "\t\tLoc " << Idx << ": ";
      switch (Loc.Type) {
      case Location::Unprocessed:
        OS << "<Unprocessed operand>";
        break;
      case Location::Register:
        OS << "Register ";
        if (TRI)
          OS << printReg(Loc.Reg, TRI);
        else
          OS << Loc.Reg;
        break;
      case Location::Direct:
        OS << "Direct ";
        if (TRI)
          OS << printReg(Loc.Reg, TRI);
        else
          OS << Loc.Reg;
        if (Loc.Offset)
          OS << " + " << Loc.Offset;
        break;
      case Location::Indirect:
        OS << "Indirect ";
        if (TRI)
          OS << printReg(Loc.Reg, TRI);
        else
          OS << Loc.Reg;
        OS << "+" << Loc.Offset;
        break;
      case Location::Constant:
        OS << "Constant " << Loc.Offset;
        break;
      case Location::ConstantIndex:
        OS << "Constant Index " << Loc.Offset;
        break;
      }
      OS << "\t[encoding: .byte " << Loc.Type << ", .byte 0"
         << ", .short " << Loc.Size << ", .short " << Loc.Reg << ", .short 0"
         << ", .int " << Loc.Offset << "]\n";
      Idx++;
    }

    OS << WSMP << "\thas " << LiveOuts.size() << " live-out registers\n";

    Idx = 0;
    for (const auto &LO : LiveOuts) {
      OS << WSMP << "\t\tLO " << Idx << ": ";
      if (TRI)
        OS << printReg(LO.Reg, TRI);
      else
        OS << LO.Reg;
      OS << "\t[encoding: .short " << LO.DwarfRegNum << ", .byte 0, .byte "
         << LO.Size << "]\n";
      Idx++;
    }
  }
}

/// Create a live-out register record for the given register Reg.
StackMaps::LiveOutReg
StackMaps::createLiveOutReg(unsigned Reg, const TargetRegisterInfo *TRI) const {
  unsigned DwarfRegNum = getDwarfRegNum(Reg, TRI);
  unsigned Size = TRI->getSpillSize(*TRI->getMinimalPhysRegClass(Reg));
  return LiveOutReg(Reg, DwarfRegNum, Size);
}

/// Parse the register live-out mask and return a vector of live-out registers
/// that need to be recorded in the stackmap.
StackMaps::LiveOutVec
StackMaps::parseRegisterLiveOutMask(const uint32_t *Mask) const {
  assert(Mask && "No register mask specified");
  const TargetRegisterInfo *TRI = AP.MF->getSubtarget().getRegisterInfo();
  LiveOutVec LiveOuts;

  // Create a LiveOutReg for each bit that is set in the register mask.
  for (unsigned Reg = 0, NumRegs = TRI->getNumRegs(); Reg != NumRegs; ++Reg)
    if ((Mask[Reg / 32] >> (Reg % 32)) & 1)
      LiveOuts.push_back(createLiveOutReg(Reg, TRI));

  // We don't need to keep track of a register if its super-register is already
  // in the list. Merge entries that refer to the same dwarf register and use
  // the maximum size that needs to be spilled.

  llvm::sort(LiveOuts, [](const LiveOutReg &LHS, const LiveOutReg &RHS) {
    // Only sort by the dwarf register number.
    return LHS.DwarfRegNum < RHS.DwarfRegNum;
  });

  for (auto I = LiveOuts.begin(), E = LiveOuts.end(); I != E; ++I) {
    for (auto *II = std::next(I); II != E; ++II) {
      if (I->DwarfRegNum != II->DwarfRegNum) {
        // Skip all the now invalid entries.
        I = --II;
        break;
      }
      I->Size = std::max(I->Size, II->Size);
      if (I->Reg && TRI->isSuperRegister(I->Reg, II->Reg))
        I->Reg = II->Reg;
      II->Reg = 0; // mark for deletion.
    }
  }

  llvm::erase_if(LiveOuts, [](const LiveOutReg &LO) { return LO.Reg == 0; });

  return LiveOuts;
}

// See statepoint MI format description in StatepointOpers' class comment
// in include/llvm/CodeGen/StackMaps.h
void StackMaps::parseStatepointOpers(const MachineInstr &MI,
                                     MachineInstr::const_mop_iterator MOI,
                                     MachineInstr::const_mop_iterator MOE,
                                     LocationVec &Locations,
                                     LiveOutVec &LiveOuts) {
  LLVM_DEBUG(dbgs() << "record statepoint : " << MI << "\n");
  StatepointOpers SO(&MI);
  MOI = parseOperand(MOI, MOE, Locations, LiveOuts); // CC
  MOI = parseOperand(MOI, MOE, Locations, LiveOuts); // Flags
  MOI = parseOperand(MOI, MOE, Locations, LiveOuts); // Num Deopts

  // Record Deopt Args.
  unsigned NumDeoptArgs = Locations.back().Offset;
  assert(Locations.back().Type == Location::Constant);
  assert(NumDeoptArgs == SO.getNumDeoptArgs());

  while (NumDeoptArgs--)
    MOI = parseOperand(MOI, MOE, Locations, LiveOuts);

  // Record gc base/derived pairs
  assert(MOI->isImm() && MOI->getImm() == StackMaps::ConstantOp);
  ++MOI;
  assert(MOI->isImm());
  unsigned NumGCPointers = MOI->getImm();
  ++MOI;
  if (NumGCPointers) {
    // Map logical index of GC ptr to MI operand index.
    SmallVector<unsigned, 8> GCPtrIndices;
    unsigned GCPtrIdx = (unsigned)SO.getFirstGCPtrIdx();
    assert((int)GCPtrIdx != -1);
    assert(MOI - MI.operands_begin() == GCPtrIdx + 0LL);
    while (NumGCPointers--) {
      GCPtrIndices.push_back(GCPtrIdx);
      GCPtrIdx = StackMaps::getNextMetaArgIdx(&MI, GCPtrIdx);
    }

    SmallVector<std::pair<unsigned, unsigned>, 8> GCPairs;
    unsigned NumGCPairs = SO.getGCPointerMap(GCPairs);
    (void)NumGCPairs;
    LLVM_DEBUG(dbgs() << "NumGCPairs = " << NumGCPairs << "\n");

    auto MOB = MI.operands_begin();
    for (auto &P : GCPairs) {
      assert(P.first < GCPtrIndices.size() && "base pointer index not found");
      assert(P.second < GCPtrIndices.size() &&
             "derived pointer index not found");
      unsigned BaseIdx = GCPtrIndices[P.first];
      unsigned DerivedIdx = GCPtrIndices[P.second];
      LLVM_DEBUG(dbgs() << "Base : " << BaseIdx << " Derived : " << DerivedIdx
                        << "\n");
      (void)parseOperand(MOB + BaseIdx, MOE, Locations, LiveOuts);
      (void)parseOperand(MOB + DerivedIdx, MOE, Locations, LiveOuts);
    }

    MOI = MOB + GCPtrIdx;
  }

  // Record gc allocas
  assert(MOI < MOE);
  assert(MOI->isImm() && MOI->getImm() == StackMaps::ConstantOp);
  ++MOI;
  unsigned NumAllocas = MOI->getImm();
  ++MOI;
  while (NumAllocas--) {
    MOI = parseOperand(MOI, MOE, Locations, LiveOuts);
    assert(MOI < MOE);
  }
}

void StackMaps::recordStackMapOpers(const MCSymbol &MILabel,
                                    const MachineInstr &MI, uint64_t ID,
                                    MachineInstr::const_mop_iterator MOI,
                                    MachineInstr::const_mop_iterator MOE,
                                    bool recordResult) {
  MCContext &OutContext = AP.OutStreamer->getContext();

  LocationVec Locations;
  LiveOutVec LiveOuts;

  if (recordResult) {
    assert(PatchPointOpers(&MI).hasDef() && "Stackmap has no return value.");
    parseOperand(MI.operands_begin(), std::next(MI.operands_begin()), Locations,
                 LiveOuts);
  }

  // Parse operands.
  if (MI.getOpcode() == TargetOpcode::STATEPOINT)
    parseStatepointOpers(MI, MOI, MOE, Locations, LiveOuts);
  else
    while (MOI != MOE)
      MOI = parseOperand(MOI, MOE, Locations, LiveOuts);

  // Create an expression to calculate the offset of the callsite from function
  // entry.
  const MCExpr *CSOffsetExpr = MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(&MILabel, OutContext),
      MCSymbolRefExpr::create(AP.CurrentFnSymForSize, OutContext), OutContext);

  CSInfos.emplace_back(CSOffsetExpr, ID, std::move(Locations),
                       std::move(LiveOuts));

  // Record the stack size of the current function and update callsite count.
  const MachineFrameInfo &MFI = AP.MF->getFrameInfo();
  const TargetRegisterInfo *RegInfo = AP.MF->getSubtarget().getRegisterInfo();
  bool HasDynamicFrameSize =
      MFI.hasVarSizedObjects() || RegInfo->hasStackRealignment(*(AP.MF));
  uint64_t FrameSize = HasDynamicFrameSize ? UINT64_MAX : MFI.getStackSize();

  auto CurrentIt = FnInfos.find(AP.CurrentFnSym);
  if (CurrentIt != FnInfos.end())
    CurrentIt->second.RecordCount++;
  else
    FnInfos.insert(std::make_pair(AP.CurrentFnSym, FunctionInfo(FrameSize)));
}

void StackMaps::recordStackMap(const MCSymbol &L, const MachineInstr &MI) {
  assert(MI.getOpcode() == TargetOpcode::STACKMAP && "expected stackmap");

  StackMapOpers opers(&MI);
  const int64_t ID = MI.getOperand(PatchPointOpers::IDPos).getImm();
  recordStackMapOpers(L, MI, ID, std::next(MI.operands_begin(),
                                           opers.getVarIdx()),
                      MI.operands_end());
}

void StackMaps::recordPatchPoint(const MCSymbol &L, const MachineInstr &MI) {
  assert(MI.getOpcode() == TargetOpcode::PATCHPOINT && "expected patchpoint");

  PatchPointOpers opers(&MI);
  const int64_t ID = opers.getID();
  auto MOI = std::next(MI.operands_begin(), opers.getStackMapStartIdx());
  recordStackMapOpers(L, MI, ID, MOI, MI.operands_end(),
                      opers.isAnyReg() && opers.hasDef());

#ifndef NDEBUG
  // verify anyregcc
  auto &Locations = CSInfos.back().Locations;
  if (opers.isAnyReg()) {
    unsigned NArgs = opers.getNumCallArgs();
    for (unsigned i = 0, e = (opers.hasDef() ? NArgs + 1 : NArgs); i != e; ++i)
      assert(Locations[i].Type == Location::Register &&
             "anyreg arg must be in reg.");
  }
#endif
}

void StackMaps::recordStatepoint(const MCSymbol &L, const MachineInstr &MI) {
  assert(MI.getOpcode() == TargetOpcode::STATEPOINT && "expected statepoint");

  StatepointOpers opers(&MI);
  const unsigned StartIdx = opers.getVarIdx();
  recordStackMapOpers(L, MI, opers.getID(), MI.operands_begin() + StartIdx,
                      MI.operands_end(), false);
}

/// Emit the stackmap header.
///
/// Header {
///   uint8  : Stack Map Version (currently 3)
///   uint8  : Reserved (expected to be 0)
///   uint16 : Reserved (expected to be 0)
/// }
/// uint32 : NumFunctions
/// uint32 : NumConstants
/// uint32 : NumRecords
void StackMaps::emitStackmapHeader(MCStreamer &OS) {
  // Header.
  OS.emitIntValue(StackMapVersion, 1); // Version.
  OS.emitIntValue(0, 1);               // Reserved.
  OS.emitInt16(0);                     // Reserved.

  // Num functions.
  LLVM_DEBUG(dbgs() << WSMP << "#functions = " << FnInfos.size() << '\n');
  OS.emitInt32(FnInfos.size());
  // Num constants.
  LLVM_DEBUG(dbgs() << WSMP << "#constants = " << ConstPool.size() << '\n');
  OS.emitInt32(ConstPool.size());
  // Num callsites.
  LLVM_DEBUG(dbgs() << WSMP << "#callsites = " << CSInfos.size() << '\n');
  OS.emitInt32(CSInfos.size());
}

/// Emit the function frame record for each function.
///
/// StkSizeRecord[NumFunctions] {
///   uint64 : Function Address
///   uint64 : Stack Size
///   uint64 : Record Count
/// }
void StackMaps::emitFunctionFrameRecords(MCStreamer &OS) {
  // Function Frame records.
  LLVM_DEBUG(dbgs() << WSMP << "functions:\n");
  for (auto const &FR : FnInfos) {
    LLVM_DEBUG(dbgs() << WSMP << "function addr: " << FR.first
                      << " frame size: " << FR.second.StackSize
                      << " callsite count: " << FR.second.RecordCount << '\n');
    OS.emitSymbolValue(FR.first, 8);
    OS.emitIntValue(FR.second.StackSize, 8);
    OS.emitIntValue(FR.second.RecordCount, 8);
  }
}

/// Emit the constant pool.
///
/// int64  : Constants[NumConstants]
void StackMaps::emitConstantPoolEntries(MCStreamer &OS) {
  // Constant pool entries.
  LLVM_DEBUG(dbgs() << WSMP << "constants:\n");
  for (const auto &ConstEntry : ConstPool) {
    LLVM_DEBUG(dbgs() << WSMP << ConstEntry.second << '\n');
    OS.emitIntValue(ConstEntry.second, 8);
  }
}

/// Emit the callsite info for each callsite.
///
/// StkMapRecord[NumRecords] {
///   uint64 : PatchPoint ID
///   uint32 : Instruction Offset
///   uint16 : Reserved (record flags)
///   uint16 : NumLocations
///   Location[NumLocations] {
///     uint8  : Register | Direct | Indirect | Constant | ConstantIndex
///     uint8  : Size in Bytes
///     uint16 : Dwarf RegNum
///     int32  : Offset
///   }
///   uint16 : Padding
///   uint16 : NumLiveOuts
///   LiveOuts[NumLiveOuts] {
///     uint16 : Dwarf RegNum
///     uint8  : Reserved
///     uint8  : Size in Bytes
///   }
///   uint32 : Padding (only if required to align to 8 byte)
/// }
///
/// Location Encoding, Type, Value:
///   0x1, Register, Reg                 (value in register)
///   0x2, Direct, Reg + Offset          (frame index)
///   0x3, Indirect, [Reg + Offset]      (spilled value)
///   0x4, Constant, Offset              (small constant)
///   0x5, ConstIndex, Constants[Offset] (large constant)
void StackMaps::emitCallsiteEntries(MCStreamer &OS) {
  LLVM_DEBUG(print(dbgs()));
  // Callsite entries.
  for (const auto &CSI : CSInfos) {
    const LocationVec &CSLocs = CSI.Locations;
    const LiveOutVec &LiveOuts = CSI.LiveOuts;

    // Verify stack map entry. It's better to communicate a problem to the
    // runtime than crash in case of in-process compilation. Currently, we do
    // simple overflow checks, but we may eventually communicate other
    // compilation errors this way.
    if (CSLocs.size() > UINT16_MAX || LiveOuts.size() > UINT16_MAX) {
      OS.emitIntValue(UINT64_MAX, 8); // Invalid ID.
      OS.emitValue(CSI.CSOffsetExpr, 4);
      OS.emitInt16(0); // Reserved.
      OS.emitInt16(0); // 0 locations.
      OS.emitInt16(0); // padding.
      OS.emitInt16(0); // 0 live-out registers.
      OS.emitInt32(0); // padding.
      continue;
    }

    OS.emitIntValue(CSI.ID, 8);
    OS.emitValue(CSI.CSOffsetExpr, 4);

    // Reserved for flags.
    OS.emitInt16(0);
    OS.emitInt16(CSLocs.size());

    for (const auto &Loc : CSLocs) {
      OS.emitIntValue(Loc.Type, 1);
      OS.emitIntValue(0, 1);  // Reserved
      OS.emitInt16(Loc.Size);
      OS.emitInt16(Loc.Reg);
      OS.emitInt16(0); // Reserved
      OS.emitInt32(Loc.Offset);
    }

    // Emit alignment to 8 byte.
    OS.emitValueToAlignment(Align(8));

    // Num live-out registers and padding to align to 4 byte.
    OS.emitInt16(0);
    OS.emitInt16(LiveOuts.size());

    for (const auto &LO : LiveOuts) {
      OS.emitInt16(LO.DwarfRegNum);
      OS.emitIntValue(0, 1);
      OS.emitIntValue(LO.Size, 1);
    }
    // Emit alignment to 8 byte.
    OS.emitValueToAlignment(Align(8));
  }
}

/// Serialize the stackmap data.
void StackMaps::serializeToStackMapSection() {
  (void)WSMP;
  // Bail out if there's no stack map data.
  assert((!CSInfos.empty() || ConstPool.empty()) &&
         "Expected empty constant pool too!");
  assert((!CSInfos.empty() || FnInfos.empty()) &&
         "Expected empty function record too!");
  if (CSInfos.empty())
    return;

  MCContext &OutContext = AP.OutStreamer->getContext();
  MCStreamer &OS = *AP.OutStreamer;

  // Create the section.
  MCSection *StackMapSection =
      OutContext.getObjectFileInfo()->getStackMapSection();
  OS.switchSection(StackMapSection);

  // Emit a dummy symbol to force section inclusion.
  OS.emitLabel(OutContext.getOrCreateSymbol(Twine("__LLVM_StackMaps")));

  // Serialize data.
  LLVM_DEBUG(dbgs() << "********** Stack Map Output **********\n");
  emitStackmapHeader(OS);
  emitFunctionFrameRecords(OS);
  emitConstantPoolEntries(OS);
  emitCallsiteEntries(OS);
  OS.addBlankLine();

  // Clean up.
  CSInfos.clear();
  ConstPool.clear();
}
