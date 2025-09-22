//===-- M68kISelDAGToDAG.cpp - M68k Dag to Dag Inst Selector ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines an instruction selector for the M68K target.
///
//===----------------------------------------------------------------------===//

#include "M68k.h"

#include "M68kMachineFunction.h"
#include "M68kRegisterInfo.h"
#include "M68kTargetMachine.h"

#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "m68k-isel"
#define PASS_NAME "M68k DAG->DAG Pattern Instruction Selection"

namespace {

// For reference, the full order of operands for memory references is:
// (Operand), Displacement, Base, Index, Scale
struct M68kISelAddressMode {
  enum class AddrType {
    ARI,   // Address Register Indirect
    ARIPI, // Address Register Indirect with Postincrement
    ARIPD, // Address Register Indirect with Postdecrement
    ARID,  // Address Register Indirect with Displacement
    ARII,  // Address Register Indirect with Index
    PCD,   // Program Counter Indirect with Displacement
    PCI,   // Program Counter Indirect with Index
    AL,    // Absolute
  };
  AddrType AM;

  enum class Base { RegBase, FrameIndexBase };
  Base BaseType;

  int64_t Disp;

  // This is really a union, discriminated by BaseType!
  SDValue BaseReg;
  int BaseFrameIndex;

  SDValue IndexReg;
  unsigned Scale;

  const GlobalValue *GV;
  const Constant *CP;
  const BlockAddress *BlockAddr;
  const char *ES;
  MCSymbol *MCSym;
  int JT;
  Align Alignment; // CP alignment.

  unsigned char SymbolFlags; // M68kII::MO_*

  M68kISelAddressMode(AddrType AT)
      : AM(AT), BaseType(Base::RegBase), Disp(0), BaseFrameIndex(0), IndexReg(),
        Scale(1), GV(nullptr), CP(nullptr), BlockAddr(nullptr), ES(nullptr),
        MCSym(nullptr), JT(-1), Alignment(), SymbolFlags(M68kII::MO_NO_FLAG) {}

  bool hasSymbolicDisplacement() const {
    return GV != nullptr || CP != nullptr || ES != nullptr ||
           MCSym != nullptr || JT != -1 || BlockAddr != nullptr;
  }

  bool hasBase() const {
    return BaseType == Base::FrameIndexBase || BaseReg.getNode() != nullptr;
  }

  bool hasFrameIndex() const { return BaseType == Base::FrameIndexBase; }

  bool hasBaseReg() const {
    return BaseType == Base::RegBase && BaseReg.getNode() != nullptr;
  }

  bool hasIndexReg() const {
    return BaseType == Base::RegBase && IndexReg.getNode() != nullptr;
  }

  /// True if address mode type supports displacement
  bool isDispAddrType() const {
    return AM == AddrType::ARII || AM == AddrType::PCI ||
           AM == AddrType::ARID || AM == AddrType::PCD || AM == AddrType::AL;
  }

  unsigned getDispSize() const {
    switch (AM) {
    default:
      return 0;
    case AddrType::ARII:
    case AddrType::PCI:
      return 8;
    // These two in the next chip generations can hold upto 32 bit
    case AddrType::ARID:
    case AddrType::PCD:
      return 16;
    case AddrType::AL:
      return 32;
    }
  }

  bool hasDisp() const { return getDispSize() != 0; }
  bool isDisp8() const { return getDispSize() == 8; }
  bool isDisp16() const { return getDispSize() == 16; }
  bool isDisp32() const { return getDispSize() == 32; }

  /// Return true if this addressing mode is already PC-relative.
  bool isPCRelative() const {
    if (BaseType != Base::RegBase)
      return false;
    if (auto *RegNode = dyn_cast_or_null<RegisterSDNode>(BaseReg.getNode()))
      return RegNode->getReg() == M68k::PC;
    return false;
  }

  void setBaseReg(SDValue Reg) {
    BaseType = Base::RegBase;
    BaseReg = Reg;
  }

  void setIndexReg(SDValue Reg) { IndexReg = Reg; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() {
    dbgs() << "M68kISelAddressMode " << this;
    dbgs() << "\nDisp: " << Disp;
    dbgs() << ", BaseReg: ";
    if (BaseReg.getNode())
      BaseReg.getNode()->dump();
    else
      dbgs() << "null";
    dbgs() << ", BaseFI: " << BaseFrameIndex;
    dbgs() << ", IndexReg: ";
    if (IndexReg.getNode()) {
      IndexReg.getNode()->dump();
    } else {
      dbgs() << "null";
      dbgs() << ", Scale: " << Scale;
    }
    dbgs() << '\n';
  }
#endif
};
} // end anonymous namespace

namespace {

class M68kDAGToDAGISel : public SelectionDAGISel {
public:
  M68kDAGToDAGISel() = delete;

