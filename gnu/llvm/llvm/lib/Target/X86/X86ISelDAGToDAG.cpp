//===- X86ISelDAGToDAG.cpp - A DAG pattern matching inst selector for X86 -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a DAG pattern matching instruction selector for X86,
// converting from a legalized dag to a X86 dag.
//
//===----------------------------------------------------------------------===//

#include "X86ISelDAGToDAG.h"
#include "X86.h"
#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "x86-isel"
#define PASS_NAME "X86 DAG->DAG Instruction Selection"

STATISTIC(NumLoadMoved, "Number of loads moved below TokenFactor");

static cl::opt<bool> AndImmShrink("x86-and-imm-shrink", cl::init(true),
    cl::desc("Enable setting constant bits to reduce size of mask immediates"),
    cl::Hidden);

static cl::opt<bool> EnablePromoteAnyextLoad(
    "x86-promote-anyext-load", cl::init(true),
    cl::desc("Enable promoting aligned anyext load to wider load"), cl::Hidden);

extern cl::opt<bool> IndirectBranchTracking;

//===----------------------------------------------------------------------===//
//                      Pattern Matcher Implementation
//===----------------------------------------------------------------------===//

namespace {
  /// This corresponds to X86AddressMode, but uses SDValue's instead of register
  /// numbers for the leaves of the matched tree.
  struct X86ISelAddressMode {
    enum {
      RegBase,
      FrameIndexBase
    } BaseType = RegBase;

    // This is really a union, discriminated by BaseType!
    SDValue Base_Reg;
    int Base_FrameIndex = 0;

    unsigned Scale = 1;
    SDValue IndexReg;
    int32_t Disp = 0;
    SDValue Segment;
    const GlobalValue *GV = nullptr;
    const Constant *CP = nullptr;
    const BlockAddress *BlockAddr = nullptr;
    const char *ES = nullptr;
    MCSymbol *MCSym = nullptr;
    int JT = -1;
    Align Alignment;            // CP alignment.
    unsigned char SymbolFlags = X86II::MO_NO_FLAG;  // X86II::MO_*
    bool NegateIndex = false;

    X86ISelAddressMode() = default;

    bool hasSymbolicDisplacement() const {
      return GV != nullptr || CP != nullptr || ES != nullptr ||
             MCSym != nullptr || JT != -1 || BlockAddr != nullptr;
    }

    bool hasBaseOrIndexReg() const {
      return BaseType == FrameIndexBase ||
             IndexReg.getNode() != nullptr || Base_Reg.getNode() != nullptr;
    }

    /// Return true if this addressing mode is already RIP-relative.
    bool isRIPRelative() const {
      if (BaseType != RegBase) return false;
      if (RegisterSDNode *RegNode =
            dyn_cast_or_null<RegisterSDNode>(Base_Reg.getNode()))
        return RegNode->getReg() == X86::RIP;
      return false;
    }

    void setBaseReg(SDValue Reg) {
      BaseType = RegBase;
      Base_Reg = Reg;
    }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    void dump(SelectionDAG *DAG = nullptr) {
      dbgs() << "X86ISelAddressMode " << this << '\n';
      dbgs() << "Base_Reg ";
      if (Base_Reg.getNode())
        Base_Reg.getNode()->dump(DAG);
      else
        dbgs() << "nul\n";
      if (BaseType == FrameIndexBase)
        dbgs() << " Base.FrameIndex " << Base_FrameIndex << '\n';
      dbgs() << " Scale " << Scale << '\n'
             << "IndexReg ";
      if (NegateIndex)
        dbgs() << "negate ";
      if (IndexReg.getNode())
        IndexReg.getNode()->dump(DAG);
      else
        dbgs() << "nul\n";
      dbgs() << " Disp " << Disp << '\n'
             << "GV ";
      if (GV)
        GV->dump();
      else
        dbgs() << "nul";
      dbgs() << " CP ";
      if (CP)
        CP->dump();
      else
        dbgs() << "nul";
      dbgs() << '\n'
             << "ES ";
      if (ES)
        dbgs() << ES;
      else
        dbgs() << "nul";
      dbgs() << " MCSym ";
      if (MCSym)
        dbgs() << MCSym;
      else
        dbgs() << "nul";
      dbgs() << " JT" << JT << " Align" << Alignment.value() << '\n';
    }
#endif
  };
}

namespace {
  //===--------------------------------------------------------------------===//
  /// ISel - X86-specific code to select X86 machine instructions for
  /// SelectionDAG operations.
  ///
  class X86DAGToDAGISel final : public SelectionDAGISel {
    /// Keep a pointer to the X86Subtarget around so that we can
    /// make the right decision when generating code for different targets.
    const X86Subtarget *Subtarget;

    /// If true, selector should try to optimize for minimum code size.
    bool OptForMinSize;

    /// Disable direct TLS access through segment registers.
    bool IndirectTlsSegRefs;

  public:
    X86DAGToDAGISel() = delete;

    explicit X86DAGToDAGISel(X86TargetMachine &tm, CodeGenOptLevel OptLevel)
        : SelectionDAGISel(tm, OptLevel), Subtarget(nullptr),
          OptForMinSize(false), IndirectTlsSegRefs(false) {}

    bool runOnMachineFunction(MachineFunction &MF) override {
      // Reset the subtarget each time through.
      Subtarget = &MF.getSubtarget<X86Subtarget>();
      IndirectTlsSegRefs = MF.getFunction().hasFnAttribute(
                             "indirect-tls-seg-refs");

      // OptFor[Min]Size are used in pattern predicates that isel is matching.
      OptForMinSize = MF.getFunction().hasMinSize();
      assert((!OptForMinSize || MF.getFunction().hasOptSize()) &&
             "OptForMinSize implies OptForSize");
      return SelectionDAGISel::runOnMachineFunction(MF);
    }

    void emitFunctionEntryCode() override;

    bool IsProfitableToFold(SDValue N, SDNode *U, SDNode *Root) const override;

    void PreprocessISelDAG() override;
    void PostprocessISelDAG() override;

// Include the pieces autogenerated from the target description.
#include "X86GenDAGISel.inc"

  private:
    void Select(SDNode *N) override;

    bool foldOffsetIntoAddress(uint64_t Offset, X86ISelAddressMode &AM);
    bool matchLoadInAddress(LoadSDNode *N, X86ISelAddressMode &AM,
                            bool AllowSegmentRegForX32 = false);
    bool matchWrapper(SDValue N, X86ISelAddressMode &AM);
    bool matchAddress(SDValue N, X86ISelAddressMode &AM);
    bool matchVectorAddress(SDValue N, X86ISelAddressMode &AM);
    bool matchAdd(SDValue &N, X86ISelAddressMode &AM, unsigned Depth);
    SDValue matchIndexRecursively(SDValue N, X86ISelAddressMode &AM,
                                  unsigned Depth);
    bool matchAddressRecursively(SDValue N, X86ISelAddressMode &AM,
                                 unsigned Depth);
    bool matchVectorAddressRecursively(SDValue N, X86ISelAddressMode &AM,
                                       unsigned Depth);
    bool matchAddressBase(SDValue N, X86ISelAddressMode &AM);
    bool selectAddr(SDNode *Parent, SDValue N, SDValue &Base,
                    SDValue &Scale, SDValue &Index, SDValue &Disp,
                    SDValue &Segment);
    bool selectVectorAddr(MemSDNode *Parent, SDValue BasePtr, SDValue IndexOp,
                          SDValue ScaleOp, SDValue &Base, SDValue &Scale,
                          SDValue &Index, SDValue &Disp, SDValue &Segment);
    bool selectMOV64Imm32(SDValue N, SDValue &Imm);
    bool selectLEAAddr(SDValue N, SDValue &Base,
                       SDValue &Scale, SDValue &Index, SDValue &Disp,
                       SDValue &Segment);
    bool selectLEA64_32Addr(SDValue N, SDValue &Base,
                            SDValue &Scale, SDValue &Index, SDValue &Disp,
                            SDValue &Segment);
    bool selectTLSADDRAddr(SDValue N, SDValue &Base,
                           SDValue &Scale, SDValue &Index, SDValue &Disp,
                           SDValue &Segment);
    bool selectRelocImm(SDValue N, SDValue &Op);

    bool tryFoldLoad(SDNode *Root, SDNode *P, SDValue N,
                     SDValue &Base, SDValue &Scale,
                     SDValue &Index, SDValue &Disp,
                     SDValue &Segment);

    // Convenience method where P is also root.
    bool tryFoldLoad(SDNode *P, SDValue N,
                     SDValue &Base, SDValue &Scale,
                     SDValue &Index, SDValue &Disp,
                     SDValue &Segment) {
      return tryFoldLoad(P, P, N, Base, Scale, Index, Disp, Segment);
    }

    bool tryFoldBroadcast(SDNode *Root, SDNode *P, SDValue N,
                          SDValue &Base, SDValue &Scale,
                          SDValue &Index, SDValue &Disp,
                          SDValue &Segment);

    bool isProfitableToFormMaskedOp(SDNode *N) const;

    /// Implement addressing mode selection for inline asm expressions.
    bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                      InlineAsm::ConstraintCode ConstraintID,
                                      std::vector<SDValue> &OutOps) override;

    void emitSpecialCodeForMain();

    inline void getAddressOperands(X86ISelAddressMode &AM, const SDLoc &DL,
                                   MVT VT, SDValue &Base, SDValue &Scale,
                                   SDValue &Index, SDValue &Disp,
                                   SDValue &Segment) {
      if (AM.BaseType == X86ISelAddressMode::FrameIndexBase)
        Base = CurDAG->getTargetFrameIndex(
            AM.Base_FrameIndex, TLI->getPointerTy(CurDAG->getDataLayout()));
      else if (AM.Base_Reg.getNode())
        Base = AM.Base_Reg;
      else
        Base = CurDAG->getRegister(0, VT);

      Scale = getI8Imm(AM.Scale, DL);

#define GET_ND_IF_ENABLED(OPC) (Subtarget->hasNDD() ? OPC##_ND : OPC)
      // Negate the index if needed.
      if (AM.NegateIndex) {
        unsigned NegOpc = VT == MVT::i64 ? GET_ND_IF_ENABLED(X86::NEG64r)
                                         : GET_ND_IF_ENABLED(X86::NEG32r);
        SDValue Neg = SDValue(CurDAG->getMachineNode(NegOpc, DL, VT, MVT::i32,
                                                     AM.IndexReg), 0);
        AM.IndexReg = Neg;
      }

      if (AM.IndexReg.getNode())
        Index = AM.IndexReg;
      else
        Index = CurDAG->getRegister(0, VT);

      // These are 32-bit even in 64-bit mode since RIP-relative offset
      // is 32-bit.
      if (AM.GV)
        Disp = CurDAG->getTargetGlobalAddress(AM.GV, SDLoc(),
                                              MVT::i32, AM.Disp,
                                              AM.SymbolFlags);
      else if (AM.CP)
        Disp = CurDAG->getTargetConstantPool(AM.CP, MVT::i32, AM.Alignment,
                                             AM.Disp, AM.SymbolFlags);
      else if (AM.ES) {
        assert(!AM.Disp && "Non-zero displacement is ignored with ES.");
        Disp = CurDAG->getTargetExternalSymbol(AM.ES, MVT::i32, AM.SymbolFlags);
      } else if (AM.MCSym) {
        assert(!AM.Disp && "Non-zero displacement is ignored with MCSym.");
        assert(AM.SymbolFlags == 0 && "oo");
        Disp = CurDAG->getMCSymbol(AM.MCSym, MVT::i32);
      } else if (AM.JT != -1) {
        assert(!AM.Disp && "Non-zero displacement is ignored with JT.");
        Disp = CurDAG->getTargetJumpTable(AM.JT, MVT::i32, AM.SymbolFlags);
      } else if (AM.BlockAddr)
        Disp = CurDAG->getTargetBlockAddress(AM.BlockAddr, MVT::i32, AM.Disp,
                                             AM.SymbolFlags);
      else
        Disp = CurDAG->getTargetConstant(AM.Disp, DL, MVT::i32);

      if (AM.Segment.getNode())
        Segment = AM.Segment;
      else
        Segment = CurDAG->getRegister(0, MVT::i16);
    }

    // Utility function to determine whether we should avoid selecting
    // immediate forms of instructions for better code size or not.
    // At a high level, we'd like to avoid such instructions when
    // we have similar constants used within the same basic block
    // that can be kept in a register.
    //
    bool shouldAvoidImmediateInstFormsForSize(SDNode *N) const {
      uint32_t UseCount = 0;

      // Do not want to hoist if we're not optimizing for size.
      // TODO: We'd like to remove this restriction.
      // See the comment in X86InstrInfo.td for more info.
      if (!CurDAG->shouldOptForSize())
        return false;

      // Walk all the users of the immediate.
      for (const SDNode *User : N->uses()) {
        if (UseCount >= 2)
          break;

        // This user is already selected. Count it as a legitimate use and
        // move on.
        if (User->isMachineOpcode()) {
          UseCount++;
          continue;
        }

        // We want to count stores of immediates as real uses.
        if (User->getOpcode() == ISD::STORE &&
            User->getOperand(1).getNode() == N) {
          UseCount++;
          continue;
        }

        // We don't currently match users that have > 2 operands (except
        // for stores, which are handled above)
        // Those instruction won't match in ISEL, for now, and would
        // be counted incorrectly.
        // This may change in the future as we add additional instruction
        // types.
        if (User->getNumOperands() != 2)
          continue;

        // If this is a sign-extended 8-bit integer immediate used in an ALU
        // instruction, there is probably an opcode encoding to save space.
        auto *C = dyn_cast<ConstantSDNode>(N);
        if (C && isInt<8>(C->getSExtValue()))
          continue;

        // Immediates that are used for offsets as part of stack
        // manipulation should be left alone. These are typically
        // used to indicate SP offsets for argument passing and
        // will get pulled into stores/pushes (implicitly).
        if (User->getOpcode() == X86ISD::ADD ||
            User->getOpcode() == ISD::ADD    ||
            User->getOpcode() == X86ISD::SUB ||
            User->getOpcode() == ISD::SUB) {

          // Find the other operand of the add/sub.
          SDValue OtherOp = User->getOperand(0);
          if (OtherOp.getNode() == N)
            OtherOp = User->getOperand(1);

          // Don't count if the other operand is SP.
          RegisterSDNode *RegNode;
          if (OtherOp->getOpcode() == ISD::CopyFromReg &&
              (RegNode = dyn_cast_or_null<RegisterSDNode>(
                 OtherOp->getOperand(1).getNode())))
            if ((RegNode->getReg() == X86::ESP) ||
                (RegNode->getReg() == X86::RSP))
              continue;
        }

        // ... otherwise, count this and move on.
        UseCount++;
      }

      // If we have more than 1 use, then recommend for hoisting.
      return (UseCount > 1);
    }

    /// Return a target constant with the specified value of type i8.
    inline SDValue getI8Imm(unsigned Imm, const SDLoc &DL) {
      return CurDAG->getTargetConstant(Imm, DL, MVT::i8);
    }

    /// Return a target constant with the specified value, of type i32.
    inline SDValue getI32Imm(unsigned Imm, const SDLoc &DL) {
      return CurDAG->getTargetConstant(Imm, DL, MVT::i32);
    }

    /// Return a target constant with the specified value, of type i64.
    inline SDValue getI64Imm(uint64_t Imm, const SDLoc &DL) {
      return CurDAG->getTargetConstant(Imm, DL, MVT::i64);
    }

    SDValue getExtractVEXTRACTImmediate(SDNode *N, unsigned VecWidth,
                                        const SDLoc &DL) {
      assert((VecWidth == 128 || VecWidth == 256) && "Unexpected vector width");
      uint64_t Index = N->getConstantOperandVal(1);
      MVT VecVT = N->getOperand(0).getSimpleValueType();
      return getI8Imm((Index * VecVT.getScalarSizeInBits()) / VecWidth, DL);
    }

    SDValue getInsertVINSERTImmediate(SDNode *N, unsigned VecWidth,
                                      const SDLoc &DL) {
      assert((VecWidth == 128 || VecWidth == 256) && "Unexpected vector width");
      uint64_t Index = N->getConstantOperandVal(2);
      MVT VecVT = N->getSimpleValueType(0);
      return getI8Imm((Index * VecVT.getScalarSizeInBits()) / VecWidth, DL);
    }

    SDValue getPermuteVINSERTCommutedImmediate(SDNode *N, unsigned VecWidth,
                                               const SDLoc &DL) {
      assert(VecWidth == 128 && "Unexpected vector width");
      uint64_t Index = N->getConstantOperandVal(2);
      MVT VecVT = N->getSimpleValueType(0);
      uint64_t InsertIdx = (Index * VecVT.getScalarSizeInBits()) / VecWidth;
      assert((InsertIdx == 0 || InsertIdx == 1) && "Bad insertf128 index");
      // vinsert(0,sub,vec) -> [sub0][vec1] -> vperm2x128(0x30,vec,sub)
      // vinsert(1,sub,vec) -> [vec0][sub0] -> vperm2x128(0x02,vec,sub)
      return getI8Imm(InsertIdx ? 0x02 : 0x30, DL);
    }

    SDValue getSBBZero(SDNode *N) {
      SDLoc dl(N);
      MVT VT = N->getSimpleValueType(0);

      // Create zero.
      SDVTList VTs = CurDAG->getVTList(MVT::i32, MVT::i32);
      SDValue Zero = SDValue(
          CurDAG->getMachineNode(X86::MOV32r0, dl, VTs, std::nullopt), 0);
      if (VT == MVT::i64) {
        Zero = SDValue(
            CurDAG->getMachineNode(
                TargetOpcode::SUBREG_TO_REG, dl, MVT::i64,
                CurDAG->getTargetConstant(0, dl, MVT::i64), Zero,
                CurDAG->getTargetConstant(X86::sub_32bit, dl, MVT::i32)),
            0);
      }

      // Copy flags to the EFLAGS register and glue it to next node.
      unsigned Opcode = N->getOpcode();
      assert((Opcode == X86ISD::SBB || Opcode == X86ISD::SETCC_CARRY) &&
             "Unexpected opcode for SBB materialization");
      unsigned FlagOpIndex = Opcode == X86ISD::SBB ? 2 : 1;
      SDValue EFLAGS =
          CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, X86::EFLAGS,
                               N->getOperand(FlagOpIndex), SDValue());

      // Create a 64-bit instruction if the result is 64-bits otherwise use the
      // 32-bit version.
      unsigned Opc = VT == MVT::i64 ? X86::SBB64rr : X86::SBB32rr;
      MVT SBBVT = VT == MVT::i64 ? MVT::i64 : MVT::i32;
      VTs = CurDAG->getVTList(SBBVT, MVT::i32);
      return SDValue(
          CurDAG->getMachineNode(Opc, dl, VTs,
                                 {Zero, Zero, EFLAGS, EFLAGS.getValue(1)}),
          0);
    }

    // Helper to detect unneeded and instructions on shift amounts. Called
    // from PatFrags in tablegen.
    bool isUnneededShiftMask(SDNode *N, unsigned Width) const {
      assert(N->getOpcode() == ISD::AND && "Unexpected opcode");
      const APInt &Val = N->getConstantOperandAPInt(1);

      if (Val.countr_one() >= Width)
        return true;

      APInt Mask = Val | CurDAG->computeKnownBits(N->getOperand(0)).Zero;
      return Mask.countr_one() >= Width;
    }

    /// Return an SDNode that returns the value of the global base register.
    /// Output instructions required to initialize the global base register,
    /// if necessary.
    SDNode *getGlobalBaseReg();

    /// Return a reference to the TargetMachine, casted to the target-specific
    /// type.
    const X86TargetMachine &getTargetMachine() const {
      return static_cast<const X86TargetMachine &>(TM);
    }

    /// Return a reference to the TargetInstrInfo, casted to the target-specific
    /// type.
    const X86InstrInfo *getInstrInfo() const {
      return Subtarget->getInstrInfo();
    }

    /// Return a condition code of the given SDNode
    X86::CondCode getCondFromNode(SDNode *N) const;

    /// Address-mode matching performs shift-of-and to and-of-shift
    /// reassociation in order to expose more scaled addressing
    /// opportunities.
    bool ComplexPatternFuncMutatesDAG() const override {
      return true;
    }

    bool isSExtAbsoluteSymbolRef(unsigned Width, SDNode *N) const;

    // Indicates we should prefer to use a non-temporal load for this load.
    bool useNonTemporalLoad(LoadSDNode *N) const {
      if (!N->isNonTemporal())
        return false;

      unsigned StoreSize = N->getMemoryVT().getStoreSize();

      if (N->getAlign().value() < StoreSize)
        return false;

      switch (StoreSize) {
      default: llvm_unreachable("Unsupported store size");
      case 4:
      case 8:
        return false;
      case 16:
        return Subtarget->hasSSE41();
      case 32:
        return Subtarget->hasAVX2();
      case 64:
        return Subtarget->hasAVX512();
      }
    }

    bool foldLoadStoreIntoMemOperand(SDNode *Node);
    MachineSDNode *matchBEXTRFromAndImm(SDNode *Node);
    bool matchBitExtract(SDNode *Node);
    bool shrinkAndImmediate(SDNode *N);
    bool isMaskZeroExtended(SDNode *N) const;
    bool tryShiftAmountMod(SDNode *N);
    bool tryShrinkShlLogicImm(SDNode *N);
    bool tryVPTERNLOG(SDNode *N);
    bool matchVPTERNLOG(SDNode *Root, SDNode *ParentA, SDNode *ParentB,
                        SDNode *ParentC, SDValue A, SDValue B, SDValue C,
                        uint8_t Imm);
    bool tryVPTESTM(SDNode *Root, SDValue Setcc, SDValue Mask);
    bool tryMatchBitSelect(SDNode *N);

    MachineSDNode *emitPCMPISTR(unsigned ROpc, unsigned MOpc, bool MayFoldLoad,
                                const SDLoc &dl, MVT VT, SDNode *Node);
    MachineSDNode *emitPCMPESTR(unsigned ROpc, unsigned MOpc, bool MayFoldLoad,
                                const SDLoc &dl, MVT VT, SDNode *Node,
                                SDValue &InGlue);

    bool tryOptimizeRem8Extend(SDNode *N);

    bool onlyUsesZeroFlag(SDValue Flags) const;
    bool hasNoSignFlagUses(SDValue Flags) const;
    bool hasNoCarryFlagUses(SDValue Flags) const;
  };

  class X86DAGToDAGISelLegacy : public SelectionDAGISelLegacy {
  public:
    static char ID;
    explicit X86DAGToDAGISelLegacy(X86TargetMachine &tm,
                                   CodeGenOptLevel OptLevel)
        : SelectionDAGISelLegacy(
              ID, std::make_unique<X86DAGToDAGISel>(tm, OptLevel)) {}
  };
}