  explicit M68kDAGToDAGISel(M68kTargetMachine &TM)
      : SelectionDAGISel(TM), Subtarget(nullptr) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  bool IsProfitableToFold(SDValue N, SDNode *U, SDNode *Root) const override;

private:
  /// Keep a pointer to the M68kSubtarget around so that we can
  /// make the right decision when generating code for different targets.
  const M68kSubtarget *Subtarget;

// Include the pieces autogenerated from the target description.
#include "M68kGenDAGISel.inc"

  /// getTargetMachine - Return a reference to the TargetMachine, casted
  /// to the target-specific type.
  const M68kTargetMachine &getTargetMachine() {
    return static_cast<const M68kTargetMachine &>(TM);
  }

  void Select(SDNode *N) override;

  // Insert instructions to initialize the global base register in the
  // first MBB of the function.
  // HMM... do i need this?
  void initGlobalBaseReg(MachineFunction &MF);

  bool foldOffsetIntoAddress(uint64_t Offset, M68kISelAddressMode &AM);

  bool matchLoadInAddress(LoadSDNode *N, M68kISelAddressMode &AM);
  bool matchAddress(SDValue N, M68kISelAddressMode &AM);
  bool matchAddressBase(SDValue N, M68kISelAddressMode &AM);
  bool matchAddressRecursively(SDValue N, M68kISelAddressMode &AM,
                               unsigned Depth);
  bool matchADD(SDValue &N, M68kISelAddressMode &AM, unsigned Depth);
  bool matchWrapper(SDValue N, M68kISelAddressMode &AM);

  std::pair<bool, SDNode *> selectNode(SDNode *Node);

  bool SelectARI(SDNode *Parent, SDValue N, SDValue &Base);
  bool SelectARIPI(SDNode *Parent, SDValue N, SDValue &Base);
  bool SelectARIPD(SDNode *Parent, SDValue N, SDValue &Base);
  bool SelectARID(SDNode *Parent, SDValue N, SDValue &Imm, SDValue &Base);
  bool SelectARII(SDNode *Parent, SDValue N, SDValue &Imm, SDValue &Base,
                  SDValue &Index);
  bool SelectAL(SDNode *Parent, SDValue N, SDValue &Sym);
  bool SelectPCD(SDNode *Parent, SDValue N, SDValue &Imm);
  bool SelectPCI(SDNode *Parent, SDValue N, SDValue &Imm, SDValue &Index);

  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override;

  // If Address Mode represents Frame Index store FI in Disp and
  // Displacement bit size in Base. These values are read symmetrically by
  // M68kRegisterInfo::eliminateFrameIndex method
  inline bool getFrameIndexAddress(M68kISelAddressMode &AM, const SDLoc &DL,
                                   SDValue &Disp, SDValue &Base) {
    if (AM.BaseType == M68kISelAddressMode::Base::FrameIndexBase) {
      Disp = getI32Imm(AM.Disp, DL);
      Base = CurDAG->getTargetFrameIndex(
          AM.BaseFrameIndex, TLI->getPointerTy(CurDAG->getDataLayout()));
      return true;
    }

    return false;
  }

  // Gets a symbol plus optional displacement
  inline bool getSymbolicDisplacement(M68kISelAddressMode &AM, const SDLoc &DL,
                                      SDValue &Sym) {
    if (AM.GV) {
      Sym = CurDAG->getTargetGlobalAddress(AM.GV, SDLoc(), MVT::i32, AM.Disp,
                                           AM.SymbolFlags);
      return true;
    }

    if (AM.CP) {
      Sym = CurDAG->getTargetConstantPool(AM.CP, MVT::i32, AM.Alignment,
                                          AM.Disp, AM.SymbolFlags);
      return true;
    }

    if (AM.ES) {
      assert(!AM.Disp && "Non-zero displacement is ignored with ES.");
      Sym = CurDAG->getTargetExternalSymbol(AM.ES, MVT::i32, AM.SymbolFlags);
      return true;
    }

    if (AM.MCSym) {
      assert(!AM.Disp && "Non-zero displacement is ignored with MCSym.");
      assert(AM.SymbolFlags == 0 && "oo");
      Sym = CurDAG->getMCSymbol(AM.MCSym, MVT::i32);
      return true;
    }

    if (AM.JT != -1) {
      assert(!AM.Disp && "Non-zero displacement is ignored with JT.");
      Sym = CurDAG->getTargetJumpTable(AM.JT, MVT::i32, AM.SymbolFlags);
      return true;
    }

    if (AM.BlockAddr) {
      Sym = CurDAG->getTargetBlockAddress(AM.BlockAddr, MVT::i32, AM.Disp,
                                          AM.SymbolFlags);
      return true;
    }

    return false;
  }