char X86DAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(X86DAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

// Returns true if this masked compare can be implemented legally with this
// type.
static bool isLegalMaskCompare(SDNode *N, const X86Subtarget *Subtarget) {
  unsigned Opcode = N->getOpcode();
  if (Opcode == X86ISD::CMPM || Opcode == X86ISD::CMPMM ||
      Opcode == X86ISD::STRICT_CMPM || Opcode == ISD::SETCC ||
      Opcode == X86ISD::CMPMM_SAE || Opcode == X86ISD::VFPCLASS) {
    // We can get 256-bit 8 element types here without VLX being enabled. When
    // this happens we will use 512-bit operations and the mask will not be
    // zero extended.
    EVT OpVT = N->getOperand(0).getValueType();
    // The first operand of X86ISD::STRICT_CMPM is chain, so we need to get the
    // second operand.
    if (Opcode == X86ISD::STRICT_CMPM)
      OpVT = N->getOperand(1).getValueType();
    if (OpVT.is256BitVector() || OpVT.is128BitVector())
      return Subtarget->hasVLX();

    return true;
  }
  // Scalar opcodes use 128 bit registers, but aren't subject to the VLX check.
  if (Opcode == X86ISD::VFPCLASSS || Opcode == X86ISD::FSETCCM ||
      Opcode == X86ISD::FSETCCM_SAE)
    return true;

  return false;
}

// Returns true if we can assume the writer of the mask has zero extended it
// for us.
bool X86DAGToDAGISel::isMaskZeroExtended(SDNode *N) const {
  // If this is an AND, check if we have a compare on either side. As long as
  // one side guarantees the mask is zero extended, the AND will preserve those
  // zeros.
  if (N->getOpcode() == ISD::AND)
    return isLegalMaskCompare(N->getOperand(0).getNode(), Subtarget) ||
           isLegalMaskCompare(N->getOperand(1).getNode(), Subtarget);

  return isLegalMaskCompare(N, Subtarget);
}

bool
X86DAGToDAGISel::IsProfitableToFold(SDValue N, SDNode *U, SDNode *Root) const {
  if (OptLevel == CodeGenOptLevel::None)
    return false;

  if (!N.hasOneUse())
    return false;

  if (N.getOpcode() != ISD::LOAD)
    return true;

  // Don't fold non-temporal loads if we have an instruction for them.
  if (useNonTemporalLoad(cast<LoadSDNode>(N)))
    return false;

  // If N is a load, do additional profitability checks.
  if (U == Root) {
    switch (U->getOpcode()) {
    default: break;
    case X86ISD::ADD:
    case X86ISD::ADC:
    case X86ISD::SUB:
    case X86ISD::SBB:
    case X86ISD::AND:
    case X86ISD::XOR:
    case X86ISD::OR:
    case ISD::ADD:
    case ISD::UADDO_CARRY:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR: {
      SDValue Op1 = U->getOperand(1);

      // If the other operand is a 8-bit immediate we should fold the immediate
      // instead. This reduces code size.
      // e.g.
      // movl 4(%esp), %eax
      // addl $4, %eax
      // vs.
      // movl $4, %eax
      // addl 4(%esp), %eax
      // The former is 2 bytes shorter. In case where the increment is 1, then
      // the saving can be 4 bytes (by using incl %eax).
      if (auto *Imm = dyn_cast<ConstantSDNode>(Op1)) {
        if (Imm->getAPIntValue().isSignedIntN(8))
          return false;

        // If this is a 64-bit AND with an immediate that fits in 32-bits,
        // prefer using the smaller and over folding the load. This is needed to
        // make sure immediates created by shrinkAndImmediate are always folded.
        // Ideally we would narrow the load during DAG combine and get the
        // best of both worlds.
        if (U->getOpcode() == ISD::AND &&
            Imm->getAPIntValue().getBitWidth() == 64 &&
            Imm->getAPIntValue().isIntN(32))
          return false;

        // If this really a zext_inreg that can be represented with a movzx
        // instruction, prefer that.
        // TODO: We could shrink the load and fold if it is non-volatile.
        if (U->getOpcode() == ISD::AND &&
            (Imm->getAPIntValue() == UINT8_MAX ||
             Imm->getAPIntValue() == UINT16_MAX ||
             Imm->getAPIntValue() == UINT32_MAX))
          return false;

        // ADD/SUB with can negate the immediate and use the opposite operation
        // to fit 128 into a sign extended 8 bit immediate.
        if ((U->getOpcode() == ISD::ADD || U->getOpcode() == ISD::SUB) &&
            (-Imm->getAPIntValue()).isSignedIntN(8))
          return false;

        if ((U->getOpcode() == X86ISD::ADD || U->getOpcode() == X86ISD::SUB) &&
            (-Imm->getAPIntValue()).isSignedIntN(8) &&
            hasNoCarryFlagUses(SDValue(U, 1)))
          return false;
      }

      // If the other operand is a TLS address, we should fold it instead.
      // This produces
      // movl    %gs:0, %eax
      // leal    i@NTPOFF(%eax), %eax
      // instead of
      // movl    $i@NTPOFF, %eax
      // addl    %gs:0, %eax
      // if the block also has an access to a second TLS address this will save
      // a load.
      // FIXME: This is probably also true for non-TLS addresses.
      if (Op1.getOpcode() == X86ISD::Wrapper) {
        SDValue Val = Op1.getOperand(0);
        if (Val.getOpcode() == ISD::TargetGlobalTLSAddress)
          return false;
      }

      // Don't fold load if this matches the BTS/BTR/BTC patterns.
      // BTS: (or X, (shl 1, n))
      // BTR: (and X, (rotl -2, n))
      // BTC: (xor X, (shl 1, n))
      if (U->getOpcode() == ISD::OR || U->getOpcode() == ISD::XOR) {
        if (U->getOperand(0).getOpcode() == ISD::SHL &&
            isOneConstant(U->getOperand(0).getOperand(0)))
          return false;

        if (U->getOperand(1).getOpcode() == ISD::SHL &&
            isOneConstant(U->getOperand(1).getOperand(0)))
          return false;
      }
      if (U->getOpcode() == ISD::AND) {
        SDValue U0 = U->getOperand(0);
        SDValue U1 = U->getOperand(1);
        if (U0.getOpcode() == ISD::ROTL) {
          auto *C = dyn_cast<ConstantSDNode>(U0.getOperand(0));
          if (C && C->getSExtValue() == -2)
            return false;
        }

        if (U1.getOpcode() == ISD::ROTL) {
          auto *C = dyn_cast<ConstantSDNode>(U1.getOperand(0));
          if (C && C->getSExtValue() == -2)
            return false;
        }
      }

      break;
    }
    case ISD::SHL:
    case ISD::SRA:
    case ISD::SRL:
      // Don't fold a load into a shift by immediate. The BMI2 instructions
      // support folding a load, but not an immediate. The legacy instructions
      // support folding an immediate, but can't fold a load. Folding an
      // immediate is preferable to folding a load.
      if (isa<ConstantSDNode>(U->getOperand(1)))
        return false;

      break;
    }
  }

  // Prevent folding a load if this can implemented with an insert_subreg or
  // a move that implicitly zeroes.
  if (Root->getOpcode() == ISD::INSERT_SUBVECTOR &&
      isNullConstant(Root->getOperand(2)) &&
      (Root->getOperand(0).isUndef() ||
       ISD::isBuildVectorAllZeros(Root->getOperand(0).getNode())))
    return false;

  return true;
}

// Indicates it is profitable to form an AVX512 masked operation. Returning
// false will favor a masked register-register masked move or vblendm and the
// operation will be selected separately.
bool X86DAGToDAGISel::isProfitableToFormMaskedOp(SDNode *N) const {
  assert(
      (N->getOpcode() == ISD::VSELECT || N->getOpcode() == X86ISD::SELECTS) &&
      "Unexpected opcode!");

  // If the operation has additional users, the operation will be duplicated.
  // Check the use count to prevent that.
  // FIXME: Are there cheap opcodes we might want to duplicate?
  return N->getOperand(1).hasOneUse();
}

/// Replace the original chain operand of the call with
/// load's chain operand and move load below the call's chain operand.
static void moveBelowOrigChain(SelectionDAG *CurDAG, SDValue Load,
                               SDValue Call, SDValue OrigChain) {
  SmallVector<SDValue, 8> Ops;
  SDValue Chain = OrigChain.getOperand(0);
  if (Chain.getNode() == Load.getNode())
    Ops.push_back(Load.getOperand(0));
  else {
    assert(Chain.getOpcode() == ISD::TokenFactor &&
           "Unexpected chain operand");
    for (unsigned i = 0, e = Chain.getNumOperands(); i != e; ++i)
      if (Chain.getOperand(i).getNode() == Load.getNode())
        Ops.push_back(Load.getOperand(0));
      else
        Ops.push_back(Chain.getOperand(i));
    SDValue NewChain =
      CurDAG->getNode(ISD::TokenFactor, SDLoc(Load), MVT::Other, Ops);
    Ops.clear();
    Ops.push_back(NewChain);
  }
  Ops.append(OrigChain->op_begin() + 1, OrigChain->op_end());
  CurDAG->UpdateNodeOperands(OrigChain.getNode(), Ops);
  CurDAG->UpdateNodeOperands(Load.getNode(), Call.getOperand(0),
                             Load.getOperand(1), Load.getOperand(2));

  Ops.clear();
  Ops.push_back(SDValue(Load.getNode(), 1));
  Ops.append(Call->op_begin() + 1, Call->op_end());
  CurDAG->UpdateNodeOperands(Call.getNode(), Ops);
}

/// Return true if call address is a load and it can be
/// moved below CALLSEQ_START and the chains leading up to the call.
/// Return the CALLSEQ_START by reference as a second output.
/// In the case of a tail call, there isn't a callseq node between the call
/// chain and the load.
static bool isCalleeLoad(SDValue Callee, SDValue &Chain, bool HasCallSeq) {
  // The transformation is somewhat dangerous if the call's chain was glued to
  // the call. After MoveBelowOrigChain the load is moved between the call and
  // the chain, this can create a cycle if the load is not folded. So it is
  // *really* important that we are sure the load will be folded.
  if (Callee.getNode() == Chain.getNode() || !Callee.hasOneUse())
    return false;
  auto *LD = dyn_cast<LoadSDNode>(Callee.getNode());
  if (!LD ||
      !LD->isSimple() ||
      LD->getAddressingMode() != ISD::UNINDEXED ||
      LD->getExtensionType() != ISD::NON_EXTLOAD)
    return false;

  // Now let's find the callseq_start.
  while (HasCallSeq && Chain.getOpcode() != ISD::CALLSEQ_START) {
    if (!Chain.hasOneUse())
      return false;
    Chain = Chain.getOperand(0);
  }

  if (!Chain.getNumOperands())
    return false;
  // Since we are not checking for AA here, conservatively abort if the chain
  // writes to memory. It's not safe to move the callee (a load) across a store.
  if (isa<MemSDNode>(Chain.getNode()) &&
      cast<MemSDNode>(Chain.getNode())->writeMem())
    return false;
  if (Chain.getOperand(0).getNode() == Callee.getNode())
    return true;
  if (Chain.getOperand(0).getOpcode() == ISD::TokenFactor &&
      Callee.getValue(1).isOperandOf(Chain.getOperand(0).getNode()) &&
      Callee.getValue(1).hasOneUse())
    return true;
  return false;
}

static bool isEndbrImm64(uint64_t Imm) {
// There may be some other prefix bytes between 0xF3 and 0x0F1EFA.
// i.g: 0xF3660F1EFA, 0xF3670F1EFA
  if ((Imm & 0x00FFFFFF) != 0x0F1EFA)
    return false;

  uint8_t OptionalPrefixBytes [] = {0x26, 0x2e, 0x36, 0x3e, 0x64,
                                    0x65, 0x66, 0x67, 0xf0, 0xf2};
  int i = 24; // 24bit 0x0F1EFA has matched
  while (i < 64) {
    uint8_t Byte = (Imm >> i) & 0xFF;
    if (Byte == 0xF3)
      return true;
    if (!llvm::is_contained(OptionalPrefixBytes, Byte))
      return false;
    i += 8;
  }

  return false;
}

static bool needBWI(MVT VT) {
  return (VT == MVT::v32i16 || VT == MVT::v32f16 || VT == MVT::v64i8);
}

void X86DAGToDAGISel::PreprocessISelDAG() {
  bool MadeChange = false;
  for (SelectionDAG::allnodes_iterator I = CurDAG->allnodes_begin(),
       E = CurDAG->allnodes_end(); I != E; ) {
    SDNode *N = &*I++; // Preincrement iterator to avoid invalidation issues.

    // This is for CET enhancement.
    //
    // ENDBR32 and ENDBR64 have specific opcodes:
    // ENDBR32: F3 0F 1E FB
    // ENDBR64: F3 0F 1E FA
    // And we want that attackers won’t find unintended ENDBR32/64
    // opcode matches in the binary
    // Here’s an example:
    // If the compiler had to generate asm for the following code:
    // a = 0xF30F1EFA
    // it could, for example, generate:
    // mov 0xF30F1EFA, dword ptr[a]
    // In such a case, the binary would include a gadget that starts
    // with a fake ENDBR64 opcode. Therefore, we split such generation
    // into multiple operations, let it not shows in the binary
    if (N->getOpcode() == ISD::Constant) {
      MVT VT = N->getSimpleValueType(0);
      int64_t Imm = cast<ConstantSDNode>(N)->getSExtValue();
      int32_t EndbrImm = Subtarget->is64Bit() ? 0xF30F1EFA : 0xF30F1EFB;
      if (Imm == EndbrImm || isEndbrImm64(Imm)) {
        // Check that the cf-protection-branch is enabled.
        Metadata *CFProtectionBranch =
            MF->getFunction().getParent()->getModuleFlag(
                "cf-protection-branch");
        if (CFProtectionBranch || IndirectBranchTracking) {
          SDLoc dl(N);
          SDValue Complement = CurDAG->getConstant(~Imm, dl, VT, false, true);
          Complement = CurDAG->getNOT(dl, Complement, VT);
          --I;
          CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), Complement);
          ++I;
          MadeChange = true;
          continue;
        }
      }
    }

    // If this is a target specific AND node with no flag usages, turn it back
    // into ISD::AND to enable test instruction matching.
    if (N->getOpcode() == X86ISD::AND && !N->hasAnyUseOfValue(1)) {
      SDValue Res = CurDAG->getNode(ISD::AND, SDLoc(N), N->getValueType(0),
                                    N->getOperand(0), N->getOperand(1));
      --I;
      CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), Res);
      ++I;
      MadeChange = true;
      continue;
    }

    // Convert vector increment or decrement to sub/add with an all-ones
    // constant:
    // add X, <1, 1...> --> sub X, <-1, -1...>
    // sub X, <1, 1...> --> add X, <-1, -1...>
    // The all-ones vector constant can be materialized using a pcmpeq
    // instruction that is commonly recognized as an idiom (has no register
    // dependency), so that's better/smaller than loading a splat 1 constant.
    //
    // But don't do this if it would inhibit a potentially profitable load
    // folding opportunity for the other operand. That only occurs with the
    // intersection of:
    // (1) The other operand (op0) is load foldable.
    // (2) The op is an add (otherwise, we are *creating* an add and can still
    //     load fold the other op).
    // (3) The target has AVX (otherwise, we have a destructive add and can't
    //     load fold the other op without killing the constant op).
    // (4) The constant 1 vector has multiple uses (so it is profitable to load
    //     into a register anyway).
    auto mayPreventLoadFold = [&]() {
      return X86::mayFoldLoad(N->getOperand(0), *Subtarget) &&
             N->getOpcode() == ISD::ADD && Subtarget->hasAVX() &&
             !N->getOperand(1).hasOneUse();
    };
    if ((N->getOpcode() == ISD::ADD || N->getOpcode() == ISD::SUB) &&
        N->getSimpleValueType(0).isVector() && !mayPreventLoadFold()) {
      APInt SplatVal;
      if (X86::isConstantSplat(N->getOperand(1), SplatVal) &&
          SplatVal.isOne()) {
        SDLoc DL(N);

        MVT VT = N->getSimpleValueType(0);
        unsigned NumElts = VT.getSizeInBits() / 32;
        SDValue AllOnes =
            CurDAG->getAllOnesConstant(DL, MVT::getVectorVT(MVT::i32, NumElts));
        AllOnes = CurDAG->getBitcast(VT, AllOnes);

        unsigned NewOpcode = N->getOpcode() == ISD::ADD ? ISD::SUB : ISD::ADD;
        SDValue Res =
            CurDAG->getNode(NewOpcode, DL, VT, N->getOperand(0), AllOnes);
        --I;
        CurDAG->ReplaceAllUsesWith(N, Res.getNode());
        ++I;
        MadeChange = true;
        continue;
      }
    }

    switch (N->getOpcode()) {
    case X86ISD::VBROADCAST: {
      MVT VT = N->getSimpleValueType(0);
      // Emulate v32i16/v64i8 broadcast without BWI.
      if (!Subtarget->hasBWI() && needBWI(VT)) {
        MVT NarrowVT = VT.getHalfNumVectorElementsVT();
        SDLoc dl(N);
        SDValue NarrowBCast =
            CurDAG->getNode(X86ISD::VBROADCAST, dl, NarrowVT, N->getOperand(0));
        SDValue Res =
            CurDAG->getNode(ISD::INSERT_SUBVECTOR, dl, VT, CurDAG->getUNDEF(VT),
                            NarrowBCast, CurDAG->getIntPtrConstant(0, dl));
        unsigned Index = NarrowVT.getVectorMinNumElements();
        Res = CurDAG->getNode(ISD::INSERT_SUBVECTOR, dl, VT, Res, NarrowBCast,
                              CurDAG->getIntPtrConstant(Index, dl));

        --I;
        CurDAG->ReplaceAllUsesWith(N, Res.getNode());
        ++I;
        MadeChange = true;
        continue;
      }

      break;
    }
    case X86ISD::VBROADCAST_LOAD: {
      MVT VT = N->getSimpleValueType(0);
      // Emulate v32i16/v64i8 broadcast without BWI.
      if (!Subtarget->hasBWI() && needBWI(VT)) {
        MVT NarrowVT = VT.getHalfNumVectorElementsVT();
        auto *MemNode = cast<MemSDNode>(N);
        SDLoc dl(N);
        SDVTList VTs = CurDAG->getVTList(NarrowVT, MVT::Other);
        SDValue Ops[] = {MemNode->getChain(), MemNode->getBasePtr()};
        SDValue NarrowBCast = CurDAG->getMemIntrinsicNode(
            X86ISD::VBROADCAST_LOAD, dl, VTs, Ops, MemNode->getMemoryVT(),
            MemNode->getMemOperand());
        SDValue Res =
            CurDAG->getNode(ISD::INSERT_SUBVECTOR, dl, VT, CurDAG->getUNDEF(VT),
                            NarrowBCast, CurDAG->getIntPtrConstant(0, dl));
        unsigned Index = NarrowVT.getVectorMinNumElements();
        Res = CurDAG->getNode(ISD::INSERT_SUBVECTOR, dl, VT, Res, NarrowBCast,
                              CurDAG->getIntPtrConstant(Index, dl));

        --I;
        SDValue To[] = {Res, NarrowBCast.getValue(1)};
        CurDAG->ReplaceAllUsesWith(N, To);
        ++I;
        MadeChange = true;
        continue;
      }

      break;
    }
    case ISD::LOAD: {
      // If this is a XMM/YMM load of the same lower bits as another YMM/ZMM
      // load, then just extract the lower subvector and avoid the second load.
      auto *Ld = cast<LoadSDNode>(N);
      MVT VT = N->getSimpleValueType(0);
      if (!ISD::isNormalLoad(Ld) || !Ld->isSimple() ||
          !(VT.is128BitVector() || VT.is256BitVector()))
        break;

      MVT MaxVT = VT;
      SDNode *MaxLd = nullptr;
      SDValue Ptr = Ld->getBasePtr();
      SDValue Chain = Ld->getChain();
      for (SDNode *User : Ptr->uses()) {
        auto *UserLd = dyn_cast<LoadSDNode>(User);
        MVT UserVT = User->getSimpleValueType(0);
        if (User != N && UserLd && ISD::isNormalLoad(User) &&
            UserLd->getBasePtr() == Ptr && UserLd->getChain() == Chain &&
            !User->hasAnyUseOfValue(1) &&
            (UserVT.is256BitVector() || UserVT.is512BitVector()) &&
            UserVT.getSizeInBits() > VT.getSizeInBits() &&
            (!MaxLd || UserVT.getSizeInBits() > MaxVT.getSizeInBits())) {
          MaxLd = User;
          MaxVT = UserVT;
        }
      }
      if (MaxLd) {
        SDLoc dl(N);
        unsigned NumSubElts = VT.getSizeInBits() / MaxVT.getScalarSizeInBits();
        MVT SubVT = MVT::getVectorVT(MaxVT.getScalarType(), NumSubElts);
        SDValue Extract = CurDAG->getNode(ISD::EXTRACT_SUBVECTOR, dl, SubVT,
                                          SDValue(MaxLd, 0),
                                          CurDAG->getIntPtrConstant(0, dl));
        SDValue Res = CurDAG->getBitcast(VT, Extract);

        --I;
        SDValue To[] = {Res, SDValue(MaxLd, 1)};
        CurDAG->ReplaceAllUsesWith(N, To);
        ++I;
        MadeChange = true;
        continue;
      }
      break;
    }
    case ISD::VSELECT: {
      // Replace VSELECT with non-mask conditions with with BLENDV/VPTERNLOG.
      EVT EleVT = N->getOperand(0).getValueType().getVectorElementType();
      if (EleVT == MVT::i1)
        break;

      assert(Subtarget->hasSSE41() && "Expected SSE4.1 support!");
      assert(N->getValueType(0).getVectorElementType() != MVT::i16 &&
             "We can't replace VSELECT with BLENDV in vXi16!");
      SDValue R;
      if (Subtarget->hasVLX() && CurDAG->ComputeNumSignBits(N->getOperand(0)) ==
                                     EleVT.getSizeInBits()) {
        R = CurDAG->getNode(X86ISD::VPTERNLOG, SDLoc(N), N->getValueType(0),
                            N->getOperand(0), N->getOperand(1), N->getOperand(2),
                            CurDAG->getTargetConstant(0xCA, SDLoc(N), MVT::i8));
      } else {
        R = CurDAG->getNode(X86ISD::BLENDV, SDLoc(N), N->getValueType(0),
                            N->getOperand(0), N->getOperand(1),
                            N->getOperand(2));
      }
      --I;
      CurDAG->ReplaceAllUsesWith(N, R.getNode());
      ++I;
      MadeChange = true;
      continue;
    }
    case ISD::FP_ROUND:
    case ISD::STRICT_FP_ROUND:
    case ISD::FP_TO_SINT:
    case ISD::FP_TO_UINT:
    case ISD::STRICT_FP_TO_SINT:
    case ISD::STRICT_FP_TO_UINT: {
      // Replace vector fp_to_s/uint with their X86 specific equivalent so we
      // don't need 2 sets of patterns.
      if (!N->getSimpleValueType(0).isVector())
        break;

      unsigned NewOpc;
      switch (N->getOpcode()) {
      default: llvm_unreachable("Unexpected opcode!");
      case ISD::FP_ROUND:          NewOpc = X86ISD::VFPROUND;        break;
      case ISD::STRICT_FP_ROUND:   NewOpc = X86ISD::STRICT_VFPROUND; break;
      case ISD::STRICT_FP_TO_SINT: NewOpc = X86ISD::STRICT_CVTTP2SI; break;
      case ISD::FP_TO_SINT:        NewOpc = X86ISD::CVTTP2SI;        break;
      case ISD::STRICT_FP_TO_UINT: NewOpc = X86ISD::STRICT_CVTTP2UI; break;
      case ISD::FP_TO_UINT:        NewOpc = X86ISD::CVTTP2UI;        break;
      }
      SDValue Res;
      if (N->isStrictFPOpcode())
        Res =
            CurDAG->getNode(NewOpc, SDLoc(N), {N->getValueType(0), MVT::Other},
                            {N->getOperand(0), N->getOperand(1)});
      else
        Res =
            CurDAG->getNode(NewOpc, SDLoc(N), N->getValueType(0),
                            N->getOperand(0));
      --I;
      CurDAG->ReplaceAllUsesWith(N, Res.getNode());
      ++I;
      MadeChange = true;
      continue;
    }
    case ISD::SHL:
    case ISD::SRA:
    case ISD::SRL: {
      // Replace vector shifts with their X86 specific equivalent so we don't
      // need 2 sets of patterns.
      if (!N->getValueType(0).isVector())
        break;

      unsigned NewOpc;
      switch (N->getOpcode()) {
      default: llvm_unreachable("Unexpected opcode!");
      case ISD::SHL: NewOpc = X86ISD::VSHLV; break;
      case ISD::SRA: NewOpc = X86ISD::VSRAV; break;
      case ISD::SRL: NewOpc = X86ISD::VSRLV; break;
      }
      SDValue Res = CurDAG->getNode(NewOpc, SDLoc(N), N->getValueType(0),
                                    N->getOperand(0), N->getOperand(1));
      --I;
      CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), Res);
      ++I;
      MadeChange = true;
      continue;
    }
    case ISD::ANY_EXTEND:
    case ISD::ANY_EXTEND_VECTOR_INREG: {
      // Replace vector any extend with the zero extend equivalents so we don't
      // need 2 sets of patterns. Ignore vXi1 extensions.
      if (!N->getValueType(0).isVector())
        break;

      unsigned NewOpc;
      if (N->getOperand(0).getScalarValueSizeInBits() == 1) {
        assert(N->getOpcode() == ISD::ANY_EXTEND &&
               "Unexpected opcode for mask vector!");
        NewOpc = ISD::SIGN_EXTEND;
      } else {
        NewOpc = N->getOpcode() == ISD::ANY_EXTEND
                              ? ISD::ZERO_EXTEND
                              : ISD::ZERO_EXTEND_VECTOR_INREG;
      }

      SDValue Res = CurDAG->getNode(NewOpc, SDLoc(N), N->getValueType(0),
                                    N->getOperand(0));
      --I;
      CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), Res);
      ++I;
      MadeChange = true;
      continue;
    }
    case ISD::FCEIL:
    case ISD::STRICT_FCEIL:
    case ISD::FFLOOR:
    case ISD::STRICT_FFLOOR:
    case ISD::FTRUNC:
    case ISD::STRICT_FTRUNC:
    case ISD::FROUNDEVEN:
    case ISD::STRICT_FROUNDEVEN:
    case ISD::FNEARBYINT:
    case ISD::STRICT_FNEARBYINT:
    case ISD::FRINT:
    case ISD::STRICT_FRINT: {
      // Replace fp rounding with their X86 specific equivalent so we don't
      // need 2 sets of patterns.
      unsigned Imm;
      switch (N->getOpcode()) {
      default: llvm_unreachable("Unexpected opcode!");
      case ISD::STRICT_FCEIL:
      case ISD::FCEIL:      Imm = 0xA; break;
      case ISD::STRICT_FFLOOR:
      case ISD::FFLOOR:     Imm = 0x9; break;
      case ISD::STRICT_FTRUNC:
      case ISD::FTRUNC:     Imm = 0xB; break;
      case ISD::STRICT_FROUNDEVEN:
      case ISD::FROUNDEVEN: Imm = 0x8; break;
      case ISD::STRICT_FNEARBYINT:
      case ISD::FNEARBYINT: Imm = 0xC; break;
      case ISD::STRICT_FRINT:
      case ISD::FRINT:      Imm = 0x4; break;
      }
      SDLoc dl(N);
      bool IsStrict = N->isStrictFPOpcode();
      SDValue Res;
      if (IsStrict)
        Res = CurDAG->getNode(X86ISD::STRICT_VRNDSCALE, dl,
                              {N->getValueType(0), MVT::Other},
                              {N->getOperand(0), N->getOperand(1),
                               CurDAG->getTargetConstant(Imm, dl, MVT::i32)});
      else
        Res = CurDAG->getNode(X86ISD::VRNDSCALE, dl, N->getValueType(0),
                              N->getOperand(0),
                              CurDAG->getTargetConstant(Imm, dl, MVT::i32));
      --I;
      CurDAG->ReplaceAllUsesWith(N, Res.getNode());
      ++I;
      MadeChange = true;
      continue;
    }
    case X86ISD::FANDN:
    case X86ISD::FAND:
    case X86ISD::FOR:
    case X86ISD::FXOR: {
      // Widen scalar fp logic ops to vector to reduce isel patterns.
      // FIXME: Can we do this during lowering/combine.
      MVT VT = N->getSimpleValueType(0);
      if (VT.isVector() || VT == MVT::f128)
        break;

      MVT VecVT = VT == MVT::f64   ? MVT::v2f64
                  : VT == MVT::f32 ? MVT::v4f32
                                   : MVT::v8f16;

      SDLoc dl(N);
      SDValue Op0 = CurDAG->getNode(ISD::SCALAR_TO_VECTOR, dl, VecVT,
                                    N->getOperand(0));
      SDValue Op1 = CurDAG->getNode(ISD::SCALAR_TO_VECTOR, dl, VecVT,
                                    N->getOperand(1));

      SDValue Res;
      if (Subtarget->hasSSE2()) {
        EVT IntVT = EVT(VecVT).changeVectorElementTypeToInteger();
        Op0 = CurDAG->getNode(ISD::BITCAST, dl, IntVT, Op0);
        Op1 = CurDAG->getNode(ISD::BITCAST, dl, IntVT, Op1);
        unsigned Opc;
        switch (N->getOpcode()) {
        default: llvm_unreachable("Unexpected opcode!");
        case X86ISD::FANDN: Opc = X86ISD::ANDNP; break;
        case X86ISD::FAND:  Opc = ISD::AND;      break;
        case X86ISD::FOR:   Opc = ISD::OR;       break;
        case X86ISD::FXOR:  Opc = ISD::XOR;      break;
        }
        Res = CurDAG->getNode(Opc, dl, IntVT, Op0, Op1);
        Res = CurDAG->getNode(ISD::BITCAST, dl, VecVT, Res);
      } else {
        Res = CurDAG->getNode(N->getOpcode(), dl, VecVT, Op0, Op1);
      }
      Res = CurDAG->getNode(ISD::EXTRACT_VECTOR_ELT, dl, VT, Res,
                            CurDAG->getIntPtrConstant(0, dl));
      --I;
      CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), Res);
      ++I;
      MadeChange = true;
      continue;
    }
    }

    if (OptLevel != CodeGenOptLevel::None &&
        // Only do this when the target can fold the load into the call or
        // jmp.
        !Subtarget->useIndirectThunkCalls() &&
        ((N->getOpcode() == X86ISD::CALL && !Subtarget->slowTwoMemOps()) ||
         (N->getOpcode() == X86ISD::TC_RETURN &&
          (Subtarget->is64Bit() ||
           !getTargetMachine().isPositionIndependent())))) {
      /// Also try moving call address load from outside callseq_start to just
      /// before the call to allow it to be folded.
      ///
      ///     [Load chain]
      ///         ^
      ///         |
      ///       [Load]
      ///       ^    ^
      ///       |    |
      ///      /      \--
      ///     /          |
      ///[CALLSEQ_START] |
      ///     ^          |
      ///     |          |
      /// [LOAD/C2Reg]   |
      ///     |          |
      ///      \        /
      ///       \      /
      ///       [CALL]
      bool HasCallSeq = N->getOpcode() == X86ISD::CALL;
      SDValue Chain = N->getOperand(0);
      SDValue Load  = N->getOperand(1);
      if (!isCalleeLoad(Load, Chain, HasCallSeq))
        continue;
      moveBelowOrigChain(CurDAG, Load, SDValue(N, 0), Chain);
      ++NumLoadMoved;
      MadeChange = true;
      continue;
    }

    // Lower fpround and fpextend nodes that target the FP stack to be store and
    // load to the stack.  This is a gross hack.  We would like to simply mark
    // these as being illegal, but when we do that, legalize produces these when
    // it expands calls, then expands these in the same legalize pass.  We would
    // like dag combine to be able to hack on these between the call expansion
    // and the node legalization.  As such this pass basically does "really
    // late" legalization of these inline with the X86 isel pass.
    // FIXME: This should only happen when not compiled with -O0.
    switch (N->getOpcode()) {
    default: continue;
    case ISD::FP_ROUND:
    case ISD::FP_EXTEND:
    {
      MVT SrcVT = N->getOperand(0).getSimpleValueType();
      MVT DstVT = N->getSimpleValueType(0);

      // If any of the sources are vectors, no fp stack involved.
      if (SrcVT.isVector() || DstVT.isVector())
        continue;

      // If the source and destination are SSE registers, then this is a legal
      // conversion that should not be lowered.
      const X86TargetLowering *X86Lowering =
          static_cast<const X86TargetLowering *>(TLI);
      bool SrcIsSSE = X86Lowering->isScalarFPTypeInSSEReg(SrcVT);
      bool DstIsSSE = X86Lowering->isScalarFPTypeInSSEReg(DstVT);
      if (SrcIsSSE && DstIsSSE)
        continue;

      if (!SrcIsSSE && !DstIsSSE) {
        // If this is an FPStack extension, it is a noop.
        if (N->getOpcode() == ISD::FP_EXTEND)
          continue;
        // If this is a value-preserving FPStack truncation, it is a noop.
        if (N->getConstantOperandVal(1))
          continue;
      }

      // Here we could have an FP stack truncation or an FPStack <-> SSE convert.
      // FPStack has extload and truncstore.  SSE can fold direct loads into other
      // operations.  Based on this, decide what we want to do.
      MVT MemVT = (N->getOpcode() == ISD::FP_ROUND) ? DstVT : SrcVT;
      SDValue MemTmp = CurDAG->CreateStackTemporary(MemVT);
      int SPFI = cast<FrameIndexSDNode>(MemTmp)->getIndex();
      MachinePointerInfo MPI =
          MachinePointerInfo::getFixedStack(CurDAG->getMachineFunction(), SPFI);
      SDLoc dl(N);

      // FIXME: optimize the case where the src/dest is a load or store?

      SDValue Store = CurDAG->getTruncStore(
          CurDAG->getEntryNode(), dl, N->getOperand(0), MemTmp, MPI, MemVT);
      SDValue Result = CurDAG->getExtLoad(ISD::EXTLOAD, dl, DstVT, Store,
                                          MemTmp, MPI, MemVT);

      // We're about to replace all uses of the FP_ROUND/FP_EXTEND with the
      // extload we created.  This will cause general havok on the dag because
      // anything below the conversion could be folded into other existing nodes.
      // To avoid invalidating 'I', back it up to the convert node.
      --I;
      CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), Result);
      break;
    }

    //The sequence of events for lowering STRICT_FP versions of these nodes requires
    //dealing with the chain differently, as there is already a preexisting chain.
    case ISD::STRICT_FP_ROUND:
    case ISD::STRICT_FP_EXTEND:
    {
      MVT SrcVT = N->getOperand(1).getSimpleValueType();
      MVT DstVT = N->getSimpleValueType(0);

      // If any of the sources are vectors, no fp stack involved.
      if (SrcVT.isVector() || DstVT.isVector())
        continue;

      // If the source and destination are SSE registers, then this is a legal
      // conversion that should not be lowered.
      const X86TargetLowering *X86Lowering =
          static_cast<const X86TargetLowering *>(TLI);
      bool SrcIsSSE = X86Lowering->isScalarFPTypeInSSEReg(SrcVT);
      bool DstIsSSE = X86Lowering->isScalarFPTypeInSSEReg(DstVT);
      if (SrcIsSSE && DstIsSSE)
        continue;

      if (!SrcIsSSE && !DstIsSSE) {
        // If this is an FPStack extension, it is a noop.
        if (N->getOpcode() == ISD::STRICT_FP_EXTEND)
          continue;
        // If this is a value-preserving FPStack truncation, it is a noop.
        if (N->getConstantOperandVal(2))
          continue;
      }

      // Here we could have an FP stack truncation or an FPStack <-> SSE convert.
      // FPStack has extload and truncstore.  SSE can fold direct loads into other
      // operations.  Based on this, decide what we want to do.
      MVT MemVT = (N->getOpcode() == ISD::STRICT_FP_ROUND) ? DstVT : SrcVT;
      SDValue MemTmp = CurDAG->CreateStackTemporary(MemVT);
      int SPFI = cast<FrameIndexSDNode>(MemTmp)->getIndex();
      MachinePointerInfo MPI =
          MachinePointerInfo::getFixedStack(CurDAG->getMachineFunction(), SPFI);
      SDLoc dl(N);

      // FIXME: optimize the case where the src/dest is a load or store?

      //Since the operation is StrictFP, use the preexisting chain.
      SDValue Store, Result;
      if (!SrcIsSSE) {
        SDVTList VTs = CurDAG->getVTList(MVT::Other);
        SDValue Ops[] = {N->getOperand(0), N->getOperand(1), MemTmp};
        Store = CurDAG->getMemIntrinsicNode(X86ISD::FST, dl, VTs, Ops, MemVT,
                                            MPI, /*Align*/ std::nullopt,
                                            MachineMemOperand::MOStore);
        if (N->getFlags().hasNoFPExcept()) {
          SDNodeFlags Flags = Store->getFlags();
          Flags.setNoFPExcept(true);
          Store->setFlags(Flags);
        }
      } else {
        assert(SrcVT == MemVT && "Unexpected VT!");
        Store = CurDAG->getStore(N->getOperand(0), dl, N->getOperand(1), MemTmp,
                                 MPI);
      }

      if (!DstIsSSE) {
        SDVTList VTs = CurDAG->getVTList(DstVT, MVT::Other);
        SDValue Ops[] = {Store, MemTmp};
        Result = CurDAG->getMemIntrinsicNode(
            X86ISD::FLD, dl, VTs, Ops, MemVT, MPI,
            /*Align*/ std::nullopt, MachineMemOperand::MOLoad);
        if (N->getFlags().hasNoFPExcept()) {
          SDNodeFlags Flags = Result->getFlags();
          Flags.setNoFPExcept(true);
          Result->setFlags(Flags);
        }
      } else {
        assert(DstVT == MemVT && "Unexpected VT!");
        Result = CurDAG->getLoad(DstVT, dl, Store, MemTmp, MPI);
      }

      // We're about to replace all uses of the FP_ROUND/FP_EXTEND with the
      // extload we created.  This will cause general havok on the dag because
      // anything below the conversion could be folded into other existing nodes.
      // To avoid invalidating 'I', back it up to the convert node.
      --I;
      CurDAG->ReplaceAllUsesWith(N, Result.getNode());
      break;
    }
    }


    // Now that we did that, the node is dead.  Increment the iterator to the
    // next node to process, then delete N.
    ++I;
    MadeChange = true;
  }

  // Remove any dead nodes that may have been left behind.
  if (MadeChange)
    CurDAG->RemoveDeadNodes();
}

// Look for a redundant movzx/movsx that can occur after an 8-bit divrem.
bool X86DAGToDAGISel::tryOptimizeRem8Extend(SDNode *N) {
  unsigned Opc = N->getMachineOpcode();
  if (Opc != X86::MOVZX32rr8 && Opc != X86::MOVSX32rr8 &&
      Opc != X86::MOVSX64rr8)
    return false;

  SDValue N0 = N->getOperand(0);

  // We need to be extracting the lower bit of an extend.
  if (!N0.isMachineOpcode() ||
      N0.getMachineOpcode() != TargetOpcode::EXTRACT_SUBREG ||
      N0.getConstantOperandVal(1) != X86::sub_8bit)
    return false;

  // We're looking for either a movsx or movzx to match the original opcode.
  unsigned ExpectedOpc = Opc == X86::MOVZX32rr8 ? X86::MOVZX32rr8_NOREX
                                                : X86::MOVSX32rr8_NOREX;
  SDValue N00 = N0.getOperand(0);
  if (!N00.isMachineOpcode() || N00.getMachineOpcode() != ExpectedOpc)
    return false;

  if (Opc == X86::MOVSX64rr8) {
    // If we had a sign extend from 8 to 64 bits. We still need to go from 32
    // to 64.
    MachineSDNode *Extend = CurDAG->getMachineNode(X86::MOVSX64rr32, SDLoc(N),
                                                   MVT::i64, N00);
    ReplaceUses(N, Extend);
  } else {
    // Ok we can drop this extend and just use the original extend.
    ReplaceUses(N, N00.getNode());
  }

  return true;
}

void X86DAGToDAGISel::PostprocessISelDAG() {
  // Skip peepholes at -O0.
  if (TM.getOptLevel() == CodeGenOptLevel::None)
    return;

  SelectionDAG::allnodes_iterator Position = CurDAG->allnodes_end();

  bool MadeChange = false;
  while (Position != CurDAG->allnodes_begin()) {
    SDNode *N = &*--Position;
    // Skip dead nodes and any non-machine opcodes.
    if (N->use_empty() || !N->isMachineOpcode())
      continue;

    if (tryOptimizeRem8Extend(N)) {
      MadeChange = true;
      continue;
    }

    unsigned Opc = N->getMachineOpcode();
    switch (Opc) {
    default:
      continue;
    // ANDrr/rm + TESTrr+ -> TESTrr/TESTmr
    case X86::TEST8rr:
    case X86::TEST16rr:
    case X86::TEST32rr:
    case X86::TEST64rr:
    // ANDrr/rm + CTESTrr -> CTESTrr/CTESTmr
    case X86::CTEST8rr:
    case X86::CTEST16rr:
    case X86::CTEST32rr:
    case X86::CTEST64rr: {
      auto &Op0 = N->getOperand(0);
      if (Op0 != N->getOperand(1) || !Op0->hasNUsesOfValue(2, Op0.getResNo()) ||
          !Op0.isMachineOpcode())
        continue;
      SDValue And = N->getOperand(0);
#define CASE_ND(OP)                                                            \
  case X86::OP:                                                                \
  case X86::OP##_ND:
      switch (And.getMachineOpcode()) {
      default:
        continue;
        CASE_ND(AND8rr)
        CASE_ND(AND16rr)
        CASE_ND(AND32rr)
        CASE_ND(AND64rr) {
          if (And->hasAnyUseOfValue(1))
            continue;
          SmallVector<SDValue> Ops(N->op_values());
          Ops[0] = And.getOperand(0);
          Ops[1] = And.getOperand(1);
          MachineSDNode *Test =
              CurDAG->getMachineNode(Opc, SDLoc(N), MVT::i32, Ops);
          ReplaceUses(N, Test);
          MadeChange = true;
          continue;
        }
        CASE_ND(AND8rm)
        CASE_ND(AND16rm)
        CASE_ND(AND32rm)
        CASE_ND(AND64rm) {
          if (And->hasAnyUseOfValue(1))
            continue;
          unsigned NewOpc;
          bool IsCTESTCC = X86::isCTESTCC(Opc);
#define FROM_TO(A, B)                                                          \
  CASE_ND(A) NewOpc = IsCTESTCC ? X86::C##B : X86::B;                          \
  break;
          switch (And.getMachineOpcode()) {
            FROM_TO(AND8rm, TEST8mr);
            FROM_TO(AND16rm, TEST16mr);
            FROM_TO(AND32rm, TEST32mr);
            FROM_TO(AND64rm, TEST64mr);
          }
#undef FROM_TO
#undef CASE_ND
          // Need to swap the memory and register operand.
          SmallVector<SDValue> Ops = {And.getOperand(1), And.getOperand(2),
                                      And.getOperand(3), And.getOperand(4),
                                      And.getOperand(5), And.getOperand(0)};
          // CC, Cflags.
          if (IsCTESTCC) {
            Ops.push_back(N->getOperand(2));
            Ops.push_back(N->getOperand(3));
          }
          // Chain of memory load
          Ops.push_back(And.getOperand(6));
          // Glue
          if (IsCTESTCC)
            Ops.push_back(N->getOperand(4));

          MachineSDNode *Test = CurDAG->getMachineNode(
              NewOpc, SDLoc(N), MVT::i32, MVT::Other, Ops);
          CurDAG->setNodeMemRefs(
              Test, cast<MachineSDNode>(And.getNode())->memoperands());
          ReplaceUses(And.getValue(2), SDValue(Test, 1));
          ReplaceUses(SDValue(N, 0), SDValue(Test, 0));
          MadeChange = true;
          continue;
        }
      }
    }
    // Look for a KAND+KORTEST and turn it into KTEST if only the zero flag is
    // used. We're doing this late so we can prefer to fold the AND into masked
    // comparisons. Doing that can be better for the live range of the mask
    // register.
    case X86::KORTESTBrr:
    case X86::KORTESTWrr:
    case X86::KORTESTDrr:
    case X86::KORTESTQrr: {
      SDValue Op0 = N->getOperand(0);
      if (Op0 != N->getOperand(1) || !N->isOnlyUserOf(Op0.getNode()) ||
          !Op0.isMachineOpcode() || !onlyUsesZeroFlag(SDValue(N, 0)))
        continue;
#define CASE(A)                                                                \
  case X86::A:                                                                 \
    break;
      switch (Op0.getMachineOpcode()) {
      default:
        continue;
        CASE(KANDBrr)
        CASE(KANDWrr)
        CASE(KANDDrr)
        CASE(KANDQrr)
      }
      unsigned NewOpc;
#define FROM_TO(A, B)                                                          \
  case X86::A:                                                                 \
    NewOpc = X86::B;                                                           \
    break;
      switch (Opc) {
        FROM_TO(KORTESTBrr, KTESTBrr)
        FROM_TO(KORTESTWrr, KTESTWrr)
        FROM_TO(KORTESTDrr, KTESTDrr)
        FROM_TO(KORTESTQrr, KTESTQrr)
      }
      // KANDW is legal with AVX512F, but KTESTW requires AVX512DQ. The other
      // KAND instructions and KTEST use the same ISA feature.
      if (NewOpc == X86::KTESTWrr && !Subtarget->hasDQI())
        continue;
#undef FROM_TO
      MachineSDNode *KTest = CurDAG->getMachineNode(
          NewOpc, SDLoc(N), MVT::i32, Op0.getOperand(0), Op0.getOperand(1));
      ReplaceUses(N, KTest);
      MadeChange = true;
      continue;
    }
    // Attempt to remove vectors moves that were inserted to zero upper bits.
    case TargetOpcode::SUBREG_TO_REG: {
      unsigned SubRegIdx = N->getConstantOperandVal(2);
      if (SubRegIdx != X86::sub_xmm && SubRegIdx != X86::sub_ymm)
        continue;

      SDValue Move = N->getOperand(1);
      if (!Move.isMachineOpcode())
        continue;

      // Make sure its one of the move opcodes we recognize.
      switch (Move.getMachineOpcode()) {
      default:
        continue;
        CASE(VMOVAPDrr)       CASE(VMOVUPDrr)
        CASE(VMOVAPSrr)       CASE(VMOVUPSrr)
        CASE(VMOVDQArr)       CASE(VMOVDQUrr)
        CASE(VMOVAPDYrr)      CASE(VMOVUPDYrr)
        CASE(VMOVAPSYrr)      CASE(VMOVUPSYrr)
        CASE(VMOVDQAYrr)      CASE(VMOVDQUYrr)
        CASE(VMOVAPDZ128rr)   CASE(VMOVUPDZ128rr)
        CASE(VMOVAPSZ128rr)   CASE(VMOVUPSZ128rr)
        CASE(VMOVDQA32Z128rr) CASE(VMOVDQU32Z128rr)
        CASE(VMOVDQA64Z128rr) CASE(VMOVDQU64Z128rr)
        CASE(VMOVAPDZ256rr)   CASE(VMOVUPDZ256rr)
        CASE(VMOVAPSZ256rr)   CASE(VMOVUPSZ256rr)
        CASE(VMOVDQA32Z256rr) CASE(VMOVDQU32Z256rr)
        CASE(VMOVDQA64Z256rr) CASE(VMOVDQU64Z256rr)
      }
#undef CASE

    SDValue In = Move.getOperand(0);
    if (!In.isMachineOpcode() ||
        In.getMachineOpcode() <= TargetOpcode::GENERIC_OP_END)
      continue;

    // Make sure the instruction has a VEX, XOP, or EVEX prefix. This covers
    // the SHA instructions which use a legacy encoding.
    uint64_t TSFlags = getInstrInfo()->get(In.getMachineOpcode()).TSFlags;
    if ((TSFlags & X86II::EncodingMask) != X86II::VEX &&
        (TSFlags & X86II::EncodingMask) != X86II::EVEX &&
        (TSFlags & X86II::EncodingMask) != X86II::XOP)
      continue;

    // Producing instruction is another vector instruction. We can drop the
    // move.
    CurDAG->UpdateNodeOperands(N, N->getOperand(0), In, N->getOperand(2));
    MadeChange = true;
    }
    }
  }

  if (MadeChange)
    CurDAG->RemoveDeadNodes();
}


/// Emit any code that needs to be executed only in the main function.
void X86DAGToDAGISel::emitSpecialCodeForMain() {
  if (Subtarget->isTargetCygMing()) {
    TargetLowering::ArgListTy Args;
    auto &DL = CurDAG->getDataLayout();

    TargetLowering::CallLoweringInfo CLI(*CurDAG);
    CLI.setChain(CurDAG->getRoot())
        .setCallee(CallingConv::C, Type::getVoidTy(*CurDAG->getContext()),
                   CurDAG->getExternalSymbol("__main", TLI->getPointerTy(DL)),
                   std::move(Args));
    const TargetLowering &TLI = CurDAG->getTargetLoweringInfo();
    std::pair<SDValue, SDValue> Result = TLI.LowerCallTo(CLI);
    CurDAG->setRoot(Result.second);
  }
}

void X86DAGToDAGISel::emitFunctionEntryCode() {
  // If this is main, emit special code for main.
  const Function &F = MF->getFunction();
  if (F.hasExternalLinkage() && F.getName() == "main")
    emitSpecialCodeForMain();
}

static bool isDispSafeForFrameIndex(int64_t Val) {
  // On 64-bit platforms, we can run into an issue where a frame index
  // includes a displacement that, when added to the explicit displacement,
  // will overflow the displacement field. Assuming that the frame index
  // displacement fits into a 31-bit integer  (which is only slightly more
  // aggressive than the current fundamental assumption that it fits into
  // a 32-bit integer), a 31-bit disp should always be safe.
  return isInt<31>(Val);
}

bool X86DAGToDAGISel::foldOffsetIntoAddress(uint64_t Offset,
                                            X86ISelAddressMode &AM) {
  // We may have already matched a displacement and the caller just added the
  // symbolic displacement. So we still need to do the checks even if Offset
  // is zero.

  int64_t Val = AM.Disp + Offset;

  // Cannot combine ExternalSymbol displacements with integer offsets.
  if (Val != 0 && (AM.ES || AM.MCSym))
    return true;

  CodeModel::Model M = TM.getCodeModel();
  if (Subtarget->is64Bit()) {
    if (Val != 0 &&
        !X86::isOffsetSuitableForCodeModel(Val, M,
                                           AM.hasSymbolicDisplacement()))
      return true;
    // In addition to the checks required for a register base, check that
    // we do not try to use an unsafe Disp with a frame index.
    if (AM.BaseType == X86ISelAddressMode::FrameIndexBase &&
        !isDispSafeForFrameIndex(Val))
      return true;
    // In ILP32 (x32) mode, pointers are 32 bits and need to be zero-extended to
    // 64 bits. Instructions with 32-bit register addresses perform this zero
    // extension for us and we can safely ignore the high bits of Offset.
    // Instructions with only a 32-bit immediate address do not, though: they
    // sign extend instead. This means only address the low 2GB of address space
    // is directly addressable, we need indirect addressing for the high 2GB of
    // address space.
    // TODO: Some of the earlier checks may be relaxed for ILP32 mode as the
    // implicit zero extension of instructions would cover up any problem.
    // However, we have asserts elsewhere that get triggered if we do, so keep
    // the checks for now.
    // TODO: We would actually be able to accept these, as well as the same
    // addresses in LP64 mode, by adding the EIZ pseudo-register as an operand
    // to get an address size override to be emitted. However, this
    // pseudo-register is not part of any register class and therefore causes
    // MIR verification to fail.
    if (Subtarget->isTarget64BitILP32() && !isUInt<31>(Val) &&
        !AM.hasBaseOrIndexReg())
      return true;
  }
  AM.Disp = Val;
  return false;
}

bool X86DAGToDAGISel::matchLoadInAddress(LoadSDNode *N, X86ISelAddressMode &AM,
                                         bool AllowSegmentRegForX32) {
  SDValue Address = N->getOperand(1);

  // load gs:0 -> GS segment register.
  // load fs:0 -> FS segment register.
  //
  // This optimization is generally valid because the GNU TLS model defines that
  // gs:0 (or fs:0 on X86-64) contains its own address. However, for X86-64 mode
  // with 32-bit registers, as we get in ILP32 mode, those registers are first
  // zero-extended to 64 bits and then added it to the base address, which gives
  // unwanted results when the register holds a negative value.
  // For more information see http://people.redhat.com/drepper/tls.pdf
  if (isNullConstant(Address) && AM.Segment.getNode() == nullptr &&
      !IndirectTlsSegRefs &&
      (Subtarget->isTargetGlibc() || Subtarget->isTargetAndroid() ||
       Subtarget->isTargetFuchsia())) {
    if (Subtarget->isTarget64BitILP32() && !AllowSegmentRegForX32)
      return true;
    switch (N->getPointerInfo().getAddrSpace()) {
    case X86AS::GS:
      AM.Segment = CurDAG->getRegister(X86::GS, MVT::i16);
      return false;
    case X86AS::FS:
      AM.Segment = CurDAG->getRegister(X86::FS, MVT::i16);
      return false;
      // Address space X86AS::SS is not handled here, because it is not used to
      // address TLS areas.
    }
  }

  return true;
}

/// Try to match X86ISD::Wrapper and X86ISD::WrapperRIP nodes into an addressing
/// mode. These wrap things that will resolve down into a symbol reference.
/// If no match is possible, this returns true, otherwise it returns false.
bool X86DAGToDAGISel::matchWrapper(SDValue N, X86ISelAddressMode &AM) {
  // If the addressing mode already has a symbol as the displacement, we can
  // never match another symbol.
  if (AM.hasSymbolicDisplacement())
    return true;

  bool IsRIPRelTLS = false;
  bool IsRIPRel = N.getOpcode() == X86ISD::WrapperRIP;
  if (IsRIPRel) {
    SDValue Val = N.getOperand(0);
    if (Val.getOpcode() == ISD::TargetGlobalTLSAddress)
      IsRIPRelTLS = true;
  }

  // We can't use an addressing mode in the 64-bit large code model.
  // Global TLS addressing is an exception. In the medium code model,
  // we use can use a mode when RIP wrappers are present.
  // That signifies access to globals that are known to be "near",
  // such as the GOT itself.
  CodeModel::Model M = TM.getCodeModel();
  if (Subtarget->is64Bit() && M == CodeModel::Large && !IsRIPRelTLS)
    return true;

  // Base and index reg must be 0 in order to use %rip as base.
  if (IsRIPRel && AM.hasBaseOrIndexReg())
    return true;

  // Make a local copy in case we can't do this fold.
  X86ISelAddressMode Backup = AM;

  int64_t Offset = 0;
  SDValue N0 = N.getOperand(0);
  if (auto *G = dyn_cast<GlobalAddressSDNode>(N0)) {
    AM.GV = G->getGlobal();
    AM.SymbolFlags = G->getTargetFlags();
    Offset = G->getOffset();
  } else if (auto *CP = dyn_cast<ConstantPoolSDNode>(N0)) {
    AM.CP = CP->getConstVal();
    AM.Alignment = CP->getAlign();
    AM.SymbolFlags = CP->getTargetFlags();
    Offset = CP->getOffset();
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
    Offset = BA->getOffset();
  } else
    llvm_unreachable("Unhandled symbol reference node.");

  // Can't use an addressing mode with large globals.
  if (Subtarget->is64Bit() && !IsRIPRel && AM.GV &&
      TM.isLargeGlobalValue(AM.GV)) {
    AM = Backup;
    return true;
  }

  if (foldOffsetIntoAddress(Offset, AM)) {
    AM = Backup;
    return true;
  }

  if (IsRIPRel)
    AM.setBaseReg(CurDAG->getRegister(X86::RIP, MVT::i64));

  // Commit the changes now that we know this fold is safe.
  return false;
}

/// Add the specified node to the specified addressing mode, returning true if
/// it cannot be done. This just pattern matches for the addressing mode.
bool X86DAGToDAGISel::matchAddress(SDValue N, X86ISelAddressMode &AM) {
  if (matchAddressRecursively(N, AM, 0))
    return true;

  // Post-processing: Make a second attempt to fold a load, if we now know
  // that there will not be any other register. This is only performed for
  // 64-bit ILP32 mode since 32-bit mode and 64-bit LP64 mode will have folded
  // any foldable load the first time.
  if (Subtarget->isTarget64BitILP32() &&
      AM.BaseType == X86ISelAddressMode::RegBase &&
      AM.Base_Reg.getNode() != nullptr && AM.IndexReg.getNode() == nullptr) {
    SDValue Save_Base_Reg = AM.Base_Reg;
    if (auto *LoadN = dyn_cast<LoadSDNode>(Save_Base_Reg)) {
      AM.Base_Reg = SDValue();
      if (matchLoadInAddress(LoadN, AM, /*AllowSegmentRegForX32=*/true))
        AM.Base_Reg = Save_Base_Reg;
    }
  }

  // Post-processing: Convert lea(,%reg,2) to lea(%reg,%reg), which has
  // a smaller encoding and avoids a scaled-index.
  if (AM.Scale == 2 &&
      AM.BaseType == X86ISelAddressMode::RegBase &&
      AM.Base_Reg.getNode() == nullptr) {
    AM.Base_Reg = AM.IndexReg;
    AM.Scale = 1;
  }

  // Post-processing: Convert foo to foo(%rip), even in non-PIC mode,
  // because it has a smaller encoding.
  if (TM.getCodeModel() != CodeModel::Large &&
      (!AM.GV || !TM.isLargeGlobalValue(AM.GV)) && Subtarget->is64Bit() &&
      AM.Scale == 1 && AM.BaseType == X86ISelAddressMode::RegBase &&
      AM.Base_Reg.getNode() == nullptr && AM.IndexReg.getNode() == nullptr &&
      AM.SymbolFlags == X86II::MO_NO_FLAG && AM.hasSymbolicDisplacement()) {
    AM.Base_Reg = CurDAG->getRegister(X86::RIP, MVT::i64);
  }

  return false;
}

bool X86DAGToDAGISel::matchAdd(SDValue &N, X86ISelAddressMode &AM,
                               unsigned Depth) {
  // Add an artificial use to this node so that we can keep track of
  // it if it gets CSE'd with a different node.
  HandleSDNode Handle(N);

  X86ISelAddressMode Backup = AM;
  if (!matchAddressRecursively(N.getOperand(0), AM, Depth+1) &&
      !matchAddressRecursively(Handle.getValue().getOperand(1), AM, Depth+1))
    return false;
  AM = Backup;

  // Try again after commutating the operands.
  if (!matchAddressRecursively(Handle.getValue().getOperand(1), AM,
                               Depth + 1) &&
      !matchAddressRecursively(Handle.getValue().getOperand(0), AM, Depth + 1))
    return false;
  AM = Backup;

  // If we couldn't fold both operands into the address at the same time,
  // see if we can just put each operand into a register and fold at least
  // the add.
  if (AM.BaseType == X86ISelAddressMode::RegBase &&
      !AM.Base_Reg.getNode() &&
      !AM.IndexReg.getNode()) {
    N = Handle.getValue();
    AM.Base_Reg = N.getOperand(0);
    AM.IndexReg = N.getOperand(1);
    AM.Scale = 1;
    return false;
  }
  N = Handle.getValue();
  return true;
}

// Insert a node into the DAG at least before the Pos node's position. This
// will reposition the node as needed, and will assign it a node ID that is <=
// the Pos node's ID. Note that this does *not* preserve the uniqueness of node
// IDs! The selection DAG must no longer depend on their uniqueness when this
// is used.
static void insertDAGNode(SelectionDAG &DAG, SDValue Pos, SDValue N) {
  if (N->getNodeId() == -1 ||
      (SelectionDAGISel::getUninvalidatedNodeId(N.getNode()) >
       SelectionDAGISel::getUninvalidatedNodeId(Pos.getNode()))) {
    DAG.RepositionNode(Pos->getIterator(), N.getNode());
    // Mark Node as invalid for pruning as after this it may be a successor to a
    // selected node but otherwise be in the same position of Pos.
    // Conservatively mark it with the same -abs(Id) to assure node id
    // invariant is preserved.
    N->setNodeId(Pos->getNodeId());
    SelectionDAGISel::InvalidateNodeId(N.getNode());
  }
}

// Transform "(X >> (8-C1)) & (0xff << C1)" to "((X >> 8) & 0xff) << C1" if
// safe. This allows us to convert the shift and and into an h-register
// extract and a scaled index. Returns false if the simplification is
// performed.
static bool foldMaskAndShiftToExtract(SelectionDAG &DAG, SDValue N,
                                      uint64_t Mask,
                                      SDValue Shift, SDValue X,
                                      X86ISelAddressMode &AM) {
  if (Shift.getOpcode() != ISD::SRL ||
      !isa<ConstantSDNode>(Shift.getOperand(1)) ||
      !Shift.hasOneUse())
    return true;

  int ScaleLog = 8 - Shift.getConstantOperandVal(1);
  if (ScaleLog <= 0 || ScaleLog >= 4 ||
      Mask != (0xffu << ScaleLog))
    return true;

  MVT XVT = X.getSimpleValueType();
  MVT VT = N.getSimpleValueType();
  SDLoc DL(N);
  SDValue Eight = DAG.getConstant(8, DL, MVT::i8);
  SDValue NewMask = DAG.getConstant(0xff, DL, XVT);
  SDValue Srl = DAG.getNode(ISD::SRL, DL, XVT, X, Eight);
  SDValue And = DAG.getNode(ISD::AND, DL, XVT, Srl, NewMask);
  SDValue Ext = DAG.getZExtOrTrunc(And, DL, VT);
  SDValue ShlCount = DAG.getConstant(ScaleLog, DL, MVT::i8);
  SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Ext, ShlCount);

  // Insert the new nodes into the topological ordering. We must do this in
  // a valid topological ordering as nothing is going to go back and re-sort
  // these nodes. We continually insert before 'N' in sequence as this is
  // essentially a pre-flattened and pre-sorted sequence of nodes. There is no
  // hierarchy left to express.
  insertDAGNode(DAG, N, Eight);
  insertDAGNode(DAG, N, NewMask);
  insertDAGNode(DAG, N, Srl);
  insertDAGNode(DAG, N, And);
  insertDAGNode(DAG, N, Ext);
  insertDAGNode(DAG, N, ShlCount);
  insertDAGNode(DAG, N, Shl);
  DAG.ReplaceAllUsesWith(N, Shl);
  DAG.RemoveDeadNode(N.getNode());
  AM.IndexReg = Ext;
  AM.Scale = (1 << ScaleLog);
  return false;
}

// Transforms "(X << C1) & C2" to "(X & (C2>>C1)) << C1" if safe and if this
// allows us to fold the shift into this addressing mode. Returns false if the
// transform succeeded.
static bool foldMaskedShiftToScaledMask(SelectionDAG &DAG, SDValue N,
                                        X86ISelAddressMode &AM) {
  SDValue Shift = N.getOperand(0);

  // Use a signed mask so that shifting right will insert sign bits. These
  // bits will be removed when we shift the result left so it doesn't matter
  // what we use. This might allow a smaller immediate encoding.
  int64_t Mask = cast<ConstantSDNode>(N->getOperand(1))->getSExtValue();

  // If we have an any_extend feeding the AND, look through it to see if there
  // is a shift behind it. But only if the AND doesn't use the extended bits.
  // FIXME: Generalize this to other ANY_EXTEND than i32 to i64?
  bool FoundAnyExtend = false;
  if (Shift.getOpcode() == ISD::ANY_EXTEND && Shift.hasOneUse() &&
      Shift.getOperand(0).getSimpleValueType() == MVT::i32 &&
      isUInt<32>(Mask)) {
    FoundAnyExtend = true;
    Shift = Shift.getOperand(0);
  }

  if (Shift.getOpcode() != ISD::SHL ||
      !isa<ConstantSDNode>(Shift.getOperand(1)))
    return true;

  SDValue X = Shift.getOperand(0);

  // Not likely to be profitable if either the AND or SHIFT node has more
  // than one use (unless all uses are for address computation). Besides,
  // isel mechanism requires their node ids to be reused.
  if (!N.hasOneUse() || !Shift.hasOneUse())
    return true;

  // Verify that the shift amount is something we can fold.
  unsigned ShiftAmt = Shift.getConstantOperandVal(1);
  if (ShiftAmt != 1 && ShiftAmt != 2 && ShiftAmt != 3)
    return true;

  MVT VT = N.getSimpleValueType();
  SDLoc DL(N);
  if (FoundAnyExtend) {
    SDValue NewX = DAG.getNode(ISD::ANY_EXTEND, DL, VT, X);
    insertDAGNode(DAG, N, NewX);
    X = NewX;
  }

  SDValue NewMask = DAG.getConstant(Mask >> ShiftAmt, DL, VT);
  SDValue NewAnd = DAG.getNode(ISD::AND, DL, VT, X, NewMask);
  SDValue NewShift = DAG.getNode(ISD::SHL, DL, VT, NewAnd, Shift.getOperand(1));

  // Insert the new nodes into the topological ordering. We must do this in
  // a valid topological ordering as nothing is going to go back and re-sort
  // these nodes. We continually insert before 'N' in sequence as this is
  // essentially a pre-flattened and pre-sorted sequence of nodes. There is no
  // hierarchy left to express.
  insertDAGNode(DAG, N, NewMask);
  insertDAGNode(DAG, N, NewAnd);
  insertDAGNode(DAG, N, NewShift);
  DAG.ReplaceAllUsesWith(N, NewShift);
  DAG.RemoveDeadNode(N.getNode());

  AM.Scale = 1 << ShiftAmt;
  AM.IndexReg = NewAnd;
  return false;
}

// Implement some heroics to detect shifts of masked values where the mask can
// be replaced by extending the shift and undoing that in the addressing mode
// scale. Patterns such as (shl (srl x, c1), c2) are canonicalized into (and
// (srl x, SHIFT), MASK) by DAGCombines that don't know the shl can be done in
// the addressing mode. This results in code such as:
//
//   int f(short *y, int *lookup_table) {
//     ...
//     return *y + lookup_table[*y >> 11];
//   }
//
// Turning into:
//   movzwl (%rdi), %eax
//   movl %eax, %ecx
//   shrl $11, %ecx
//   addl (%rsi,%rcx,4), %eax
//
// Instead of:
//   movzwl (%rdi), %eax
//   movl %eax, %ecx
//   shrl $9, %ecx
//   andl $124, %rcx
//   addl (%rsi,%rcx), %eax
//
// Note that this function assumes the mask is provided as a mask *after* the
// value is shifted. The input chain may or may not match that, but computing
// such a mask is trivial.
static bool foldMaskAndShiftToScale(SelectionDAG &DAG, SDValue N,
                                    uint64_t Mask,
                                    SDValue Shift, SDValue X,
                                    X86ISelAddressMode &AM) {
  if (Shift.getOpcode() != ISD::SRL || !Shift.hasOneUse() ||
      !isa<ConstantSDNode>(Shift.getOperand(1)))
    return true;

  // We need to ensure that mask is a continuous run of bits.
  unsigned MaskIdx, MaskLen;
  if (!isShiftedMask_64(Mask, MaskIdx, MaskLen))
    return true;
  unsigned MaskLZ = 64 - (MaskIdx + MaskLen);

  unsigned ShiftAmt = Shift.getConstantOperandVal(1);

  // The amount of shift we're trying to fit into the addressing mode is taken
  // from the shifted mask index (number of trailing zeros of the mask).
  unsigned AMShiftAmt = MaskIdx;

  // There is nothing we can do here unless the mask is removing some bits.
  // Also, the addressing mode can only represent shifts of 1, 2, or 3 bits.
  if (AMShiftAmt == 0 || AMShiftAmt > 3) return true;

  // Scale the leading zero count down based on the actual size of the value.
  // Also scale it down based on the size of the shift.
  unsigned ScaleDown = (64 - X.getSimpleValueType().getSizeInBits()) + ShiftAmt;
  if (MaskLZ < ScaleDown)
    return true;
  MaskLZ -= ScaleDown;

  // The final check is to ensure that any masked out high bits of X are
  // already known to be zero. Otherwise, the mask has a semantic impact
  // other than masking out a couple of low bits. Unfortunately, because of
  // the mask, zero extensions will be removed from operands in some cases.
  // This code works extra hard to look through extensions because we can
  // replace them with zero extensions cheaply if necessary.
  bool ReplacingAnyExtend = false;
  if (X.getOpcode() == ISD::ANY_EXTEND) {
    unsigned ExtendBits = X.getSimpleValueType().getSizeInBits() -
                          X.getOperand(0).getSimpleValueType().getSizeInBits();
    // Assume that we'll replace the any-extend with a zero-extend, and
    // narrow the search to the extended value.
    X = X.getOperand(0);
    MaskLZ = ExtendBits > MaskLZ ? 0 : MaskLZ - ExtendBits;
    ReplacingAnyExtend = true;
  }
  APInt MaskedHighBits =
    APInt::getHighBitsSet(X.getSimpleValueType().getSizeInBits(), MaskLZ);
  if (!DAG.MaskedValueIsZero(X, MaskedHighBits))
    return true;

  // We've identified a pattern that can be transformed into a single shift
  // and an addressing mode. Make it so.
  MVT VT = N.getSimpleValueType();
  if (ReplacingAnyExtend) {
    assert(X.getValueType() != VT);
    // We looked through an ANY_EXTEND node, insert a ZERO_EXTEND.
    SDValue NewX = DAG.getNode(ISD::ZERO_EXTEND, SDLoc(X), VT, X);
    insertDAGNode(DAG, N, NewX);
    X = NewX;
  }

  MVT XVT = X.getSimpleValueType();
  SDLoc DL(N);
  SDValue NewSRLAmt = DAG.getConstant(ShiftAmt + AMShiftAmt, DL, MVT::i8);
  SDValue NewSRL = DAG.getNode(ISD::SRL, DL, XVT, X, NewSRLAmt);
  SDValue NewExt = DAG.getZExtOrTrunc(NewSRL, DL, VT);
  SDValue NewSHLAmt = DAG.getConstant(AMShiftAmt, DL, MVT::i8);
  SDValue NewSHL = DAG.getNode(ISD::SHL, DL, VT, NewExt, NewSHLAmt);

  // Insert the new nodes into the topological ordering. We must do this in
  // a valid topological ordering as nothing is going to go back and re-sort
  // these nodes. We continually insert before 'N' in sequence as this is
  // essentially a pre-flattened and pre-sorted sequence of nodes. There is no
  // hierarchy left to express.
  insertDAGNode(DAG, N, NewSRLAmt);
  insertDAGNode(DAG, N, NewSRL);
  insertDAGNode(DAG, N, NewExt);
  insertDAGNode(DAG, N, NewSHLAmt);
  insertDAGNode(DAG, N, NewSHL);
  DAG.ReplaceAllUsesWith(N, NewSHL);
  DAG.RemoveDeadNode(N.getNode());

  AM.Scale = 1 << AMShiftAmt;
  AM.IndexReg = NewExt;
  return false;
}

// Transform "(X >> SHIFT) & (MASK << C1)" to
// "((X >> (SHIFT + C1)) & (MASK)) << C1". Everything before the SHL will be
// matched to a BEXTR later. Returns false if the simplification is performed.
static bool foldMaskedShiftToBEXTR(SelectionDAG &DAG, SDValue N,
                                   uint64_t Mask,
                                   SDValue Shift, SDValue X,
                                   X86ISelAddressMode &AM,
                                   const X86Subtarget &Subtarget) {
  if (Shift.getOpcode() != ISD::SRL ||
      !isa<ConstantSDNode>(Shift.getOperand(1)) ||
      !Shift.hasOneUse() || !N.hasOneUse())
    return true;

  // Only do this if BEXTR will be matched by matchBEXTRFromAndImm.
  if (!Subtarget.hasTBM() &&
      !(Subtarget.hasBMI() && Subtarget.hasFastBEXTR()))
    return true;

  // We need to ensure that mask is a continuous run of bits.
  unsigned MaskIdx, MaskLen;
  if (!isShiftedMask_64(Mask, MaskIdx, MaskLen))
    return true;

  unsigned ShiftAmt = Shift.getConstantOperandVal(1);

  // The amount of shift we're trying to fit into the addressing mode is taken
  // from the shifted mask index (number of trailing zeros of the mask).
  unsigned AMShiftAmt = MaskIdx;

  // There is nothing we can do here unless the mask is removing some bits.
  // Also, the addressing mode can only represent shifts of 1, 2, or 3 bits.
  if (AMShiftAmt == 0 || AMShiftAmt > 3) return true;

  MVT XVT = X.getSimpleValueType();
  MVT VT = N.getSimpleValueType();
  SDLoc DL(N);
  SDValue NewSRLAmt = DAG.getConstant(ShiftAmt + AMShiftAmt, DL, MVT::i8);
  SDValue NewSRL = DAG.getNode(ISD::SRL, DL, XVT, X, NewSRLAmt);
  SDValue NewMask = DAG.getConstant(Mask >> AMShiftAmt, DL, XVT);
  SDValue NewAnd = DAG.getNode(ISD::AND, DL, XVT, NewSRL, NewMask);
  SDValue NewExt = DAG.getZExtOrTrunc(NewAnd, DL, VT);
  SDValue NewSHLAmt = DAG.getConstant(AMShiftAmt, DL, MVT::i8);
  SDValue NewSHL = DAG.getNode(ISD::SHL, DL, VT, NewExt, NewSHLAmt);

  // Insert the new nodes into the topological ordering. We must do this in
  // a valid topological ordering as nothing is going to go back and re-sort
  // these nodes. We continually insert before 'N' in sequence as this is
  // essentially a pre-flattened and pre-sorted sequence of nodes. There is no
  // hierarchy left to express.
  insertDAGNode(DAG, N, NewSRLAmt);
  insertDAGNode(DAG, N, NewSRL);
  insertDAGNode(DAG, N, NewMask);
  insertDAGNode(DAG, N, NewAnd);
  insertDAGNode(DAG, N, NewExt);
  insertDAGNode(DAG, N, NewSHLAmt);
  insertDAGNode(DAG, N, NewSHL);
  DAG.ReplaceAllUsesWith(N, NewSHL);
  DAG.RemoveDeadNode(N.getNode());

  AM.Scale = 1 << AMShiftAmt;
  AM.IndexReg = NewExt;
  return false;
}

// Attempt to peek further into a scaled index register, collecting additional
// extensions / offsets / etc. Returns /p N if we can't peek any further.
SDValue X86DAGToDAGISel::matchIndexRecursively(SDValue N,
                                               X86ISelAddressMode &AM,
                                               unsigned Depth) {
  assert(AM.IndexReg.getNode() == nullptr && "IndexReg already matched");
  assert((AM.Scale == 1 || AM.Scale == 2 || AM.Scale == 4 || AM.Scale == 8) &&
         "Illegal index scale");

  // Limit recursion.
  if (Depth >= SelectionDAG::MaxRecursionDepth)
    return N;

  EVT VT = N.getValueType();
  unsigned Opc = N.getOpcode();

  // index: add(x,c) -> index: x, disp + c
  if (CurDAG->isBaseWithConstantOffset(N)) {
    auto *AddVal = cast<ConstantSDNode>(N.getOperand(1));
    uint64_t Offset = (uint64_t)AddVal->getSExtValue() * AM.Scale;
    if (!foldOffsetIntoAddress(Offset, AM))
      return matchIndexRecursively(N.getOperand(0), AM, Depth + 1);
  }

  // index: add(x,x) -> index: x, scale * 2
  if (Opc == ISD::ADD && N.getOperand(0) == N.getOperand(1)) {
    if (AM.Scale <= 4) {
      AM.Scale *= 2;
      return matchIndexRecursively(N.getOperand(0), AM, Depth + 1);
    }
  }

  // index: shl(x,i) -> index: x, scale * (1 << i)
  if (Opc == X86ISD::VSHLI) {
    uint64_t ShiftAmt = N.getConstantOperandVal(1);
    uint64_t ScaleAmt = 1ULL << ShiftAmt;
    if ((AM.Scale * ScaleAmt) <= 8) {
      AM.Scale *= ScaleAmt;
      return matchIndexRecursively(N.getOperand(0), AM, Depth + 1);
    }
  }

  // index: sext(add_nsw(x,c)) -> index: sext(x), disp + sext(c)
  // TODO: call matchIndexRecursively(AddSrc) if we won't corrupt sext?
  if (Opc == ISD::SIGN_EXTEND && !VT.isVector() && N.hasOneUse()) {
    SDValue Src = N.getOperand(0);
    if (Src.getOpcode() == ISD::ADD && Src->getFlags().hasNoSignedWrap() &&
        Src.hasOneUse()) {
      if (CurDAG->isBaseWithConstantOffset(Src)) {
        SDValue AddSrc = Src.getOperand(0);
        auto *AddVal = cast<ConstantSDNode>(Src.getOperand(1));
        uint64_t Offset = (uint64_t)AddVal->getSExtValue();
        if (!foldOffsetIntoAddress(Offset * AM.Scale, AM)) {
          SDLoc DL(N);
          SDValue ExtSrc = CurDAG->getNode(Opc, DL, VT, AddSrc);
          SDValue ExtVal = CurDAG->getConstant(Offset, DL, VT);
          SDValue ExtAdd = CurDAG->getNode(ISD::ADD, DL, VT, ExtSrc, ExtVal);
          insertDAGNode(*CurDAG, N, ExtSrc);
          insertDAGNode(*CurDAG, N, ExtVal);
          insertDAGNode(*CurDAG, N, ExtAdd);
          CurDAG->ReplaceAllUsesWith(N, ExtAdd);
          CurDAG->RemoveDeadNode(N.getNode());
          return ExtSrc;
        }
      }
    }
  }

  // index: zext(add_nuw(x,c)) -> index: zext(x), disp + zext(c)
  // index: zext(addlike(x,c)) -> index: zext(x), disp + zext(c)
  // TODO: call matchIndexRecursively(AddSrc) if we won't corrupt sext?
  if (Opc == ISD::ZERO_EXTEND && !VT.isVector() && N.hasOneUse()) {
    SDValue Src = N.getOperand(0);
    unsigned SrcOpc = Src.getOpcode();
    if (((SrcOpc == ISD::ADD && Src->getFlags().hasNoUnsignedWrap()) ||
         CurDAG->isADDLike(Src, /*NoWrap=*/true)) &&
        Src.hasOneUse()) {
      if (CurDAG->isBaseWithConstantOffset(Src)) {
        SDValue AddSrc = Src.getOperand(0);
        uint64_t Offset = Src.getConstantOperandVal(1);
        if (!foldOffsetIntoAddress(Offset * AM.Scale, AM)) {
          SDLoc DL(N);
          SDValue Res;
          // If we're also scaling, see if we can use that as well.
          if (AddSrc.getOpcode() == ISD::SHL &&
              isa<ConstantSDNode>(AddSrc.getOperand(1))) {
            SDValue ShVal = AddSrc.getOperand(0);
            uint64_t ShAmt = AddSrc.getConstantOperandVal(1);
            APInt HiBits =
                APInt::getHighBitsSet(AddSrc.getScalarValueSizeInBits(), ShAmt);
            uint64_t ScaleAmt = 1ULL << ShAmt;
            if ((AM.Scale * ScaleAmt) <= 8 &&
                (AddSrc->getFlags().hasNoUnsignedWrap() ||
                 CurDAG->MaskedValueIsZero(ShVal, HiBits))) {
              AM.Scale *= ScaleAmt;
              SDValue ExtShVal = CurDAG->getNode(Opc, DL, VT, ShVal);
              SDValue ExtShift = CurDAG->getNode(ISD::SHL, DL, VT, ExtShVal,
                                                 AddSrc.getOperand(1));
              insertDAGNode(*CurDAG, N, ExtShVal);
              insertDAGNode(*CurDAG, N, ExtShift);
              AddSrc = ExtShift;
              Res = ExtShVal;
            }
          }
          SDValue ExtSrc = CurDAG->getNode(Opc, DL, VT, AddSrc);
          SDValue ExtVal = CurDAG->getConstant(Offset, DL, VT);
          SDValue ExtAdd = CurDAG->getNode(SrcOpc, DL, VT, ExtSrc, ExtVal);
          insertDAGNode(*CurDAG, N, ExtSrc);
          insertDAGNode(*CurDAG, N, ExtVal);
          insertDAGNode(*CurDAG, N, ExtAdd);
          CurDAG->ReplaceAllUsesWith(N, ExtAdd);
          CurDAG->RemoveDeadNode(N.getNode());
          return Res ? Res : ExtSrc;
        }
      }
    }
  }

  // TODO: Handle extensions, shifted masks etc.
  return N;
}