  /// Return a target constant with the specified value of type i8.
  inline SDValue getI8Imm(int64_t Imm, const SDLoc &DL) {
    return CurDAG->getTargetConstant(Imm, DL, MVT::i8);
  }

  /// Return a target constant with the specified value of type i8.
  inline SDValue getI16Imm(int64_t Imm, const SDLoc &DL) {
    return CurDAG->getTargetConstant(Imm, DL, MVT::i16);
  }

  /// Return a target constant with the specified value, of type i32.
  inline SDValue getI32Imm(int64_t Imm, const SDLoc &DL) {
    return CurDAG->getTargetConstant(Imm, DL, MVT::i32);
  }

  /// Return a reference to the TargetInstrInfo, casted to the target-specific
  /// type.
  const M68kInstrInfo *getInstrInfo() const {
    return Subtarget->getInstrInfo();
  }

  /// Return an SDNode that returns the value of the global base register.
  /// Output instructions required to initialize the global base register,
  /// if necessary.
  SDNode *getGlobalBaseReg();
};

class M68kDAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  explicit M68kDAGToDAGISelLegacy(M68kTargetMachine &TM)
      : SelectionDAGISelLegacy(ID, std::make_unique<M68kDAGToDAGISel>(TM)) {}
};

char M68kDAGToDAGISelLegacy::ID;

} // namespace

INITIALIZE_PASS(M68kDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

bool M68kDAGToDAGISel::IsProfitableToFold(SDValue N, SDNode *U,
                                          SDNode *Root) const {
  if (OptLevel == CodeGenOptLevel::None)
    return false;

  if (U == Root) {
    switch (U->getOpcode()) {
    default:
      return true;
    case M68kISD::SUB:
    case ISD::SUB:
      // Prefer NEG instruction when zero subtracts a value.
      // e.g.
      //   move.l	#0, %d0
      //   sub.l	(4,%sp), %d0
      // vs.
      //   move.l	(4,%sp), %d0
      //   neg.l	%d0
      if (llvm::isNullConstant(U->getOperand(0)))
        return false;
      break;
    }
  }

  return true;
}

bool M68kDAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<M68kSubtarget>();
  return SelectionDAGISel::runOnMachineFunction(MF);
}

/// This pass converts a legalized DAG into a M68k-specific DAG,
/// ready for instruction scheduling.
FunctionPass *llvm::createM68kISelDag(M68kTargetMachine &TM) {
  return new M68kDAGToDAGISelLegacy(TM);
}

static bool doesDispFitFI(M68kISelAddressMode &AM) {
  if (!AM.isDispAddrType())
    return false;
  // -1 to make sure that resolved FI will fit into Disp field
  return isIntN(AM.getDispSize() - 1, AM.Disp);
}

static bool doesDispFit(M68kISelAddressMode &AM, int64_t Val) {
  if (!AM.isDispAddrType())
    return false;
  return isIntN(AM.getDispSize(), Val);
}

/// Return an SDNode that returns the value of the global base register.
/// Output instructions required to initialize the global base register,
/// if necessary.
SDNode *M68kDAGToDAGISel::getGlobalBaseReg() {
  unsigned GlobalBaseReg = getInstrInfo()->getGlobalBaseReg(MF);
  auto &DL = MF->getDataLayout();
  return CurDAG->getRegister(GlobalBaseReg, TLI->getPointerTy(DL)).getNode();
}