bool X86DAGToDAGISel::matchAddressRecursively(SDValue N, X86ISelAddressMode &AM,
                                              unsigned Depth) {
  SDLoc dl(N);
  LLVM_DEBUG({
    dbgs() << "MatchAddress: ";
    AM.dump(CurDAG);
  });
  // Limit recursion.
  if (Depth >= SelectionDAG::MaxRecursionDepth)
    return matchAddressBase(N, AM);

  // If this is already a %rip relative address, we can only merge immediates
  // into it.  Instead of handling this in every case, we handle it here.
  // RIP relative addressing: %rip + 32-bit displacement!
  if (AM.isRIPRelative()) {
    // FIXME: JumpTable and ExternalSymbol address currently don't like
    // displacements.  It isn't very important, but this should be fixed for
    // consistency.
    if (!(AM.ES || AM.MCSym) && AM.JT != -1)
      return true;

    if (auto *Cst = dyn_cast<ConstantSDNode>(N))
      if (!foldOffsetIntoAddress(Cst->getSExtValue(), AM))
        return false;
    return true;
  }

  switch (N.getOpcode()) {
  default: break;
  case ISD::LOCAL_RECOVER: {
    if (!AM.hasSymbolicDisplacement() && AM.Disp == 0)
      if (const auto *ESNode = dyn_cast<MCSymbolSDNode>(N.getOperand(0))) {
        // Use the symbol and don't prefix it.
        AM.MCSym = ESNode->getMCSymbol();
        return false;
      }
    break;
  }
  case ISD::Constant: {
    uint64_t Val = cast<ConstantSDNode>(N)->getSExtValue();
    if (!foldOffsetIntoAddress(Val, AM))
      return false;
    break;
  }

  case X86ISD::Wrapper:
  case X86ISD::WrapperRIP:
    if (!matchWrapper(N, AM))
      return false;
    break;

  case ISD::LOAD:
    if (!matchLoadInAddress(cast<LoadSDNode>(N), AM))
      return false;
    break;

  case ISD::FrameIndex:
    if (AM.BaseType == X86ISelAddressMode::RegBase &&
        AM.Base_Reg.getNode() == nullptr &&
        (!Subtarget->is64Bit() || isDispSafeForFrameIndex(AM.Disp))) {
      AM.BaseType = X86ISelAddressMode::FrameIndexBase;
      AM.Base_FrameIndex = cast<FrameIndexSDNode>(N)->getIndex();
      return false;
    }
    break;

  case ISD::SHL:
    if (AM.IndexReg.getNode() != nullptr || AM.Scale != 1)
      break;

    if (auto *CN = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      unsigned Val = CN->getZExtValue();
      // Note that we handle x<<1 as (,x,2) rather than (x,x) here so
      // that the base operand remains free for further matching. If
      // the base doesn't end up getting used, a post-processing step
      // in MatchAddress turns (,x,2) into (x,x), which is cheaper.
      if (Val == 1 || Val == 2 || Val == 3) {
        SDValue ShVal = N.getOperand(0);
        AM.Scale = 1 << Val;
        AM.IndexReg = matchIndexRecursively(ShVal, AM, Depth + 1);
        return false;
      }
    }
    break;

  case ISD::SRL: {
    // Scale must not be used already.
    if (AM.IndexReg.getNode() != nullptr || AM.Scale != 1) break;

    // We only handle up to 64-bit values here as those are what matter for
    // addressing mode optimizations.
    assert(N.getSimpleValueType().getSizeInBits() <= 64 &&
           "Unexpected value size!");

    SDValue And = N.getOperand(0);
    if (And.getOpcode() != ISD::AND) break;
    SDValue X = And.getOperand(0);

    // The mask used for the transform is expected to be post-shift, but we
    // found the shift first so just apply the shift to the mask before passing
    // it down.
    if (!isa<ConstantSDNode>(N.getOperand(1)) ||
        !isa<ConstantSDNode>(And.getOperand(1)))
      break;
    uint64_t Mask = And.getConstantOperandVal(1) >> N.getConstantOperandVal(1);

    // Try to fold the mask and shift into the scale, and return false if we
    // succeed.
    if (!foldMaskAndShiftToScale(*CurDAG, N, Mask, N, X, AM))
      return false;
    break;
  }

  case ISD::SMUL_LOHI:
  case ISD::UMUL_LOHI:
    // A mul_lohi where we need the low part can be folded as a plain multiply.
    if (N.getResNo() != 0) break;
    [[fallthrough]];
  case ISD::MUL:
  case X86ISD::MUL_IMM:
    // X*[3,5,9] -> X+X*[2,4,8]
    if (AM.BaseType == X86ISelAddressMode::RegBase &&
        AM.Base_Reg.getNode() == nullptr &&
        AM.IndexReg.getNode() == nullptr) {
      if (auto *CN = dyn_cast<ConstantSDNode>(N.getOperand(1)))
        if (CN->getZExtValue() == 3 || CN->getZExtValue() == 5 ||
            CN->getZExtValue() == 9) {
          AM.Scale = unsigned(CN->getZExtValue())-1;

          SDValue MulVal = N.getOperand(0);
          SDValue Reg;

          // Okay, we know that we have a scale by now.  However, if the scaled
          // value is an add of something and a constant, we can fold the
          // constant into the disp field here.
          if (MulVal.getNode()->getOpcode() == ISD::ADD && MulVal.hasOneUse() &&
              isa<ConstantSDNode>(MulVal.getOperand(1))) {
            Reg = MulVal.getOperand(0);
            auto *AddVal = cast<ConstantSDNode>(MulVal.getOperand(1));
            uint64_t Disp = AddVal->getSExtValue() * CN->getZExtValue();
            if (foldOffsetIntoAddress(Disp, AM))
              Reg = N.getOperand(0);
          } else {
            Reg = N.getOperand(0);
          }

          AM.IndexReg = AM.Base_Reg = Reg;
          return false;
        }
    }
    break;

  case ISD::SUB: {
    // Given A-B, if A can be completely folded into the address and
    // the index field with the index field unused, use -B as the index.
    // This is a win if a has multiple parts that can be folded into
    // the address. Also, this saves a mov if the base register has
    // other uses, since it avoids a two-address sub instruction, however
    // it costs an additional mov if the index register has other uses.

    // Add an artificial use to this node so that we can keep track of
    // it if it gets CSE'd with a different node.
    HandleSDNode Handle(N);

    // Test if the LHS of the sub can be folded.
    X86ISelAddressMode Backup = AM;
    if (matchAddressRecursively(N.getOperand(0), AM, Depth+1)) {
      N = Handle.getValue();
      AM = Backup;
      break;
    }
    N = Handle.getValue();
    // Test if the index field is free for use.
    if (AM.IndexReg.getNode() || AM.isRIPRelative()) {
      AM = Backup;
      break;
    }

    int Cost = 0;
    SDValue RHS = N.getOperand(1);
    // If the RHS involves a register with multiple uses, this
    // transformation incurs an extra mov, due to the neg instruction
    // clobbering its operand.
    if (!RHS.getNode()->hasOneUse() ||
        RHS.getNode()->getOpcode() == ISD::CopyFromReg ||
        RHS.getNode()->getOpcode() == ISD::TRUNCATE ||
        RHS.getNode()->getOpcode() == ISD::ANY_EXTEND ||
        (RHS.getNode()->getOpcode() == ISD::ZERO_EXTEND &&
         RHS.getOperand(0).getValueType() == MVT::i32))
      ++Cost;
    // If the base is a register with multiple uses, this
    // transformation may save a mov.
    if ((AM.BaseType == X86ISelAddressMode::RegBase && AM.Base_Reg.getNode() &&
         !AM.Base_Reg.getNode()->hasOneUse()) ||
        AM.BaseType == X86ISelAddressMode::FrameIndexBase)
      --Cost;
    // If the folded LHS was interesting, this transformation saves
    // address arithmetic.
    if ((AM.hasSymbolicDisplacement() && !Backup.hasSymbolicDisplacement()) +
        ((AM.Disp != 0) && (Backup.Disp == 0)) +
        (AM.Segment.getNode() && !Backup.Segment.getNode()) >= 2)
      --Cost;
    // If it doesn't look like it may be an overall win, don't do it.
    if (Cost >= 0) {
      AM = Backup;
      break;
    }

    // Ok, the transformation is legal and appears profitable. Go for it.
    // Negation will be emitted later to avoid creating dangling nodes if this
    // was an unprofitable LEA.
    AM.IndexReg = RHS;
    AM.NegateIndex = true;
    AM.Scale = 1;
    return false;
  }

  case ISD::OR:
  case ISD::XOR:
    // See if we can treat the OR/XOR node as an ADD node.
    if (!CurDAG->isADDLike(N))
      break;
    [[fallthrough]];
  case ISD::ADD:
    if (!matchAdd(N, AM, Depth))
      return false;
    break;

  case ISD::AND: {
    // Perform some heroic transforms on an and of a constant-count shift
    // with a constant to enable use of the scaled offset field.

    // Scale must not be used already.
    if (AM.IndexReg.getNode() != nullptr || AM.Scale != 1) break;

    // We only handle up to 64-bit values here as those are what matter for
    // addressing mode optimizations.
    assert(N.getSimpleValueType().getSizeInBits() <= 64 &&
           "Unexpected value size!");

    if (!isa<ConstantSDNode>(N.getOperand(1)))
      break;

    if (N.getOperand(0).getOpcode() == ISD::SRL) {
      SDValue Shift = N.getOperand(0);
      SDValue X = Shift.getOperand(0);

      uint64_t Mask = N.getConstantOperandVal(1);

      // Try to fold the mask and shift into an extract and scale.
      if (!foldMaskAndShiftToExtract(*CurDAG, N, Mask, Shift, X, AM))
        return false;

      // Try to fold the mask and shift directly into the scale.
      if (!foldMaskAndShiftToScale(*CurDAG, N, Mask, Shift, X, AM))
        return false;

      // Try to fold the mask and shift into BEXTR and scale.
      if (!foldMaskedShiftToBEXTR(*CurDAG, N, Mask, Shift, X, AM, *Subtarget))
        return false;
    }

    // Try to swap the mask and shift to place shifts which can be done as
    // a scale on the outside of the mask.
    if (!foldMaskedShiftToScaledMask(*CurDAG, N, AM))
      return false;

    break;
  }
  case ISD::ZERO_EXTEND: {
    // Try to widen a zexted shift left to the same size as its use, so we can
    // match the shift as a scale factor.
    if (AM.IndexReg.getNode() != nullptr || AM.Scale != 1)
      break;

    SDValue Src = N.getOperand(0);

    // See if we can match a zext(addlike(x,c)).
    // TODO: Move more ZERO_EXTEND patterns into matchIndexRecursively.
    if (Src.getOpcode() == ISD::ADD || Src.getOpcode() == ISD::OR)
      if (SDValue Index = matchIndexRecursively(N, AM, Depth + 1))
        if (Index != N) {
          AM.IndexReg = Index;
          return false;
        }

    // Peek through mask: zext(and(shl(x,c1),c2))
    APInt Mask = APInt::getAllOnes(Src.getScalarValueSizeInBits());
    if (Src.getOpcode() == ISD::AND && Src.hasOneUse())
      if (auto *MaskC = dyn_cast<ConstantSDNode>(Src.getOperand(1))) {
        Mask = MaskC->getAPIntValue();
        Src = Src.getOperand(0);
      }

    if (Src.getOpcode() == ISD::SHL && Src.hasOneUse() && N->hasOneUse()) {
      // Give up if the shift is not a valid scale factor [1,2,3].
      SDValue ShlSrc = Src.getOperand(0);
      SDValue ShlAmt = Src.getOperand(1);
      auto *ShAmtC = dyn_cast<ConstantSDNode>(ShlAmt);
      if (!ShAmtC)
        break;
      unsigned ShAmtV = ShAmtC->getZExtValue();
      if (ShAmtV > 3)
        break;

      // The narrow shift must only shift out zero bits (it must be 'nuw').
      // That makes it safe to widen to the destination type.
      APInt HighZeros =
          APInt::getHighBitsSet(ShlSrc.getValueSizeInBits(), ShAmtV);
      if (!Src->getFlags().hasNoUnsignedWrap() &&
          !CurDAG->MaskedValueIsZero(ShlSrc, HighZeros & Mask))
        break;

      // zext (shl nuw i8 %x, C1) to i32
      // --> shl (zext i8 %x to i32), (zext C1)
      // zext (and (shl nuw i8 %x, C1), C2) to i32
      // --> shl (zext i8 (and %x, C2 >> C1) to i32), (zext C1)
      MVT SrcVT = ShlSrc.getSimpleValueType();
      MVT VT = N.getSimpleValueType();
      SDLoc DL(N);

      SDValue Res = ShlSrc;
      if (!Mask.isAllOnes()) {
        Res = CurDAG->getConstant(Mask.lshr(ShAmtV), DL, SrcVT);
        insertDAGNode(*CurDAG, N, Res);
        Res = CurDAG->getNode(ISD::AND, DL, SrcVT, ShlSrc, Res);
        insertDAGNode(*CurDAG, N, Res);
      }
      SDValue Zext = CurDAG->getNode(ISD::ZERO_EXTEND, DL, VT, Res);
      insertDAGNode(*CurDAG, N, Zext);
      SDValue NewShl = CurDAG->getNode(ISD::SHL, DL, VT, Zext, ShlAmt);
      insertDAGNode(*CurDAG, N, NewShl);
      CurDAG->ReplaceAllUsesWith(N, NewShl);
      CurDAG->RemoveDeadNode(N.getNode());

      // Convert the shift to scale factor.
      AM.Scale = 1 << ShAmtV;
      // If matchIndexRecursively is not called here,
      // Zext may be replaced by other nodes but later used to call a builder
      // method
      AM.IndexReg = matchIndexRecursively(Zext, AM, Depth + 1);
      return false;
    }

    if (Src.getOpcode() == ISD::SRL && !Mask.isAllOnes()) {
      // Try to fold the mask and shift into an extract and scale.
      if (!foldMaskAndShiftToExtract(*CurDAG, N, Mask.getZExtValue(), Src,
                                     Src.getOperand(0), AM))
        return false;

      // Try to fold the mask and shift directly into the scale.
      if (!foldMaskAndShiftToScale(*CurDAG, N, Mask.getZExtValue(), Src,
                                   Src.getOperand(0), AM))
        return false;

      // Try to fold the mask and shift into BEXTR and scale.
      if (!foldMaskedShiftToBEXTR(*CurDAG, N, Mask.getZExtValue(), Src,
                                  Src.getOperand(0), AM, *Subtarget))
        return false;
    }

    break;
  }
  }

  return matchAddressBase(N, AM);
}

/// Helper for MatchAddress. Add the specified node to the
/// specified addressing mode without any further recursion.
bool X86DAGToDAGISel::matchAddressBase(SDValue N, X86ISelAddressMode &AM) {
  // Is the base register already occupied?
  if (AM.BaseType != X86ISelAddressMode::RegBase || AM.Base_Reg.getNode()) {
    // If so, check to see if the scale index register is set.
    if (!AM.IndexReg.getNode()) {
      AM.IndexReg = N;
      AM.Scale = 1;
      return false;
    }

    // Otherwise, we cannot select it.
    return true;
  }

  // Default, generate it as a register.
  AM.BaseType = X86ISelAddressMode::RegBase;
  AM.Base_Reg = N;
  return false;
}

bool X86DAGToDAGISel::matchVectorAddressRecursively(SDValue N,
                                                    X86ISelAddressMode &AM,
                                                    unsigned Depth) {
  SDLoc dl(N);
  LLVM_DEBUG({
    dbgs() << "MatchVectorAddress: ";
    AM.dump(CurDAG);
  });
  // Limit recursion.
  if (Depth >= SelectionDAG::MaxRecursionDepth)
    return matchAddressBase(N, AM);

  // TODO: Support other operations.
  switch (N.getOpcode()) {
  case ISD::Constant: {
    uint64_t Val = cast<ConstantSDNode>(N)->getSExtValue();
    if (!foldOffsetIntoAddress(Val, AM))
      return false;
    break;
  }
  case X86ISD::Wrapper:
    if (!matchWrapper(N, AM))
      return false;
    break;
  case ISD::ADD: {
    // Add an artificial use to this node so that we can keep track of
    // it if it gets CSE'd with a different node.
    HandleSDNode Handle(N);

    X86ISelAddressMode Backup = AM;
    if (!matchVectorAddressRecursively(N.getOperand(0), AM, Depth + 1) &&
        !matchVectorAddressRecursively(Handle.getValue().getOperand(1), AM,
                                       Depth + 1))
      return false;
    AM = Backup;

    // Try again after commuting the operands.
    if (!matchVectorAddressRecursively(Handle.getValue().getOperand(1), AM,
                                       Depth + 1) &&
        !matchVectorAddressRecursively(Handle.getValue().getOperand(0), AM,
                                       Depth + 1))
      return false;
    AM = Backup;

    N = Handle.getValue();
    break;
  }
  }

  return matchAddressBase(N, AM);
}

/// Helper for selectVectorAddr. Handles things that can be folded into a
/// gather/scatter address. The index register and scale should have already
/// been handled.
bool X86DAGToDAGISel::matchVectorAddress(SDValue N, X86ISelAddressMode &AM) {
  return matchVectorAddressRecursively(N, AM, 0);
}

bool X86DAGToDAGISel::selectVectorAddr(MemSDNode *Parent, SDValue BasePtr,
                                       SDValue IndexOp, SDValue ScaleOp,
                                       SDValue &Base, SDValue &Scale,
                                       SDValue &Index, SDValue &Disp,
                                       SDValue &Segment) {
  X86ISelAddressMode AM;
  AM.Scale = ScaleOp->getAsZExtVal();

  // Attempt to match index patterns, as long as we're not relying on implicit
  // sign-extension, which is performed BEFORE scale.
  if (IndexOp.getScalarValueSizeInBits() == BasePtr.getScalarValueSizeInBits())
    AM.IndexReg = matchIndexRecursively(IndexOp, AM, 0);
  else
    AM.IndexReg = IndexOp;

  unsigned AddrSpace = Parent->getPointerInfo().getAddrSpace();
  if (AddrSpace == X86AS::GS)
    AM.Segment = CurDAG->getRegister(X86::GS, MVT::i16);
  if (AddrSpace == X86AS::FS)
    AM.Segment = CurDAG->getRegister(X86::FS, MVT::i16);
  if (AddrSpace == X86AS::SS)
    AM.Segment = CurDAG->getRegister(X86::SS, MVT::i16);

  SDLoc DL(BasePtr);
  MVT VT = BasePtr.getSimpleValueType();

  // Try to match into the base and displacement fields.
  if (matchVectorAddress(BasePtr, AM))
    return false;

  getAddressOperands(AM, DL, VT, Base, Scale, Index, Disp, Segment);
  return true;
}

/// Returns true if it is able to pattern match an addressing mode.
/// It returns the operands which make up the maximal addressing mode it can
/// match by reference.
///
/// Parent is the parent node of the addr operand that is being matched.  It
/// is always a load, store, atomic node, or null.  It is only null when
/// checking memory operands for inline asm nodes.
bool X86DAGToDAGISel::selectAddr(SDNode *Parent, SDValue N, SDValue &Base,
                                 SDValue &Scale, SDValue &Index,
                                 SDValue &Disp, SDValue &Segment) {
  X86ISelAddressMode AM;

  if (Parent &&
      // This list of opcodes are all the nodes that have an "addr:$ptr" operand
      // that are not a MemSDNode, and thus don't have proper addrspace info.
      Parent->getOpcode() != ISD::INTRINSIC_W_CHAIN && // unaligned loads, fixme
      Parent->getOpcode() != ISD::INTRINSIC_VOID && // nontemporal stores
      Parent->getOpcode() != X86ISD::TLSCALL && // Fixme
      Parent->getOpcode() != X86ISD::ENQCMD && // Fixme
      Parent->getOpcode() != X86ISD::ENQCMDS && // Fixme
      Parent->getOpcode() != X86ISD::EH_SJLJ_SETJMP && // setjmp
      Parent->getOpcode() != X86ISD::EH_SJLJ_LONGJMP) { // longjmp
    unsigned AddrSpace =
      cast<MemSDNode>(Parent)->getPointerInfo().getAddrSpace();
    if (AddrSpace == X86AS::GS)
      AM.Segment = CurDAG->getRegister(X86::GS, MVT::i16);
    if (AddrSpace == X86AS::FS)
      AM.Segment = CurDAG->getRegister(X86::FS, MVT::i16);
    if (AddrSpace == X86AS::SS)
      AM.Segment = CurDAG->getRegister(X86::SS, MVT::i16);
  }

  // Save the DL and VT before calling matchAddress, it can invalidate N.
  SDLoc DL(N);
  MVT VT = N.getSimpleValueType();

  if (matchAddress(N, AM))
    return false;

  getAddressOperands(AM, DL, VT, Base, Scale, Index, Disp, Segment);
  return true;
}

bool X86DAGToDAGISel::selectMOV64Imm32(SDValue N, SDValue &Imm) {
  // Cannot use 32 bit constants to reference objects in kernel/large code
  // model.
  if (TM.getCodeModel() == CodeModel::Kernel ||
      TM.getCodeModel() == CodeModel::Large)
    return false;

  // In static codegen with small code model, we can get the address of a label
  // into a register with 'movl'
  if (N->getOpcode() != X86ISD::Wrapper)
    return false;

  N = N.getOperand(0);

  // At least GNU as does not accept 'movl' for TPOFF relocations.
  // FIXME: We could use 'movl' when we know we are targeting MC.
  if (N->getOpcode() == ISD::TargetGlobalTLSAddress)
    return false;

  Imm = N;
  // Small/medium code model can reference non-TargetGlobalAddress objects with
  // 32 bit constants.
  if (N->getOpcode() != ISD::TargetGlobalAddress) {
    return TM.getCodeModel() == CodeModel::Small ||
           TM.getCodeModel() == CodeModel::Medium;
  }

  const GlobalValue *GV = cast<GlobalAddressSDNode>(N)->getGlobal();
  if (std::optional<ConstantRange> CR = GV->getAbsoluteSymbolRange())
    return CR->getUnsignedMax().ult(1ull << 32);

  return !TM.isLargeGlobalValue(GV);
}

bool X86DAGToDAGISel::selectLEA64_32Addr(SDValue N, SDValue &Base,
                                         SDValue &Scale, SDValue &Index,
                                         SDValue &Disp, SDValue &Segment) {
  // Save the debug loc before calling selectLEAAddr, in case it invalidates N.
  SDLoc DL(N);

  if (!selectLEAAddr(N, Base, Scale, Index, Disp, Segment))
    return false;

  auto *RN = dyn_cast<RegisterSDNode>(Base);
  if (RN && RN->getReg() == 0)
    Base = CurDAG->getRegister(0, MVT::i64);
  else if (Base.getValueType() == MVT::i32 && !isa<FrameIndexSDNode>(Base)) {
    // Base could already be %rip, particularly in the x32 ABI.
    SDValue ImplDef = SDValue(CurDAG->getMachineNode(X86::IMPLICIT_DEF, DL,
                                                     MVT::i64), 0);
    Base = CurDAG->getTargetInsertSubreg(X86::sub_32bit, DL, MVT::i64, ImplDef,
                                         Base);
  }

  RN = dyn_cast<RegisterSDNode>(Index);
  if (RN && RN->getReg() == 0)
    Index = CurDAG->getRegister(0, MVT::i64);
  else {
    assert(Index.getValueType() == MVT::i32 &&
           "Expect to be extending 32-bit registers for use in LEA");
    SDValue ImplDef = SDValue(CurDAG->getMachineNode(X86::IMPLICIT_DEF, DL,
                                                     MVT::i64), 0);
    Index = CurDAG->getTargetInsertSubreg(X86::sub_32bit, DL, MVT::i64, ImplDef,
                                          Index);
  }

  return true;
}

/// Calls SelectAddr and determines if the maximal addressing
/// mode it matches can be cost effectively emitted as an LEA instruction.
bool X86DAGToDAGISel::selectLEAAddr(SDValue N,
                                    SDValue &Base, SDValue &Scale,
                                    SDValue &Index, SDValue &Disp,
                                    SDValue &Segment) {
  X86ISelAddressMode AM;

  // Save the DL and VT before calling matchAddress, it can invalidate N.
  SDLoc DL(N);
  MVT VT = N.getSimpleValueType();

  // Set AM.Segment to prevent MatchAddress from using one. LEA doesn't support
  // segments.
  SDValue Copy = AM.Segment;
  SDValue T = CurDAG->getRegister(0, MVT::i32);
  AM.Segment = T;
  if (matchAddress(N, AM))
    return false;
  assert (T == AM.Segment);
  AM.Segment = Copy;

  unsigned Complexity = 0;
  if (AM.BaseType == X86ISelAddressMode::RegBase && AM.Base_Reg.getNode())
    Complexity = 1;
  else if (AM.BaseType == X86ISelAddressMode::FrameIndexBase)
    Complexity = 4;

  if (AM.IndexReg.getNode())
    Complexity++;

  // Don't match just leal(,%reg,2). It's cheaper to do addl %reg, %reg, or with
  // a simple shift.
  if (AM.Scale > 1)
    Complexity++;

  // FIXME: We are artificially lowering the criteria to turn ADD %reg, $GA
  // to a LEA. This is determined with some experimentation but is by no means
  // optimal (especially for code size consideration). LEA is nice because of
  // its three-address nature. Tweak the cost function again when we can run
  // convertToThreeAddress() at register allocation time.
  if (AM.hasSymbolicDisplacement()) {
    // For X86-64, always use LEA to materialize RIP-relative addresses.
    if (Subtarget->is64Bit())
      Complexity = 4;
    else
      Complexity += 2;
  }

  // Heuristic: try harder to form an LEA from ADD if the operands set flags.
  // Unlike ADD, LEA does not affect flags, so we will be less likely to require
  // duplicating flag-producing instructions later in the pipeline.
  if (N.getOpcode() == ISD::ADD) {
    auto isMathWithFlags = [](SDValue V) {
      switch (V.getOpcode()) {
      case X86ISD::ADD:
      case X86ISD::SUB:
      case X86ISD::ADC:
      case X86ISD::SBB:
      case X86ISD::SMUL:
      case X86ISD::UMUL:
      /* TODO: These opcodes can be added safely, but we may want to justify
               their inclusion for different reasons (better for reg-alloc).
      case X86ISD::OR:
      case X86ISD::XOR:
      case X86ISD::AND:
      */
        // Value 1 is the flag output of the node - verify it's not dead.
        return !SDValue(V.getNode(), 1).use_empty();
      default:
        return false;
      }
    };
    // TODO: We might want to factor in whether there's a load folding
    // opportunity for the math op that disappears with LEA.
    if (isMathWithFlags(N.getOperand(0)) || isMathWithFlags(N.getOperand(1)))
      Complexity++;
  }

  if (AM.Disp)
    Complexity++;

  // If it isn't worth using an LEA, reject it.
  if (Complexity <= 2)
    return false;

  getAddressOperands(AM, DL, VT, Base, Scale, Index, Disp, Segment);
  return true;
}

/// This is only run on TargetGlobalTLSAddress nodes.
bool X86DAGToDAGISel::selectTLSADDRAddr(SDValue N, SDValue &Base,
                                        SDValue &Scale, SDValue &Index,
                                        SDValue &Disp, SDValue &Segment) {
  assert(N.getOpcode() == ISD::TargetGlobalTLSAddress ||
         N.getOpcode() == ISD::TargetExternalSymbol);

  X86ISelAddressMode AM;
  if (auto *GA = dyn_cast<GlobalAddressSDNode>(N)) {
    AM.GV = GA->getGlobal();
    AM.Disp += GA->getOffset();
    AM.SymbolFlags = GA->getTargetFlags();
  } else {
    auto *SA = cast<ExternalSymbolSDNode>(N);
    AM.ES = SA->getSymbol();
    AM.SymbolFlags = SA->getTargetFlags();
  }

  if (Subtarget->is32Bit()) {
    AM.Scale = 1;
    AM.IndexReg = CurDAG->getRegister(X86::EBX, MVT::i32);
  }

  MVT VT = N.getSimpleValueType();
  getAddressOperands(AM, SDLoc(N), VT, Base, Scale, Index, Disp, Segment);
  return true;
}

bool X86DAGToDAGISel::selectRelocImm(SDValue N, SDValue &Op) {
  // Keep track of the original value type and whether this value was
  // truncated. If we see a truncation from pointer type to VT that truncates
  // bits that are known to be zero, we can use a narrow reference.
  EVT VT = N.getValueType();
  bool WasTruncated = false;
  if (N.getOpcode() == ISD::TRUNCATE) {
    WasTruncated = true;
    N = N.getOperand(0);
  }

  if (N.getOpcode() != X86ISD::Wrapper)
    return false;

  // We can only use non-GlobalValues as immediates if they were not truncated,
  // as we do not have any range information. If we have a GlobalValue and the
  // address was not truncated, we can select it as an operand directly.
  unsigned Opc = N.getOperand(0)->getOpcode();
  if (Opc != ISD::TargetGlobalAddress || !WasTruncated) {
    Op = N.getOperand(0);
    // We can only select the operand directly if we didn't have to look past a
    // truncate.
    return !WasTruncated;
  }

  // Check that the global's range fits into VT.
  auto *GA = cast<GlobalAddressSDNode>(N.getOperand(0));
  std::optional<ConstantRange> CR = GA->getGlobal()->getAbsoluteSymbolRange();
  if (!CR || CR->getUnsignedMax().uge(1ull << VT.getSizeInBits()))
    return false;

  // Okay, we can use a narrow reference.
  Op = CurDAG->getTargetGlobalAddress(GA->getGlobal(), SDLoc(N), VT,
                                      GA->getOffset(), GA->getTargetFlags());
  return true;
}

bool X86DAGToDAGISel::tryFoldLoad(SDNode *Root, SDNode *P, SDValue N,
                                  SDValue &Base, SDValue &Scale,
                                  SDValue &Index, SDValue &Disp,
                                  SDValue &Segment) {
  assert(Root && P && "Unknown root/parent nodes");
  if (!ISD::isNON_EXTLoad(N.getNode()) ||
      !IsProfitableToFold(N, P, Root) ||
      !IsLegalToFold(N, P, Root, OptLevel))
    return false;

  return selectAddr(N.getNode(),
                    N.getOperand(1), Base, Scale, Index, Disp, Segment);
}

bool X86DAGToDAGISel::tryFoldBroadcast(SDNode *Root, SDNode *P, SDValue N,
                                       SDValue &Base, SDValue &Scale,
                                       SDValue &Index, SDValue &Disp,
                                       SDValue &Segment) {
  assert(Root && P && "Unknown root/parent nodes");
  if (N->getOpcode() != X86ISD::VBROADCAST_LOAD ||
      !IsProfitableToFold(N, P, Root) ||
      !IsLegalToFold(N, P, Root, OptLevel))
    return false;

  return selectAddr(N.getNode(),
                    N.getOperand(1), Base, Scale, Index, Disp, Segment);
}

/// Return an SDNode that returns the value of the global base register.
/// Output instructions required to initialize the global base register,
/// if necessary.
SDNode *X86DAGToDAGISel::getGlobalBaseReg() {
  unsigned GlobalBaseReg = getInstrInfo()->getGlobalBaseReg(MF);
  auto &DL = MF->getDataLayout();
  return CurDAG->getRegister(GlobalBaseReg, TLI->getPointerTy(DL)).getNode();
}

bool X86DAGToDAGISel::isSExtAbsoluteSymbolRef(unsigned Width, SDNode *N) const {
  if (N->getOpcode() == ISD::TRUNCATE)
    N = N->getOperand(0).getNode();
  if (N->getOpcode() != X86ISD::Wrapper)
    return false;

  auto *GA = dyn_cast<GlobalAddressSDNode>(N->getOperand(0));
  if (!GA)
    return false;

  auto *GV = GA->getGlobal();
  std::optional<ConstantRange> CR = GV->getAbsoluteSymbolRange();
  if (CR)
    return CR->getSignedMin().sge(-1ull << Width) &&
           CR->getSignedMax().slt(1ull << Width);
  // In the kernel code model, globals are in the negative 2GB of the address
  // space, so globals can be a sign extended 32-bit immediate.
  // In other code models, small globals are in the low 2GB of the address
  // space, so sign extending them is equivalent to zero extending them.
  return Width == 32 && !TM.isLargeGlobalValue(GV);
}

X86::CondCode X86DAGToDAGISel::getCondFromNode(SDNode *N) const {
  assert(N->isMachineOpcode() && "Unexpected node");
  unsigned Opc = N->getMachineOpcode();
  const MCInstrDesc &MCID = getInstrInfo()->get(Opc);
  int CondNo = X86::getCondSrcNoFromDesc(MCID);
  if (CondNo < 0)
    return X86::COND_INVALID;

  return static_cast<X86::CondCode>(N->getConstantOperandVal(CondNo));
}

/// Test whether the given X86ISD::CMP node has any users that use a flag
/// other than ZF.
bool X86DAGToDAGISel::onlyUsesZeroFlag(SDValue Flags) const {
  // Examine each user of the node.
  for (SDNode::use_iterator UI = Flags->use_begin(), UE = Flags->use_end();
         UI != UE; ++UI) {
    // Only check things that use the flags.
    if (UI.getUse().getResNo() != Flags.getResNo())
      continue;
    // Only examine CopyToReg uses that copy to EFLAGS.
    if (UI->getOpcode() != ISD::CopyToReg ||
        cast<RegisterSDNode>(UI->getOperand(1))->getReg() != X86::EFLAGS)
      return false;
    // Examine each user of the CopyToReg use.
    for (SDNode::use_iterator FlagUI = UI->use_begin(),
           FlagUE = UI->use_end(); FlagUI != FlagUE; ++FlagUI) {
      // Only examine the Flag result.
      if (FlagUI.getUse().getResNo() != 1) continue;
      // Anything unusual: assume conservatively.
      if (!FlagUI->isMachineOpcode()) return false;
      // Examine the condition code of the user.
      X86::CondCode CC = getCondFromNode(*FlagUI);

      switch (CC) {
      // Comparisons which only use the zero flag.
      case X86::COND_E: case X86::COND_NE:
        continue;
      // Anything else: assume conservatively.
      default:
        return false;
      }
    }
  }
  return true;
}

/// Test whether the given X86ISD::CMP node has any uses which require the SF
/// flag to be accurate.
bool X86DAGToDAGISel::hasNoSignFlagUses(SDValue Flags) const {
  // Examine each user of the node.
  for (SDNode::use_iterator UI = Flags->use_begin(), UE = Flags->use_end();
         UI != UE; ++UI) {
    // Only check things that use the flags.
    if (UI.getUse().getResNo() != Flags.getResNo())
      continue;
    // Only examine CopyToReg uses that copy to EFLAGS.
    if (UI->getOpcode() != ISD::CopyToReg ||
        cast<RegisterSDNode>(UI->getOperand(1))->getReg() != X86::EFLAGS)
      return false;
    // Examine each user of the CopyToReg use.
    for (SDNode::use_iterator FlagUI = UI->use_begin(),
           FlagUE = UI->use_end(); FlagUI != FlagUE; ++FlagUI) {
      // Only examine the Flag result.
      if (FlagUI.getUse().getResNo() != 1) continue;
      // Anything unusual: assume conservatively.
      if (!FlagUI->isMachineOpcode()) return false;
      // Examine the condition code of the user.
      X86::CondCode CC = getCondFromNode(*FlagUI);

      switch (CC) {
      // Comparisons which don't examine the SF flag.
      case X86::COND_A: case X86::COND_AE:
      case X86::COND_B: case X86::COND_BE:
      case X86::COND_E: case X86::COND_NE:
      case X86::COND_O: case X86::COND_NO:
      case X86::COND_P: case X86::COND_NP:
        continue;
      // Anything else: assume conservatively.
      default:
        return false;
      }
    }
  }
  return true;
}

static bool mayUseCarryFlag(X86::CondCode CC) {
  switch (CC) {
  // Comparisons which don't examine the CF flag.
  case X86::COND_O: case X86::COND_NO:
  case X86::COND_E: case X86::COND_NE:
  case X86::COND_S: case X86::COND_NS:
  case X86::COND_P: case X86::COND_NP:
  case X86::COND_L: case X86::COND_GE:
  case X86::COND_G: case X86::COND_LE:
    return false;
  // Anything else: assume conservatively.
  default:
    return true;
  }
}

/// Test whether the given node which sets flags has any uses which require the
/// CF flag to be accurate.
 bool X86DAGToDAGISel::hasNoCarryFlagUses(SDValue Flags) const {
  // Examine each user of the node.
  for (SDNode::use_iterator UI = Flags->use_begin(), UE = Flags->use_end();
         UI != UE; ++UI) {
    // Only check things that use the flags.
    if (UI.getUse().getResNo() != Flags.getResNo())
      continue;

    unsigned UIOpc = UI->getOpcode();

    if (UIOpc == ISD::CopyToReg) {
      // Only examine CopyToReg uses that copy to EFLAGS.
      if (cast<RegisterSDNode>(UI->getOperand(1))->getReg() != X86::EFLAGS)
        return false;
      // Examine each user of the CopyToReg use.
      for (SDNode::use_iterator FlagUI = UI->use_begin(), FlagUE = UI->use_end();
           FlagUI != FlagUE; ++FlagUI) {
        // Only examine the Flag result.
        if (FlagUI.getUse().getResNo() != 1)
          continue;
        // Anything unusual: assume conservatively.
        if (!FlagUI->isMachineOpcode())
          return false;
        // Examine the condition code of the user.
        X86::CondCode CC = getCondFromNode(*FlagUI);

        if (mayUseCarryFlag(CC))
          return false;
      }

      // This CopyToReg is ok. Move on to the next user.
      continue;
    }

    // This might be an unselected node. So look for the pre-isel opcodes that
    // use flags.
    unsigned CCOpNo;
    switch (UIOpc) {
    default:
      // Something unusual. Be conservative.
      return false;
    case X86ISD::SETCC:       CCOpNo = 0; break;
    case X86ISD::SETCC_CARRY: CCOpNo = 0; break;
    case X86ISD::CMOV:        CCOpNo = 2; break;
    case X86ISD::BRCOND:      CCOpNo = 2; break;
    }

    X86::CondCode CC = (X86::CondCode)UI->getConstantOperandVal(CCOpNo);
    if (mayUseCarryFlag(CC))
      return false;
  }
  return true;
}

/// Check whether or not the chain ending in StoreNode is suitable for doing
/// the {load; op; store} to modify transformation.
static bool isFusableLoadOpStorePattern(StoreSDNode *StoreNode,
                                        SDValue StoredVal, SelectionDAG *CurDAG,
                                        unsigned LoadOpNo,
                                        LoadSDNode *&LoadNode,
                                        SDValue &InputChain) {
  // Is the stored value result 0 of the operation?
  if (StoredVal.getResNo() != 0) return false;

  // Are there other uses of the operation other than the store?
  if (!StoredVal.getNode()->hasNUsesOfValue(1, 0)) return false;

  // Is the store non-extending and non-indexed?
  if (!ISD::isNormalStore(StoreNode) || StoreNode->isNonTemporal())
    return false;

  SDValue Load = StoredVal->getOperand(LoadOpNo);
  // Is the stored value a non-extending and non-indexed load?
  if (!ISD::isNormalLoad(Load.getNode())) return false;

  // Return LoadNode by reference.
  LoadNode = cast<LoadSDNode>(Load);

  // Is store the only read of the loaded value?
  if (!Load.hasOneUse())
    return false;

  // Is the address of the store the same as the load?
  if (LoadNode->getBasePtr() != StoreNode->getBasePtr() ||
      LoadNode->getOffset() != StoreNode->getOffset())
    return false;

  bool FoundLoad = false;
  SmallVector<SDValue, 4> ChainOps;
  SmallVector<const SDNode *, 4> LoopWorklist;
  SmallPtrSet<const SDNode *, 16> Visited;
  const unsigned int Max = 1024;

  //  Visualization of Load-Op-Store fusion:
  // -------------------------
  // Legend:
  //    *-lines = Chain operand dependencies.
  //    |-lines = Normal operand dependencies.
  //    Dependencies flow down and right. n-suffix references multiple nodes.
  //
  //        C                        Xn  C
  //        *                         *  *
  //        *                          * *
  //  Xn  A-LD    Yn                    TF         Yn
  //   *    * \   |                       *        |
  //    *   *  \  |                        *       |
  //     *  *   \ |             =>       A--LD_OP_ST
  //      * *    \|                                 \
  //       TF    OP                                  \
  //         *   | \                                  Zn
  //          *  |  \
  //         A-ST    Zn
  //

  // This merge induced dependences from: #1: Xn -> LD, OP, Zn
  //                                      #2: Yn -> LD
  //                                      #3: ST -> Zn

  // Ensure the transform is safe by checking for the dual
  // dependencies to make sure we do not induce a loop.

  // As LD is a predecessor to both OP and ST we can do this by checking:
  //  a). if LD is a predecessor to a member of Xn or Yn.
  //  b). if a Zn is a predecessor to ST.

  // However, (b) can only occur through being a chain predecessor to
  // ST, which is the same as Zn being a member or predecessor of Xn,
  // which is a subset of LD being a predecessor of Xn. So it's
  // subsumed by check (a).

  SDValue Chain = StoreNode->getChain();

  // Gather X elements in ChainOps.
  if (Chain == Load.getValue(1)) {
    FoundLoad = true;
    ChainOps.push_back(Load.getOperand(0));
  } else if (Chain.getOpcode() == ISD::TokenFactor) {
    for (unsigned i = 0, e = Chain.getNumOperands(); i != e; ++i) {
      SDValue Op = Chain.getOperand(i);
      if (Op == Load.getValue(1)) {
        FoundLoad = true;
        // Drop Load, but keep its chain. No cycle check necessary.
        ChainOps.push_back(Load.getOperand(0));
        continue;
      }
      LoopWorklist.push_back(Op.getNode());
      ChainOps.push_back(Op);
    }
  }

  if (!FoundLoad)
    return false;

  // Worklist is currently Xn. Add Yn to worklist.
  for (SDValue Op : StoredVal->ops())
    if (Op.getNode() != LoadNode)
      LoopWorklist.push_back(Op.getNode());

  // Check (a) if Load is a predecessor to Xn + Yn
  if (SDNode::hasPredecessorHelper(Load.getNode(), Visited, LoopWorklist, Max,
                                   true))
    return false;

  InputChain =
      CurDAG->getNode(ISD::TokenFactor, SDLoc(Chain), MVT::Other, ChainOps);
  return true;
}

// Change a chain of {load; op; store} of the same value into a simple op
// through memory of that value, if the uses of the modified value and its
// address are suitable.
//
// The tablegen pattern memory operand pattern is currently not able to match
// the case where the EFLAGS on the original operation are used.
//
// To move this to tablegen, we'll need to improve tablegen to allow flags to
// be transferred from a node in the pattern to the result node, probably with
// a new keyword. For example, we have this
// def DEC64m : RI<0xFF, MRM1m, (outs), (ins i64mem:$dst), "dec{q}\t$dst",
//  [(store (add (loadi64 addr:$dst), -1), addr:$dst),
//   (implicit EFLAGS)]>;
// but maybe need something like this
// def DEC64m : RI<0xFF, MRM1m, (outs), (ins i64mem:$dst), "dec{q}\t$dst",
//  [(store (add (loadi64 addr:$dst), -1), addr:$dst),
//   (transferrable EFLAGS)]>;
//
// Until then, we manually fold these and instruction select the operation
// here.
bool X86DAGToDAGISel::foldLoadStoreIntoMemOperand(SDNode *Node) {
  auto *StoreNode = cast<StoreSDNode>(Node);
  SDValue StoredVal = StoreNode->getOperand(1);
  unsigned Opc = StoredVal->getOpcode();

  // Before we try to select anything, make sure this is memory operand size
  // and opcode we can handle. Note that this must match the code below that
  // actually lowers the opcodes.
  EVT MemVT = StoreNode->getMemoryVT();
  if (MemVT != MVT::i64 && MemVT != MVT::i32 && MemVT != MVT::i16 &&
      MemVT != MVT::i8)
    return false;

  bool IsCommutable = false;
  bool IsNegate = false;
  switch (Opc) {
  default:
    return false;
  case X86ISD::SUB:
    IsNegate = isNullConstant(StoredVal.getOperand(0));
    break;
  case X86ISD::SBB:
    break;
  case X86ISD::ADD:
  case X86ISD::ADC:
  case X86ISD::AND:
  case X86ISD::OR:
  case X86ISD::XOR:
    IsCommutable = true;
    break;
  }

  unsigned LoadOpNo = IsNegate ? 1 : 0;
  LoadSDNode *LoadNode = nullptr;
  SDValue InputChain;
  if (!isFusableLoadOpStorePattern(StoreNode, StoredVal, CurDAG, LoadOpNo,
                                   LoadNode, InputChain)) {
    if (!IsCommutable)
      return false;

    // This operation is commutable, try the other operand.
    LoadOpNo = 1;
    if (!isFusableLoadOpStorePattern(StoreNode, StoredVal, CurDAG, LoadOpNo,
                                     LoadNode, InputChain))
      return false;
  }

  SDValue Base, Scale, Index, Disp, Segment;
  if (!selectAddr(LoadNode, LoadNode->getBasePtr(), Base, Scale, Index, Disp,
                  Segment))
    return false;

  auto SelectOpcode = [&](unsigned Opc64, unsigned Opc32, unsigned Opc16,
                          unsigned Opc8) {
    switch (MemVT.getSimpleVT().SimpleTy) {
    case MVT::i64:
      return Opc64;
    case MVT::i32:
      return Opc32;
    case MVT::i16:
      return Opc16;
    case MVT::i8:
      return Opc8;
    default:
      llvm_unreachable("Invalid size!");
    }
  };

  MachineSDNode *Result;
  switch (Opc) {
  case X86ISD::SUB:
    // Handle negate.
    if (IsNegate) {
      unsigned NewOpc = SelectOpcode(X86::NEG64m, X86::NEG32m, X86::NEG16m,
                                     X86::NEG8m);
      const SDValue Ops[] = {Base, Scale, Index, Disp, Segment, InputChain};
      Result = CurDAG->getMachineNode(NewOpc, SDLoc(Node), MVT::i32,
                                      MVT::Other, Ops);
      break;
    }
   [[fallthrough]];
  case X86ISD::ADD:
    // Try to match inc/dec.
    if (!Subtarget->slowIncDec() || CurDAG->shouldOptForSize()) {
      bool IsOne = isOneConstant(StoredVal.getOperand(1));
      bool IsNegOne = isAllOnesConstant(StoredVal.getOperand(1));
      // ADD/SUB with 1/-1 and carry flag isn't used can use inc/dec.
      if ((IsOne || IsNegOne) && hasNoCarryFlagUses(StoredVal.getValue(1))) {
        unsigned NewOpc =
          ((Opc == X86ISD::ADD) == IsOne)
              ? SelectOpcode(X86::INC64m, X86::INC32m, X86::INC16m, X86::INC8m)
              : SelectOpcode(X86::DEC64m, X86::DEC32m, X86::DEC16m, X86::DEC8m);
        const SDValue Ops[] = {Base, Scale, Index, Disp, Segment, InputChain};
        Result = CurDAG->getMachineNode(NewOpc, SDLoc(Node), MVT::i32,
                                        MVT::Other, Ops);
        break;
      }
    }
    [[fallthrough]];
  case X86ISD::ADC:
  case X86ISD::SBB:
  case X86ISD::AND:
  case X86ISD::OR:
  case X86ISD::XOR: {
    auto SelectRegOpcode = [SelectOpcode](unsigned Opc) {
      switch (Opc) {
      case X86ISD::ADD:
        return SelectOpcode(X86::ADD64mr, X86::ADD32mr, X86::ADD16mr,
                            X86::ADD8mr);
      case X86ISD::ADC:
        return SelectOpcode(X86::ADC64mr, X86::ADC32mr, X86::ADC16mr,
                            X86::ADC8mr);
      case X86ISD::SUB:
        return SelectOpcode(X86::SUB64mr, X86::SUB32mr, X86::SUB16mr,
                            X86::SUB8mr);
      case X86ISD::SBB:
        return SelectOpcode(X86::SBB64mr, X86::SBB32mr, X86::SBB16mr,
                            X86::SBB8mr);
      case X86ISD::AND:
        return SelectOpcode(X86::AND64mr, X86::AND32mr, X86::AND16mr,
                            X86::AND8mr);
      case X86ISD::OR:
        return SelectOpcode(X86::OR64mr, X86::OR32mr, X86::OR16mr, X86::OR8mr);
      case X86ISD::XOR:
        return SelectOpcode(X86::XOR64mr, X86::XOR32mr, X86::XOR16mr,
                            X86::XOR8mr);
      default:
        llvm_unreachable("Invalid opcode!");
      }
    };
    auto SelectImmOpcode = [SelectOpcode](unsigned Opc) {
      switch (Opc) {
      case X86ISD::ADD:
        return SelectOpcode(X86::ADD64mi32, X86::ADD32mi, X86::ADD16mi,
                            X86::ADD8mi);
      case X86ISD::ADC:
        return SelectOpcode(X86::ADC64mi32, X86::ADC32mi, X86::ADC16mi,
                            X86::ADC8mi);
      case X86ISD::SUB:
        return SelectOpcode(X86::SUB64mi32, X86::SUB32mi, X86::SUB16mi,
                            X86::SUB8mi);
      case X86ISD::SBB:
        return SelectOpcode(X86::SBB64mi32, X86::SBB32mi, X86::SBB16mi,
                            X86::SBB8mi);
      case X86ISD::AND:
        return SelectOpcode(X86::AND64mi32, X86::AND32mi, X86::AND16mi,
                            X86::AND8mi);
      case X86ISD::OR:
        return SelectOpcode(X86::OR64mi32, X86::OR32mi, X86::OR16mi,
                            X86::OR8mi);
      case X86ISD::XOR:
        return SelectOpcode(X86::XOR64mi32, X86::XOR32mi, X86::XOR16mi,
                            X86::XOR8mi);
      default:
        llvm_unreachable("Invalid opcode!");
      }
    };

    unsigned NewOpc = SelectRegOpcode(Opc);
    SDValue Operand = StoredVal->getOperand(1-LoadOpNo);

    // See if the operand is a constant that we can fold into an immediate
    // operand.
    if (auto *OperandC = dyn_cast<ConstantSDNode>(Operand)) {
      int64_t OperandV = OperandC->getSExtValue();

      // Check if we can shrink the operand enough to fit in an immediate (or
      // fit into a smaller immediate) by negating it and switching the
      // operation.
      if ((Opc == X86ISD::ADD || Opc == X86ISD::SUB) &&
          ((MemVT != MVT::i8 && !isInt<8>(OperandV) && isInt<8>(-OperandV)) ||
           (MemVT == MVT::i64 && !isInt<32>(OperandV) &&
            isInt<32>(-OperandV))) &&
          hasNoCarryFlagUses(StoredVal.getValue(1))) {
        OperandV = -OperandV;
        Opc = Opc == X86ISD::ADD ? X86ISD::SUB : X86ISD::ADD;
      }

      if (MemVT != MVT::i64 || isInt<32>(OperandV)) {
        Operand = CurDAG->getTargetConstant(OperandV, SDLoc(Node), MemVT);
        NewOpc = SelectImmOpcode(Opc);
      }
    }

    if (Opc == X86ISD::ADC || Opc == X86ISD::SBB) {
      SDValue CopyTo =
          CurDAG->getCopyToReg(InputChain, SDLoc(Node), X86::EFLAGS,
                               StoredVal.getOperand(2), SDValue());

      const SDValue Ops[] = {Base,    Scale,   Index,  Disp,
                             Segment, Operand, CopyTo, CopyTo.getValue(1)};
      Result = CurDAG->getMachineNode(NewOpc, SDLoc(Node), MVT::i32, MVT::Other,
                                      Ops);
    } else {
      const SDValue Ops[] = {Base,    Scale,   Index,     Disp,
                             Segment, Operand, InputChain};
      Result = CurDAG->getMachineNode(NewOpc, SDLoc(Node), MVT::i32, MVT::Other,
                                      Ops);
    }
    break;
  }
  default:
    llvm_unreachable("Invalid opcode!");
  }

  MachineMemOperand *MemOps[] = {StoreNode->getMemOperand(),
                                 LoadNode->getMemOperand()};
  CurDAG->setNodeMemRefs(Result, MemOps);

  // Update Load Chain uses as well.
  ReplaceUses(SDValue(LoadNode, 1), SDValue(Result, 1));
  ReplaceUses(SDValue(StoreNode, 0), SDValue(Result, 1));
  ReplaceUses(SDValue(StoredVal.getNode(), 1), SDValue(Result, 0));
  CurDAG->RemoveDeadNode(Node);
  return true;
}

// See if this is an  X & Mask  that we can match to BEXTR/BZHI.
// Where Mask is one of the following patterns:
//   a) x &  (1 << nbits) - 1
//   b) x & ~(-1 << nbits)
//   c) x &  (-1 >> (32 - y))
//   d) x << (32 - y) >> (32 - y)
//   e) (1 << nbits) - 1
bool X86DAGToDAGISel::matchBitExtract(SDNode *Node) {
  assert(
      (Node->getOpcode() == ISD::ADD || Node->getOpcode() == ISD::AND ||
       Node->getOpcode() == ISD::SRL) &&
      "Should be either an and-mask, or right-shift after clearing high bits.");

  // BEXTR is BMI instruction, BZHI is BMI2 instruction. We need at least one.
  if (!Subtarget->hasBMI() && !Subtarget->hasBMI2())
    return false;

  MVT NVT = Node->getSimpleValueType(0);

  // Only supported for 32 and 64 bits.
  if (NVT != MVT::i32 && NVT != MVT::i64)
    return false;

  SDValue NBits;
  bool NegateNBits;

  // If we have BMI2's BZHI, we are ok with muti-use patterns.
  // Else, if we only have BMI1's BEXTR, we require one-use.
  const bool AllowExtraUsesByDefault = Subtarget->hasBMI2();
  auto checkUses = [AllowExtraUsesByDefault](
                       SDValue Op, unsigned NUses,
                       std::optional<bool> AllowExtraUses) {
    return AllowExtraUses.value_or(AllowExtraUsesByDefault) ||
           Op.getNode()->hasNUsesOfValue(NUses, Op.getResNo());
  };
  auto checkOneUse = [checkUses](SDValue Op,
                                 std::optional<bool> AllowExtraUses =
                                     std::nullopt) {
    return checkUses(Op, 1, AllowExtraUses);
  };
  auto checkTwoUse = [checkUses](SDValue Op,
                                 std::optional<bool> AllowExtraUses =
                                     std::nullopt) {
    return checkUses(Op, 2, AllowExtraUses);
  };

  auto peekThroughOneUseTruncation = [checkOneUse](SDValue V) {
    if (V->getOpcode() == ISD::TRUNCATE && checkOneUse(V)) {
      assert(V.getSimpleValueType() == MVT::i32 &&
             V.getOperand(0).getSimpleValueType() == MVT::i64 &&
             "Expected i64 -> i32 truncation");
      V = V.getOperand(0);
    }
    return V;
  };

  // a) x & ((1 << nbits) + (-1))
  auto matchPatternA = [checkOneUse, peekThroughOneUseTruncation, &NBits,
                        &NegateNBits](SDValue Mask) -> bool {
    // Match `add`. Must only have one use!
    if (Mask->getOpcode() != ISD::ADD || !checkOneUse(Mask))
      return false;
    // We should be adding all-ones constant (i.e. subtracting one.)
    if (!isAllOnesConstant(Mask->getOperand(1)))
      return false;
    // Match `1 << nbits`. Might be truncated. Must only have one use!
    SDValue M0 = peekThroughOneUseTruncation(Mask->getOperand(0));
    if (M0->getOpcode() != ISD::SHL || !checkOneUse(M0))
      return false;
    if (!isOneConstant(M0->getOperand(0)))
      return false;
    NBits = M0->getOperand(1);
    NegateNBits = false;
    return true;
  };

  auto isAllOnes = [this, peekThroughOneUseTruncation, NVT](SDValue V) {
    V = peekThroughOneUseTruncation(V);
    return CurDAG->MaskedValueIsAllOnes(
        V, APInt::getLowBitsSet(V.getSimpleValueType().getSizeInBits(),
                                NVT.getSizeInBits()));
  };

  // b) x & ~(-1 << nbits)
  auto matchPatternB = [checkOneUse, isAllOnes, peekThroughOneUseTruncation,
                        &NBits, &NegateNBits](SDValue Mask) -> bool {
    // Match `~()`. Must only have one use!
    if (Mask.getOpcode() != ISD::XOR || !checkOneUse(Mask))
      return false;
    // The -1 only has to be all-ones for the final Node's NVT.
    if (!isAllOnes(Mask->getOperand(1)))
      return false;
    // Match `-1 << nbits`. Might be truncated. Must only have one use!
    SDValue M0 = peekThroughOneUseTruncation(Mask->getOperand(0));
    if (M0->getOpcode() != ISD::SHL || !checkOneUse(M0))
      return false;
    // The -1 only has to be all-ones for the final Node's NVT.
    if (!isAllOnes(M0->getOperand(0)))
      return false;
    NBits = M0->getOperand(1);
    NegateNBits = false;
    return true;
  };

  // Try to match potentially-truncated shift amount as `(bitwidth - y)`,
  // or leave the shift amount as-is, but then we'll have to negate it.
  auto canonicalizeShiftAmt = [&NBits, &NegateNBits](SDValue ShiftAmt,
                                                     unsigned Bitwidth) {
    NBits = ShiftAmt;
    NegateNBits = true;
    // Skip over a truncate of the shift amount, if any.
    if (NBits.getOpcode() == ISD::TRUNCATE)
      NBits = NBits.getOperand(0);
    // Try to match the shift amount as (bitwidth - y). It should go away, too.
    // If it doesn't match, that's fine, we'll just negate it ourselves.
    if (NBits.getOpcode() != ISD::SUB)
      return;
    auto *V0 = dyn_cast<ConstantSDNode>(NBits.getOperand(0));
    if (!V0 || V0->getZExtValue() != Bitwidth)
      return;
    NBits = NBits.getOperand(1);
    NegateNBits = false;
  };

  // c) x &  (-1 >> z)  but then we'll have to subtract z from bitwidth
  //   or
  // c) x &  (-1 >> (32 - y))
  auto matchPatternC = [checkOneUse, peekThroughOneUseTruncation, &NegateNBits,
                        canonicalizeShiftAmt](SDValue Mask) -> bool {
    // The mask itself may be truncated.
    Mask = peekThroughOneUseTruncation(Mask);
    unsigned Bitwidth = Mask.getSimpleValueType().getSizeInBits();
    // Match `l>>`. Must only have one use!
    if (Mask.getOpcode() != ISD::SRL || !checkOneUse(Mask))
      return false;
    // We should be shifting truly all-ones constant.
    if (!isAllOnesConstant(Mask.getOperand(0)))
      return false;
    SDValue M1 = Mask.getOperand(1);
    // The shift amount should not be used externally.
    if (!checkOneUse(M1))
      return false;
    canonicalizeShiftAmt(M1, Bitwidth);
    // Pattern c. is non-canonical, and is expanded into pattern d. iff there
    // is no extra use of the mask. Clearly, there was one since we are here.
    // But at the same time, if we need to negate the shift amount,
    // then we don't want the mask to stick around, else it's unprofitable.
    return !NegateNBits;
  };

  SDValue X;

  // d) x << z >> z  but then we'll have to subtract z from bitwidth
  //   or
  // d) x << (32 - y) >> (32 - y)
  auto matchPatternD = [checkOneUse, checkTwoUse, canonicalizeShiftAmt,
                        AllowExtraUsesByDefault, &NegateNBits,
                        &X](SDNode *Node) -> bool {
    if (Node->getOpcode() != ISD::SRL)
      return false;
    SDValue N0 = Node->getOperand(0);
    if (N0->getOpcode() != ISD::SHL)
      return false;
    unsigned Bitwidth = N0.getSimpleValueType().getSizeInBits();
    SDValue N1 = Node->getOperand(1);
    SDValue N01 = N0->getOperand(1);
    // Both of the shifts must be by the exact same value.
    if (N1 != N01)
      return false;
    canonicalizeShiftAmt(N1, Bitwidth);
    // There should not be any external uses of the inner shift / shift amount.
    // Note that while we are generally okay with external uses given BMI2,
    // iff we need to negate the shift amount, we are not okay with extra uses.
    const bool AllowExtraUses = AllowExtraUsesByDefault && !NegateNBits;
    if (!checkOneUse(N0, AllowExtraUses) || !checkTwoUse(N1, AllowExtraUses))
      return false;
    X = N0->getOperand(0);
    return true;
  };

  auto matchLowBitMask = [matchPatternA, matchPatternB,
                          matchPatternC](SDValue Mask) -> bool {
    return matchPatternA(Mask) || matchPatternB(Mask) || matchPatternC(Mask);
  };

  if (Node->getOpcode() == ISD::AND) {
    X = Node->getOperand(0);
    SDValue Mask = Node->getOperand(1);

    if (matchLowBitMask(Mask)) {
      // Great.
    } else {
      std::swap(X, Mask);
      if (!matchLowBitMask(Mask))
        return false;
    }
  } else if (matchLowBitMask(SDValue(Node, 0))) {
    X = CurDAG->getAllOnesConstant(SDLoc(Node), NVT);
  } else if (!matchPatternD(Node))
    return false;

  // If we need to negate the shift amount, require BMI2 BZHI support.
  // It's just too unprofitable for BMI1 BEXTR.
  if (NegateNBits && !Subtarget->hasBMI2())
    return false;

  SDLoc DL(Node);

  // Truncate the shift amount.
  NBits = CurDAG->getNode(ISD::TRUNCATE, DL, MVT::i8, NBits);
  insertDAGNode(*CurDAG, SDValue(Node, 0), NBits);

  // Insert 8-bit NBits into lowest 8 bits of 32-bit register.
  // All the other bits are undefined, we do not care about them.
  SDValue ImplDef = SDValue(
      CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, DL, MVT::i32), 0);
  insertDAGNode(*CurDAG, SDValue(Node, 0), ImplDef);

  SDValue SRIdxVal = CurDAG->getTargetConstant(X86::sub_8bit, DL, MVT::i32);
  insertDAGNode(*CurDAG, SDValue(Node, 0), SRIdxVal);
  NBits = SDValue(CurDAG->getMachineNode(TargetOpcode::INSERT_SUBREG, DL,
                                         MVT::i32, ImplDef, NBits, SRIdxVal),
                  0);
  insertDAGNode(*CurDAG, SDValue(Node, 0), NBits);

  // We might have matched the amount of high bits to be cleared,
  // but we want the amount of low bits to be kept, so negate it then.
  if (NegateNBits) {
    SDValue BitWidthC = CurDAG->getConstant(NVT.getSizeInBits(), DL, MVT::i32);
    insertDAGNode(*CurDAG, SDValue(Node, 0), BitWidthC);

    NBits = CurDAG->getNode(ISD::SUB, DL, MVT::i32, BitWidthC, NBits);
    insertDAGNode(*CurDAG, SDValue(Node, 0), NBits);
  }

  if (Subtarget->hasBMI2()) {
    // Great, just emit the BZHI..
    if (NVT != MVT::i32) {
      // But have to place the bit count into the wide-enough register first.
      NBits = CurDAG->getNode(ISD::ANY_EXTEND, DL, NVT, NBits);
      insertDAGNode(*CurDAG, SDValue(Node, 0), NBits);
    }

    SDValue Extract = CurDAG->getNode(X86ISD::BZHI, DL, NVT, X, NBits);
    ReplaceNode(Node, Extract.getNode());
    SelectCode(Extract.getNode());
    return true;
  }

  // Else, if we do *NOT* have BMI2, let's find out if the if the 'X' is
  // *logically* shifted (potentially with one-use trunc inbetween),
  // and the truncation was the only use of the shift,
  // and if so look past one-use truncation.
  {
    SDValue RealX = peekThroughOneUseTruncation(X);
    // FIXME: only if the shift is one-use?
    if (RealX != X && RealX.getOpcode() == ISD::SRL)
      X = RealX;
  }

  MVT XVT = X.getSimpleValueType();

  // Else, emitting BEXTR requires one more step.
  // The 'control' of BEXTR has the pattern of:
  // [15...8 bit][ 7...0 bit] location
  // [ bit count][     shift] name
  // I.e. 0b000000011'00000001 means  (x >> 0b1) & 0b11

  // Shift NBits left by 8 bits, thus producing 'control'.
  // This makes the low 8 bits to be zero.
  SDValue C8 = CurDAG->getConstant(8, DL, MVT::i8);
  insertDAGNode(*CurDAG, SDValue(Node, 0), C8);
  SDValue Control = CurDAG->getNode(ISD::SHL, DL, MVT::i32, NBits, C8);
  insertDAGNode(*CurDAG, SDValue(Node, 0), Control);

  // If the 'X' is *logically* shifted, we can fold that shift into 'control'.
  // FIXME: only if the shift is one-use?
  if (X.getOpcode() == ISD::SRL) {
    SDValue ShiftAmt = X.getOperand(1);
    X = X.getOperand(0);

    assert(ShiftAmt.getValueType() == MVT::i8 &&
           "Expected shift amount to be i8");

    // Now, *zero*-extend the shift amount. The bits 8...15 *must* be zero!
    // We could zext to i16 in some form, but we intentionally don't do that.
    SDValue OrigShiftAmt = ShiftAmt;
    ShiftAmt = CurDAG->getNode(ISD::ZERO_EXTEND, DL, MVT::i32, ShiftAmt);
    insertDAGNode(*CurDAG, OrigShiftAmt, ShiftAmt);

    // And now 'or' these low 8 bits of shift amount into the 'control'.
    Control = CurDAG->getNode(ISD::OR, DL, MVT::i32, Control, ShiftAmt);
    insertDAGNode(*CurDAG, SDValue(Node, 0), Control);
  }

  // But have to place the 'control' into the wide-enough register first.
  if (XVT != MVT::i32) {
    Control = CurDAG->getNode(ISD::ANY_EXTEND, DL, XVT, Control);
    insertDAGNode(*CurDAG, SDValue(Node, 0), Control);
  }

  // And finally, form the BEXTR itself.
  SDValue Extract = CurDAG->getNode(X86ISD::BEXTR, DL, XVT, X, Control);

  // The 'X' was originally truncated. Do that now.
  if (XVT != NVT) {
    insertDAGNode(*CurDAG, SDValue(Node, 0), Extract);
    Extract = CurDAG->getNode(ISD::TRUNCATE, DL, NVT, Extract);
  }

  ReplaceNode(Node, Extract.getNode());
  SelectCode(Extract.getNode());

  return true;
}

// See if this is an (X >> C1) & C2 that we can match to BEXTR/BEXTRI.
MachineSDNode *X86DAGToDAGISel::matchBEXTRFromAndImm(SDNode *Node) {
  MVT NVT = Node->getSimpleValueType(0);
  SDLoc dl(Node);

  SDValue N0 = Node->getOperand(0);
  SDValue N1 = Node->getOperand(1);

  // If we have TBM we can use an immediate for the control. If we have BMI
  // we should only do this if the BEXTR instruction is implemented well.
  // Otherwise moving the control into a register makes this more costly.
  // TODO: Maybe load folding, greater than 32-bit masks, or a guarantee of LICM
  // hoisting the move immediate would make it worthwhile with a less optimal
  // BEXTR?
  bool PreferBEXTR =
      Subtarget->hasTBM() || (Subtarget->hasBMI() && Subtarget->hasFastBEXTR());
  if (!PreferBEXTR && !Subtarget->hasBMI2())
    return nullptr;

  // Must have a shift right.
  if (N0->getOpcode() != ISD::SRL && N0->getOpcode() != ISD::SRA)
    return nullptr;

  // Shift can't have additional users.
  if (!N0->hasOneUse())
    return nullptr;

  // Only supported for 32 and 64 bits.
  if (NVT != MVT::i32 && NVT != MVT::i64)
    return nullptr;

  // Shift amount and RHS of and must be constant.
  auto *MaskCst = dyn_cast<ConstantSDNode>(N1);
  auto *ShiftCst = dyn_cast<ConstantSDNode>(N0->getOperand(1));
  if (!MaskCst || !ShiftCst)
    return nullptr;

  // And RHS must be a mask.
  uint64_t Mask = MaskCst->getZExtValue();
  if (!isMask_64(Mask))
    return nullptr;

  uint64_t Shift = ShiftCst->getZExtValue();
  uint64_t MaskSize = llvm::popcount(Mask);

  // Don't interfere with something that can be handled by extracting AH.
  // TODO: If we are able to fold a load, BEXTR might still be better than AH.
  if (Shift == 8 && MaskSize == 8)
    return nullptr;

  // Make sure we are only using bits that were in the original value, not
  // shifted in.
  if (Shift + MaskSize > NVT.getSizeInBits())
    return nullptr;

  // BZHI, if available, is always fast, unlike BEXTR. But even if we decide
  // that we can't use BEXTR, it is only worthwhile using BZHI if the mask
  // does not fit into 32 bits. Load folding is not a sufficient reason.
  if (!PreferBEXTR && MaskSize <= 32)
    return nullptr;

  SDValue Control;
  unsigned ROpc, MOpc;

#define GET_EGPR_IF_ENABLED(OPC) (Subtarget->hasEGPR() ? OPC##_EVEX : OPC)
  if (!PreferBEXTR) {
    assert(Subtarget->hasBMI2() && "We must have BMI2's BZHI then.");
    // If we can't make use of BEXTR then we can't fuse shift+mask stages.
    // Let's perform the mask first, and apply shift later. Note that we need to
    // widen the mask to account for the fact that we'll apply shift afterwards!
    Control = CurDAG->getTargetConstant(Shift + MaskSize, dl, NVT);
    ROpc = NVT == MVT::i64 ? GET_EGPR_IF_ENABLED(X86::BZHI64rr)
                           : GET_EGPR_IF_ENABLED(X86::BZHI32rr);
    MOpc = NVT == MVT::i64 ? GET_EGPR_IF_ENABLED(X86::BZHI64rm)
                           : GET_EGPR_IF_ENABLED(X86::BZHI32rm);
    unsigned NewOpc = NVT == MVT::i64 ? X86::MOV32ri64 : X86::MOV32ri;
    Control = SDValue(CurDAG->getMachineNode(NewOpc, dl, NVT, Control), 0);
  } else {
    // The 'control' of BEXTR has the pattern of:
    // [15...8 bit][ 7...0 bit] location
    // [ bit count][     shift] name
    // I.e. 0b000000011'00000001 means  (x >> 0b1) & 0b11
    Control = CurDAG->getTargetConstant(Shift | (MaskSize << 8), dl, NVT);
    if (Subtarget->hasTBM()) {
      ROpc = NVT == MVT::i64 ? X86::BEXTRI64ri : X86::BEXTRI32ri;
      MOpc = NVT == MVT::i64 ? X86::BEXTRI64mi : X86::BEXTRI32mi;
    } else {
      assert(Subtarget->hasBMI() && "We must have BMI1's BEXTR then.");
      // BMI requires the immediate to placed in a register.
      ROpc = NVT == MVT::i64 ? GET_EGPR_IF_ENABLED(X86::BEXTR64rr)
                             : GET_EGPR_IF_ENABLED(X86::BEXTR32rr);
      MOpc = NVT == MVT::i64 ? GET_EGPR_IF_ENABLED(X86::BEXTR64rm)
                             : GET_EGPR_IF_ENABLED(X86::BEXTR32rm);
      unsigned NewOpc = NVT == MVT::i64 ? X86::MOV32ri64 : X86::MOV32ri;
      Control = SDValue(CurDAG->getMachineNode(NewOpc, dl, NVT, Control), 0);
    }
  }

  MachineSDNode *NewNode;
  SDValue Input = N0->getOperand(0);
  SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
  if (tryFoldLoad(Node, N0.getNode(), Input, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
    SDValue Ops[] = {
        Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, Control, Input.getOperand(0)};
    SDVTList VTs = CurDAG->getVTList(NVT, MVT::i32, MVT::Other);
    NewNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
    // Update the chain.
    ReplaceUses(Input.getValue(1), SDValue(NewNode, 2));
    // Record the mem-refs
    CurDAG->setNodeMemRefs(NewNode, {cast<LoadSDNode>(Input)->getMemOperand()});
  } else {
    NewNode = CurDAG->getMachineNode(ROpc, dl, NVT, MVT::i32, Input, Control);
  }

  if (!PreferBEXTR) {
    // We still need to apply the shift.
    SDValue ShAmt = CurDAG->getTargetConstant(Shift, dl, NVT);
    unsigned NewOpc = NVT == MVT::i64 ? GET_ND_IF_ENABLED(X86::SHR64ri)
                                      : GET_ND_IF_ENABLED(X86::SHR32ri);
    NewNode =
        CurDAG->getMachineNode(NewOpc, dl, NVT, SDValue(NewNode, 0), ShAmt);
  }

  return NewNode;
}

// Emit a PCMISTR(I/M) instruction.
MachineSDNode *X86DAGToDAGISel::emitPCMPISTR(unsigned ROpc, unsigned MOpc,
                                             bool MayFoldLoad, const SDLoc &dl,
                                             MVT VT, SDNode *Node) {
  SDValue N0 = Node->getOperand(0);
  SDValue N1 = Node->getOperand(1);
  SDValue Imm = Node->getOperand(2);
  auto *Val = cast<ConstantSDNode>(Imm)->getConstantIntValue();
  Imm = CurDAG->getTargetConstant(*Val, SDLoc(Node), Imm.getValueType());

  // Try to fold a load. No need to check alignment.
  SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
  if (MayFoldLoad && tryFoldLoad(Node, N1, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
    SDValue Ops[] = { N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, Imm,
                      N1.getOperand(0) };
    SDVTList VTs = CurDAG->getVTList(VT, MVT::i32, MVT::Other);
    MachineSDNode *CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
    // Update the chain.
    ReplaceUses(N1.getValue(1), SDValue(CNode, 2));
    // Record the mem-refs
    CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N1)->getMemOperand()});
    return CNode;
  }

  SDValue Ops[] = { N0, N1, Imm };
  SDVTList VTs = CurDAG->getVTList(VT, MVT::i32);
  MachineSDNode *CNode = CurDAG->getMachineNode(ROpc, dl, VTs, Ops);
  return CNode;
}