bool M68kDAGToDAGISel::foldOffsetIntoAddress(uint64_t Offset,
                                             M68kISelAddressMode &AM) {
  // Cannot combine ExternalSymbol displacements with integer offsets.
  if (Offset != 0 && (AM.ES || AM.MCSym))
    return false;

  int64_t Val = AM.Disp + Offset;

  if (doesDispFit(AM, Val)) {
    AM.Disp = Val;
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Matchers
//===----------------------------------------------------------------------===//

/// Helper for MatchAddress. Add the specified node to the
/// specified addressing mode without any further recursion.
bool M68kDAGToDAGISel::matchAddressBase(SDValue N, M68kISelAddressMode &AM) {
  // Is the base register already occupied?
  if (AM.hasBase()) {
    // If so, check to see if the scale index register is set.
    if (!AM.hasIndexReg()) {
      AM.IndexReg = N;
      AM.Scale = 1;
      return true;
    }

    // Otherwise, we cannot select it.
    return false;
  }

  // Default, generate it as a register.
  AM.BaseType = M68kISelAddressMode::Base::RegBase;
  AM.BaseReg = N;
  return true;
}

/// TODO Add TLS support
bool M68kDAGToDAGISel::matchLoadInAddress(LoadSDNode *N,
                                          M68kISelAddressMode &AM) {
  return false;
}

bool M68kDAGToDAGISel::matchAddressRecursively(SDValue N,
                                               M68kISelAddressMode &AM,
                                               unsigned Depth) {
  SDLoc DL(N);

  // Limit recursion.
  if (Depth > 5)
    return matchAddressBase(N, AM);

  // If this is already a %PC relative address, we can only merge immediates
  // into it.  Instead of handling this in every case, we handle it here.
  // PC relative addressing: %PC + 16-bit displacement!
  if (AM.isPCRelative()) {
    // FIXME JumpTable and ExternalSymbol address currently don't like
    // displacements.  It isn't very important, but should be fixed for
    // consistency.

    if (ConstantSDNode *Cst = dyn_cast<ConstantSDNode>(N))
      if (foldOffsetIntoAddress(Cst->getSExtValue(), AM))
        return true;
    return false;
  }

  switch (N.getOpcode()) {
  default:
    break;

  case ISD::Constant: {
    uint64_t Val = cast<ConstantSDNode>(N)->getSExtValue();
    if (foldOffsetIntoAddress(Val, AM))
      return true;
    break;
  }

  case M68kISD::Wrapper:
  case M68kISD::WrapperPC:
    if (matchWrapper(N, AM))
      return true;
    break;

  case ISD::LOAD:
    if (matchLoadInAddress(cast<LoadSDNode>(N), AM))
      return true;
    break;

  case ISD::OR:
    // We want to look through a transform in InstCombine and DAGCombiner that
    // turns 'add' into 'or', so we can treat this 'or' exactly like an 'add'.
    // Example: (or (and x, 1), (shl y, 3)) --> (add (and x, 1), (shl y, 3))
    // An 'lea' can then be used to match the shift (multiply) and add:
    // and $1, %esi
    // lea (%rsi, %rdi, 8), %rax
    if (CurDAG->haveNoCommonBitsSet(N.getOperand(0), N.getOperand(1)) &&
        matchADD(N, AM, Depth))
      return true;
    break;

  case ISD::ADD:
    if (matchADD(N, AM, Depth))
      return true;
    break;

  case ISD::FrameIndex:
    if (AM.isDispAddrType() &&
        AM.BaseType == M68kISelAddressMode::Base::RegBase &&
        AM.BaseReg.getNode() == nullptr && doesDispFitFI(AM)) {
      AM.BaseType = M68kISelAddressMode::Base::FrameIndexBase;
      AM.BaseFrameIndex = cast<FrameIndexSDNode>(N)->getIndex();
      return true;
    }
    break;

  case ISD::TargetGlobalTLSAddress: {
    GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(N);
    AM.GV = GA->getGlobal();
    AM.SymbolFlags = GA->getTargetFlags();
    return true;
  }
  }

  return matchAddressBase(N, AM);
}

/// Add the specified node to the specified addressing mode, returning true if
/// it cannot be done. This just pattern matches for the addressing mode.
bool M68kDAGToDAGISel::matchAddress(SDValue N, M68kISelAddressMode &AM) {
  // TODO: Post-processing: Convert lea(,%reg,2) to lea(%reg,%reg), which has
  // a smaller encoding and avoids a scaled-index.
  // And make sure it is an indexed mode

  // TODO: Post-processing: Convert foo to foo(%pc), even in non-PIC mode,
  // because it has a smaller encoding.
  // Make sure this must be done only if PC* modes are currently being matched
  return matchAddressRecursively(N, AM, 0);
}

bool M68kDAGToDAGISel::matchADD(SDValue &N, M68kISelAddressMode &AM,
                                unsigned Depth) {
  // Add an artificial use to this node so that we can keep track of
  // it if it gets CSE'd with a different node.
  HandleSDNode Handle(N);

  M68kISelAddressMode Backup = AM;
  if (matchAddressRecursively(N.getOperand(0), AM, Depth + 1) &&
      matchAddressRecursively(Handle.getValue().getOperand(1), AM, Depth + 1)) {
    return true;
  }
  AM = Backup;

  // Try again after commuting the operands.
  if (matchAddressRecursively(Handle.getValue().getOperand(1), AM, Depth + 1) &&
      matchAddressRecursively(Handle.getValue().getOperand(0), AM, Depth + 1)) {
    return true;
  }
  AM = Backup;

  // If we couldn't fold both operands into the address at the same time,
  // see if we can just put each operand into a register and fold at least
  // the add.
  if (!AM.hasBase() && !AM.hasIndexReg()) {
    N = Handle.getValue();
    AM.BaseReg = N.getOperand(0);
    AM.IndexReg = N.getOperand(1);
    AM.Scale = 1;
    return true;
  }

  N = Handle.getValue();
  return false;
}

/// Try to match M68kISD::Wrapper and M68kISD::WrapperPC nodes into an
/// addressing mode. These wrap things that will resolve down into a symbol
/// reference. If no match is possible, this returns true, otherwise it returns
/// false.
bool M68kDAGToDAGISel::matchWrapper(SDValue N, M68kISelAddressMode &AM) {
  // If the addressing mode already has a symbol as the displacement, we can
  // never match another symbol.
  if (AM.hasSymbolicDisplacement())
    return false;

  SDValue N0 = N.getOperand(0);

  if (N.getOpcode() == M68kISD::WrapperPC) {

    // If cannot match here just restore the old version
    M68kISelAddressMode Backup = AM;

    if (AM.hasBase()) {
      return false;
    }

    if (auto *G = dyn_cast<GlobalAddressSDNode>(N0)) {
      AM.GV = G->getGlobal();
      AM.SymbolFlags = G->getTargetFlags();
      if (!foldOffsetIntoAddress(G->getOffset(), AM)) {
        AM = Backup;
        return false;
      }
    } else if (auto *CP = dyn_cast<ConstantPoolSDNode>(N0)) {
      AM.CP = CP->getConstVal();
      AM.Alignment = CP->getAlign();
      AM.SymbolFlags = CP->getTargetFlags();
      if (!foldOffsetIntoAddress(CP->getOffset(), AM)) {
        AM = Backup;
        return false;
      }
    } else if (auto *S = dyn_cast<ExternalSymbolSDNode>(N0)) {
      AM.ES = S->getSymbol();
      AM.SymbolFlags = S->getTargetFlags();
    } else if (auto *S = dyn_cast<MCSymbolSDNode>(N0)) {
      AM.MCSym = S->getMCSymbol();
    } else if (auto *J = dyn_cast<JumpTableSDNode>(N0)) {
      AM.JT = J->getIndex();
      AM.SymbolFlags = J->getTargetFlags();
    } else if (auto *BA = dyn_cast<BlockAddressSDNode>(N0)) {
      AM.BlockAddr = BA->getBlockAddress();
      AM.SymbolFlags = BA->getTargetFlags();
      if (!foldOffsetIntoAddress(BA->getOffset(), AM)) {
        AM = Backup;
        return false;
      }
    } else
      llvm_unreachable("Unhandled symbol reference node.");

    AM.setBaseReg(CurDAG->getRegister(M68k::PC, MVT::i32));
    return true;
  }

  // This wrapper requires 32bit disp/imm field for Medium CM
  if (!AM.isDisp32()) {
    return false;
  }

  if (N.getOpcode() == M68kISD::Wrapper) {
    if (auto *G = dyn_cast<GlobalAddressSDNode>(N0)) {
      AM.GV = G->getGlobal();
      AM.Disp += G->getOffset();
      AM.SymbolFlags = G->getTargetFlags();
    } else if (auto *CP = dyn_cast<ConstantPoolSDNode>(N0)) {
      AM.CP = CP->getConstVal();
      AM.Alignment = CP->getAlign();
      AM.Disp += CP->getOffset();
      AM.SymbolFlags = CP->getTargetFlags();
    } else if (auto *S = dyn_cast<ExternalSymbolSDNode>(N0)) {
      AM.ES = S->getSymbol();
      AM.SymbolFlags = S->getTargetFlags();
    } else if (auto *S = dyn_cast<MCSymbolSDNode>(N0)) {
      AM.MCSym = S->getMCSymbol();
    } else if (auto *J = dyn_cast<JumpTableSDNode>(N0)) {
      AM.JT = J->getIndex();
      AM.SymbolFlags = J->getTargetFlags();
    } else if (auto *BA = dyn_cast<BlockAddressSDNode>(N0)) {
      AM.BlockAddr = BA->getBlockAddress();
      AM.Disp += BA->getOffset();
      AM.SymbolFlags = BA->getTargetFlags();
    } else
      llvm_unreachable("Unhandled symbol reference node.");
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Selectors
//===----------------------------------------------------------------------===//

void M68kDAGToDAGISel::Select(SDNode *Node) {
  unsigned Opcode = Node->getOpcode();
  SDLoc DL(Node);

  LLVM_DEBUG(dbgs() << "Selecting: "; Node->dump(CurDAG); dbgs() << '\n');

  if (Node->isMachineOpcode()) {
    LLVM_DEBUG(dbgs() << "== "; Node->dump(CurDAG); dbgs() << '\n');
    Node->setNodeId(-1);
    return; // Already selected.
  }

  switch (Opcode) {
  default:
    break;

  case ISD::GLOBAL_OFFSET_TABLE: {
    SDValue GOT = CurDAG->getTargetExternalSymbol(
        "_GLOBAL_OFFSET_TABLE_", MVT::i32, M68kII::MO_GOTPCREL);
    MachineSDNode *Res =
        CurDAG->getMachineNode(M68k::LEA32q, DL, MVT::i32, GOT);
    ReplaceNode(Node, Res);
    return;
  }

  case M68kISD::GLOBAL_BASE_REG:
    ReplaceNode(Node, getGlobalBaseReg());
    return;
  }

  SelectCode(Node);
}

bool M68kDAGToDAGISel::SelectARIPI(SDNode *Parent, SDValue N, SDValue &Base) {
  LLVM_DEBUG(dbgs() << "Selecting AddrType::ARIPI: ");
  LLVM_DEBUG(dbgs() << "NOT IMPLEMENTED\n");
  return false;
}

bool M68kDAGToDAGISel::SelectARIPD(SDNode *Parent, SDValue N, SDValue &Base) {
  LLVM_DEBUG(dbgs() << "Selecting AddrType::ARIPD: ");
  LLVM_DEBUG(dbgs() << "NOT IMPLEMENTED\n");
  return false;
}

bool M68kDAGToDAGISel::SelectARID(SDNode *Parent, SDValue N, SDValue &Disp,
                                  SDValue &Base) {
  LLVM_DEBUG(dbgs() << "Selecting AddrType::ARID: ");
  M68kISelAddressMode AM(M68kISelAddressMode::AddrType::ARID);

  if (!matchAddress(N, AM))
    return false;

  if (AM.isPCRelative()) {
    LLVM_DEBUG(dbgs() << "REJECT: Cannot match PC relative address\n");
    return false;
  }

  // If this is a frame index, grab it
  if (getFrameIndexAddress(AM, SDLoc(N), Disp, Base)) {
    LLVM_DEBUG(dbgs() << "SUCCESS matched FI\n");
    return true;
  }

  if (AM.hasIndexReg()) {
    LLVM_DEBUG(dbgs() << "REJECT: Cannot match Index\n");
    return false;
  }

  if (!AM.hasBaseReg()) {
    LLVM_DEBUG(dbgs() << "REJECT: No Base reg\n");
    return false;
  }

  Base = AM.BaseReg;

  if (getSymbolicDisplacement(AM, SDLoc(N), Disp)) {
    assert(!AM.Disp && "Should not be any displacement");
    LLVM_DEBUG(dbgs() << "SUCCESS, matched Symbol\n");
    return true;
  }

  // Give a chance to AddrType::ARI
  if (AM.Disp == 0) {
    LLVM_DEBUG(dbgs() << "REJECT: No displacement\n");
    return false;
  }

  Disp = getI16Imm(AM.Disp, SDLoc(N));

  LLVM_DEBUG(dbgs() << "SUCCESS\n");
  return true;
}

static bool isAddressBase(const SDValue &N) {
  switch (N.getOpcode()) {
  case ISD::ADD:
  case ISD::ADDC:
    return llvm::any_of(N.getNode()->ops(),
                        [](const SDUse &U) { return isAddressBase(U.get()); });
  case M68kISD::Wrapper:
  case M68kISD::WrapperPC:
  case M68kISD::GLOBAL_BASE_REG:
    return true;
  default:
    return false;
  }
}

bool M68kDAGToDAGISel::SelectARII(SDNode *Parent, SDValue N, SDValue &Disp,
                                  SDValue &Base, SDValue &Index) {
  M68kISelAddressMode AM(M68kISelAddressMode::AddrType::ARII);
  LLVM_DEBUG(dbgs() << "Selecting AddrType::ARII: ");

  if (!matchAddress(N, AM))
    return false;

  if (AM.isPCRelative()) {
    LLVM_DEBUG(dbgs() << "REJECT: PC relative\n");
    return false;
  }

  if (!AM.hasIndexReg()) {
    LLVM_DEBUG(dbgs() << "REJECT: No Index\n");
    return false;
  }

  if (!AM.hasBaseReg()) {
    LLVM_DEBUG(dbgs() << "REJECT: No Base\n");
    return false;
  }

  if (!isAddressBase(AM.BaseReg) && isAddressBase(AM.IndexReg)) {
    Base = AM.IndexReg;
    Index = AM.BaseReg;
  } else {
    Base = AM.BaseReg;
    Index = AM.IndexReg;
  }

  if (AM.hasSymbolicDisplacement()) {
    LLVM_DEBUG(dbgs() << "REJECT, Cannot match symbolic displacement\n");
    return false;
  }

  // The idea here is that we want to use AddrType::ARII without displacement
  // only if necessary like memory operations, otherwise this must be lowered
  // into addition
  if (AM.Disp == 0 && (!Parent || (Parent->getOpcode() != ISD::LOAD &&
                                   Parent->getOpcode() != ISD::STORE))) {
    LLVM_DEBUG(dbgs() << "REJECT: Displacement is Zero\n");
    return false;
  }

  Disp = getI8Imm(AM.Disp, SDLoc(N));

  LLVM_DEBUG(dbgs() << "SUCCESS\n");
  return true;
}

bool M68kDAGToDAGISel::SelectAL(SDNode *Parent, SDValue N, SDValue &Sym) {
  LLVM_DEBUG(dbgs() << "Selecting AddrType::AL: ");
  M68kISelAddressMode AM(M68kISelAddressMode::AddrType::AL);

  if (!matchAddress(N, AM)) {
    LLVM_DEBUG(dbgs() << "REJECT: Match failed\n");
    return false;
  }

  if (AM.isPCRelative()) {
    LLVM_DEBUG(dbgs() << "REJECT: Cannot match PC relative address\n");
    return false;
  }

  if (AM.hasBase()) {
    LLVM_DEBUG(dbgs() << "REJECT: Cannot match Base\n");
    return false;
  }

  if (AM.hasIndexReg()) {
    LLVM_DEBUG(dbgs() << "REJECT: Cannot match Index\n");
    return false;
  }

  if (getSymbolicDisplacement(AM, SDLoc(N), Sym)) {
    LLVM_DEBUG(dbgs() << "SUCCESS: Matched symbol\n");
    return true;
  }

  if (AM.Disp) {
    Sym = getI32Imm(AM.Disp, SDLoc(N));
    LLVM_DEBUG(dbgs() << "SUCCESS\n");
    return true;
  }

  LLVM_DEBUG(dbgs() << "REJECT: Not Symbol or Disp\n");
  return false;
  ;
}

bool M68kDAGToDAGISel::SelectPCD(SDNode *Parent, SDValue N, SDValue &Disp) {
  LLVM_DEBUG(dbgs() << "Selecting AddrType::PCD: ");
  M68kISelAddressMode AM(M68kISelAddressMode::AddrType::PCD);

  if (!matchAddress(N, AM))
    return false;

  if (!AM.isPCRelative()) {
    LLVM_DEBUG(dbgs() << "REJECT: Not PC relative\n");
    return false;
  }

  if (AM.hasIndexReg()) {
    LLVM_DEBUG(dbgs() << "REJECT: Cannot match Index\n");
    return false;
  }

  if (getSymbolicDisplacement(AM, SDLoc(N), Disp)) {
    LLVM_DEBUG(dbgs() << "SUCCESS, matched Symbol\n");
    return true;
  }

  Disp = getI16Imm(AM.Disp, SDLoc(N));

  LLVM_DEBUG(dbgs() << "SUCCESS\n");
  return true;
}

bool M68kDAGToDAGISel::SelectPCI(SDNode *Parent, SDValue N, SDValue &Disp,
                                 SDValue &Index) {
  LLVM_DEBUG(dbgs() << "Selecting AddrType::PCI: ");
  M68kISelAddressMode AM(M68kISelAddressMode::AddrType::PCI);

  if (!matchAddress(N, AM))
    return false;

  if (!AM.isPCRelative()) {
    LLVM_DEBUG(dbgs() << "REJECT: Not PC relative\n");
    return false;
  }

  if (!AM.hasIndexReg()) {
    LLVM_DEBUG(dbgs() << "REJECT: No Index\n");
    return false;
  }

  Index = AM.IndexReg;

  if (getSymbolicDisplacement(AM, SDLoc(N), Disp)) {
    assert(!AM.Disp && "Should not be any displacement");
    LLVM_DEBUG(dbgs() << "SUCCESS, matched Symbol\n");
    return true;
  }

  Disp = getI8Imm(AM.Disp, SDLoc(N));

  LLVM_DEBUG(dbgs() << "SUCCESS\n");
  return true;
}

bool M68kDAGToDAGISel::SelectARI(SDNode *Parent, SDValue N, SDValue &Base) {
  LLVM_DEBUG(dbgs() << "Selecting AddrType::ARI: ");
  M68kISelAddressMode AM(M68kISelAddressMode::AddrType::ARI);

  if (!matchAddress(N, AM)) {
    LLVM_DEBUG(dbgs() << "REJECT: Match failed\n");
    return false;
  }

  if (AM.isPCRelative()) {
    LLVM_DEBUG(dbgs() << "REJECT: Cannot match PC relative address\n");
    return false;
  }

  // AddrType::ARI does not use these
  if (AM.hasIndexReg() || AM.Disp != 0) {
    LLVM_DEBUG(dbgs() << "REJECT: Cannot match Index or Disp\n");
    return false;
  }

  // Must be matched by AddrType::AL
  if (AM.hasSymbolicDisplacement()) {
    LLVM_DEBUG(dbgs() << "REJECT: Cannot match Symbolic Disp\n");
    return false;
  }

  if (AM.hasBaseReg()) {
    Base = AM.BaseReg;
    LLVM_DEBUG(dbgs() << "SUCCESS\n");
    return true;
  }

  return false;
}

bool M68kDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  // In order to tell AsmPrinter the exact addressing mode we select here, which
  // might comprise of multiple SDValues (hence MachineOperands), a 32-bit
  // immediate value is prepended to the list of selected SDValues to indicate
  // the addressing mode kind.
  using AMK = M68k::MemAddrModeKind;
  auto addKind = [this](SDValue &Opnd, AMK Kind) -> bool {
    Opnd = CurDAG->getTargetConstant(unsigned(Kind), SDLoc(), MVT::i32);
    return true;
  };

  switch (ConstraintID) {
  // Generic memory operand.
  case InlineAsm::ConstraintCode::m: {
    // Try every supported (memory) addressing modes.
    SDValue Operands[4];

    // TODO: The ordering of the following SelectXXX is relatively...arbitrary,
    // right now we simply sort them by descending complexity. Maybe we should
    // adjust this by code model and/or relocation mode in the future.
    if (SelectARII(nullptr, Op, Operands[1], Operands[2], Operands[3]) &&
        addKind(Operands[0], AMK::f)) {
      OutOps.insert(OutOps.end(), &Operands[0], Operands + 4);
      return false;
    }

    if ((SelectPCI(nullptr, Op, Operands[1], Operands[2]) &&
         addKind(Operands[0], AMK::k)) ||
        (SelectARID(nullptr, Op, Operands[1], Operands[2]) &&
         addKind(Operands[0], AMK::p))) {
      OutOps.insert(OutOps.end(), &Operands[0], Operands + 3);
      return false;
    }

    if ((SelectPCD(nullptr, Op, Operands[1]) && addKind(Operands[0], AMK::q)) ||
        (SelectARI(nullptr, Op, Operands[1]) && addKind(Operands[0], AMK::j)) ||
        (SelectAL(nullptr, Op, Operands[1]) && addKind(Operands[0], AMK::b))) {
      OutOps.insert(OutOps.end(), {Operands[0], Operands[1]});
      return false;
    }

    return true;
  }
  // 'Q': Address register indirect addressing.
  case InlineAsm::ConstraintCode::Q: {
    SDValue AMKind, Base;
    // 'j' addressing mode.
    // TODO: Add support for 'o' and 'e' after their
    // select functions are implemented.
    if (SelectARI(nullptr, Op, Base) && addKind(AMKind, AMK::j)) {
      OutOps.insert(OutOps.end(), {AMKind, Base});
      return false;
    }
    return true;
  }
  // 'U': Address register indirect w/ constant offset addressing.
  case InlineAsm::ConstraintCode::Um: {
    SDValue AMKind, Base, Offset;
    // 'p' addressing mode.
    if (SelectARID(nullptr, Op, Offset, Base) && addKind(AMKind, AMK::p)) {
      OutOps.insert(OutOps.end(), {AMKind, Offset, Base});
      return false;
    }
    return true;
  }
  default:
    return true;
  }
}