// Emit a PCMESTR(I/M) instruction. Also return the Glue result in case we need
// to emit a second instruction after this one. This is needed since we have two
// copyToReg nodes glued before this and we need to continue that glue through.
MachineSDNode *X86DAGToDAGISel::emitPCMPESTR(unsigned ROpc, unsigned MOpc,
                                             bool MayFoldLoad, const SDLoc &dl,
                                             MVT VT, SDNode *Node,
                                             SDValue &InGlue) {
  SDValue N0 = Node->getOperand(0);
  SDValue N2 = Node->getOperand(2);
  SDValue Imm = Node->getOperand(4);
  auto *Val = cast<ConstantSDNode>(Imm)->getConstantIntValue();
  Imm = CurDAG->getTargetConstant(*Val, SDLoc(Node), Imm.getValueType());

  // Try to fold a load. No need to check alignment.
  SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
  if (MayFoldLoad && tryFoldLoad(Node, N2, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
    SDValue Ops[] = { N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, Imm,
                      N2.getOperand(0), InGlue };
    SDVTList VTs = CurDAG->getVTList(VT, MVT::i32, MVT::Other, MVT::Glue);
    MachineSDNode *CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
    InGlue = SDValue(CNode, 3);
    // Update the chain.
    ReplaceUses(N2.getValue(1), SDValue(CNode, 2));
    // Record the mem-refs
    CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N2)->getMemOperand()});
    return CNode;
  }

  SDValue Ops[] = { N0, N2, Imm, InGlue };
  SDVTList VTs = CurDAG->getVTList(VT, MVT::i32, MVT::Glue);
  MachineSDNode *CNode = CurDAG->getMachineNode(ROpc, dl, VTs, Ops);
  InGlue = SDValue(CNode, 2);
  return CNode;
}

bool X86DAGToDAGISel::tryShiftAmountMod(SDNode *N) {
  EVT VT = N->getValueType(0);

  // Only handle scalar shifts.
  if (VT.isVector())
    return false;

  // Narrower shifts only mask to 5 bits in hardware.
  unsigned Size = VT == MVT::i64 ? 64 : 32;

  SDValue OrigShiftAmt = N->getOperand(1);
  SDValue ShiftAmt = OrigShiftAmt;
  SDLoc DL(N);

  // Skip over a truncate of the shift amount.
  if (ShiftAmt->getOpcode() == ISD::TRUNCATE)
    ShiftAmt = ShiftAmt->getOperand(0);

  // This function is called after X86DAGToDAGISel::matchBitExtract(),
  // so we are not afraid that we might mess up BZHI/BEXTR pattern.

  SDValue NewShiftAmt;
  if (ShiftAmt->getOpcode() == ISD::ADD || ShiftAmt->getOpcode() == ISD::SUB ||
      ShiftAmt->getOpcode() == ISD::XOR) {
    SDValue Add0 = ShiftAmt->getOperand(0);
    SDValue Add1 = ShiftAmt->getOperand(1);
    auto *Add0C = dyn_cast<ConstantSDNode>(Add0);
    auto *Add1C = dyn_cast<ConstantSDNode>(Add1);
    // If we are shifting by X+/-/^N where N == 0 mod Size, then just shift by X
    // to avoid the ADD/SUB/XOR.
    if (Add1C && Add1C->getAPIntValue().urem(Size) == 0) {
      NewShiftAmt = Add0;

    } else if (ShiftAmt->getOpcode() != ISD::ADD && ShiftAmt.hasOneUse() &&
               ((Add0C && Add0C->getAPIntValue().urem(Size) == Size - 1) ||
                (Add1C && Add1C->getAPIntValue().urem(Size) == Size - 1))) {
      // If we are doing a NOT on just the lower bits with (Size*N-1) -/^ X
      // we can replace it with a NOT. In the XOR case it may save some code
      // size, in the SUB case it also may save a move.
      assert(Add0C == nullptr || Add1C == nullptr);

      // We can only do N-X, not X-N
      if (ShiftAmt->getOpcode() == ISD::SUB && Add0C == nullptr)
        return false;

      EVT OpVT = ShiftAmt.getValueType();

      SDValue AllOnes = CurDAG->getAllOnesConstant(DL, OpVT);
      NewShiftAmt = CurDAG->getNode(ISD::XOR, DL, OpVT,
                                    Add0C == nullptr ? Add0 : Add1, AllOnes);
      insertDAGNode(*CurDAG, OrigShiftAmt, AllOnes);
      insertDAGNode(*CurDAG, OrigShiftAmt, NewShiftAmt);
      // If we are shifting by N-X where N == 0 mod Size, then just shift by
      // -X to generate a NEG instead of a SUB of a constant.
    } else if (ShiftAmt->getOpcode() == ISD::SUB && Add0C &&
               Add0C->getZExtValue() != 0) {
      EVT SubVT = ShiftAmt.getValueType();
      SDValue X;
      if (Add0C->getZExtValue() % Size == 0)
        X = Add1;
      else if (ShiftAmt.hasOneUse() && Size == 64 &&
               Add0C->getZExtValue() % 32 == 0) {
        // We have a 64-bit shift by (n*32-x), turn it into -(x+n*32).
        // This is mainly beneficial if we already compute (x+n*32).
        if (Add1.getOpcode() == ISD::TRUNCATE) {
          Add1 = Add1.getOperand(0);
          SubVT = Add1.getValueType();
        }
        if (Add0.getValueType() != SubVT) {
          Add0 = CurDAG->getZExtOrTrunc(Add0, DL, SubVT);
          insertDAGNode(*CurDAG, OrigShiftAmt, Add0);
        }

        X = CurDAG->getNode(ISD::ADD, DL, SubVT, Add1, Add0);
        insertDAGNode(*CurDAG, OrigShiftAmt, X);
      } else
        return false;
      // Insert a negate op.
      // TODO: This isn't guaranteed to replace the sub if there is a logic cone
      // that uses it that's not a shift.
      SDValue Zero = CurDAG->getConstant(0, DL, SubVT);
      SDValue Neg = CurDAG->getNode(ISD::SUB, DL, SubVT, Zero, X);
      NewShiftAmt = Neg;

      // Insert these operands into a valid topological order so they can
      // get selected independently.
      insertDAGNode(*CurDAG, OrigShiftAmt, Zero);
      insertDAGNode(*CurDAG, OrigShiftAmt, Neg);
    } else
      return false;
  } else
    return false;

  if (NewShiftAmt.getValueType() != MVT::i8) {
    // Need to truncate the shift amount.
    NewShiftAmt = CurDAG->getNode(ISD::TRUNCATE, DL, MVT::i8, NewShiftAmt);
    // Add to a correct topological ordering.
    insertDAGNode(*CurDAG, OrigShiftAmt, NewShiftAmt);
  }

  // Insert a new mask to keep the shift amount legal. This should be removed
  // by isel patterns.
  NewShiftAmt = CurDAG->getNode(ISD::AND, DL, MVT::i8, NewShiftAmt,
                                CurDAG->getConstant(Size - 1, DL, MVT::i8));
  // Place in a correct topological ordering.
  insertDAGNode(*CurDAG, OrigShiftAmt, NewShiftAmt);

  SDNode *UpdatedNode = CurDAG->UpdateNodeOperands(N, N->getOperand(0),
                                                   NewShiftAmt);
  if (UpdatedNode != N) {
    // If we found an existing node, we should replace ourselves with that node
    // and wait for it to be selected after its other users.
    ReplaceNode(N, UpdatedNode);
    return true;
  }

  // If the original shift amount is now dead, delete it so that we don't run
  // it through isel.
  if (OrigShiftAmt.getNode()->use_empty())
    CurDAG->RemoveDeadNode(OrigShiftAmt.getNode());

  // Now that we've optimized the shift amount, defer to normal isel to get
  // load folding and legacy vs BMI2 selection without repeating it here.
  SelectCode(N);
  return true;
}

bool X86DAGToDAGISel::tryShrinkShlLogicImm(SDNode *N) {
  MVT NVT = N->getSimpleValueType(0);
  unsigned Opcode = N->getOpcode();
  SDLoc dl(N);

  // For operations of the form (x << C1) op C2, check if we can use a smaller
  // encoding for C2 by transforming it into (x op (C2>>C1)) << C1.
  SDValue Shift = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  auto *Cst = dyn_cast<ConstantSDNode>(N1);
  if (!Cst)
    return false;

  int64_t Val = Cst->getSExtValue();

  // If we have an any_extend feeding the AND, look through it to see if there
  // is a shift behind it. But only if the AND doesn't use the extended bits.
  // FIXME: Generalize this to other ANY_EXTEND than i32 to i64?
  bool FoundAnyExtend = false;
  if (Shift.getOpcode() == ISD::ANY_EXTEND && Shift.hasOneUse() &&
      Shift.getOperand(0).getSimpleValueType() == MVT::i32 &&
      isUInt<32>(Val)) {
    FoundAnyExtend = true;
    Shift = Shift.getOperand(0);
  }

  if (Shift.getOpcode() != ISD::SHL || !Shift.hasOneUse())
    return false;

  // i8 is unshrinkable, i16 should be promoted to i32.
  if (NVT != MVT::i32 && NVT != MVT::i64)
    return false;

  auto *ShlCst = dyn_cast<ConstantSDNode>(Shift.getOperand(1));
  if (!ShlCst)
    return false;

  uint64_t ShAmt = ShlCst->getZExtValue();

  // Make sure that we don't change the operation by removing bits.
  // This only matters for OR and XOR, AND is unaffected.
  uint64_t RemovedBitsMask = (1ULL << ShAmt) - 1;
  if (Opcode != ISD::AND && (Val & RemovedBitsMask) != 0)
    return false;

  // Check the minimum bitwidth for the new constant.
  // TODO: Using 16 and 8 bit operations is also possible for or32 & xor32.
  auto CanShrinkImmediate = [&](int64_t &ShiftedVal) {
    if (Opcode == ISD::AND) {
      // AND32ri is the same as AND64ri32 with zext imm.
      // Try this before sign extended immediates below.
      ShiftedVal = (uint64_t)Val >> ShAmt;
      if (NVT == MVT::i64 && !isUInt<32>(Val) && isUInt<32>(ShiftedVal))
        return true;
      // Also swap order when the AND can become MOVZX.
      if (ShiftedVal == UINT8_MAX || ShiftedVal == UINT16_MAX)
        return true;
    }
    ShiftedVal = Val >> ShAmt;
    if ((!isInt<8>(Val) && isInt<8>(ShiftedVal)) ||
        (!isInt<32>(Val) && isInt<32>(ShiftedVal)))
      return true;
    if (Opcode != ISD::AND) {
      // MOV32ri+OR64r/XOR64r is cheaper than MOV64ri64+OR64rr/XOR64rr
      ShiftedVal = (uint64_t)Val >> ShAmt;
      if (NVT == MVT::i64 && !isUInt<32>(Val) && isUInt<32>(ShiftedVal))
        return true;
    }
    return false;
  };

  int64_t ShiftedVal;
  if (!CanShrinkImmediate(ShiftedVal))
    return false;

  // Ok, we can reorder to get a smaller immediate.

  // But, its possible the original immediate allowed an AND to become MOVZX.
  // Doing this late due to avoid the MakedValueIsZero call as late as
  // possible.
  if (Opcode == ISD::AND) {
    // Find the smallest zext this could possibly be.
    unsigned ZExtWidth = Cst->getAPIntValue().getActiveBits();
    ZExtWidth = llvm::bit_ceil(std::max(ZExtWidth, 8U));

    // Figure out which bits need to be zero to achieve that mask.
    APInt NeededMask = APInt::getLowBitsSet(NVT.getSizeInBits(),
                                            ZExtWidth);
    NeededMask &= ~Cst->getAPIntValue();

    if (CurDAG->MaskedValueIsZero(N->getOperand(0), NeededMask))
      return false;
  }

  SDValue X = Shift.getOperand(0);
  if (FoundAnyExtend) {
    SDValue NewX = CurDAG->getNode(ISD::ANY_EXTEND, dl, NVT, X);
    insertDAGNode(*CurDAG, SDValue(N, 0), NewX);
    X = NewX;
  }

  SDValue NewCst = CurDAG->getConstant(ShiftedVal, dl, NVT);
  insertDAGNode(*CurDAG, SDValue(N, 0), NewCst);
  SDValue NewBinOp = CurDAG->getNode(Opcode, dl, NVT, X, NewCst);
  insertDAGNode(*CurDAG, SDValue(N, 0), NewBinOp);
  SDValue NewSHL = CurDAG->getNode(ISD::SHL, dl, NVT, NewBinOp,
                                   Shift.getOperand(1));
  ReplaceNode(N, NewSHL.getNode());
  SelectCode(NewSHL.getNode());
  return true;
}

bool X86DAGToDAGISel::matchVPTERNLOG(SDNode *Root, SDNode *ParentA,
                                     SDNode *ParentB, SDNode *ParentC,
                                     SDValue A, SDValue B, SDValue C,
                                     uint8_t Imm) {
  assert(A.isOperandOf(ParentA) && B.isOperandOf(ParentB) &&
         C.isOperandOf(ParentC) && "Incorrect parent node");

  auto tryFoldLoadOrBCast =
      [this](SDNode *Root, SDNode *P, SDValue &L, SDValue &Base, SDValue &Scale,
             SDValue &Index, SDValue &Disp, SDValue &Segment) {
        if (tryFoldLoad(Root, P, L, Base, Scale, Index, Disp, Segment))
          return true;

        // Not a load, check for broadcast which may be behind a bitcast.
        if (L.getOpcode() == ISD::BITCAST && L.hasOneUse()) {
          P = L.getNode();
          L = L.getOperand(0);
        }

        if (L.getOpcode() != X86ISD::VBROADCAST_LOAD)
          return false;

        // Only 32 and 64 bit broadcasts are supported.
        auto *MemIntr = cast<MemIntrinsicSDNode>(L);
        unsigned Size = MemIntr->getMemoryVT().getSizeInBits();
        if (Size != 32 && Size != 64)
          return false;

        return tryFoldBroadcast(Root, P, L, Base, Scale, Index, Disp, Segment);
      };

  bool FoldedLoad = false;
  SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
  if (tryFoldLoadOrBCast(Root, ParentC, C, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
    FoldedLoad = true;
  } else if (tryFoldLoadOrBCast(Root, ParentA, A, Tmp0, Tmp1, Tmp2, Tmp3,
                                Tmp4)) {
    FoldedLoad = true;
    std::swap(A, C);
    // Swap bits 1/4 and 3/6.
    uint8_t OldImm = Imm;
    Imm = OldImm & 0xa5;
    if (OldImm & 0x02) Imm |= 0x10;
    if (OldImm & 0x10) Imm |= 0x02;
    if (OldImm & 0x08) Imm |= 0x40;
    if (OldImm & 0x40) Imm |= 0x08;
  } else if (tryFoldLoadOrBCast(Root, ParentB, B, Tmp0, Tmp1, Tmp2, Tmp3,
                                Tmp4)) {
    FoldedLoad = true;
    std::swap(B, C);
    // Swap bits 1/2 and 5/6.
    uint8_t OldImm = Imm;
    Imm = OldImm & 0x99;
    if (OldImm & 0x02) Imm |= 0x04;
    if (OldImm & 0x04) Imm |= 0x02;
    if (OldImm & 0x20) Imm |= 0x40;
    if (OldImm & 0x40) Imm |= 0x20;
  }

  SDLoc DL(Root);

  SDValue TImm = CurDAG->getTargetConstant(Imm, DL, MVT::i8);

  MVT NVT = Root->getSimpleValueType(0);

  MachineSDNode *MNode;
  if (FoldedLoad) {
    SDVTList VTs = CurDAG->getVTList(NVT, MVT::Other);

    unsigned Opc;
    if (C.getOpcode() == X86ISD::VBROADCAST_LOAD) {
      auto *MemIntr = cast<MemIntrinsicSDNode>(C);
      unsigned EltSize = MemIntr->getMemoryVT().getSizeInBits();
      assert((EltSize == 32 || EltSize == 64) && "Unexpected broadcast size!");

      bool UseD = EltSize == 32;
      if (NVT.is128BitVector())
        Opc = UseD ? X86::VPTERNLOGDZ128rmbi : X86::VPTERNLOGQZ128rmbi;
      else if (NVT.is256BitVector())
        Opc = UseD ? X86::VPTERNLOGDZ256rmbi : X86::VPTERNLOGQZ256rmbi;
      else if (NVT.is512BitVector())
        Opc = UseD ? X86::VPTERNLOGDZrmbi : X86::VPTERNLOGQZrmbi;
      else
        llvm_unreachable("Unexpected vector size!");
    } else {
      bool UseD = NVT.getVectorElementType() == MVT::i32;
      if (NVT.is128BitVector())
        Opc = UseD ? X86::VPTERNLOGDZ128rmi : X86::VPTERNLOGQZ128rmi;
      else if (NVT.is256BitVector())
        Opc = UseD ? X86::VPTERNLOGDZ256rmi : X86::VPTERNLOGQZ256rmi;
      else if (NVT.is512BitVector())
        Opc = UseD ? X86::VPTERNLOGDZrmi : X86::VPTERNLOGQZrmi;
      else
        llvm_unreachable("Unexpected vector size!");
    }

    SDValue Ops[] = {A, B, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, TImm, C.getOperand(0)};
    MNode = CurDAG->getMachineNode(Opc, DL, VTs, Ops);

    // Update the chain.
    ReplaceUses(C.getValue(1), SDValue(MNode, 1));
    // Record the mem-refs
    CurDAG->setNodeMemRefs(MNode, {cast<MemSDNode>(C)->getMemOperand()});
  } else {
    bool UseD = NVT.getVectorElementType() == MVT::i32;
    unsigned Opc;
    if (NVT.is128BitVector())
      Opc = UseD ? X86::VPTERNLOGDZ128rri : X86::VPTERNLOGQZ128rri;
    else if (NVT.is256BitVector())
      Opc = UseD ? X86::VPTERNLOGDZ256rri : X86::VPTERNLOGQZ256rri;
    else if (NVT.is512BitVector())
      Opc = UseD ? X86::VPTERNLOGDZrri : X86::VPTERNLOGQZrri;
    else
      llvm_unreachable("Unexpected vector size!");

    MNode = CurDAG->getMachineNode(Opc, DL, NVT, {A, B, C, TImm});
  }

  ReplaceUses(SDValue(Root, 0), SDValue(MNode, 0));
  CurDAG->RemoveDeadNode(Root);
  return true;
}

// Try to match two logic ops to a VPTERNLOG.
// FIXME: Handle more complex patterns that use an operand more than once?
bool X86DAGToDAGISel::tryVPTERNLOG(SDNode *N) {
  MVT NVT = N->getSimpleValueType(0);

  // Make sure we support VPTERNLOG.
  if (!NVT.isVector() || !Subtarget->hasAVX512() ||
      NVT.getVectorElementType() == MVT::i1)
    return false;

  // We need VLX for 128/256-bit.
  if (!(Subtarget->hasVLX() || NVT.is512BitVector()))
    return false;

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  auto getFoldableLogicOp = [](SDValue Op) {
    // Peek through single use bitcast.
    if (Op.getOpcode() == ISD::BITCAST && Op.hasOneUse())
      Op = Op.getOperand(0);

    if (!Op.hasOneUse())
      return SDValue();

    unsigned Opc = Op.getOpcode();
    if (Opc == ISD::AND || Opc == ISD::OR || Opc == ISD::XOR ||
        Opc == X86ISD::ANDNP)
      return Op;

    return SDValue();
  };

  SDValue A, FoldableOp;
  if ((FoldableOp = getFoldableLogicOp(N1))) {
    A = N0;
  } else if ((FoldableOp = getFoldableLogicOp(N0))) {
    A = N1;
  } else
    return false;

  SDValue B = FoldableOp.getOperand(0);
  SDValue C = FoldableOp.getOperand(1);
  SDNode *ParentA = N;
  SDNode *ParentB = FoldableOp.getNode();
  SDNode *ParentC = FoldableOp.getNode();

  // We can build the appropriate control immediate by performing the logic
  // operation we're matching using these constants for A, B, and C.
  uint8_t TernlogMagicA = 0xf0;
  uint8_t TernlogMagicB = 0xcc;
  uint8_t TernlogMagicC = 0xaa;

  // Some of the inputs may be inverted, peek through them and invert the
  // magic values accordingly.
  // TODO: There may be a bitcast before the xor that we should peek through.
  auto PeekThroughNot = [](SDValue &Op, SDNode *&Parent, uint8_t &Magic) {
    if (Op.getOpcode() == ISD::XOR && Op.hasOneUse() &&
        ISD::isBuildVectorAllOnes(Op.getOperand(1).getNode())) {
      Magic = ~Magic;
      Parent = Op.getNode();
      Op = Op.getOperand(0);
    }
  };

  PeekThroughNot(A, ParentA, TernlogMagicA);
  PeekThroughNot(B, ParentB, TernlogMagicB);
  PeekThroughNot(C, ParentC, TernlogMagicC);

  uint8_t Imm;
  switch (FoldableOp.getOpcode()) {
  default: llvm_unreachable("Unexpected opcode!");
  case ISD::AND:      Imm = TernlogMagicB & TernlogMagicC; break;
  case ISD::OR:       Imm = TernlogMagicB | TernlogMagicC; break;
  case ISD::XOR:      Imm = TernlogMagicB ^ TernlogMagicC; break;
  case X86ISD::ANDNP: Imm = ~(TernlogMagicB) & TernlogMagicC; break;
  }

  switch (N->getOpcode()) {
  default: llvm_unreachable("Unexpected opcode!");
  case X86ISD::ANDNP:
    if (A == N0)
      Imm &= ~TernlogMagicA;
    else
      Imm = ~(Imm) & TernlogMagicA;
    break;
  case ISD::AND: Imm &= TernlogMagicA; break;
  case ISD::OR:  Imm |= TernlogMagicA; break;
  case ISD::XOR: Imm ^= TernlogMagicA; break;
  }

  return matchVPTERNLOG(N, ParentA, ParentB, ParentC, A, B, C, Imm);
}

/// If the high bits of an 'and' operand are known zero, try setting the
/// high bits of an 'and' constant operand to produce a smaller encoding by
/// creating a small, sign-extended negative immediate rather than a large
/// positive one. This reverses a transform in SimplifyDemandedBits that
/// shrinks mask constants by clearing bits. There is also a possibility that
/// the 'and' mask can be made -1, so the 'and' itself is unnecessary. In that
/// case, just replace the 'and'. Return 'true' if the node is replaced.
bool X86DAGToDAGISel::shrinkAndImmediate(SDNode *And) {
  // i8 is unshrinkable, i16 should be promoted to i32, and vector ops don't
  // have immediate operands.
  MVT VT = And->getSimpleValueType(0);
  if (VT != MVT::i32 && VT != MVT::i64)
    return false;

  auto *And1C = dyn_cast<ConstantSDNode>(And->getOperand(1));
  if (!And1C)
    return false;

  // Bail out if the mask constant is already negative. It's can't shrink more.
  // If the upper 32 bits of a 64 bit mask are all zeros, we have special isel
  // patterns to use a 32-bit and instead of a 64-bit and by relying on the
  // implicit zeroing of 32 bit ops. So we should check if the lower 32 bits
  // are negative too.
  APInt MaskVal = And1C->getAPIntValue();
  unsigned MaskLZ = MaskVal.countl_zero();
  if (!MaskLZ || (VT == MVT::i64 && MaskLZ == 32))
    return false;

  // Don't extend into the upper 32 bits of a 64 bit mask.
  if (VT == MVT::i64 && MaskLZ >= 32) {
    MaskLZ -= 32;
    MaskVal = MaskVal.trunc(32);
  }

  SDValue And0 = And->getOperand(0);
  APInt HighZeros = APInt::getHighBitsSet(MaskVal.getBitWidth(), MaskLZ);
  APInt NegMaskVal = MaskVal | HighZeros;

  // If a negative constant would not allow a smaller encoding, there's no need
  // to continue. Only change the constant when we know it's a win.
  unsigned MinWidth = NegMaskVal.getSignificantBits();
  if (MinWidth > 32 || (MinWidth > 8 && MaskVal.getSignificantBits() <= 32))
    return false;

  // Extend masks if we truncated above.
  if (VT == MVT::i64 && MaskVal.getBitWidth() < 64) {
    NegMaskVal = NegMaskVal.zext(64);
    HighZeros = HighZeros.zext(64);
  }

  // The variable operand must be all zeros in the top bits to allow using the
  // new, negative constant as the mask.
  if (!CurDAG->MaskedValueIsZero(And0, HighZeros))
    return false;

  // Check if the mask is -1. In that case, this is an unnecessary instruction
  // that escaped earlier analysis.
  if (NegMaskVal.isAllOnes()) {
    ReplaceNode(And, And0.getNode());
    return true;
  }

  // A negative mask allows a smaller encoding. Create a new 'and' node.
  SDValue NewMask = CurDAG->getConstant(NegMaskVal, SDLoc(And), VT);
  insertDAGNode(*CurDAG, SDValue(And, 0), NewMask);
  SDValue NewAnd = CurDAG->getNode(ISD::AND, SDLoc(And), VT, And0, NewMask);
  ReplaceNode(And, NewAnd.getNode());
  SelectCode(NewAnd.getNode());
  return true;
}

static unsigned getVPTESTMOpc(MVT TestVT, bool IsTestN, bool FoldedLoad,
                              bool FoldedBCast, bool Masked) {
#define VPTESTM_CASE(VT, SUFFIX) \
case MVT::VT: \
  if (Masked) \
    return IsTestN ? X86::VPTESTNM##SUFFIX##k: X86::VPTESTM##SUFFIX##k; \
  return IsTestN ? X86::VPTESTNM##SUFFIX : X86::VPTESTM##SUFFIX;


#define VPTESTM_BROADCAST_CASES(SUFFIX) \
default: llvm_unreachable("Unexpected VT!"); \
VPTESTM_CASE(v4i32, DZ128##SUFFIX) \
VPTESTM_CASE(v2i64, QZ128##SUFFIX) \
VPTESTM_CASE(v8i32, DZ256##SUFFIX) \
VPTESTM_CASE(v4i64, QZ256##SUFFIX) \
VPTESTM_CASE(v16i32, DZ##SUFFIX) \
VPTESTM_CASE(v8i64, QZ##SUFFIX)

#define VPTESTM_FULL_CASES(SUFFIX) \
VPTESTM_BROADCAST_CASES(SUFFIX) \
VPTESTM_CASE(v16i8, BZ128##SUFFIX) \
VPTESTM_CASE(v8i16, WZ128##SUFFIX) \
VPTESTM_CASE(v32i8, BZ256##SUFFIX) \
VPTESTM_CASE(v16i16, WZ256##SUFFIX) \
VPTESTM_CASE(v64i8, BZ##SUFFIX) \
VPTESTM_CASE(v32i16, WZ##SUFFIX)

  if (FoldedBCast) {
    switch (TestVT.SimpleTy) {
    VPTESTM_BROADCAST_CASES(rmb)
    }
  }

  if (FoldedLoad) {
    switch (TestVT.SimpleTy) {
    VPTESTM_FULL_CASES(rm)
    }
  }

  switch (TestVT.SimpleTy) {
  VPTESTM_FULL_CASES(rr)
  }

#undef VPTESTM_FULL_CASES
#undef VPTESTM_BROADCAST_CASES
#undef VPTESTM_CASE
}

// Try to create VPTESTM instruction. If InMask is not null, it will be used
// to form a masked operation.
bool X86DAGToDAGISel::tryVPTESTM(SDNode *Root, SDValue Setcc,
                                 SDValue InMask) {
  assert(Subtarget->hasAVX512() && "Expected AVX512!");
  assert(Setcc.getSimpleValueType().getVectorElementType() == MVT::i1 &&
         "Unexpected VT!");

  // Look for equal and not equal compares.
  ISD::CondCode CC = cast<CondCodeSDNode>(Setcc.getOperand(2))->get();
  if (CC != ISD::SETEQ && CC != ISD::SETNE)
    return false;

  SDValue SetccOp0 = Setcc.getOperand(0);
  SDValue SetccOp1 = Setcc.getOperand(1);

  // Canonicalize the all zero vector to the RHS.
  if (ISD::isBuildVectorAllZeros(SetccOp0.getNode()))
    std::swap(SetccOp0, SetccOp1);

  // See if we're comparing against zero.
  if (!ISD::isBuildVectorAllZeros(SetccOp1.getNode()))
    return false;

  SDValue N0 = SetccOp0;

  MVT CmpVT = N0.getSimpleValueType();
  MVT CmpSVT = CmpVT.getVectorElementType();

  // Start with both operands the same. We'll try to refine this.
  SDValue Src0 = N0;
  SDValue Src1 = N0;

  {
    // Look through single use bitcasts.
    SDValue N0Temp = N0;
    if (N0Temp.getOpcode() == ISD::BITCAST && N0Temp.hasOneUse())
      N0Temp = N0.getOperand(0);

     // Look for single use AND.
    if (N0Temp.getOpcode() == ISD::AND && N0Temp.hasOneUse()) {
      Src0 = N0Temp.getOperand(0);
      Src1 = N0Temp.getOperand(1);
    }
  }

  // Without VLX we need to widen the operation.
  bool Widen = !Subtarget->hasVLX() && !CmpVT.is512BitVector();

  auto tryFoldLoadOrBCast = [&](SDNode *Root, SDNode *P, SDValue &L,
                                SDValue &Base, SDValue &Scale, SDValue &Index,
                                SDValue &Disp, SDValue &Segment) {
    // If we need to widen, we can't fold the load.
    if (!Widen)
      if (tryFoldLoad(Root, P, L, Base, Scale, Index, Disp, Segment))
        return true;

    // If we didn't fold a load, try to match broadcast. No widening limitation
    // for this. But only 32 and 64 bit types are supported.
    if (CmpSVT != MVT::i32 && CmpSVT != MVT::i64)
      return false;

    // Look through single use bitcasts.
    if (L.getOpcode() == ISD::BITCAST && L.hasOneUse()) {
      P = L.getNode();
      L = L.getOperand(0);
    }

    if (L.getOpcode() != X86ISD::VBROADCAST_LOAD)
      return false;

    auto *MemIntr = cast<MemIntrinsicSDNode>(L);
    if (MemIntr->getMemoryVT().getSizeInBits() != CmpSVT.getSizeInBits())
      return false;

    return tryFoldBroadcast(Root, P, L, Base, Scale, Index, Disp, Segment);
  };

  // We can only fold loads if the sources are unique.
  bool CanFoldLoads = Src0 != Src1;

  bool FoldedLoad = false;
  SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
  if (CanFoldLoads) {
    FoldedLoad = tryFoldLoadOrBCast(Root, N0.getNode(), Src1, Tmp0, Tmp1, Tmp2,
                                    Tmp3, Tmp4);
    if (!FoldedLoad) {
      // And is commutative.
      FoldedLoad = tryFoldLoadOrBCast(Root, N0.getNode(), Src0, Tmp0, Tmp1,
                                      Tmp2, Tmp3, Tmp4);
      if (FoldedLoad)
        std::swap(Src0, Src1);
    }
  }

  bool FoldedBCast = FoldedLoad && Src1.getOpcode() == X86ISD::VBROADCAST_LOAD;

  bool IsMasked = InMask.getNode() != nullptr;

  SDLoc dl(Root);

  MVT ResVT = Setcc.getSimpleValueType();
  MVT MaskVT = ResVT;
  if (Widen) {
    // Widen the inputs using insert_subreg or copy_to_regclass.
    unsigned Scale = CmpVT.is128BitVector() ? 4 : 2;
    unsigned SubReg = CmpVT.is128BitVector() ? X86::sub_xmm : X86::sub_ymm;
    unsigned NumElts = CmpVT.getVectorNumElements() * Scale;
    CmpVT = MVT::getVectorVT(CmpSVT, NumElts);
    MaskVT = MVT::getVectorVT(MVT::i1, NumElts);
    SDValue ImplDef = SDValue(CurDAG->getMachineNode(X86::IMPLICIT_DEF, dl,
                                                     CmpVT), 0);
    Src0 = CurDAG->getTargetInsertSubreg(SubReg, dl, CmpVT, ImplDef, Src0);

    if (!FoldedBCast)
      Src1 = CurDAG->getTargetInsertSubreg(SubReg, dl, CmpVT, ImplDef, Src1);

    if (IsMasked) {
      // Widen the mask.
      unsigned RegClass = TLI->getRegClassFor(MaskVT)->getID();
      SDValue RC = CurDAG->getTargetConstant(RegClass, dl, MVT::i32);
      InMask = SDValue(CurDAG->getMachineNode(TargetOpcode::COPY_TO_REGCLASS,
                                              dl, MaskVT, InMask, RC), 0);
    }
  }

  bool IsTestN = CC == ISD::SETEQ;
  unsigned Opc = getVPTESTMOpc(CmpVT, IsTestN, FoldedLoad, FoldedBCast,
                               IsMasked);

  MachineSDNode *CNode;
  if (FoldedLoad) {
    SDVTList VTs = CurDAG->getVTList(MaskVT, MVT::Other);

    if (IsMasked) {
      SDValue Ops[] = { InMask, Src0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4,
                        Src1.getOperand(0) };
      CNode = CurDAG->getMachineNode(Opc, dl, VTs, Ops);
    } else {
      SDValue Ops[] = { Src0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4,
                        Src1.getOperand(0) };
      CNode = CurDAG->getMachineNode(Opc, dl, VTs, Ops);
    }

    // Update the chain.
    ReplaceUses(Src1.getValue(1), SDValue(CNode, 1));
    // Record the mem-refs
    CurDAG->setNodeMemRefs(CNode, {cast<MemSDNode>(Src1)->getMemOperand()});
  } else {
    if (IsMasked)
      CNode = CurDAG->getMachineNode(Opc, dl, MaskVT, InMask, Src0, Src1);
    else
      CNode = CurDAG->getMachineNode(Opc, dl, MaskVT, Src0, Src1);
  }

  // If we widened, we need to shrink the mask VT.
  if (Widen) {
    unsigned RegClass = TLI->getRegClassFor(ResVT)->getID();
    SDValue RC = CurDAG->getTargetConstant(RegClass, dl, MVT::i32);
    CNode = CurDAG->getMachineNode(TargetOpcode::COPY_TO_REGCLASS,
                                   dl, ResVT, SDValue(CNode, 0), RC);
  }

  ReplaceUses(SDValue(Root, 0), SDValue(CNode, 0));
  CurDAG->RemoveDeadNode(Root);
  return true;
}

// Try to match the bitselect pattern (or (and A, B), (andn A, C)). Turn it
// into vpternlog.
bool X86DAGToDAGISel::tryMatchBitSelect(SDNode *N) {
  assert(N->getOpcode() == ISD::OR && "Unexpected opcode!");

  MVT NVT = N->getSimpleValueType(0);

  // Make sure we support VPTERNLOG.
  if (!NVT.isVector() || !Subtarget->hasAVX512())
    return false;

  // We need VLX for 128/256-bit.
  if (!(Subtarget->hasVLX() || NVT.is512BitVector()))
    return false;

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // Canonicalize AND to LHS.
  if (N1.getOpcode() == ISD::AND)
    std::swap(N0, N1);

  if (N0.getOpcode() != ISD::AND ||
      N1.getOpcode() != X86ISD::ANDNP ||
      !N0.hasOneUse() || !N1.hasOneUse())
    return false;

  // ANDN is not commutable, use it to pick down A and C.
  SDValue A = N1.getOperand(0);
  SDValue C = N1.getOperand(1);

  // AND is commutable, if one operand matches A, the other operand is B.
  // Otherwise this isn't a match.
  SDValue B;
  if (N0.getOperand(0) == A)
    B = N0.getOperand(1);
  else if (N0.getOperand(1) == A)
    B = N0.getOperand(0);
  else
    return false;

  SDLoc dl(N);
  SDValue Imm = CurDAG->getTargetConstant(0xCA, dl, MVT::i8);
  SDValue Ternlog = CurDAG->getNode(X86ISD::VPTERNLOG, dl, NVT, A, B, C, Imm);
  ReplaceNode(N, Ternlog.getNode());

  return matchVPTERNLOG(Ternlog.getNode(), Ternlog.getNode(), Ternlog.getNode(),
                        Ternlog.getNode(), A, B, C, 0xCA);
}

void X86DAGToDAGISel::Select(SDNode *Node) {
  MVT NVT = Node->getSimpleValueType(0);
  unsigned Opcode = Node->getOpcode();
  SDLoc dl(Node);

  if (Node->isMachineOpcode()) {
    LLVM_DEBUG(dbgs() << "== "; Node->dump(CurDAG); dbgs() << '\n');
    Node->setNodeId(-1);
    return;   // Already selected.
  }

  switch (Opcode) {
  default: break;
  case ISD::INTRINSIC_W_CHAIN: {
    unsigned IntNo = Node->getConstantOperandVal(1);
    switch (IntNo) {
    default: break;
    case Intrinsic::x86_encodekey128:
    case Intrinsic::x86_encodekey256: {
      if (!Subtarget->hasKL())
        break;

      unsigned Opcode;
      switch (IntNo) {
      default: llvm_unreachable("Impossible intrinsic");
      case Intrinsic::x86_encodekey128:
        Opcode = X86::ENCODEKEY128;
        break;
      case Intrinsic::x86_encodekey256:
        Opcode = X86::ENCODEKEY256;
        break;
      }

      SDValue Chain = Node->getOperand(0);
      Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM0, Node->getOperand(3),
                                   SDValue());
      if (Opcode == X86::ENCODEKEY256)
        Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM1, Node->getOperand(4),
                                     Chain.getValue(1));

      MachineSDNode *Res = CurDAG->getMachineNode(
          Opcode, dl, Node->getVTList(),
          {Node->getOperand(2), Chain, Chain.getValue(1)});
      ReplaceNode(Node, Res);
      return;
    }
    case Intrinsic::x86_tileloadd64_internal:
    case Intrinsic::x86_tileloaddt164_internal: {
      if (!Subtarget->hasAMXTILE())
        break;
      auto *MFI =
          CurDAG->getMachineFunction().getInfo<X86MachineFunctionInfo>();
      MFI->setAMXProgModel(AMXProgModelEnum::ManagedRA);
      unsigned Opc = IntNo == Intrinsic::x86_tileloadd64_internal
                         ? X86::PTILELOADDV
                         : X86::PTILELOADDT1V;
      // _tile_loadd_internal(row, col, buf, STRIDE)
      SDValue Base = Node->getOperand(4);
      SDValue Scale = getI8Imm(1, dl);
      SDValue Index = Node->getOperand(5);
      SDValue Disp = CurDAG->getTargetConstant(0, dl, MVT::i32);
      SDValue Segment = CurDAG->getRegister(0, MVT::i16);
      SDValue Chain = Node->getOperand(0);
      MachineSDNode *CNode;
      SDValue Ops[] = {Node->getOperand(2),
                       Node->getOperand(3),
                       Base,
                       Scale,
                       Index,
                       Disp,
                       Segment,
                       Chain};
      CNode = CurDAG->getMachineNode(Opc, dl, {MVT::x86amx, MVT::Other}, Ops);
      ReplaceNode(Node, CNode);
      return;
    }
    }
    break;
  }
  case ISD::INTRINSIC_VOID: {
    unsigned IntNo = Node->getConstantOperandVal(1);
    switch (IntNo) {
    default: break;
    case Intrinsic::x86_sse3_monitor:
    case Intrinsic::x86_monitorx:
    case Intrinsic::x86_clzero: {
      bool Use64BitPtr = Node->getOperand(2).getValueType() == MVT::i64;

      unsigned Opc = 0;
      switch (IntNo) {
      default: llvm_unreachable("Unexpected intrinsic!");
      case Intrinsic::x86_sse3_monitor:
        if (!Subtarget->hasSSE3())
          break;
        Opc = Use64BitPtr ? X86::MONITOR64rrr : X86::MONITOR32rrr;
        break;
      case Intrinsic::x86_monitorx:
        if (!Subtarget->hasMWAITX())
          break;
        Opc = Use64BitPtr ? X86::MONITORX64rrr : X86::MONITORX32rrr;
        break;
      case Intrinsic::x86_clzero:
        if (!Subtarget->hasCLZERO())
          break;
        Opc = Use64BitPtr ? X86::CLZERO64r : X86::CLZERO32r;
        break;
      }

      if (Opc) {
        unsigned PtrReg = Use64BitPtr ? X86::RAX : X86::EAX;
        SDValue Chain = CurDAG->getCopyToReg(Node->getOperand(0), dl, PtrReg,
                                             Node->getOperand(2), SDValue());
        SDValue InGlue = Chain.getValue(1);

        if (IntNo == Intrinsic::x86_sse3_monitor ||
            IntNo == Intrinsic::x86_monitorx) {
          // Copy the other two operands to ECX and EDX.
          Chain = CurDAG->getCopyToReg(Chain, dl, X86::ECX, Node->getOperand(3),
                                       InGlue);
          InGlue = Chain.getValue(1);
          Chain = CurDAG->getCopyToReg(Chain, dl, X86::EDX, Node->getOperand(4),
                                       InGlue);
          InGlue = Chain.getValue(1);
        }

        MachineSDNode *CNode = CurDAG->getMachineNode(Opc, dl, MVT::Other,
                                                      { Chain, InGlue});
        ReplaceNode(Node, CNode);
        return;
      }

      break;
    }
    case Intrinsic::x86_tilestored64_internal: {
      auto *MFI =
          CurDAG->getMachineFunction().getInfo<X86MachineFunctionInfo>();
      MFI->setAMXProgModel(AMXProgModelEnum::ManagedRA);
      unsigned Opc = X86::PTILESTOREDV;
      // _tile_stored_internal(row, col, buf, STRIDE, c)
      SDValue Base = Node->getOperand(4);
      SDValue Scale = getI8Imm(1, dl);
      SDValue Index = Node->getOperand(5);
      SDValue Disp = CurDAG->getTargetConstant(0, dl, MVT::i32);
      SDValue Segment = CurDAG->getRegister(0, MVT::i16);
      SDValue Chain = Node->getOperand(0);
      MachineSDNode *CNode;
      SDValue Ops[] = {Node->getOperand(2),
                       Node->getOperand(3),
                       Base,
                       Scale,
                       Index,
                       Disp,
                       Segment,
                       Node->getOperand(6),
                       Chain};
      CNode = CurDAG->getMachineNode(Opc, dl, MVT::Other, Ops);
      ReplaceNode(Node, CNode);
      return;
    }
    case Intrinsic::x86_tileloadd64:
    case Intrinsic::x86_tileloaddt164:
    case Intrinsic::x86_tilestored64: {
      if (!Subtarget->hasAMXTILE())
        break;
      auto *MFI =
          CurDAG->getMachineFunction().getInfo<X86MachineFunctionInfo>();
      MFI->setAMXProgModel(AMXProgModelEnum::DirectReg);
      unsigned Opc;
      switch (IntNo) {
      default: llvm_unreachable("Unexpected intrinsic!");
      case Intrinsic::x86_tileloadd64:   Opc = X86::PTILELOADD; break;
      case Intrinsic::x86_tileloaddt164: Opc = X86::PTILELOADDT1; break;
      case Intrinsic::x86_tilestored64:  Opc = X86::PTILESTORED; break;
      }
      // FIXME: Match displacement and scale.
      unsigned TIndex = Node->getConstantOperandVal(2);
      SDValue TReg = getI8Imm(TIndex, dl);
      SDValue Base = Node->getOperand(3);
      SDValue Scale = getI8Imm(1, dl);
      SDValue Index = Node->getOperand(4);
      SDValue Disp = CurDAG->getTargetConstant(0, dl, MVT::i32);
      SDValue Segment = CurDAG->getRegister(0, MVT::i16);
      SDValue Chain = Node->getOperand(0);
      MachineSDNode *CNode;
      if (Opc == X86::PTILESTORED) {
        SDValue Ops[] = { Base, Scale, Index, Disp, Segment, TReg, Chain };
        CNode = CurDAG->getMachineNode(Opc, dl, MVT::Other, Ops);
      } else {
        SDValue Ops[] = { TReg, Base, Scale, Index, Disp, Segment, Chain };
        CNode = CurDAG->getMachineNode(Opc, dl, MVT::Other, Ops);
      }
      ReplaceNode(Node, CNode);
      return;
    }
    }
    break;
  }
  case ISD::BRIND:
  case X86ISD::NT_BRIND: {
    if (Subtarget->isTargetNaCl())
      // NaCl has its own pass where jmp %r32 are converted to jmp %r64. We
      // leave the instruction alone.
      break;
    if (Subtarget->isTarget64BitILP32()) {
      // Converts a 32-bit register to a 64-bit, zero-extended version of
      // it. This is needed because x86-64 can do many things, but jmp %r32
      // ain't one of them.
      SDValue Target = Node->getOperand(1);
      assert(Target.getValueType() == MVT::i32 && "Unexpected VT!");
      SDValue ZextTarget = CurDAG->getZExtOrTrunc(Target, dl, MVT::i64);
      SDValue Brind = CurDAG->getNode(Opcode, dl, MVT::Other,
                                      Node->getOperand(0), ZextTarget);
      ReplaceNode(Node, Brind.getNode());
      SelectCode(ZextTarget.getNode());
      SelectCode(Brind.getNode());
      return;
    }
    break;
  }
  case X86ISD::GlobalBaseReg:
    ReplaceNode(Node, getGlobalBaseReg());
    return;

  case ISD::BITCAST:
    // Just drop all 128/256/512-bit bitcasts.
    if (NVT.is512BitVector() || NVT.is256BitVector() || NVT.is128BitVector() ||
        NVT == MVT::f128) {
      ReplaceUses(SDValue(Node, 0), Node->getOperand(0));
      CurDAG->RemoveDeadNode(Node);
      return;
    }
    break;

  case ISD::SRL:
    if (matchBitExtract(Node))
      return;
    [[fallthrough]];
  case ISD::SRA:
  case ISD::SHL:
    if (tryShiftAmountMod(Node))
      return;
    break;

  case X86ISD::VPTERNLOG: {
    uint8_t Imm = Node->getConstantOperandVal(3);
    if (matchVPTERNLOG(Node, Node, Node, Node, Node->getOperand(0),
                       Node->getOperand(1), Node->getOperand(2), Imm))
      return;
    break;
  }

  case X86ISD::ANDNP:
    if (tryVPTERNLOG(Node))
      return;
    break;

  case ISD::AND:
    if (NVT.isVector() && NVT.getVectorElementType() == MVT::i1) {
      // Try to form a masked VPTESTM. Operands can be in either order.
      SDValue N0 = Node->getOperand(0);
      SDValue N1 = Node->getOperand(1);
      if (N0.getOpcode() == ISD::SETCC && N0.hasOneUse() &&
          tryVPTESTM(Node, N0, N1))
        return;
      if (N1.getOpcode() == ISD::SETCC && N1.hasOneUse() &&
          tryVPTESTM(Node, N1, N0))
        return;
    }

    if (MachineSDNode *NewNode = matchBEXTRFromAndImm(Node)) {
      ReplaceUses(SDValue(Node, 0), SDValue(NewNode, 0));
      CurDAG->RemoveDeadNode(Node);
      return;
    }
    if (matchBitExtract(Node))
      return;
    if (AndImmShrink && shrinkAndImmediate(Node))
      return;

    [[fallthrough]];
  case ISD::OR:
  case ISD::XOR:
    if (tryShrinkShlLogicImm(Node))
      return;
    if (Opcode == ISD::OR && tryMatchBitSelect(Node))
      return;
    if (tryVPTERNLOG(Node))
      return;

    [[fallthrough]];
  case ISD::ADD:
    if (Opcode == ISD::ADD && matchBitExtract(Node))
      return;
    [[fallthrough]];
  case ISD::SUB: {
    // Try to avoid folding immediates with multiple uses for optsize.
    // This code tries to select to register form directly to avoid going
    // through the isel table which might fold the immediate. We can't change
    // the patterns on the add/sub/and/or/xor with immediate paterns in the
    // tablegen files to check immediate use count without making the patterns
    // unavailable to the fast-isel table.
    if (!CurDAG->shouldOptForSize())
      break;

    // Only handle i8/i16/i32/i64.
    if (NVT != MVT::i8 && NVT != MVT::i16 && NVT != MVT::i32 && NVT != MVT::i64)
      break;

    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    auto *Cst = dyn_cast<ConstantSDNode>(N1);
    if (!Cst)
      break;

    int64_t Val = Cst->getSExtValue();

    // Make sure its an immediate that is considered foldable.
    // FIXME: Handle unsigned 32 bit immediates for 64-bit AND.
    if (!isInt<8>(Val) && !isInt<32>(Val))
      break;

    // If this can match to INC/DEC, let it go.
    if (Opcode == ISD::ADD && (Val == 1 || Val == -1))
      break;

    // Check if we should avoid folding this immediate.
    if (!shouldAvoidImmediateInstFormsForSize(N1.getNode()))
      break;

    // We should not fold the immediate. So we need a register form instead.
    unsigned ROpc, MOpc;
    switch (NVT.SimpleTy) {
    default: llvm_unreachable("Unexpected VT!");
    case MVT::i8:
      switch (Opcode) {
      default: llvm_unreachable("Unexpected opcode!");
      case ISD::ADD:
        ROpc = GET_ND_IF_ENABLED(X86::ADD8rr);
        MOpc = GET_ND_IF_ENABLED(X86::ADD8rm);
        break;
      case ISD::SUB:
        ROpc = GET_ND_IF_ENABLED(X86::SUB8rr);
        MOpc = GET_ND_IF_ENABLED(X86::SUB8rm);
        break;
      case ISD::AND:
        ROpc = GET_ND_IF_ENABLED(X86::AND8rr);
        MOpc = GET_ND_IF_ENABLED(X86::AND8rm);
        break;
      case ISD::OR:
        ROpc = GET_ND_IF_ENABLED(X86::OR8rr);
        MOpc = GET_ND_IF_ENABLED(X86::OR8rm);
        break;
      case ISD::XOR:
        ROpc = GET_ND_IF_ENABLED(X86::XOR8rr);
        MOpc = GET_ND_IF_ENABLED(X86::XOR8rm);
        break;
      }
      break;
    case MVT::i16:
      switch (Opcode) {
      default: llvm_unreachable("Unexpected opcode!");
      case ISD::ADD:
        ROpc = GET_ND_IF_ENABLED(X86::ADD16rr);
        MOpc = GET_ND_IF_ENABLED(X86::ADD16rm);
        break;
      case ISD::SUB:
        ROpc = GET_ND_IF_ENABLED(X86::SUB16rr);
        MOpc = GET_ND_IF_ENABLED(X86::SUB16rm);
        break;
      case ISD::AND:
        ROpc = GET_ND_IF_ENABLED(X86::AND16rr);
        MOpc = GET_ND_IF_ENABLED(X86::AND16rm);
        break;
      case ISD::OR:
        ROpc = GET_ND_IF_ENABLED(X86::OR16rr);
        MOpc = GET_ND_IF_ENABLED(X86::OR16rm);
        break;
      case ISD::XOR:
        ROpc = GET_ND_IF_ENABLED(X86::XOR16rr);
        MOpc = GET_ND_IF_ENABLED(X86::XOR16rm);
        break;
      }
      break;
    case MVT::i32:
      switch (Opcode) {
      default: llvm_unreachable("Unexpected opcode!");
      case ISD::ADD:
        ROpc = GET_ND_IF_ENABLED(X86::ADD32rr);
        MOpc = GET_ND_IF_ENABLED(X86::ADD32rm);
        break;
      case ISD::SUB:
        ROpc = GET_ND_IF_ENABLED(X86::SUB32rr);
        MOpc = GET_ND_IF_ENABLED(X86::SUB32rm);
        break;
      case ISD::AND:
        ROpc = GET_ND_IF_ENABLED(X86::AND32rr);
        MOpc = GET_ND_IF_ENABLED(X86::AND32rm);
        break;
      case ISD::OR:
        ROpc = GET_ND_IF_ENABLED(X86::OR32rr);
        MOpc = GET_ND_IF_ENABLED(X86::OR32rm);
        break;
      case ISD::XOR:
        ROpc = GET_ND_IF_ENABLED(X86::XOR32rr);
        MOpc = GET_ND_IF_ENABLED(X86::XOR32rm);
        break;
      }
      break;
    case MVT::i64:
      switch (Opcode) {
      default: llvm_unreachable("Unexpected opcode!");
      case ISD::ADD:
        ROpc = GET_ND_IF_ENABLED(X86::ADD64rr);
        MOpc = GET_ND_IF_ENABLED(X86::ADD64rm);
        break;
      case ISD::SUB:
        ROpc = GET_ND_IF_ENABLED(X86::SUB64rr);
        MOpc = GET_ND_IF_ENABLED(X86::SUB64rm);
        break;
      case ISD::AND:
        ROpc = GET_ND_IF_ENABLED(X86::AND64rr);
        MOpc = GET_ND_IF_ENABLED(X86::AND64rm);
        break;
      case ISD::OR:
        ROpc = GET_ND_IF_ENABLED(X86::OR64rr);
        MOpc = GET_ND_IF_ENABLED(X86::OR64rm);
        break;
      case ISD::XOR:
        ROpc = GET_ND_IF_ENABLED(X86::XOR64rr);
        MOpc = GET_ND_IF_ENABLED(X86::XOR64rm);
        break;
      }
      break;
    }

    // Ok this is a AND/OR/XOR/ADD/SUB with constant.

    // If this is a not a subtract, we can still try to fold a load.
    if (Opcode != ISD::SUB) {
      SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
      if (tryFoldLoad(Node, N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
        SDValue Ops[] = { N1, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, N0.getOperand(0) };
        SDVTList VTs = CurDAG->getVTList(NVT, MVT::i32, MVT::Other);
        MachineSDNode *CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
        // Update the chain.
        ReplaceUses(N0.getValue(1), SDValue(CNode, 2));
        // Record the mem-refs
        CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N0)->getMemOperand()});
        ReplaceUses(SDValue(Node, 0), SDValue(CNode, 0));
        CurDAG->RemoveDeadNode(Node);
        return;
      }
    }

    CurDAG->SelectNodeTo(Node, ROpc, NVT, MVT::i32, N0, N1);
    return;
  }

  case X86ISD::SMUL:
    // i16/i32/i64 are handled with isel patterns.
    if (NVT != MVT::i8)
      break;
    [[fallthrough]];
  case X86ISD::UMUL: {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    unsigned LoReg, ROpc, MOpc;
    switch (NVT.SimpleTy) {
    default: llvm_unreachable("Unsupported VT!");
    case MVT::i8:
      LoReg = X86::AL;
      ROpc = Opcode == X86ISD::SMUL ? X86::IMUL8r : X86::MUL8r;
      MOpc = Opcode == X86ISD::SMUL ? X86::IMUL8m : X86::MUL8m;
      break;
    case MVT::i16:
      LoReg = X86::AX;
      ROpc = X86::MUL16r;
      MOpc = X86::MUL16m;
      break;
    case MVT::i32:
      LoReg = X86::EAX;
      ROpc = X86::MUL32r;
      MOpc = X86::MUL32m;
      break;
    case MVT::i64:
      LoReg = X86::RAX;
      ROpc = X86::MUL64r;
      MOpc = X86::MUL64m;
      break;
    }

    SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
    bool FoldedLoad = tryFoldLoad(Node, N1, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
    // Multiply is commutative.
    if (!FoldedLoad) {
      FoldedLoad = tryFoldLoad(Node, N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
      if (FoldedLoad)
        std::swap(N0, N1);
    }

    SDValue InGlue = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, LoReg,
                                          N0, SDValue()).getValue(1);

    MachineSDNode *CNode;
    if (FoldedLoad) {
      // i16/i32/i64 use an instruction that produces a low and high result even
      // though only the low result is used.
      SDVTList VTs;
      if (NVT == MVT::i8)
        VTs = CurDAG->getVTList(NVT, MVT::i32, MVT::Other);
      else
        VTs = CurDAG->getVTList(NVT, NVT, MVT::i32, MVT::Other);

      SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, N1.getOperand(0),
                        InGlue };
      CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);

      // Update the chain.
      ReplaceUses(N1.getValue(1), SDValue(CNode, NVT == MVT::i8 ? 2 : 3));
      // Record the mem-refs
      CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N1)->getMemOperand()});
    } else {
      // i16/i32/i64 use an instruction that produces a low and high result even
      // though only the low result is used.
      SDVTList VTs;
      if (NVT == MVT::i8)
        VTs = CurDAG->getVTList(NVT, MVT::i32);
      else
        VTs = CurDAG->getVTList(NVT, NVT, MVT::i32);

      CNode = CurDAG->getMachineNode(ROpc, dl, VTs, {N1, InGlue});
    }

    ReplaceUses(SDValue(Node, 0), SDValue(CNode, 0));
    ReplaceUses(SDValue(Node, 1), SDValue(CNode, NVT == MVT::i8 ? 1 : 2));
    CurDAG->RemoveDeadNode(Node);
    return;
  }

  case ISD::SMUL_LOHI:
  case ISD::UMUL_LOHI: {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    unsigned Opc, MOpc;
    unsigned LoReg, HiReg;
    bool IsSigned = Opcode == ISD::SMUL_LOHI;
    bool UseMULX = !IsSigned && Subtarget->hasBMI2();
    bool UseMULXHi = UseMULX && SDValue(Node, 0).use_empty();
    switch (NVT.SimpleTy) {
    default: llvm_unreachable("Unsupported VT!");
    case MVT::i32:
      Opc = UseMULXHi  ? X86::MULX32Hrr
            : UseMULX  ? GET_EGPR_IF_ENABLED(X86::MULX32rr)
            : IsSigned ? X86::IMUL32r
                       : X86::MUL32r;
      MOpc = UseMULXHi  ? X86::MULX32Hrm
             : UseMULX  ? GET_EGPR_IF_ENABLED(X86::MULX32rm)
             : IsSigned ? X86::IMUL32m
                        : X86::MUL32m;
      LoReg = UseMULX ? X86::EDX : X86::EAX;
      HiReg = X86::EDX;
      break;
    case MVT::i64:
      Opc = UseMULXHi  ? X86::MULX64Hrr
            : UseMULX  ? GET_EGPR_IF_ENABLED(X86::MULX64rr)
            : IsSigned ? X86::IMUL64r
                       : X86::MUL64r;
      MOpc = UseMULXHi  ? X86::MULX64Hrm
             : UseMULX  ? GET_EGPR_IF_ENABLED(X86::MULX64rm)
             : IsSigned ? X86::IMUL64m
                        : X86::MUL64m;
      LoReg = UseMULX ? X86::RDX : X86::RAX;
      HiReg = X86::RDX;
      break;
    }

    SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
    bool foldedLoad = tryFoldLoad(Node, N1, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
    // Multiply is commutative.
    if (!foldedLoad) {
      foldedLoad = tryFoldLoad(Node, N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
      if (foldedLoad)
        std::swap(N0, N1);
    }

    SDValue InGlue = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, LoReg,
                                          N0, SDValue()).getValue(1);
    SDValue ResHi, ResLo;
    if (foldedLoad) {
      SDValue Chain;
      MachineSDNode *CNode = nullptr;
      SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, N1.getOperand(0),
                        InGlue };
      if (UseMULXHi) {
        SDVTList VTs = CurDAG->getVTList(NVT, MVT::Other);
        CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
        ResHi = SDValue(CNode, 0);
        Chain = SDValue(CNode, 1);
      } else if (UseMULX) {
        SDVTList VTs = CurDAG->getVTList(NVT, NVT, MVT::Other);
        CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
        ResHi = SDValue(CNode, 0);
        ResLo = SDValue(CNode, 1);
        Chain = SDValue(CNode, 2);
      } else {
        SDVTList VTs = CurDAG->getVTList(MVT::Other, MVT::Glue);
        CNode = CurDAG->getMachineNode(MOpc, dl, VTs, Ops);
        Chain = SDValue(CNode, 0);
        InGlue = SDValue(CNode, 1);
      }

      // Update the chain.
      ReplaceUses(N1.getValue(1), Chain);
      // Record the mem-refs
      CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N1)->getMemOperand()});
    } else {
      SDValue Ops[] = { N1, InGlue };
      if (UseMULXHi) {
        SDVTList VTs = CurDAG->getVTList(NVT);
        SDNode *CNode = CurDAG->getMachineNode(Opc, dl, VTs, Ops);
        ResHi = SDValue(CNode, 0);
      } else if (UseMULX) {
        SDVTList VTs = CurDAG->getVTList(NVT, NVT);
        SDNode *CNode = CurDAG->getMachineNode(Opc, dl, VTs, Ops);
        ResHi = SDValue(CNode, 0);
        ResLo = SDValue(CNode, 1);
      } else {
        SDVTList VTs = CurDAG->getVTList(MVT::Glue);
        SDNode *CNode = CurDAG->getMachineNode(Opc, dl, VTs, Ops);
        InGlue = SDValue(CNode, 0);
      }
    }

    // Copy the low half of the result, if it is needed.
    if (!SDValue(Node, 0).use_empty()) {
      if (!ResLo) {
        assert(LoReg && "Register for low half is not defined!");
        ResLo = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), dl, LoReg,
                                       NVT, InGlue);
        InGlue = ResLo.getValue(2);
      }
      ReplaceUses(SDValue(Node, 0), ResLo);
      LLVM_DEBUG(dbgs() << "=> "; ResLo.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }
    // Copy the high half of the result, if it is needed.
    if (!SDValue(Node, 1).use_empty()) {
      if (!ResHi) {
        assert(HiReg && "Register for high half is not defined!");
        ResHi = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), dl, HiReg,
                                       NVT, InGlue);
        InGlue = ResHi.getValue(2);
      }
      ReplaceUses(SDValue(Node, 1), ResHi);
      LLVM_DEBUG(dbgs() << "=> "; ResHi.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }

    CurDAG->RemoveDeadNode(Node);
    return;
  }

  case ISD::SDIVREM:
  case ISD::UDIVREM: {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    unsigned ROpc, MOpc;
    bool isSigned = Opcode == ISD::SDIVREM;
    if (!isSigned) {
      switch (NVT.SimpleTy) {
      default: llvm_unreachable("Unsupported VT!");
      case MVT::i8:  ROpc = X86::DIV8r;  MOpc = X86::DIV8m;  break;
      case MVT::i16: ROpc = X86::DIV16r; MOpc = X86::DIV16m; break;
      case MVT::i32: ROpc = X86::DIV32r; MOpc = X86::DIV32m; break;
      case MVT::i64: ROpc = X86::DIV64r; MOpc = X86::DIV64m; break;
      }
    } else {
      switch (NVT.SimpleTy) {
      default: llvm_unreachable("Unsupported VT!");
      case MVT::i8:  ROpc = X86::IDIV8r;  MOpc = X86::IDIV8m;  break;
      case MVT::i16: ROpc = X86::IDIV16r; MOpc = X86::IDIV16m; break;
      case MVT::i32: ROpc = X86::IDIV32r; MOpc = X86::IDIV32m; break;
      case MVT::i64: ROpc = X86::IDIV64r; MOpc = X86::IDIV64m; break;
      }
    }

    unsigned LoReg, HiReg, ClrReg;
    unsigned SExtOpcode;
    switch (NVT.SimpleTy) {
    default: llvm_unreachable("Unsupported VT!");
    case MVT::i8:
      LoReg = X86::AL;  ClrReg = HiReg = X86::AH;
      SExtOpcode = 0; // Not used.
      break;
    case MVT::i16:
      LoReg = X86::AX;  HiReg = X86::DX;
      ClrReg = X86::DX;
      SExtOpcode = X86::CWD;
      break;
    case MVT::i32:
      LoReg = X86::EAX; ClrReg = HiReg = X86::EDX;
      SExtOpcode = X86::CDQ;
      break;
    case MVT::i64:
      LoReg = X86::RAX; ClrReg = HiReg = X86::RDX;
      SExtOpcode = X86::CQO;
      break;
    }

    SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
    bool foldedLoad = tryFoldLoad(Node, N1, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4);
    bool signBitIsZero = CurDAG->SignBitIsZero(N0);

    SDValue InGlue;
    if (NVT == MVT::i8) {
      // Special case for div8, just use a move with zero extension to AX to
      // clear the upper 8 bits (AH).
      SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, Chain;
      MachineSDNode *Move;
      if (tryFoldLoad(Node, N0, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
        SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, N0.getOperand(0) };
        unsigned Opc = (isSigned && !signBitIsZero) ? X86::MOVSX16rm8
                                                    : X86::MOVZX16rm8;
        Move = CurDAG->getMachineNode(Opc, dl, MVT::i16, MVT::Other, Ops);
        Chain = SDValue(Move, 1);
        ReplaceUses(N0.getValue(1), Chain);
        // Record the mem-refs
        CurDAG->setNodeMemRefs(Move, {cast<LoadSDNode>(N0)->getMemOperand()});
      } else {
        unsigned Opc = (isSigned && !signBitIsZero) ? X86::MOVSX16rr8
                                                    : X86::MOVZX16rr8;
        Move = CurDAG->getMachineNode(Opc, dl, MVT::i16, N0);
        Chain = CurDAG->getEntryNode();
      }
      Chain  = CurDAG->getCopyToReg(Chain, dl, X86::AX, SDValue(Move, 0),
                                    SDValue());
      InGlue = Chain.getValue(1);
    } else {
      InGlue =
        CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl,
                             LoReg, N0, SDValue()).getValue(1);
      if (isSigned && !signBitIsZero) {
        // Sign extend the low part into the high part.
        InGlue =
          SDValue(CurDAG->getMachineNode(SExtOpcode, dl, MVT::Glue, InGlue),0);
      } else {
        // Zero out the high part, effectively zero extending the input.
        SDVTList VTs = CurDAG->getVTList(MVT::i32, MVT::i32);
        SDValue ClrNode = SDValue(
            CurDAG->getMachineNode(X86::MOV32r0, dl, VTs, std::nullopt), 0);
        switch (NVT.SimpleTy) {
        case MVT::i16:
          ClrNode =
              SDValue(CurDAG->getMachineNode(
                          TargetOpcode::EXTRACT_SUBREG, dl, MVT::i16, ClrNode,
                          CurDAG->getTargetConstant(X86::sub_16bit, dl,
                                                    MVT::i32)),
                      0);
          break;
        case MVT::i32:
          break;
        case MVT::i64:
          ClrNode =
              SDValue(CurDAG->getMachineNode(
                          TargetOpcode::SUBREG_TO_REG, dl, MVT::i64,
                          CurDAG->getTargetConstant(0, dl, MVT::i64), ClrNode,
                          CurDAG->getTargetConstant(X86::sub_32bit, dl,
                                                    MVT::i32)),
                      0);
          break;
        default:
          llvm_unreachable("Unexpected division source");
        }

        InGlue = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, ClrReg,
                                      ClrNode, InGlue).getValue(1);
      }
    }

    if (foldedLoad) {
      SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, N1.getOperand(0),
                        InGlue };
      MachineSDNode *CNode =
        CurDAG->getMachineNode(MOpc, dl, MVT::Other, MVT::Glue, Ops);
      InGlue = SDValue(CNode, 1);
      // Update the chain.
      ReplaceUses(N1.getValue(1), SDValue(CNode, 0));
      // Record the mem-refs
      CurDAG->setNodeMemRefs(CNode, {cast<LoadSDNode>(N1)->getMemOperand()});
    } else {
      InGlue =
        SDValue(CurDAG->getMachineNode(ROpc, dl, MVT::Glue, N1, InGlue), 0);
    }

    // Prevent use of AH in a REX instruction by explicitly copying it to
    // an ABCD_L register.
    //
    // The current assumption of the register allocator is that isel
    // won't generate explicit references to the GR8_ABCD_H registers. If
    // the allocator and/or the backend get enhanced to be more robust in
    // that regard, this can be, and should be, removed.
    if (HiReg == X86::AH && !SDValue(Node, 1).use_empty()) {
      SDValue AHCopy = CurDAG->getRegister(X86::AH, MVT::i8);
      unsigned AHExtOpcode =
          isSigned ? X86::MOVSX32rr8_NOREX : X86::MOVZX32rr8_NOREX;

      SDNode *RNode = CurDAG->getMachineNode(AHExtOpcode, dl, MVT::i32,
                                             MVT::Glue, AHCopy, InGlue);
      SDValue Result(RNode, 0);
      InGlue = SDValue(RNode, 1);

      Result =
          CurDAG->getTargetExtractSubreg(X86::sub_8bit, dl, MVT::i8, Result);

      ReplaceUses(SDValue(Node, 1), Result);
      LLVM_DEBUG(dbgs() << "=> "; Result.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }
    // Copy the division (low) result, if it is needed.
    if (!SDValue(Node, 0).use_empty()) {
      SDValue Result = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), dl,
                                                LoReg, NVT, InGlue);
      InGlue = Result.getValue(2);
      ReplaceUses(SDValue(Node, 0), Result);
      LLVM_DEBUG(dbgs() << "=> "; Result.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }
    // Copy the remainder (high) result, if it is needed.
    if (!SDValue(Node, 1).use_empty()) {
      SDValue Result = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), dl,
                                              HiReg, NVT, InGlue);
      InGlue = Result.getValue(2);
      ReplaceUses(SDValue(Node, 1), Result);
      LLVM_DEBUG(dbgs() << "=> "; Result.getNode()->dump(CurDAG);
                 dbgs() << '\n');
    }
    CurDAG->RemoveDeadNode(Node);
    return;
  }

  case X86ISD::FCMP:
  case X86ISD::STRICT_FCMP:
  case X86ISD::STRICT_FCMPS: {
    bool IsStrictCmp = Node->getOpcode() == X86ISD::STRICT_FCMP ||
                       Node->getOpcode() == X86ISD::STRICT_FCMPS;
    SDValue N0 = Node->getOperand(IsStrictCmp ? 1 : 0);
    SDValue N1 = Node->getOperand(IsStrictCmp ? 2 : 1);

    // Save the original VT of the compare.
    MVT CmpVT = N0.getSimpleValueType();

    // Floating point needs special handling if we don't have FCOMI.
    if (Subtarget->canUseCMOV())
      break;

    bool IsSignaling = Node->getOpcode() == X86ISD::STRICT_FCMPS;

    unsigned Opc;
    switch (CmpVT.SimpleTy) {
    default: llvm_unreachable("Unexpected type!");
    case MVT::f32:
      Opc = IsSignaling ? X86::COM_Fpr32 : X86::UCOM_Fpr32;
      break;
    case MVT::f64:
      Opc = IsSignaling ? X86::COM_Fpr64 : X86::UCOM_Fpr64;
      break;
    case MVT::f80:
      Opc = IsSignaling ? X86::COM_Fpr80 : X86::UCOM_Fpr80;
      break;
    }

    SDValue Chain =
        IsStrictCmp ? Node->getOperand(0) : CurDAG->getEntryNode();
    SDValue Glue;
    if (IsStrictCmp) {
      SDVTList VTs = CurDAG->getVTList(MVT::Other, MVT::Glue);
      Chain = SDValue(CurDAG->getMachineNode(Opc, dl, VTs, {N0, N1, Chain}), 0);
      Glue = Chain.getValue(1);
    } else {
      Glue = SDValue(CurDAG->getMachineNode(Opc, dl, MVT::Glue, N0, N1), 0);
    }

    // Move FPSW to AX.
    SDValue FNSTSW =
        SDValue(CurDAG->getMachineNode(X86::FNSTSW16r, dl, MVT::i16, Glue), 0);

    // Extract upper 8-bits of AX.
    SDValue Extract =
        CurDAG->getTargetExtractSubreg(X86::sub_8bit_hi, dl, MVT::i8, FNSTSW);

    // Move AH into flags.
    // Some 64-bit targets lack SAHF support, but they do support FCOMI.
    assert(Subtarget->canUseLAHFSAHF() &&
           "Target doesn't support SAHF or FCOMI?");
    SDValue AH = CurDAG->getCopyToReg(Chain, dl, X86::AH, Extract, SDValue());
    Chain = AH;
    SDValue SAHF = SDValue(
        CurDAG->getMachineNode(X86::SAHF, dl, MVT::i32, AH.getValue(1)), 0);

    if (IsStrictCmp)
      ReplaceUses(SDValue(Node, 1), Chain);

    ReplaceUses(SDValue(Node, 0), SAHF);
    CurDAG->RemoveDeadNode(Node);
    return;
  }

  case X86ISD::CMP: {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);

    // Optimizations for TEST compares.
    if (!isNullConstant(N1))
      break;

    // Save the original VT of the compare.
    MVT CmpVT = N0.getSimpleValueType();

    // If we are comparing (and (shr X, C, Mask) with 0, emit a BEXTR followed
    // by a test instruction. The test should be removed later by
    // analyzeCompare if we are using only the zero flag.
    // TODO: Should we check the users and use the BEXTR flags directly?
    if (N0.getOpcode() == ISD::AND && N0.hasOneUse()) {
      if (MachineSDNode *NewNode = matchBEXTRFromAndImm(N0.getNode())) {
        unsigned TestOpc = CmpVT == MVT::i64 ? X86::TEST64rr
                                             : X86::TEST32rr;
        SDValue BEXTR = SDValue(NewNode, 0);
        NewNode = CurDAG->getMachineNode(TestOpc, dl, MVT::i32, BEXTR, BEXTR);
        ReplaceUses(SDValue(Node, 0), SDValue(NewNode, 0));
        CurDAG->RemoveDeadNode(Node);
        return;
      }
    }

    // We can peek through truncates, but we need to be careful below.
    if (N0.getOpcode() == ISD::TRUNCATE && N0.hasOneUse())
      N0 = N0.getOperand(0);

    // Look for (X86cmp (and $op, $imm), 0) and see if we can convert it to
    // use a smaller encoding.
    // Look past the truncate if CMP is the only use of it.
    if (N0.getOpcode() == ISD::AND && N0.getNode()->hasOneUse() &&
        N0.getValueType() != MVT::i8) {
      auto *MaskC = dyn_cast<ConstantSDNode>(N0.getOperand(1));
      if (!MaskC)
        break;

      // We may have looked through a truncate so mask off any bits that
      // shouldn't be part of the compare.
      uint64_t Mask = MaskC->getZExtValue();
      Mask &= maskTrailingOnes<uint64_t>(CmpVT.getScalarSizeInBits());

      // Check if we can replace AND+IMM{32,64} with a shift. This is possible
      // for masks like 0xFF000000 or 0x00FFFFFF and if we care only about the
      // zero flag.
      if (CmpVT == MVT::i64 && !isInt<8>(Mask) && isShiftedMask_64(Mask) &&
          onlyUsesZeroFlag(SDValue(Node, 0))) {
        unsigned ShiftOpcode = ISD::DELETED_NODE;
        unsigned ShiftAmt;
        unsigned SubRegIdx;
        MVT SubRegVT;
        unsigned TestOpcode;
        unsigned LeadingZeros = llvm::countl_zero(Mask);
        unsigned TrailingZeros = llvm::countr_zero(Mask);

        // With leading/trailing zeros, the transform is profitable if we can
        // eliminate a movabsq or shrink a 32-bit immediate to 8-bit without
        // incurring any extra register moves.
        bool SavesBytes = !isInt<32>(Mask) || N0.getOperand(0).hasOneUse();
        if (LeadingZeros == 0 && SavesBytes) {
          // If the mask covers the most significant bit, then we can replace
          // TEST+AND with a SHR and check eflags.
          // This emits a redundant TEST which is subsequently eliminated.
          ShiftOpcode = GET_ND_IF_ENABLED(X86::SHR64ri);
          ShiftAmt = TrailingZeros;
          SubRegIdx = 0;
          TestOpcode = X86::TEST64rr;
        } else if (TrailingZeros == 0 && SavesBytes) {
          // If the mask covers the least significant bit, then we can replace
          // TEST+AND with a SHL and check eflags.
          // This emits a redundant TEST which is subsequently eliminated.
          ShiftOpcode = GET_ND_IF_ENABLED(X86::SHL64ri);
          ShiftAmt = LeadingZeros;
          SubRegIdx = 0;
          TestOpcode = X86::TEST64rr;
        } else if (MaskC->hasOneUse() && !isInt<32>(Mask)) {
          // If the shifted mask extends into the high half and is 8/16/32 bits
          // wide, then replace it with a SHR and a TEST8rr/TEST16rr/TEST32rr.
          unsigned PopCount = 64 - LeadingZeros - TrailingZeros;
          if (PopCount == 8) {
            ShiftOpcode = GET_ND_IF_ENABLED(X86::SHR64ri);
            ShiftAmt = TrailingZeros;
            SubRegIdx = X86::sub_8bit;
            SubRegVT = MVT::i8;
            TestOpcode = X86::TEST8rr;
          } else if (PopCount == 16) {
            ShiftOpcode = GET_ND_IF_ENABLED(X86::SHR64ri);
            ShiftAmt = TrailingZeros;
            SubRegIdx = X86::sub_16bit;
            SubRegVT = MVT::i16;
            TestOpcode = X86::TEST16rr;
          } else if (PopCount == 32) {
            ShiftOpcode = GET_ND_IF_ENABLED(X86::SHR64ri);
            ShiftAmt = TrailingZeros;
            SubRegIdx = X86::sub_32bit;
            SubRegVT = MVT::i32;
            TestOpcode = X86::TEST32rr;
          }
        }
        if (ShiftOpcode != ISD::DELETED_NODE) {
          SDValue ShiftC = CurDAG->getTargetConstant(ShiftAmt, dl, MVT::i64);
          SDValue Shift = SDValue(
              CurDAG->getMachineNode(ShiftOpcode, dl, MVT::i64, MVT::i32,
                                     N0.getOperand(0), ShiftC),
              0);
          if (SubRegIdx != 0) {
            Shift =
                CurDAG->getTargetExtractSubreg(SubRegIdx, dl, SubRegVT, Shift);
          }
          MachineSDNode *Test =
              CurDAG->getMachineNode(TestOpcode, dl, MVT::i32, Shift, Shift);
          ReplaceNode(Node, Test);
          return;
        }
      }

      MVT VT;
      int SubRegOp;
      unsigned ROpc, MOpc;

      // For each of these checks we need to be careful if the sign flag is
      // being used. It is only safe to use the sign flag in two conditions,
      // either the sign bit in the shrunken mask is zero or the final test
      // size is equal to the original compare size.

      if (isUInt<8>(Mask) &&
          (!(Mask & 0x80) || CmpVT == MVT::i8 ||
           hasNoSignFlagUses(SDValue(Node, 0)))) {
        // For example, convert "testl %eax, $8" to "testb %al, $8"
        VT = MVT::i8;
        SubRegOp = X86::sub_8bit;
        ROpc = X86::TEST8ri;
        MOpc = X86::TEST8mi;
      } else if (OptForMinSize && isUInt<16>(Mask) &&
                 (!(Mask & 0x8000) || CmpVT == MVT::i16 ||
                  hasNoSignFlagUses(SDValue(Node, 0)))) {
        // For example, "testl %eax, $32776" to "testw %ax, $32776".
        // NOTE: We only want to form TESTW instructions if optimizing for
        // min size. Otherwise we only save one byte and possibly get a length
        // changing prefix penalty in the decoders.
        VT = MVT::i16;
        SubRegOp = X86::sub_16bit;
        ROpc = X86::TEST16ri;
        MOpc = X86::TEST16mi;
      } else if (isUInt<32>(Mask) && N0.getValueType() != MVT::i16 &&
                 ((!(Mask & 0x80000000) &&
                   // Without minsize 16-bit Cmps can get here so we need to
                   // be sure we calculate the correct sign flag if needed.
                   (CmpVT != MVT::i16 || !(Mask & 0x8000))) ||
                  CmpVT == MVT::i32 ||
                  hasNoSignFlagUses(SDValue(Node, 0)))) {
        // For example, "testq %rax, $268468232" to "testl %eax, $268468232".
        // NOTE: We only want to run that transform if N0 is 32 or 64 bits.
        // Otherwize, we find ourselves in a position where we have to do
        // promotion. If previous passes did not promote the and, we assume
        // they had a good reason not to and do not promote here.
        VT = MVT::i32;
        SubRegOp = X86::sub_32bit;
        ROpc = X86::TEST32ri;
        MOpc = X86::TEST32mi;
      } else {
        // No eligible transformation was found.
        break;
      }

      SDValue Imm = CurDAG->getTargetConstant(Mask, dl, VT);
      SDValue Reg = N0.getOperand(0);

      // Emit a testl or testw.
      MachineSDNode *NewNode;
      SDValue Tmp0, Tmp1, Tmp2, Tmp3, Tmp4;
      if (tryFoldLoad(Node, N0.getNode(), Reg, Tmp0, Tmp1, Tmp2, Tmp3, Tmp4)) {
        if (auto *LoadN = dyn_cast<LoadSDNode>(N0.getOperand(0).getNode())) {
          if (!LoadN->isSimple()) {
            unsigned NumVolBits = LoadN->getValueType(0).getSizeInBits();
            if ((MOpc == X86::TEST8mi && NumVolBits != 8) ||
                (MOpc == X86::TEST16mi && NumVolBits != 16) ||
                (MOpc == X86::TEST32mi && NumVolBits != 32))
              break;
          }
        }
        SDValue Ops[] = { Tmp0, Tmp1, Tmp2, Tmp3, Tmp4, Imm,
                          Reg.getOperand(0) };
        NewNode = CurDAG->getMachineNode(MOpc, dl, MVT::i32, MVT::Other, Ops);
        // Update the chain.
        ReplaceUses(Reg.getValue(1), SDValue(NewNode, 1));
        // Record the mem-refs
        CurDAG->setNodeMemRefs(NewNode,
                               {cast<LoadSDNode>(Reg)->getMemOperand()});
      } else {
        // Extract the subregister if necessary.
        if (N0.getValueType() != VT)
          Reg = CurDAG->getTargetExtractSubreg(SubRegOp, dl, VT, Reg);

        NewNode = CurDAG->getMachineNode(ROpc, dl, MVT::i32, Reg, Imm);
      }
      // Replace CMP with TEST.
      ReplaceNode(Node, NewNode);
      return;
    }
    break;
  }
  case X86ISD::PCMPISTR: {
    if (!Subtarget->hasSSE42())
      break;

    bool NeedIndex = !SDValue(Node, 0).use_empty();
    bool NeedMask = !SDValue(Node, 1).use_empty();
    // We can't fold a load if we are going to make two instructions.
    bool MayFoldLoad = !NeedIndex || !NeedMask;

    MachineSDNode *CNode;
    if (NeedMask) {
      unsigned ROpc =
          Subtarget->hasAVX() ? X86::VPCMPISTRMrri : X86::PCMPISTRMrri;
      unsigned MOpc =
          Subtarget->hasAVX() ? X86::VPCMPISTRMrmi : X86::PCMPISTRMrmi;
      CNode = emitPCMPISTR(ROpc, MOpc, MayFoldLoad, dl, MVT::v16i8, Node);
      ReplaceUses(SDValue(Node, 1), SDValue(CNode, 0));
    }
    if (NeedIndex || !NeedMask) {
      unsigned ROpc =
          Subtarget->hasAVX() ? X86::VPCMPISTRIrri : X86::PCMPISTRIrri;
      unsigned MOpc =
          Subtarget->hasAVX() ? X86::VPCMPISTRIrmi : X86::PCMPISTRIrmi;
      CNode = emitPCMPISTR(ROpc, MOpc, MayFoldLoad, dl, MVT::i32, Node);
      ReplaceUses(SDValue(Node, 0), SDValue(CNode, 0));
    }

    // Connect the flag usage to the last instruction created.
    ReplaceUses(SDValue(Node, 2), SDValue(CNode, 1));
    CurDAG->RemoveDeadNode(Node);
    return;
  }
  case X86ISD::PCMPESTR: {
    if (!Subtarget->hasSSE42())
      break;

    // Copy the two implicit register inputs.
    SDValue InGlue = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, X86::EAX,
                                          Node->getOperand(1),
                                          SDValue()).getValue(1);
    InGlue = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, X86::EDX,
                                  Node->getOperand(3), InGlue).getValue(1);

    bool NeedIndex = !SDValue(Node, 0).use_empty();
    bool NeedMask = !SDValue(Node, 1).use_empty();
    // We can't fold a load if we are going to make two instructions.
    bool MayFoldLoad = !NeedIndex || !NeedMask;

    MachineSDNode *CNode;
    if (NeedMask) {
      unsigned ROpc =
          Subtarget->hasAVX() ? X86::VPCMPESTRMrri : X86::PCMPESTRMrri;
      unsigned MOpc =
          Subtarget->hasAVX() ? X86::VPCMPESTRMrmi : X86::PCMPESTRMrmi;
      CNode =
          emitPCMPESTR(ROpc, MOpc, MayFoldLoad, dl, MVT::v16i8, Node, InGlue);
      ReplaceUses(SDValue(Node, 1), SDValue(CNode, 0));
    }
    if (NeedIndex || !NeedMask) {
      unsigned ROpc =
          Subtarget->hasAVX() ? X86::VPCMPESTRIrri : X86::PCMPESTRIrri;
      unsigned MOpc =
          Subtarget->hasAVX() ? X86::VPCMPESTRIrmi : X86::PCMPESTRIrmi;
      CNode = emitPCMPESTR(ROpc, MOpc, MayFoldLoad, dl, MVT::i32, Node, InGlue);
      ReplaceUses(SDValue(Node, 0), SDValue(CNode, 0));
    }
    // Connect the flag usage to the last instruction created.
    ReplaceUses(SDValue(Node, 2), SDValue(CNode, 1));
    CurDAG->RemoveDeadNode(Node);
    return;
  }

  case ISD::SETCC: {
    if (NVT.isVector() && tryVPTESTM(Node, SDValue(Node, 0), SDValue()))
      return;

    break;
  }

  case ISD::STORE:
    if (foldLoadStoreIntoMemOperand(Node))
      return;
    break;

  case X86ISD::SETCC_CARRY: {
    MVT VT = Node->getSimpleValueType(0);
    SDValue Result;
    if (Subtarget->hasSBBDepBreaking()) {
      // We have to do this manually because tblgen will put the eflags copy in
      // the wrong place if we use an extract_subreg in the pattern.
      // Copy flags to the EFLAGS register and glue it to next node.
      SDValue EFLAGS =
          CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, X86::EFLAGS,
                               Node->getOperand(1), SDValue());

      // Create a 64-bit instruction if the result is 64-bits otherwise use the
      // 32-bit version.
      unsigned Opc = VT == MVT::i64 ? X86::SETB_C64r : X86::SETB_C32r;
      MVT SetVT = VT == MVT::i64 ? MVT::i64 : MVT::i32;
      Result = SDValue(
          CurDAG->getMachineNode(Opc, dl, SetVT, EFLAGS, EFLAGS.getValue(1)),
          0);
    } else {
      // The target does not recognize sbb with the same reg operand as a
      // no-source idiom, so we explicitly zero the input values.
      Result = getSBBZero(Node);
    }

    // For less than 32-bits we need to extract from the 32-bit node.
    if (VT == MVT::i8 || VT == MVT::i16) {
      int SubIndex = VT == MVT::i16 ? X86::sub_16bit : X86::sub_8bit;
      Result = CurDAG->getTargetExtractSubreg(SubIndex, dl, VT, Result);
    }

    ReplaceUses(SDValue(Node, 0), Result);
    CurDAG->RemoveDeadNode(Node);
    return;
  }
  case X86ISD::SBB: {
    if (isNullConstant(Node->getOperand(0)) &&
        isNullConstant(Node->getOperand(1))) {
      SDValue Result = getSBBZero(Node);

      // Replace the flag use.
      ReplaceUses(SDValue(Node, 1), Result.getValue(1));

      // Replace the result use.
      if (!SDValue(Node, 0).use_empty()) {
        // For less than 32-bits we need to extract from the 32-bit node.
        MVT VT = Node->getSimpleValueType(0);
        if (VT == MVT::i8 || VT == MVT::i16) {
          int SubIndex = VT == MVT::i16 ? X86::sub_16bit : X86::sub_8bit;
          Result = CurDAG->getTargetExtractSubreg(SubIndex, dl, VT, Result);
        }
        ReplaceUses(SDValue(Node, 0), Result);
      }

      CurDAG->RemoveDeadNode(Node);
      return;
    }
    break;
  }
  case X86ISD::MGATHER: {
    auto *Mgt = cast<X86MaskedGatherSDNode>(Node);
    SDValue IndexOp = Mgt->getIndex();
    SDValue Mask = Mgt->getMask();
    MVT IndexVT = IndexOp.getSimpleValueType();
    MVT ValueVT = Node->getSimpleValueType(0);
    MVT MaskVT = Mask.getSimpleValueType();

    // This is just to prevent crashes if the nodes are malformed somehow. We're
    // otherwise only doing loose type checking in here based on type what
    // a type constraint would say just like table based isel.
    if (!ValueVT.isVector() || !MaskVT.isVector())
      break;

    unsigned NumElts = ValueVT.getVectorNumElements();
    MVT ValueSVT = ValueVT.getVectorElementType();

    bool IsFP = ValueSVT.isFloatingPoint();
    unsigned EltSize = ValueSVT.getSizeInBits();

    unsigned Opc = 0;
    bool AVX512Gather = MaskVT.getVectorElementType() == MVT::i1;
    if (AVX512Gather) {
      if (IndexVT == MVT::v4i32 && NumElts == 4 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERDPSZ128rm : X86::VPGATHERDDZ128rm;
      else if (IndexVT == MVT::v8i32 && NumElts == 8 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERDPSZ256rm : X86::VPGATHERDDZ256rm;
      else if (IndexVT == MVT::v16i32 && NumElts == 16 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERDPSZrm : X86::VPGATHERDDZrm;
      else if (IndexVT == MVT::v4i32 && NumElts == 2 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERDPDZ128rm : X86::VPGATHERDQZ128rm;
      else if (IndexVT == MVT::v4i32 && NumElts == 4 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERDPDZ256rm : X86::VPGATHERDQZ256rm;
      else if (IndexVT == MVT::v8i32 && NumElts == 8 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERDPDZrm : X86::VPGATHERDQZrm;
      else if (IndexVT == MVT::v2i64 && NumElts == 4 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERQPSZ128rm : X86::VPGATHERQDZ128rm;
      else if (IndexVT == MVT::v4i64 && NumElts == 4 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERQPSZ256rm : X86::VPGATHERQDZ256rm;
      else if (IndexVT == MVT::v8i64 && NumElts == 8 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERQPSZrm : X86::VPGATHERQDZrm;
      else if (IndexVT == MVT::v2i64 && NumElts == 2 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERQPDZ128rm : X86::VPGATHERQQZ128rm;
      else if (IndexVT == MVT::v4i64 && NumElts == 4 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERQPDZ256rm : X86::VPGATHERQQZ256rm;
      else if (IndexVT == MVT::v8i64 && NumElts == 8 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERQPDZrm : X86::VPGATHERQQZrm;
    } else {
      assert(EVT(MaskVT) == EVT(ValueVT).changeVectorElementTypeToInteger() &&
             "Unexpected mask VT!");
      if (IndexVT == MVT::v4i32 && NumElts == 4 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERDPSrm : X86::VPGATHERDDrm;
      else if (IndexVT == MVT::v8i32 && NumElts == 8 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERDPSYrm : X86::VPGATHERDDYrm;
      else if (IndexVT == MVT::v4i32 && NumElts == 2 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERDPDrm : X86::VPGATHERDQrm;
      else if (IndexVT == MVT::v4i32 && NumElts == 4 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERDPDYrm : X86::VPGATHERDQYrm;
      else if (IndexVT == MVT::v2i64 && NumElts == 4 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERQPSrm : X86::VPGATHERQDrm;
      else if (IndexVT == MVT::v4i64 && NumElts == 4 && EltSize == 32)
        Opc = IsFP ? X86::VGATHERQPSYrm : X86::VPGATHERQDYrm;
      else if (IndexVT == MVT::v2i64 && NumElts == 2 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERQPDrm : X86::VPGATHERQQrm;
      else if (IndexVT == MVT::v4i64 && NumElts == 4 && EltSize == 64)
        Opc = IsFP ? X86::VGATHERQPDYrm : X86::VPGATHERQQYrm;
    }

    if (!Opc)
      break;

    SDValue Base, Scale, Index, Disp, Segment;
    if (!selectVectorAddr(Mgt, Mgt->getBasePtr(), IndexOp, Mgt->getScale(),
                          Base, Scale, Index, Disp, Segment))
      break;

    SDValue PassThru = Mgt->getPassThru();
    SDValue Chain = Mgt->getChain();
    // Gather instructions have a mask output not in the ISD node.
    SDVTList VTs = CurDAG->getVTList(ValueVT, MaskVT, MVT::Other);

    MachineSDNode *NewNode;
    if (AVX512Gather) {
      SDValue Ops[] = {PassThru, Mask, Base,    Scale,
                       Index,    Disp, Segment, Chain};
      NewNode = CurDAG->getMachineNode(Opc, SDLoc(dl), VTs, Ops);
    } else {
      SDValue Ops[] = {PassThru, Base,    Scale, Index,
                       Disp,     Segment, Mask,  Chain};
      NewNode = CurDAG->getMachineNode(Opc, SDLoc(dl), VTs, Ops);
    }
    CurDAG->setNodeMemRefs(NewNode, {Mgt->getMemOperand()});
    ReplaceUses(SDValue(Node, 0), SDValue(NewNode, 0));
    ReplaceUses(SDValue(Node, 1), SDValue(NewNode, 2));
    CurDAG->RemoveDeadNode(Node);
    return;
  }
  case X86ISD::MSCATTER: {
    auto *Sc = cast<X86MaskedScatterSDNode>(Node);
    SDValue Value = Sc->getValue();
    SDValue IndexOp = Sc->getIndex();
    MVT IndexVT = IndexOp.getSimpleValueType();
    MVT ValueVT = Value.getSimpleValueType();

    // This is just to prevent crashes if the nodes are malformed somehow. We're
    // otherwise only doing loose type checking in here based on type what
    // a type constraint would say just like table based isel.
    if (!ValueVT.isVector())
      break;

    unsigned NumElts = ValueVT.getVectorNumElements();
    MVT ValueSVT = ValueVT.getVectorElementType();

    bool IsFP = ValueSVT.isFloatingPoint();
    unsigned EltSize = ValueSVT.getSizeInBits();

    unsigned Opc;
    if (IndexVT == MVT::v4i32 && NumElts == 4 && EltSize == 32)
      Opc = IsFP ? X86::VSCATTERDPSZ128mr : X86::VPSCATTERDDZ128mr;
    else if (IndexVT == MVT::v8i32 && NumElts == 8 && EltSize == 32)
      Opc = IsFP ? X86::VSCATTERDPSZ256mr : X86::VPSCATTERDDZ256mr;
    else if (IndexVT == MVT::v16i32 && NumElts == 16 && EltSize == 32)
      Opc = IsFP ? X86::VSCATTERDPSZmr : X86::VPSCATTERDDZmr;
    else if (IndexVT == MVT::v4i32 && NumElts == 2 && EltSize == 64)
      Opc = IsFP ? X86::VSCATTERDPDZ128mr : X86::VPSCATTERDQZ128mr;
    else if (IndexVT == MVT::v4i32 && NumElts == 4 && EltSize == 64)
      Opc = IsFP ? X86::VSCATTERDPDZ256mr : X86::VPSCATTERDQZ256mr;
    else if (IndexVT == MVT::v8i32 && NumElts == 8 && EltSize == 64)
      Opc = IsFP ? X86::VSCATTERDPDZmr : X86::VPSCATTERDQZmr;
    else if (IndexVT == MVT::v2i64 && NumElts == 4 && EltSize == 32)
      Opc = IsFP ? X86::VSCATTERQPSZ128mr : X86::VPSCATTERQDZ128mr;
    else if (IndexVT == MVT::v4i64 && NumElts == 4 && EltSize == 32)
      Opc = IsFP ? X86::VSCATTERQPSZ256mr : X86::VPSCATTERQDZ256mr;
    else if (IndexVT == MVT::v8i64 && NumElts == 8 && EltSize == 32)
      Opc = IsFP ? X86::VSCATTERQPSZmr : X86::VPSCATTERQDZmr;
    else if (IndexVT == MVT::v2i64 && NumElts == 2 && EltSize == 64)
      Opc = IsFP ? X86::VSCATTERQPDZ128mr : X86::VPSCATTERQQZ128mr;
    else if (IndexVT == MVT::v4i64 && NumElts == 4 && EltSize == 64)
      Opc = IsFP ? X86::VSCATTERQPDZ256mr : X86::VPSCATTERQQZ256mr;
    else if (IndexVT == MVT::v8i64 && NumElts == 8 && EltSize == 64)
      Opc = IsFP ? X86::VSCATTERQPDZmr : X86::VPSCATTERQQZmr;
    else
      break;

    SDValue Base, Scale, Index, Disp, Segment;
    if (!selectVectorAddr(Sc, Sc->getBasePtr(), IndexOp, Sc->getScale(),
                          Base, Scale, Index, Disp, Segment))
      break;

    SDValue Mask = Sc->getMask();
    SDValue Chain = Sc->getChain();
    // Scatter instructions have a mask output not in the ISD node.
    SDVTList VTs = CurDAG->getVTList(Mask.getValueType(), MVT::Other);
    SDValue Ops[] = {Base, Scale, Index, Disp, Segment, Mask, Value, Chain};

    MachineSDNode *NewNode = CurDAG->getMachineNode(Opc, SDLoc(dl), VTs, Ops);
    CurDAG->setNodeMemRefs(NewNode, {Sc->getMemOperand()});
    ReplaceUses(SDValue(Node, 0), SDValue(NewNode, 1));
    CurDAG->RemoveDeadNode(Node);
    return;
  }
  case ISD::PREALLOCATED_SETUP: {
    auto *MFI = CurDAG->getMachineFunction().getInfo<X86MachineFunctionInfo>();
    auto CallId = MFI->getPreallocatedIdForCallSite(
        cast<SrcValueSDNode>(Node->getOperand(1))->getValue());
    SDValue Chain = Node->getOperand(0);
    SDValue CallIdValue = CurDAG->getTargetConstant(CallId, dl, MVT::i32);
    MachineSDNode *New = CurDAG->getMachineNode(
        TargetOpcode::PREALLOCATED_SETUP, dl, MVT::Other, CallIdValue, Chain);
    ReplaceUses(SDValue(Node, 0), SDValue(New, 0)); // Chain
    CurDAG->RemoveDeadNode(Node);
    return;
  }
  case ISD::PREALLOCATED_ARG: {
    auto *MFI = CurDAG->getMachineFunction().getInfo<X86MachineFunctionInfo>();
    auto CallId = MFI->getPreallocatedIdForCallSite(
        cast<SrcValueSDNode>(Node->getOperand(1))->getValue());
    SDValue Chain = Node->getOperand(0);
    SDValue CallIdValue = CurDAG->getTargetConstant(CallId, dl, MVT::i32);
    SDValue ArgIndex = Node->getOperand(2);
    SDValue Ops[3];
    Ops[0] = CallIdValue;
    Ops[1] = ArgIndex;
    Ops[2] = Chain;
    MachineSDNode *New = CurDAG->getMachineNode(
        TargetOpcode::PREALLOCATED_ARG, dl,
        CurDAG->getVTList(TLI->getPointerTy(CurDAG->getDataLayout()),
                          MVT::Other),
        Ops);
    ReplaceUses(SDValue(Node, 0), SDValue(New, 0)); // Arg pointer
    ReplaceUses(SDValue(Node, 1), SDValue(New, 1)); // Chain
    CurDAG->RemoveDeadNode(Node);
    return;
  }
  case X86ISD::AESENCWIDE128KL:
  case X86ISD::AESDECWIDE128KL:
  case X86ISD::AESENCWIDE256KL:
  case X86ISD::AESDECWIDE256KL: {
    if (!Subtarget->hasWIDEKL())
      break;

    unsigned Opcode;
    switch (Node->getOpcode()) {
    default:
      llvm_unreachable("Unexpected opcode!");
    case X86ISD::AESENCWIDE128KL:
      Opcode = X86::AESENCWIDE128KL;
      break;
    case X86ISD::AESDECWIDE128KL:
      Opcode = X86::AESDECWIDE128KL;
      break;
    case X86ISD::AESENCWIDE256KL:
      Opcode = X86::AESENCWIDE256KL;
      break;
    case X86ISD::AESDECWIDE256KL:
      Opcode = X86::AESDECWIDE256KL;
      break;
    }

    SDValue Chain = Node->getOperand(0);
    SDValue Addr = Node->getOperand(1);

    SDValue Base, Scale, Index, Disp, Segment;
    if (!selectAddr(Node, Addr, Base, Scale, Index, Disp, Segment))
      break;

    Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM0, Node->getOperand(2),
                                 SDValue());
    Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM1, Node->getOperand(3),
                                 Chain.getValue(1));
    Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM2, Node->getOperand(4),
                                 Chain.getValue(1));
    Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM3, Node->getOperand(5),
                                 Chain.getValue(1));
    Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM4, Node->getOperand(6),
                                 Chain.getValue(1));
    Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM5, Node->getOperand(7),
                                 Chain.getValue(1));
    Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM6, Node->getOperand(8),
                                 Chain.getValue(1));
    Chain = CurDAG->getCopyToReg(Chain, dl, X86::XMM7, Node->getOperand(9),
                                 Chain.getValue(1));

    MachineSDNode *Res = CurDAG->getMachineNode(
        Opcode, dl, Node->getVTList(),
        {Base, Scale, Index, Disp, Segment, Chain, Chain.getValue(1)});
    CurDAG->setNodeMemRefs(Res, cast<MemSDNode>(Node)->getMemOperand());
    ReplaceNode(Node, Res);
    return;
  }
  }

  SelectCode(Node);
}

bool X86DAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  SDValue Op0, Op1, Op2, Op3, Op4;
  switch (ConstraintID) {
  default:
    llvm_unreachable("Unexpected asm memory constraint");
  case InlineAsm::ConstraintCode::o: // offsetable        ??
  case InlineAsm::ConstraintCode::v: // not offsetable    ??
  case InlineAsm::ConstraintCode::m: // memory
  case InlineAsm::ConstraintCode::X:
  case InlineAsm::ConstraintCode::p: // address
    if (!selectAddr(nullptr, Op, Op0, Op1, Op2, Op3, Op4))
      return true;
    break;
  }

  OutOps.push_back(Op0);
  OutOps.push_back(Op1);
  OutOps.push_back(Op2);
  OutOps.push_back(Op3);
  OutOps.push_back(Op4);
  return false;
}

X86ISelDAGToDAGPass::X86ISelDAGToDAGPass(X86TargetMachine &TM)
    : SelectionDAGISelPass(
          std::make_unique<X86DAGToDAGISel>(TM, TM.getOptLevel())) {}

/// This pass converts a legalized DAG into a X86-specific DAG,
/// ready for instruction scheduling.
FunctionPass *llvm::createX86ISelDag(X86TargetMachine &TM,
                                     CodeGenOptLevel OptLevel) {
  return new X86DAGToDAGISelLegacy(TM, OptLevel);
}
