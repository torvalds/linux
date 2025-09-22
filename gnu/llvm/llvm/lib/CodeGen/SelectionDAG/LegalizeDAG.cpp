//===- LegalizeDAG.cpp - Implement SelectionDAG::Legalize -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SelectionDAG::Legalize method.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cassert>
#include <cstdint>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "legalizedag"

namespace {

/// Keeps track of state when getting the sign of a floating-point value as an
/// integer.
struct FloatSignAsInt {
  EVT FloatVT;
  SDValue Chain;
  SDValue FloatPtr;
  SDValue IntPtr;
  MachinePointerInfo IntPointerInfo;
  MachinePointerInfo FloatPointerInfo;
  SDValue IntValue;
  APInt SignMask;
  uint8_t SignBit;
};

//===----------------------------------------------------------------------===//
/// This takes an arbitrary SelectionDAG as input and
/// hacks on it until the target machine can handle it.  This involves
/// eliminating value sizes the machine cannot handle (promoting small sizes to
/// large sizes or splitting up large values into small values) as well as
/// eliminating operations the machine cannot handle.
///
/// This code also does a small amount of optimization and recognition of idioms
/// as part of its processing.  For example, if a target does not support a
/// 'setcc' instruction efficiently, but does support 'brcc' instruction, this
/// will attempt merge setcc and brc instructions into brcc's.
class SelectionDAGLegalize {
  const TargetMachine &TM;
  const TargetLowering &TLI;
  SelectionDAG &DAG;

  /// The set of nodes which have already been legalized. We hold a
  /// reference to it in order to update as necessary on node deletion.
  SmallPtrSetImpl<SDNode *> &LegalizedNodes;

  /// A set of all the nodes updated during legalization.
  SmallSetVector<SDNode *, 16> *UpdatedNodes;

  EVT getSetCCResultType(EVT VT) const {
    return TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  }

  // Libcall insertion helpers.

public:
  SelectionDAGLegalize(SelectionDAG &DAG,
                       SmallPtrSetImpl<SDNode *> &LegalizedNodes,
                       SmallSetVector<SDNode *, 16> *UpdatedNodes = nullptr)
      : TM(DAG.getTarget()), TLI(DAG.getTargetLoweringInfo()), DAG(DAG),
        LegalizedNodes(LegalizedNodes), UpdatedNodes(UpdatedNodes) {}

  /// Legalizes the given operation.
  void LegalizeOp(SDNode *Node);

private:
  SDValue OptimizeFloatStore(StoreSDNode *ST);

  void LegalizeLoadOps(SDNode *Node);
  void LegalizeStoreOps(SDNode *Node);

  SDValue ExpandINSERT_VECTOR_ELT(SDValue Op);

  /// Return a vector shuffle operation which
  /// performs the same shuffe in terms of order or result bytes, but on a type
  /// whose vector element type is narrower than the original shuffle type.
  /// e.g. <v4i32> <0, 1, 0, 1> -> v8i16 <0, 1, 2, 3, 0, 1, 2, 3>
  SDValue ShuffleWithNarrowerEltType(EVT NVT, EVT VT, const SDLoc &dl,
                                     SDValue N1, SDValue N2,
                                     ArrayRef<int> Mask) const;

  std::pair<SDValue, SDValue> ExpandLibCall(RTLIB::Libcall LC, SDNode *Node,
                        TargetLowering::ArgListTy &&Args, bool isSigned);
  std::pair<SDValue, SDValue> ExpandLibCall(RTLIB::Libcall LC, SDNode *Node, bool isSigned);

  void ExpandFrexpLibCall(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandFPLibCall(SDNode *Node, RTLIB::Libcall LC,
                       SmallVectorImpl<SDValue> &Results);
  void ExpandFPLibCall(SDNode *Node, RTLIB::Libcall Call_F32,
                       RTLIB::Libcall Call_F64, RTLIB::Libcall Call_F80,
                       RTLIB::Libcall Call_F128,
                       RTLIB::Libcall Call_PPCF128,
                       SmallVectorImpl<SDValue> &Results);
  SDValue ExpandIntLibCall(SDNode *Node, bool isSigned,
                           RTLIB::Libcall Call_I8,
                           RTLIB::Libcall Call_I16,
                           RTLIB::Libcall Call_I32,
                           RTLIB::Libcall Call_I64,
                           RTLIB::Libcall Call_I128);
  void ExpandArgFPLibCall(SDNode *Node,
                          RTLIB::Libcall Call_F32, RTLIB::Libcall Call_F64,
                          RTLIB::Libcall Call_F80, RTLIB::Libcall Call_F128,
                          RTLIB::Libcall Call_PPCF128,
                          SmallVectorImpl<SDValue> &Results);
  void ExpandDivRemLibCall(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandSinCosLibCall(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  SDValue EmitStackConvert(SDValue SrcOp, EVT SlotVT, EVT DestVT,
                           const SDLoc &dl);
  SDValue EmitStackConvert(SDValue SrcOp, EVT SlotVT, EVT DestVT,
                           const SDLoc &dl, SDValue ChainIn);
  SDValue ExpandBUILD_VECTOR(SDNode *Node);
  SDValue ExpandSPLAT_VECTOR(SDNode *Node);
  SDValue ExpandSCALAR_TO_VECTOR(SDNode *Node);
  void ExpandDYNAMIC_STACKALLOC(SDNode *Node,
                                SmallVectorImpl<SDValue> &Results);
  void getSignAsIntValue(FloatSignAsInt &State, const SDLoc &DL,
                         SDValue Value) const;
  SDValue modifySignAsInt(const FloatSignAsInt &State, const SDLoc &DL,
                          SDValue NewIntValue) const;
  SDValue ExpandFCOPYSIGN(SDNode *Node) const;
  SDValue ExpandFABS(SDNode *Node) const;
  SDValue ExpandFNEG(SDNode *Node) const;
  SDValue expandLdexp(SDNode *Node) const;
  SDValue expandFrexp(SDNode *Node) const;

  SDValue ExpandLegalINT_TO_FP(SDNode *Node, SDValue &Chain);
  void PromoteLegalINT_TO_FP(SDNode *N, const SDLoc &dl,
                             SmallVectorImpl<SDValue> &Results);
  void PromoteLegalFP_TO_INT(SDNode *N, const SDLoc &dl,
                             SmallVectorImpl<SDValue> &Results);
  SDValue PromoteLegalFP_TO_INT_SAT(SDNode *Node, const SDLoc &dl);

  /// Implements vector reduce operation promotion.
  ///
  /// All vector operands are promoted to a vector type with larger element
  /// type, and the start value is promoted to a larger scalar type. Then the
  /// result is truncated back to the original scalar type.
  SDValue PromoteReduction(SDNode *Node);

  SDValue ExpandPARITY(SDValue Op, const SDLoc &dl);

  SDValue ExpandExtractFromVectorThroughStack(SDValue Op);
  SDValue ExpandInsertToVectorThroughStack(SDValue Op);
  SDValue ExpandVectorBuildThroughStack(SDNode* Node);

  SDValue ExpandConstantFP(ConstantFPSDNode *CFP, bool UseCP);
  SDValue ExpandConstant(ConstantSDNode *CP);

  // if ExpandNode returns false, LegalizeOp falls back to ConvertNodeToLibcall
  bool ExpandNode(SDNode *Node);
  void ConvertNodeToLibcall(SDNode *Node);
  void PromoteNode(SDNode *Node);

public:
  // Node replacement helpers

  void ReplacedNode(SDNode *N) {
    LegalizedNodes.erase(N);
    if (UpdatedNodes)
      UpdatedNodes->insert(N);
  }

  void ReplaceNode(SDNode *Old, SDNode *New) {
    LLVM_DEBUG(dbgs() << " ... replacing: "; Old->dump(&DAG);
               dbgs() << "     with:      "; New->dump(&DAG));

    assert(Old->getNumValues() == New->getNumValues() &&
           "Replacing one node with another that produces a different number "
           "of values!");
    DAG.ReplaceAllUsesWith(Old, New);
    if (UpdatedNodes)
      UpdatedNodes->insert(New);
    ReplacedNode(Old);
  }

  void ReplaceNode(SDValue Old, SDValue New) {
    LLVM_DEBUG(dbgs() << " ... replacing: "; Old->dump(&DAG);
               dbgs() << "     with:      "; New->dump(&DAG));

    DAG.ReplaceAllUsesWith(Old, New);
    if (UpdatedNodes)
      UpdatedNodes->insert(New.getNode());
    ReplacedNode(Old.getNode());
  }

  void ReplaceNode(SDNode *Old, const SDValue *New) {
    LLVM_DEBUG(dbgs() << " ... replacing: "; Old->dump(&DAG));

    DAG.ReplaceAllUsesWith(Old, New);
    for (unsigned i = 0, e = Old->getNumValues(); i != e; ++i) {
      LLVM_DEBUG(dbgs() << (i == 0 ? "     with:      " : "      and:      ");
                 New[i]->dump(&DAG));
      if (UpdatedNodes)
        UpdatedNodes->insert(New[i].getNode());
    }
    ReplacedNode(Old);
  }

  void ReplaceNodeWithValue(SDValue Old, SDValue New) {
    LLVM_DEBUG(dbgs() << " ... replacing: "; Old->dump(&DAG);
               dbgs() << "     with:      "; New->dump(&DAG));

    DAG.ReplaceAllUsesOfValueWith(Old, New);
    if (UpdatedNodes)
      UpdatedNodes->insert(New.getNode());
    ReplacedNode(Old.getNode());
  }
};

} // end anonymous namespace

// Helper function that generates an MMO that considers the alignment of the
// stack, and the size of the stack object
static MachineMemOperand *getStackAlignedMMO(SDValue StackPtr,
                                             MachineFunction &MF,
                                             bool isObjectScalable) {
  auto &MFI = MF.getFrameInfo();
  int FI = cast<FrameIndexSDNode>(StackPtr)->getIndex();
  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(MF, FI);
  LocationSize ObjectSize = isObjectScalable
                                ? LocationSize::beforeOrAfterPointer()
                                : LocationSize::precise(MFI.getObjectSize(FI));
  return MF.getMachineMemOperand(PtrInfo, MachineMemOperand::MOStore,
                                 ObjectSize, MFI.getObjectAlign(FI));
}

/// Return a vector shuffle operation which
/// performs the same shuffle in terms of order or result bytes, but on a type
/// whose vector element type is narrower than the original shuffle type.
/// e.g. <v4i32> <0, 1, 0, 1> -> v8i16 <0, 1, 2, 3, 0, 1, 2, 3>
SDValue SelectionDAGLegalize::ShuffleWithNarrowerEltType(
    EVT NVT, EVT VT, const SDLoc &dl, SDValue N1, SDValue N2,
    ArrayRef<int> Mask) const {
  unsigned NumMaskElts = VT.getVectorNumElements();
  unsigned NumDestElts = NVT.getVectorNumElements();
  unsigned NumEltsGrowth = NumDestElts / NumMaskElts;

  assert(NumEltsGrowth && "Cannot promote to vector type with fewer elts!");

  if (NumEltsGrowth == 1)
    return DAG.getVectorShuffle(NVT, dl, N1, N2, Mask);

  SmallVector<int, 8> NewMask;
  for (unsigned i = 0; i != NumMaskElts; ++i) {
    int Idx = Mask[i];
    for (unsigned j = 0; j != NumEltsGrowth; ++j) {
      if (Idx < 0)
        NewMask.push_back(-1);
      else
        NewMask.push_back(Idx * NumEltsGrowth + j);
    }
  }
  assert(NewMask.size() == NumDestElts && "Non-integer NumEltsGrowth?");
  assert(TLI.isShuffleMaskLegal(NewMask, NVT) && "Shuffle not legal?");
  return DAG.getVectorShuffle(NVT, dl, N1, N2, NewMask);
}

/// Expands the ConstantFP node to an integer constant or
/// a load from the constant pool.
SDValue
SelectionDAGLegalize::ExpandConstantFP(ConstantFPSDNode *CFP, bool UseCP) {
  bool Extend = false;
  SDLoc dl(CFP);

  // If a FP immediate is precise when represented as a float and if the
  // target can do an extending load from float to double, we put it into
  // the constant pool as a float, even if it's is statically typed as a
  // double.  This shrinks FP constants and canonicalizes them for targets where
  // an FP extending load is the same cost as a normal load (such as on the x87
  // fp stack or PPC FP unit).
  EVT VT = CFP->getValueType(0);
  ConstantFP *LLVMC = const_cast<ConstantFP*>(CFP->getConstantFPValue());
  if (!UseCP) {
    assert((VT == MVT::f64 || VT == MVT::f32) && "Invalid type expansion");
    return DAG.getConstant(LLVMC->getValueAPF().bitcastToAPInt(), dl,
                           (VT == MVT::f64) ? MVT::i64 : MVT::i32);
  }

  APFloat APF = CFP->getValueAPF();
  EVT OrigVT = VT;
  EVT SVT = VT;

  // We don't want to shrink SNaNs. Converting the SNaN back to its real type
  // can cause it to be changed into a QNaN on some platforms (e.g. on SystemZ).
  if (!APF.isSignaling()) {
    while (SVT != MVT::f32 && SVT != MVT::f16 && SVT != MVT::bf16) {
      SVT = (MVT::SimpleValueType)(SVT.getSimpleVT().SimpleTy - 1);
      if (ConstantFPSDNode::isValueValidForType(SVT, APF) &&
          // Only do this if the target has a native EXTLOAD instruction from
          // smaller type.
          TLI.isLoadExtLegal(ISD::EXTLOAD, OrigVT, SVT) &&
          TLI.ShouldShrinkFPConstant(OrigVT)) {
        Type *SType = SVT.getTypeForEVT(*DAG.getContext());
        LLVMC = cast<ConstantFP>(ConstantFoldCastOperand(
            Instruction::FPTrunc, LLVMC, SType, DAG.getDataLayout()));
        VT = SVT;
        Extend = true;
      }
    }
  }

  SDValue CPIdx =
      DAG.getConstantPool(LLVMC, TLI.getPointerTy(DAG.getDataLayout()));
  Align Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlign();
  if (Extend) {
    SDValue Result = DAG.getExtLoad(
        ISD::EXTLOAD, dl, OrigVT, DAG.getEntryNode(), CPIdx,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), VT,
        Alignment);
    return Result;
  }
  SDValue Result = DAG.getLoad(
      OrigVT, dl, DAG.getEntryNode(), CPIdx,
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), Alignment);
  return Result;
}

/// Expands the Constant node to a load from the constant pool.
SDValue SelectionDAGLegalize::ExpandConstant(ConstantSDNode *CP) {
  SDLoc dl(CP);
  EVT VT = CP->getValueType(0);
  SDValue CPIdx = DAG.getConstantPool(CP->getConstantIntValue(),
                                      TLI.getPointerTy(DAG.getDataLayout()));
  Align Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlign();
  SDValue Result = DAG.getLoad(
      VT, dl, DAG.getEntryNode(), CPIdx,
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), Alignment);
  return Result;
}

SDValue SelectionDAGLegalize::ExpandINSERT_VECTOR_ELT(SDValue Op) {
  SDValue Vec = Op.getOperand(0);
  SDValue Val = Op.getOperand(1);
  SDValue Idx = Op.getOperand(2);
  SDLoc dl(Op);

  if (ConstantSDNode *InsertPos = dyn_cast<ConstantSDNode>(Idx)) {
    // SCALAR_TO_VECTOR requires that the type of the value being inserted
    // match the element type of the vector being created, except for
    // integers in which case the inserted value can be over width.
    EVT EltVT = Vec.getValueType().getVectorElementType();
    if (Val.getValueType() == EltVT ||
        (EltVT.isInteger() && Val.getValueType().bitsGE(EltVT))) {
      SDValue ScVec = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl,
                                  Vec.getValueType(), Val);

      unsigned NumElts = Vec.getValueType().getVectorNumElements();
      // We generate a shuffle of InVec and ScVec, so the shuffle mask
      // should be 0,1,2,3,4,5... with the appropriate element replaced with
      // elt 0 of the RHS.
      SmallVector<int, 8> ShufOps;
      for (unsigned i = 0; i != NumElts; ++i)
        ShufOps.push_back(i != InsertPos->getZExtValue() ? i : NumElts);

      return DAG.getVectorShuffle(Vec.getValueType(), dl, Vec, ScVec, ShufOps);
    }
  }
  return ExpandInsertToVectorThroughStack(Op);
}

SDValue SelectionDAGLegalize::OptimizeFloatStore(StoreSDNode* ST) {
  if (!ISD::isNormalStore(ST))
    return SDValue();

  LLVM_DEBUG(dbgs() << "Optimizing float store operations\n");
  // Turn 'store float 1.0, Ptr' -> 'store int 0x12345678, Ptr'
  // FIXME: move this to the DAG Combiner!  Note that we can't regress due
  // to phase ordering between legalized code and the dag combiner.  This
  // probably means that we need to integrate dag combiner and legalizer
  // together.
  // We generally can't do this one for long doubles.
  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();
  SDValue Value = ST->getValue();
  MachineMemOperand::Flags MMOFlags = ST->getMemOperand()->getFlags();
  AAMDNodes AAInfo = ST->getAAInfo();
  SDLoc dl(ST);

  // Don't optimise TargetConstantFP
  if (Value.getOpcode() == ISD::TargetConstantFP)
    return SDValue();

  if (ConstantFPSDNode *CFP = dyn_cast<ConstantFPSDNode>(Value)) {
    if (CFP->getValueType(0) == MVT::f32 &&
        TLI.isTypeLegal(MVT::i32)) {
      SDValue Con = DAG.getConstant(CFP->getValueAPF().
                                      bitcastToAPInt().zextOrTrunc(32),
                                    SDLoc(CFP), MVT::i32);
      return DAG.getStore(Chain, dl, Con, Ptr, ST->getPointerInfo(),
                          ST->getOriginalAlign(), MMOFlags, AAInfo);
    }

    if (CFP->getValueType(0) == MVT::f64 &&
        !TLI.isFPImmLegal(CFP->getValueAPF(), MVT::f64)) {
      // If this target supports 64-bit registers, do a single 64-bit store.
      if (TLI.isTypeLegal(MVT::i64)) {
        SDValue Con = DAG.getConstant(CFP->getValueAPF().bitcastToAPInt().
                                      zextOrTrunc(64), SDLoc(CFP), MVT::i64);
        return DAG.getStore(Chain, dl, Con, Ptr, ST->getPointerInfo(),
                            ST->getOriginalAlign(), MMOFlags, AAInfo);
      }

      if (TLI.isTypeLegal(MVT::i32) && !ST->isVolatile()) {
        // Otherwise, if the target supports 32-bit registers, use 2 32-bit
        // stores.  If the target supports neither 32- nor 64-bits, this
        // xform is certainly not worth it.
        const APInt &IntVal = CFP->getValueAPF().bitcastToAPInt();
        SDValue Lo = DAG.getConstant(IntVal.trunc(32), dl, MVT::i32);
        SDValue Hi = DAG.getConstant(IntVal.lshr(32).trunc(32), dl, MVT::i32);
        if (DAG.getDataLayout().isBigEndian())
          std::swap(Lo, Hi);

        Lo = DAG.getStore(Chain, dl, Lo, Ptr, ST->getPointerInfo(),
                          ST->getOriginalAlign(), MMOFlags, AAInfo);
        Ptr = DAG.getMemBasePlusOffset(Ptr, TypeSize::getFixed(4), dl);
        Hi = DAG.getStore(Chain, dl, Hi, Ptr,
                          ST->getPointerInfo().getWithOffset(4),
                          ST->getOriginalAlign(), MMOFlags, AAInfo);

        return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo, Hi);
      }
    }
  }
  return SDValue();
}

void SelectionDAGLegalize::LegalizeStoreOps(SDNode *Node) {
  StoreSDNode *ST = cast<StoreSDNode>(Node);
  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();
  SDLoc dl(Node);

  MachineMemOperand::Flags MMOFlags = ST->getMemOperand()->getFlags();
  AAMDNodes AAInfo = ST->getAAInfo();

  if (!ST->isTruncatingStore()) {
    LLVM_DEBUG(dbgs() << "Legalizing store operation\n");
    if (SDNode *OptStore = OptimizeFloatStore(ST).getNode()) {
      ReplaceNode(ST, OptStore);
      return;
    }

    SDValue Value = ST->getValue();
    MVT VT = Value.getSimpleValueType();
    switch (TLI.getOperationAction(ISD::STORE, VT)) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Legal: {
      // If this is an unaligned store and the target doesn't support it,
      // expand it.
      EVT MemVT = ST->getMemoryVT();
      const DataLayout &DL = DAG.getDataLayout();
      if (!TLI.allowsMemoryAccessForAlignment(*DAG.getContext(), DL, MemVT,
                                              *ST->getMemOperand())) {
        LLVM_DEBUG(dbgs() << "Expanding unsupported unaligned store\n");
        SDValue Result = TLI.expandUnalignedStore(ST, DAG);
        ReplaceNode(SDValue(ST, 0), Result);
      } else
        LLVM_DEBUG(dbgs() << "Legal store\n");
      break;
    }
    case TargetLowering::Custom: {
      LLVM_DEBUG(dbgs() << "Trying custom lowering\n");
      SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG);
      if (Res && Res != SDValue(Node, 0))
        ReplaceNode(SDValue(Node, 0), Res);
      return;
    }
    case TargetLowering::Promote: {
      MVT NVT = TLI.getTypeToPromoteTo(ISD::STORE, VT);
      assert(NVT.getSizeInBits() == VT.getSizeInBits() &&
             "Can only promote stores to same size type");
      Value = DAG.getNode(ISD::BITCAST, dl, NVT, Value);
      SDValue Result = DAG.getStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                                    ST->getOriginalAlign(), MMOFlags, AAInfo);
      ReplaceNode(SDValue(Node, 0), Result);
      break;
    }
    }
    return;
  }

  LLVM_DEBUG(dbgs() << "Legalizing truncating store operations\n");
  SDValue Value = ST->getValue();
  EVT StVT = ST->getMemoryVT();
  TypeSize StWidth = StVT.getSizeInBits();
  TypeSize StSize = StVT.getStoreSizeInBits();
  auto &DL = DAG.getDataLayout();

  if (StWidth != StSize) {
    // Promote to a byte-sized store with upper bits zero if not
    // storing an integral number of bytes.  For example, promote
    // TRUNCSTORE:i1 X -> TRUNCSTORE:i8 (and X, 1)
    EVT NVT = EVT::getIntegerVT(*DAG.getContext(), StSize.getFixedValue());
    Value = DAG.getZeroExtendInReg(Value, dl, StVT);
    SDValue Result =
        DAG.getTruncStore(Chain, dl, Value, Ptr, ST->getPointerInfo(), NVT,
                          ST->getOriginalAlign(), MMOFlags, AAInfo);
    ReplaceNode(SDValue(Node, 0), Result);
  } else if (!StVT.isVector() && !isPowerOf2_64(StWidth.getFixedValue())) {
    // If not storing a power-of-2 number of bits, expand as two stores.
    assert(!StVT.isVector() && "Unsupported truncstore!");
    unsigned StWidthBits = StWidth.getFixedValue();
    unsigned LogStWidth = Log2_32(StWidthBits);
    assert(LogStWidth < 32);
    unsigned RoundWidth = 1 << LogStWidth;
    assert(RoundWidth < StWidthBits);
    unsigned ExtraWidth = StWidthBits - RoundWidth;
    assert(ExtraWidth < RoundWidth);
    assert(!(RoundWidth % 8) && !(ExtraWidth % 8) &&
           "Store size not an integral number of bytes!");
    EVT RoundVT = EVT::getIntegerVT(*DAG.getContext(), RoundWidth);
    EVT ExtraVT = EVT::getIntegerVT(*DAG.getContext(), ExtraWidth);
    SDValue Lo, Hi;
    unsigned IncrementSize;

    if (DL.isLittleEndian()) {
      // TRUNCSTORE:i24 X -> TRUNCSTORE:i16 X, TRUNCSTORE@+2:i8 (srl X, 16)
      // Store the bottom RoundWidth bits.
      Lo = DAG.getTruncStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                             RoundVT, ST->getOriginalAlign(), MMOFlags, AAInfo);

      // Store the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr =
          DAG.getMemBasePlusOffset(Ptr, TypeSize::getFixed(IncrementSize), dl);
      Hi = DAG.getNode(
          ISD::SRL, dl, Value.getValueType(), Value,
          DAG.getConstant(RoundWidth, dl,
                          TLI.getShiftAmountTy(Value.getValueType(), DL)));
      Hi = DAG.getTruncStore(Chain, dl, Hi, Ptr,
                             ST->getPointerInfo().getWithOffset(IncrementSize),
                             ExtraVT, ST->getOriginalAlign(), MMOFlags, AAInfo);
    } else {
      // Big endian - avoid unaligned stores.
      // TRUNCSTORE:i24 X -> TRUNCSTORE:i16 (srl X, 8), TRUNCSTORE@+2:i8 X
      // Store the top RoundWidth bits.
      Hi = DAG.getNode(
          ISD::SRL, dl, Value.getValueType(), Value,
          DAG.getConstant(ExtraWidth, dl,
                          TLI.getShiftAmountTy(Value.getValueType(), DL)));
      Hi = DAG.getTruncStore(Chain, dl, Hi, Ptr, ST->getPointerInfo(), RoundVT,
                             ST->getOriginalAlign(), MMOFlags, AAInfo);

      // Store the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                        DAG.getConstant(IncrementSize, dl,
                                        Ptr.getValueType()));
      Lo = DAG.getTruncStore(Chain, dl, Value, Ptr,
                             ST->getPointerInfo().getWithOffset(IncrementSize),
                             ExtraVT, ST->getOriginalAlign(), MMOFlags, AAInfo);
    }

    // The order of the stores doesn't matter.
    SDValue Result = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo, Hi);
    ReplaceNode(SDValue(Node, 0), Result);
  } else {
    switch (TLI.getTruncStoreAction(ST->getValue().getValueType(), StVT)) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Legal: {
      EVT MemVT = ST->getMemoryVT();
      // If this is an unaligned store and the target doesn't support it,
      // expand it.
      if (!TLI.allowsMemoryAccessForAlignment(*DAG.getContext(), DL, MemVT,
                                              *ST->getMemOperand())) {
        SDValue Result = TLI.expandUnalignedStore(ST, DAG);
        ReplaceNode(SDValue(ST, 0), Result);
      }
      break;
    }
    case TargetLowering::Custom: {
      SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG);
      if (Res && Res != SDValue(Node, 0))
        ReplaceNode(SDValue(Node, 0), Res);
      return;
    }
    case TargetLowering::Expand:
      assert(!StVT.isVector() &&
             "Vector Stores are handled in LegalizeVectorOps");

      SDValue Result;

      // TRUNCSTORE:i16 i32 -> STORE i16
      if (TLI.isTypeLegal(StVT)) {
        Value = DAG.getNode(ISD::TRUNCATE, dl, StVT, Value);
        Result = DAG.getStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                              ST->getOriginalAlign(), MMOFlags, AAInfo);
      } else {
        // The in-memory type isn't legal. Truncate to the type it would promote
        // to, and then do a truncstore.
        Value = DAG.getNode(ISD::TRUNCATE, dl,
                            TLI.getTypeToTransformTo(*DAG.getContext(), StVT),
                            Value);
        Result =
            DAG.getTruncStore(Chain, dl, Value, Ptr, ST->getPointerInfo(), StVT,
                              ST->getOriginalAlign(), MMOFlags, AAInfo);
      }

      ReplaceNode(SDValue(Node, 0), Result);
      break;
    }
  }
}

void SelectionDAGLegalize::LegalizeLoadOps(SDNode *Node) {
  LoadSDNode *LD = cast<LoadSDNode>(Node);
  SDValue Chain = LD->getChain();  // The chain.
  SDValue Ptr = LD->getBasePtr();  // The base pointer.
  SDValue Value;                   // The value returned by the load op.
  SDLoc dl(Node);

  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD) {
    LLVM_DEBUG(dbgs() << "Legalizing non-extending load operation\n");
    MVT VT = Node->getSimpleValueType(0);
    SDValue RVal = SDValue(Node, 0);
    SDValue RChain = SDValue(Node, 1);

    switch (TLI.getOperationAction(Node->getOpcode(), VT)) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Legal: {
      EVT MemVT = LD->getMemoryVT();
      const DataLayout &DL = DAG.getDataLayout();
      // If this is an unaligned load and the target doesn't support it,
      // expand it.
      if (!TLI.allowsMemoryAccessForAlignment(*DAG.getContext(), DL, MemVT,
                                              *LD->getMemOperand())) {
        std::tie(RVal, RChain) = TLI.expandUnalignedLoad(LD, DAG);
      }
      break;
    }
    case TargetLowering::Custom:
      if (SDValue Res = TLI.LowerOperation(RVal, DAG)) {
        RVal = Res;
        RChain = Res.getValue(1);
      }
      break;

    case TargetLowering::Promote: {
      MVT NVT = TLI.getTypeToPromoteTo(Node->getOpcode(), VT);
      assert(NVT.getSizeInBits() == VT.getSizeInBits() &&
             "Can only promote loads to same size type");

      SDValue Res = DAG.getLoad(NVT, dl, Chain, Ptr, LD->getMemOperand());
      RVal = DAG.getNode(ISD::BITCAST, dl, VT, Res);
      RChain = Res.getValue(1);
      break;
    }
    }
    if (RChain.getNode() != Node) {
      assert(RVal.getNode() != Node && "Load must be completely replaced");
      DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 0), RVal);
      DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), RChain);
      if (UpdatedNodes) {
        UpdatedNodes->insert(RVal.getNode());
        UpdatedNodes->insert(RChain.getNode());
      }
      ReplacedNode(Node);
    }
    return;
  }

  LLVM_DEBUG(dbgs() << "Legalizing extending load operation\n");
  EVT SrcVT = LD->getMemoryVT();
  TypeSize SrcWidth = SrcVT.getSizeInBits();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  if (SrcWidth != SrcVT.getStoreSizeInBits() &&
      // Some targets pretend to have an i1 loading operation, and actually
      // load an i8.  This trick is correct for ZEXTLOAD because the top 7
      // bits are guaranteed to be zero; it helps the optimizers understand
      // that these bits are zero.  It is also useful for EXTLOAD, since it
      // tells the optimizers that those bits are undefined.  It would be
      // nice to have an effective generic way of getting these benefits...
      // Until such a way is found, don't insist on promoting i1 here.
      (SrcVT != MVT::i1 ||
       TLI.getLoadExtAction(ExtType, Node->getValueType(0), MVT::i1) ==
         TargetLowering::Promote)) {
    // Promote to a byte-sized load if not loading an integral number of
    // bytes.  For example, promote EXTLOAD:i20 -> EXTLOAD:i24.
    unsigned NewWidth = SrcVT.getStoreSizeInBits();
    EVT NVT = EVT::getIntegerVT(*DAG.getContext(), NewWidth);
    SDValue Ch;

    // The extra bits are guaranteed to be zero, since we stored them that
    // way.  A zext load from NVT thus automatically gives zext from SrcVT.

    ISD::LoadExtType NewExtType =
      ExtType == ISD::ZEXTLOAD ? ISD::ZEXTLOAD : ISD::EXTLOAD;

    SDValue Result = DAG.getExtLoad(NewExtType, dl, Node->getValueType(0),
                                    Chain, Ptr, LD->getPointerInfo(), NVT,
                                    LD->getOriginalAlign(), MMOFlags, AAInfo);

    Ch = Result.getValue(1); // The chain.

    if (ExtType == ISD::SEXTLOAD)
      // Having the top bits zero doesn't help when sign extending.
      Result = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl,
                           Result.getValueType(),
                           Result, DAG.getValueType(SrcVT));
    else if (ExtType == ISD::ZEXTLOAD || NVT == Result.getValueType())
      // All the top bits are guaranteed to be zero - inform the optimizers.
      Result = DAG.getNode(ISD::AssertZext, dl,
                           Result.getValueType(), Result,
                           DAG.getValueType(SrcVT));

    Value = Result;
    Chain = Ch;
  } else if (!isPowerOf2_64(SrcWidth.getKnownMinValue())) {
    // If not loading a power-of-2 number of bits, expand as two loads.
    assert(!SrcVT.isVector() && "Unsupported extload!");
    unsigned SrcWidthBits = SrcWidth.getFixedValue();
    unsigned LogSrcWidth = Log2_32(SrcWidthBits);
    assert(LogSrcWidth < 32);
    unsigned RoundWidth = 1 << LogSrcWidth;
    assert(RoundWidth < SrcWidthBits);
    unsigned ExtraWidth = SrcWidthBits - RoundWidth;
    assert(ExtraWidth < RoundWidth);
    assert(!(RoundWidth % 8) && !(ExtraWidth % 8) &&
           "Load size not an integral number of bytes!");
    EVT RoundVT = EVT::getIntegerVT(*DAG.getContext(), RoundWidth);
    EVT ExtraVT = EVT::getIntegerVT(*DAG.getContext(), ExtraWidth);
    SDValue Lo, Hi, Ch;
    unsigned IncrementSize;
    auto &DL = DAG.getDataLayout();

    if (DL.isLittleEndian()) {
      // EXTLOAD:i24 -> ZEXTLOAD:i16 | (shl EXTLOAD@+2:i8, 16)
      // Load the bottom RoundWidth bits.
      Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo(), RoundVT, LD->getOriginalAlign(),
                          MMOFlags, AAInfo);

      // Load the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr =
          DAG.getMemBasePlusOffset(Ptr, TypeSize::getFixed(IncrementSize), dl);
      Hi = DAG.getExtLoad(ExtType, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo().getWithOffset(IncrementSize),
                          ExtraVT, LD->getOriginalAlign(), MMOFlags, AAInfo);

      // Build a factor node to remember that this load is independent of
      // the other one.
      Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                       Hi.getValue(1));

      // Move the top bits to the right place.
      Hi = DAG.getNode(
          ISD::SHL, dl, Hi.getValueType(), Hi,
          DAG.getConstant(RoundWidth, dl,
                          TLI.getShiftAmountTy(Hi.getValueType(), DL)));

      // Join the hi and lo parts.
      Value = DAG.getNode(ISD::OR, dl, Node->getValueType(0), Lo, Hi);
    } else {
      // Big endian - avoid unaligned loads.
      // EXTLOAD:i24 -> (shl EXTLOAD:i16, 8) | ZEXTLOAD@+2:i8
      // Load the top RoundWidth bits.
      Hi = DAG.getExtLoad(ExtType, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo(), RoundVT, LD->getOriginalAlign(),
                          MMOFlags, AAInfo);

      // Load the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr =
          DAG.getMemBasePlusOffset(Ptr, TypeSize::getFixed(IncrementSize), dl);
      Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo().getWithOffset(IncrementSize),
                          ExtraVT, LD->getOriginalAlign(), MMOFlags, AAInfo);

      // Build a factor node to remember that this load is independent of
      // the other one.
      Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                       Hi.getValue(1));

      // Move the top bits to the right place.
      Hi = DAG.getNode(
          ISD::SHL, dl, Hi.getValueType(), Hi,
          DAG.getConstant(ExtraWidth, dl,
                          TLI.getShiftAmountTy(Hi.getValueType(), DL)));

      // Join the hi and lo parts.
      Value = DAG.getNode(ISD::OR, dl, Node->getValueType(0), Lo, Hi);
    }

    Chain = Ch;
  } else {
    bool isCustom = false;
    switch (TLI.getLoadExtAction(ExtType, Node->getValueType(0),
                                 SrcVT.getSimpleVT())) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Custom:
      isCustom = true;
      [[fallthrough]];
    case TargetLowering::Legal:
      Value = SDValue(Node, 0);
      Chain = SDValue(Node, 1);

      if (isCustom) {
        if (SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG)) {
          Value = Res;
          Chain = Res.getValue(1);
        }
      } else {
        // If this is an unaligned load and the target doesn't support it,
        // expand it.
        EVT MemVT = LD->getMemoryVT();
        const DataLayout &DL = DAG.getDataLayout();
        if (!TLI.allowsMemoryAccess(*DAG.getContext(), DL, MemVT,
                                    *LD->getMemOperand())) {
          std::tie(Value, Chain) = TLI.expandUnalignedLoad(LD, DAG);
        }
      }
      break;

    case TargetLowering::Expand: {
      EVT DestVT = Node->getValueType(0);
      if (!TLI.isLoadExtLegal(ISD::EXTLOAD, DestVT, SrcVT)) {
        // If the source type is not legal, see if there is a legal extload to
        // an intermediate type that we can then extend further.
        EVT LoadVT = TLI.getRegisterType(SrcVT.getSimpleVT());
        if ((LoadVT.isFloatingPoint() == SrcVT.isFloatingPoint()) &&
            (TLI.isTypeLegal(SrcVT) || // Same as SrcVT == LoadVT?
             TLI.isLoadExtLegal(ExtType, LoadVT, SrcVT))) {
          // If we are loading a legal type, this is a non-extload followed by a
          // full extend.
          ISD::LoadExtType MidExtType =
              (LoadVT == SrcVT) ? ISD::NON_EXTLOAD : ExtType;

          SDValue Load = DAG.getExtLoad(MidExtType, dl, LoadVT, Chain, Ptr,
                                        SrcVT, LD->getMemOperand());
          unsigned ExtendOp =
              ISD::getExtForLoadExtType(SrcVT.isFloatingPoint(), ExtType);
          Value = DAG.getNode(ExtendOp, dl, Node->getValueType(0), Load);
          Chain = Load.getValue(1);
          break;
        }

        // Handle the special case of fp16 extloads. EXTLOAD doesn't have the
        // normal undefined upper bits behavior to allow using an in-reg extend
        // with the illegal FP type, so load as an integer and do the
        // from-integer conversion.
        EVT SVT = SrcVT.getScalarType();
        if (SVT == MVT::f16 || SVT == MVT::bf16) {
          EVT ISrcVT = SrcVT.changeTypeToInteger();
          EVT IDestVT = DestVT.changeTypeToInteger();
          EVT ILoadVT = TLI.getRegisterType(IDestVT.getSimpleVT());

          SDValue Result = DAG.getExtLoad(ISD::ZEXTLOAD, dl, ILoadVT, Chain,
                                          Ptr, ISrcVT, LD->getMemOperand());
          Value =
              DAG.getNode(SVT == MVT::f16 ? ISD::FP16_TO_FP : ISD::BF16_TO_FP,
                          dl, DestVT, Result);
          Chain = Result.getValue(1);
          break;
        }
      }

      assert(!SrcVT.isVector() &&
             "Vector Loads are handled in LegalizeVectorOps");

      // FIXME: This does not work for vectors on most targets.  Sign-
      // and zero-extend operations are currently folded into extending
      // loads, whether they are legal or not, and then we end up here
      // without any support for legalizing them.
      assert(ExtType != ISD::EXTLOAD &&
             "EXTLOAD should always be supported!");
      // Turn the unsupported load into an EXTLOAD followed by an
      // explicit zero/sign extend inreg.
      SDValue Result = DAG.getExtLoad(ISD::EXTLOAD, dl,
                                      Node->getValueType(0),
                                      Chain, Ptr, SrcVT,
                                      LD->getMemOperand());
      SDValue ValRes;
      if (ExtType == ISD::SEXTLOAD)
        ValRes = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl,
                             Result.getValueType(),
                             Result, DAG.getValueType(SrcVT));
      else
        ValRes = DAG.getZeroExtendInReg(Result, dl, SrcVT);
      Value = ValRes;
      Chain = Result.getValue(1);
      break;
    }
    }
  }

  // Since loads produce two values, make sure to remember that we legalized
  // both of them.
  if (Chain.getNode() != Node) {
    assert(Value.getNode() != Node && "Load must be completely replaced");
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 0), Value);
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), Chain);
    if (UpdatedNodes) {
      UpdatedNodes->insert(Value.getNode());
      UpdatedNodes->insert(Chain.getNode());
    }
    ReplacedNode(Node);
  }
}

/// Return a legal replacement for the given operation, with all legal operands.
void SelectionDAGLegalize::LegalizeOp(SDNode *Node) {
  LLVM_DEBUG(dbgs() << "\nLegalizing: "; Node->dump(&DAG));

  // Allow illegal target nodes and illegal registers.
  if (Node->getOpcode() == ISD::TargetConstant ||
      Node->getOpcode() == ISD::Register)
    return;

#ifndef NDEBUG
  for (unsigned i = 0, e = Node->getNumValues(); i != e; ++i)
    assert(TLI.getTypeAction(*DAG.getContext(), Node->getValueType(i)) ==
             TargetLowering::TypeLegal &&
           "Unexpected illegal type!");

  for (const SDValue &Op : Node->op_values())
    assert((TLI.getTypeAction(*DAG.getContext(), Op.getValueType()) ==
              TargetLowering::TypeLegal ||
            Op.getOpcode() == ISD::TargetConstant ||
            Op.getOpcode() == ISD::Register) &&
            "Unexpected illegal type!");
#endif

  // Figure out the correct action; the way to query this varies by opcode
  TargetLowering::LegalizeAction Action = TargetLowering::Legal;
  bool SimpleFinishLegalizing = true;
  switch (Node->getOpcode()) {
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_WO_CHAIN:
  case ISD::INTRINSIC_VOID:
  case ISD::STACKSAVE:
    Action = TLI.getOperationAction(Node->getOpcode(), MVT::Other);
    break;
  case ISD::GET_DYNAMIC_AREA_OFFSET:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getValueType(0));
    break;
  case ISD::VAARG:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getValueType(0));
    if (Action != TargetLowering::Promote)
      Action = TLI.getOperationAction(Node->getOpcode(), MVT::Other);
    break;
  case ISD::SET_FPENV:
  case ISD::SET_FPMODE:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(1).getValueType());
    break;
  case ISD::FP_TO_FP16:
  case ISD::FP_TO_BF16:
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::EXTRACT_VECTOR_ELT:
  case ISD::LROUND:
  case ISD::LLROUND:
  case ISD::LRINT:
  case ISD::LLRINT:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(0).getValueType());
    break;
  case ISD::STRICT_FP_TO_FP16:
  case ISD::STRICT_FP_TO_BF16:
  case ISD::STRICT_SINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
  case ISD::STRICT_LRINT:
  case ISD::STRICT_LLRINT:
  case ISD::STRICT_LROUND:
  case ISD::STRICT_LLROUND:
    // These pseudo-ops are the same as the other STRICT_ ops except
    // they are registered with setOperationAction() using the input type
    // instead of the output type.
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(1).getValueType());
    break;
  case ISD::SIGN_EXTEND_INREG: {
    EVT InnerType = cast<VTSDNode>(Node->getOperand(1))->getVT();
    Action = TLI.getOperationAction(Node->getOpcode(), InnerType);
    break;
  }
  case ISD::ATOMIC_STORE:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(1).getValueType());
    break;
  case ISD::SELECT_CC:
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:
  case ISD::SETCC:
  case ISD::SETCCCARRY:
  case ISD::VP_SETCC:
  case ISD::BR_CC: {
    unsigned Opc = Node->getOpcode();
    unsigned CCOperand = Opc == ISD::SELECT_CC                         ? 4
                         : Opc == ISD::STRICT_FSETCC                   ? 3
                         : Opc == ISD::STRICT_FSETCCS                  ? 3
                         : Opc == ISD::SETCCCARRY                      ? 3
                         : (Opc == ISD::SETCC || Opc == ISD::VP_SETCC) ? 2
                                                                       : 1;
    unsigned CompareOperand = Opc == ISD::BR_CC            ? 2
                              : Opc == ISD::STRICT_FSETCC  ? 1
                              : Opc == ISD::STRICT_FSETCCS ? 1
                                                           : 0;
    MVT OpVT = Node->getOperand(CompareOperand).getSimpleValueType();
    ISD::CondCode CCCode =
        cast<CondCodeSDNode>(Node->getOperand(CCOperand))->get();
    Action = TLI.getCondCodeAction(CCCode, OpVT);
    if (Action == TargetLowering::Legal) {
      if (Node->getOpcode() == ISD::SELECT_CC)
        Action = TLI.getOperationAction(Node->getOpcode(),
                                        Node->getValueType(0));
      else
        Action = TLI.getOperationAction(Node->getOpcode(), OpVT);
    }
    break;
  }
  case ISD::LOAD:
  case ISD::STORE:
    // FIXME: Model these properly.  LOAD and STORE are complicated, and
    // STORE expects the unlegalized operand in some cases.
    SimpleFinishLegalizing = false;
    break;
  case ISD::CALLSEQ_START:
  case ISD::CALLSEQ_END:
    // FIXME: This shouldn't be necessary.  These nodes have special properties
    // dealing with the recursive nature of legalization.  Removing this
    // special case should be done as part of making LegalizeDAG non-recursive.
    SimpleFinishLegalizing = false;
    break;
  case ISD::EXTRACT_ELEMENT:
  case ISD::GET_ROUNDING:
  case ISD::MERGE_VALUES:
  case ISD::EH_RETURN:
  case ISD::FRAME_TO_ARGS_OFFSET:
  case ISD::EH_DWARF_CFA:
  case ISD::EH_SJLJ_SETJMP:
  case ISD::EH_SJLJ_LONGJMP:
  case ISD::EH_SJLJ_SETUP_DISPATCH:
    // These operations lie about being legal: when they claim to be legal,
    // they should actually be expanded.
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Legal)
      Action = TargetLowering::Expand;
    break;
  case ISD::INIT_TRAMPOLINE:
  case ISD::ADJUST_TRAMPOLINE:
  case ISD::FRAMEADDR:
  case ISD::RETURNADDR:
  case ISD::ADDROFRETURNADDR:
  case ISD::SPONENTRY:
    // These operations lie about being legal: when they claim to be legal,
    // they should actually be custom-lowered.
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Legal)
      Action = TargetLowering::Custom;
    break;
  case ISD::CLEAR_CACHE:
    // This operation is typically going to be LibCall unless the target wants
    // something differrent.
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    break;
  case ISD::READCYCLECOUNTER:
  case ISD::READSTEADYCOUNTER:
    // READCYCLECOUNTER and READSTEADYCOUNTER return a i64, even if type
    // legalization might have expanded that to several smaller types.
    Action = TLI.getOperationAction(Node->getOpcode(), MVT::i64);
    break;
  case ISD::READ_REGISTER:
  case ISD::WRITE_REGISTER:
    // Named register is legal in the DAG, but blocked by register name
    // selection if not implemented by target (to chose the correct register)
    // They'll be converted to Copy(To/From)Reg.
    Action = TargetLowering::Legal;
    break;
  case ISD::UBSANTRAP:
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Expand) {
      // replace ISD::UBSANTRAP with ISD::TRAP
      SDValue NewVal;
      NewVal = DAG.getNode(ISD::TRAP, SDLoc(Node), Node->getVTList(),
                           Node->getOperand(0));
      ReplaceNode(Node, NewVal.getNode());
      LegalizeOp(NewVal.getNode());
      return;
    }
    break;
  case ISD::DEBUGTRAP:
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Expand) {
      // replace ISD::DEBUGTRAP with ISD::TRAP
      SDValue NewVal;
      NewVal = DAG.getNode(ISD::TRAP, SDLoc(Node), Node->getVTList(),
                           Node->getOperand(0));
      ReplaceNode(Node, NewVal.getNode());
      LegalizeOp(NewVal.getNode());
      return;
    }
    break;
  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT:
  case ISD::SSHLSAT:
  case ISD::USHLSAT:
  case ISD::SCMP:
  case ISD::UCMP:
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    break;
  case ISD::SMULFIX:
  case ISD::SMULFIXSAT:
  case ISD::UMULFIX:
  case ISD::UMULFIXSAT:
  case ISD::SDIVFIX:
  case ISD::SDIVFIXSAT:
  case ISD::UDIVFIX:
  case ISD::UDIVFIXSAT: {
    unsigned Scale = Node->getConstantOperandVal(2);
    Action = TLI.getFixedPointOperationAction(Node->getOpcode(),
                                              Node->getValueType(0), Scale);
    break;
  }
  case ISD::MSCATTER:
    Action = TLI.getOperationAction(Node->getOpcode(),
                    cast<MaskedScatterSDNode>(Node)->getValue().getValueType());
    break;
  case ISD::MSTORE:
    Action = TLI.getOperationAction(Node->getOpcode(),
                    cast<MaskedStoreSDNode>(Node)->getValue().getValueType());
    break;
  case ISD::VP_SCATTER:
    Action = TLI.getOperationAction(
        Node->getOpcode(),
        cast<VPScatterSDNode>(Node)->getValue().getValueType());
    break;
  case ISD::VP_STORE:
    Action = TLI.getOperationAction(
        Node->getOpcode(),
        cast<VPStoreSDNode>(Node)->getValue().getValueType());
    break;
  case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
    Action = TLI.getOperationAction(
        Node->getOpcode(),
        cast<VPStridedStoreSDNode>(Node)->getValue().getValueType());
    break;
  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_FMUL:
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMINIMUM:
  case ISD::IS_FPCLASS:
    Action = TLI.getOperationAction(
        Node->getOpcode(), Node->getOperand(0).getValueType());
    break;
  case ISD::VECREDUCE_SEQ_FADD:
  case ISD::VECREDUCE_SEQ_FMUL:
  case ISD::VP_REDUCE_FADD:
  case ISD::VP_REDUCE_FMUL:
  case ISD::VP_REDUCE_ADD:
  case ISD::VP_REDUCE_MUL:
  case ISD::VP_REDUCE_AND:
  case ISD::VP_REDUCE_OR:
  case ISD::VP_REDUCE_XOR:
  case ISD::VP_REDUCE_SMAX:
  case ISD::VP_REDUCE_SMIN:
  case ISD::VP_REDUCE_UMAX:
  case ISD::VP_REDUCE_UMIN:
  case ISD::VP_REDUCE_FMAX:
  case ISD::VP_REDUCE_FMIN:
  case ISD::VP_REDUCE_FMAXIMUM:
  case ISD::VP_REDUCE_FMINIMUM:
  case ISD::VP_REDUCE_SEQ_FADD:
  case ISD::VP_REDUCE_SEQ_FMUL:
    Action = TLI.getOperationAction(
        Node->getOpcode(), Node->getOperand(1).getValueType());
    break;
  case ISD::VP_CTTZ_ELTS:
  case ISD::VP_CTTZ_ELTS_ZERO_UNDEF:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(0).getValueType());
    break;
  default:
    if (Node->getOpcode() >= ISD::BUILTIN_OP_END) {
      Action = TLI.getCustomOperationAction(*Node);
    } else {
      Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    }
    break;
  }

  if (SimpleFinishLegalizing) {
    SDNode *NewNode = Node;
    switch (Node->getOpcode()) {
    default: break;
    case ISD::SHL:
    case ISD::SRL:
    case ISD::SRA:
    case ISD::ROTL:
    case ISD::ROTR: {
      // Legalizing shifts/rotates requires adjusting the shift amount
      // to the appropriate width.
      SDValue Op0 = Node->getOperand(0);
      SDValue Op1 = Node->getOperand(1);
      if (!Op1.getValueType().isVector()) {
        SDValue SAO = DAG.getShiftAmountOperand(Op0.getValueType(), Op1);
        // The getShiftAmountOperand() may create a new operand node or
        // return the existing one. If new operand is created we need
        // to update the parent node.
        // Do not try to legalize SAO here! It will be automatically legalized
        // in the next round.
        if (SAO != Op1)
          NewNode = DAG.UpdateNodeOperands(Node, Op0, SAO);
      }
    }
    break;
    case ISD::FSHL:
    case ISD::FSHR:
    case ISD::SRL_PARTS:
    case ISD::SRA_PARTS:
    case ISD::SHL_PARTS: {
      // Legalizing shifts/rotates requires adjusting the shift amount
      // to the appropriate width.
      SDValue Op0 = Node->getOperand(0);
      SDValue Op1 = Node->getOperand(1);
      SDValue Op2 = Node->getOperand(2);
      if (!Op2.getValueType().isVector()) {
        SDValue SAO = DAG.getShiftAmountOperand(Op0.getValueType(), Op2);
        // The getShiftAmountOperand() may create a new operand node or
        // return the existing one. If new operand is created we need
        // to update the parent node.
        if (SAO != Op2)
          NewNode = DAG.UpdateNodeOperands(Node, Op0, Op1, SAO);
      }
      break;
    }
    }

    if (NewNode != Node) {
      ReplaceNode(Node, NewNode);
      Node = NewNode;
    }
    switch (Action) {
    case TargetLowering::Legal:
      LLVM_DEBUG(dbgs() << "Legal node: nothing to do\n");
      return;
    case TargetLowering::Custom:
      LLVM_DEBUG(dbgs() << "Trying custom legalization\n");
      // FIXME: The handling for custom lowering with multiple results is
      // a complete mess.
      if (SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG)) {
        if (!(Res.getNode() != Node || Res.getResNo() != 0))
          return;

        if (Node->getNumValues() == 1) {
          // Verify the new types match the original. Glue is waived because
          // ISD::ADDC can be legalized by replacing Glue with an integer type.
          assert((Res.getValueType() == Node->getValueType(0) ||
                  Node->getValueType(0) == MVT::Glue) &&
                 "Type mismatch for custom legalized operation");
          LLVM_DEBUG(dbgs() << "Successfully custom legalized node\n");
          // We can just directly replace this node with the lowered value.
          ReplaceNode(SDValue(Node, 0), Res);
          return;
        }

        SmallVector<SDValue, 8> ResultVals;
        for (unsigned i = 0, e = Node->getNumValues(); i != e; ++i) {
          // Verify the new types match the original. Glue is waived because
          // ISD::ADDC can be legalized by replacing Glue with an integer type.
          assert((Res->getValueType(i) == Node->getValueType(i) ||
                  Node->getValueType(i) == MVT::Glue) &&
                 "Type mismatch for custom legalized operation");
          ResultVals.push_back(Res.getValue(i));
        }
        LLVM_DEBUG(dbgs() << "Successfully custom legalized node\n");
        ReplaceNode(Node, ResultVals.data());
        return;
      }
      LLVM_DEBUG(dbgs() << "Could not custom legalize node\n");
      [[fallthrough]];
    case TargetLowering::Expand:
      if (ExpandNode(Node))
        return;
      [[fallthrough]];
    case TargetLowering::LibCall:
      ConvertNodeToLibcall(Node);
      return;
    case TargetLowering::Promote:
      PromoteNode(Node);
      return;
    }
  }

  switch (Node->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "NODE: ";
    Node->dump( &DAG);
    dbgs() << "\n";
#endif
    llvm_unreachable("Do not know how to legalize this operator!");

  case ISD::CALLSEQ_START:
  case ISD::CALLSEQ_END:
    break;
  case ISD::LOAD:
    return LegalizeLoadOps(Node);
  case ISD::STORE:
    return LegalizeStoreOps(Node);
  }
}

SDValue SelectionDAGLegalize::ExpandExtractFromVectorThroughStack(SDValue Op) {
  SDValue Vec = Op.getOperand(0);
  SDValue Idx = Op.getOperand(1);
  SDLoc dl(Op);

  // Before we generate a new store to a temporary stack slot, see if there is
  // already one that we can use. There often is because when we scalarize
  // vector operations (using SelectionDAG::UnrollVectorOp for example) a whole
  // series of EXTRACT_VECTOR_ELT nodes are generated, one for each element in
  // the vector. If all are expanded here, we don't want one store per vector
  // element.

  // Caches for hasPredecessorHelper
  SmallPtrSet<const SDNode *, 32> Visited;
  SmallVector<const SDNode *, 16> Worklist;
  Visited.insert(Op.getNode());
  Worklist.push_back(Idx.getNode());
  SDValue StackPtr, Ch;
  for (SDNode *User : Vec.getNode()->uses()) {
    if (StoreSDNode *ST = dyn_cast<StoreSDNode>(User)) {
      if (ST->isIndexed() || ST->isTruncatingStore() ||
          ST->getValue() != Vec)
        continue;

      // Make sure that nothing else could have stored into the destination of
      // this store.
      if (!ST->getChain().reachesChainWithoutSideEffects(DAG.getEntryNode()))
        continue;

      // If the index is dependent on the store we will introduce a cycle when
      // creating the load (the load uses the index, and by replacing the chain
      // we will make the index dependent on the load). Also, the store might be
      // dependent on the extractelement and introduce a cycle when creating
      // the load.
      if (SDNode::hasPredecessorHelper(ST, Visited, Worklist) ||
          ST->hasPredecessor(Op.getNode()))
        continue;

      StackPtr = ST->getBasePtr();
      Ch = SDValue(ST, 0);
      break;
    }
  }

  EVT VecVT = Vec.getValueType();

  if (!Ch.getNode()) {
    // Store the value to a temporary stack slot, then LOAD the returned part.
    StackPtr = DAG.CreateStackTemporary(VecVT);
    MachineMemOperand *StoreMMO = getStackAlignedMMO(
        StackPtr, DAG.getMachineFunction(), VecVT.isScalableVector());
    Ch = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, StoreMMO);
  }

  SDValue NewLoad;
  Align ElementAlignment =
      std::min(cast<StoreSDNode>(Ch)->getAlign(),
               DAG.getDataLayout().getPrefTypeAlign(
                   Op.getValueType().getTypeForEVT(*DAG.getContext())));

  if (Op.getValueType().isVector()) {
    StackPtr = TLI.getVectorSubVecPointer(DAG, StackPtr, VecVT,
                                          Op.getValueType(), Idx);
    NewLoad = DAG.getLoad(Op.getValueType(), dl, Ch, StackPtr,
                          MachinePointerInfo(), ElementAlignment);
  } else {
    StackPtr = TLI.getVectorElementPointer(DAG, StackPtr, VecVT, Idx);
    NewLoad = DAG.getExtLoad(ISD::EXTLOAD, dl, Op.getValueType(), Ch, StackPtr,
                             MachinePointerInfo(), VecVT.getVectorElementType(),
                             ElementAlignment);
  }

  // Replace the chain going out of the store, by the one out of the load.
  DAG.ReplaceAllUsesOfValueWith(Ch, SDValue(NewLoad.getNode(), 1));

  // We introduced a cycle though, so update the loads operands, making sure
  // to use the original store's chain as an incoming chain.
  SmallVector<SDValue, 6> NewLoadOperands(NewLoad->op_begin(),
                                          NewLoad->op_end());
  NewLoadOperands[0] = Ch;
  NewLoad =
      SDValue(DAG.UpdateNodeOperands(NewLoad.getNode(), NewLoadOperands), 0);
  return NewLoad;
}

SDValue SelectionDAGLegalize::ExpandInsertToVectorThroughStack(SDValue Op) {
  assert(Op.getValueType().isVector() && "Non-vector insert subvector!");

  SDValue Vec  = Op.getOperand(0);
  SDValue Part = Op.getOperand(1);
  SDValue Idx  = Op.getOperand(2);
  SDLoc dl(Op);

  // Store the value to a temporary stack slot, then LOAD the returned part.
  EVT VecVT = Vec.getValueType();
  EVT PartVT = Part.getValueType();
  SDValue StackPtr = DAG.CreateStackTemporary(VecVT);
  int FI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);

  // First store the whole vector.
  SDValue Ch = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, PtrInfo);

  // Freeze the index so we don't poison the clamping code we're about to emit.
  Idx = DAG.getFreeze(Idx);

  // Then store the inserted part.
  if (PartVT.isVector()) {
    SDValue SubStackPtr =
        TLI.getVectorSubVecPointer(DAG, StackPtr, VecVT, PartVT, Idx);

    // Store the subvector.
    Ch = DAG.getStore(
        Ch, dl, Part, SubStackPtr,
        MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()));
  } else {
    SDValue SubStackPtr =
        TLI.getVectorElementPointer(DAG, StackPtr, VecVT, Idx);

    // Store the scalar value.
    Ch = DAG.getTruncStore(
        Ch, dl, Part, SubStackPtr,
        MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()),
        VecVT.getVectorElementType());
  }

  // Finally, load the updated vector.
  return DAG.getLoad(Op.getValueType(), dl, Ch, StackPtr, PtrInfo);
}

SDValue SelectionDAGLegalize::ExpandVectorBuildThroughStack(SDNode* Node) {
  assert((Node->getOpcode() == ISD::BUILD_VECTOR ||
          Node->getOpcode() == ISD::CONCAT_VECTORS) &&
         "Unexpected opcode!");

  // We can't handle this case efficiently.  Allocate a sufficiently
  // aligned object on the stack, store each operand into it, then load
  // the result as a vector.
  // Create the stack frame object.
  EVT VT = Node->getValueType(0);
  EVT MemVT = isa<BuildVectorSDNode>(Node) ? VT.getVectorElementType()
                                           : Node->getOperand(0).getValueType();
  SDLoc dl(Node);
  SDValue FIPtr = DAG.CreateStackTemporary(VT);
  int FI = cast<FrameIndexSDNode>(FIPtr.getNode())->getIndex();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);

  // Emit a store of each element to the stack slot.
  SmallVector<SDValue, 8> Stores;
  unsigned TypeByteSize = MemVT.getSizeInBits() / 8;
  assert(TypeByteSize > 0 && "Vector element type too small for stack store!");

  // If the destination vector element type of a BUILD_VECTOR is narrower than
  // the source element type, only store the bits necessary.
  bool Truncate = isa<BuildVectorSDNode>(Node) &&
                  MemVT.bitsLT(Node->getOperand(0).getValueType());

  // Store (in the right endianness) the elements to memory.
  for (unsigned i = 0, e = Node->getNumOperands(); i != e; ++i) {
    // Ignore undef elements.
    if (Node->getOperand(i).isUndef()) continue;

    unsigned Offset = TypeByteSize*i;

    SDValue Idx =
        DAG.getMemBasePlusOffset(FIPtr, TypeSize::getFixed(Offset), dl);

    if (Truncate)
      Stores.push_back(DAG.getTruncStore(DAG.getEntryNode(), dl,
                                         Node->getOperand(i), Idx,
                                         PtrInfo.getWithOffset(Offset), MemVT));
    else
      Stores.push_back(DAG.getStore(DAG.getEntryNode(), dl, Node->getOperand(i),
                                    Idx, PtrInfo.getWithOffset(Offset)));
  }

  SDValue StoreChain;
  if (!Stores.empty())    // Not all undef elements?
    StoreChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Stores);
  else
    StoreChain = DAG.getEntryNode();

  // Result is a load from the stack slot.
  return DAG.getLoad(VT, dl, StoreChain, FIPtr, PtrInfo);
}

/// Bitcast a floating-point value to an integer value. Only bitcast the part
/// containing the sign bit if the target has no integer value capable of
/// holding all bits of the floating-point value.
void SelectionDAGLegalize::getSignAsIntValue(FloatSignAsInt &State,
                                             const SDLoc &DL,
                                             SDValue Value) const {
  EVT FloatVT = Value.getValueType();
  unsigned NumBits = FloatVT.getScalarSizeInBits();
  State.FloatVT = FloatVT;
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), NumBits);
  // Convert to an integer of the same size.
  if (TLI.isTypeLegal(IVT)) {
    State.IntValue = DAG.getNode(ISD::BITCAST, DL, IVT, Value);
    State.SignMask = APInt::getSignMask(NumBits);
    State.SignBit = NumBits - 1;
    return;
  }

  auto &DataLayout = DAG.getDataLayout();
  // Store the float to memory, then load the sign part out as an integer.
  MVT LoadTy = TLI.getRegisterType(MVT::i8);
  // First create a temporary that is aligned for both the load and store.
  SDValue StackPtr = DAG.CreateStackTemporary(FloatVT, LoadTy);
  int FI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  // Then store the float to it.
  State.FloatPtr = StackPtr;
  MachineFunction &MF = DAG.getMachineFunction();
  State.FloatPointerInfo = MachinePointerInfo::getFixedStack(MF, FI);
  State.Chain = DAG.getStore(DAG.getEntryNode(), DL, Value, State.FloatPtr,
                             State.FloatPointerInfo);

  SDValue IntPtr;
  if (DataLayout.isBigEndian()) {
    assert(FloatVT.isByteSized() && "Unsupported floating point type!");
    // Load out a legal integer with the same sign bit as the float.
    IntPtr = StackPtr;
    State.IntPointerInfo = State.FloatPointerInfo;
  } else {
    // Advance the pointer so that the loaded byte will contain the sign bit.
    unsigned ByteOffset = (NumBits / 8) - 1;
    IntPtr =
        DAG.getMemBasePlusOffset(StackPtr, TypeSize::getFixed(ByteOffset), DL);
    State.IntPointerInfo = MachinePointerInfo::getFixedStack(MF, FI,
                                                             ByteOffset);
  }

  State.IntPtr = IntPtr;
  State.IntValue = DAG.getExtLoad(ISD::EXTLOAD, DL, LoadTy, State.Chain, IntPtr,
                                  State.IntPointerInfo, MVT::i8);
  State.SignMask = APInt::getOneBitSet(LoadTy.getScalarSizeInBits(), 7);
  State.SignBit = 7;
}

/// Replace the integer value produced by getSignAsIntValue() with a new value
/// and cast the result back to a floating-point type.
SDValue SelectionDAGLegalize::modifySignAsInt(const FloatSignAsInt &State,
                                              const SDLoc &DL,
                                              SDValue NewIntValue) const {
  if (!State.Chain)
    return DAG.getNode(ISD::BITCAST, DL, State.FloatVT, NewIntValue);

  // Override the part containing the sign bit in the value stored on the stack.
  SDValue Chain = DAG.getTruncStore(State.Chain, DL, NewIntValue, State.IntPtr,
                                    State.IntPointerInfo, MVT::i8);
  return DAG.getLoad(State.FloatVT, DL, Chain, State.FloatPtr,
                     State.FloatPointerInfo);
}

SDValue SelectionDAGLegalize::ExpandFCOPYSIGN(SDNode *Node) const {
  SDLoc DL(Node);
  SDValue Mag = Node->getOperand(0);
  SDValue Sign = Node->getOperand(1);

  // Get sign bit into an integer value.
  FloatSignAsInt SignAsInt;
  getSignAsIntValue(SignAsInt, DL, Sign);

  EVT IntVT = SignAsInt.IntValue.getValueType();
  SDValue SignMask = DAG.getConstant(SignAsInt.SignMask, DL, IntVT);
  SDValue SignBit = DAG.getNode(ISD::AND, DL, IntVT, SignAsInt.IntValue,
                                SignMask);

  // If FABS is legal transform FCOPYSIGN(x, y) => sign(x) ? -FABS(x) : FABS(X)
  EVT FloatVT = Mag.getValueType();
  if (TLI.isOperationLegalOrCustom(ISD::FABS, FloatVT) &&
      TLI.isOperationLegalOrCustom(ISD::FNEG, FloatVT)) {
    SDValue AbsValue = DAG.getNode(ISD::FABS, DL, FloatVT, Mag);
    SDValue NegValue = DAG.getNode(ISD::FNEG, DL, FloatVT, AbsValue);
    SDValue Cond = DAG.getSetCC(DL, getSetCCResultType(IntVT), SignBit,
                                DAG.getConstant(0, DL, IntVT), ISD::SETNE);
    return DAG.getSelect(DL, FloatVT, Cond, NegValue, AbsValue);
  }

  // Transform Mag value to integer, and clear the sign bit.
  FloatSignAsInt MagAsInt;
  getSignAsIntValue(MagAsInt, DL, Mag);
  EVT MagVT = MagAsInt.IntValue.getValueType();
  SDValue ClearSignMask = DAG.getConstant(~MagAsInt.SignMask, DL, MagVT);
  SDValue ClearedSign = DAG.getNode(ISD::AND, DL, MagVT, MagAsInt.IntValue,
                                    ClearSignMask);

  // Get the signbit at the right position for MagAsInt.
  int ShiftAmount = SignAsInt.SignBit - MagAsInt.SignBit;
  EVT ShiftVT = IntVT;
  if (SignBit.getScalarValueSizeInBits() <
      ClearedSign.getScalarValueSizeInBits()) {
    SignBit = DAG.getNode(ISD::ZERO_EXTEND, DL, MagVT, SignBit);
    ShiftVT = MagVT;
  }
  if (ShiftAmount > 0) {
    SDValue ShiftCnst = DAG.getConstant(ShiftAmount, DL, ShiftVT);
    SignBit = DAG.getNode(ISD::SRL, DL, ShiftVT, SignBit, ShiftCnst);
  } else if (ShiftAmount < 0) {
    SDValue ShiftCnst = DAG.getConstant(-ShiftAmount, DL, ShiftVT);
    SignBit = DAG.getNode(ISD::SHL, DL, ShiftVT, SignBit, ShiftCnst);
  }
  if (SignBit.getScalarValueSizeInBits() >
      ClearedSign.getScalarValueSizeInBits()) {
    SignBit = DAG.getNode(ISD::TRUNCATE, DL, MagVT, SignBit);
  }

  SDNodeFlags Flags;
  Flags.setDisjoint(true);

  // Store the part with the modified sign and convert back to float.
  SDValue CopiedSign =
      DAG.getNode(ISD::OR, DL, MagVT, ClearedSign, SignBit, Flags);

  return modifySignAsInt(MagAsInt, DL, CopiedSign);
}

SDValue SelectionDAGLegalize::ExpandFNEG(SDNode *Node) const {
  // Get the sign bit as an integer.
  SDLoc DL(Node);
  FloatSignAsInt SignAsInt;
  getSignAsIntValue(SignAsInt, DL, Node->getOperand(0));
  EVT IntVT = SignAsInt.IntValue.getValueType();

  // Flip the sign.
  SDValue SignMask = DAG.getConstant(SignAsInt.SignMask, DL, IntVT);
  SDValue SignFlip =
      DAG.getNode(ISD::XOR, DL, IntVT, SignAsInt.IntValue, SignMask);

  // Convert back to float.
  return modifySignAsInt(SignAsInt, DL, SignFlip);
}

SDValue SelectionDAGLegalize::ExpandFABS(SDNode *Node) const {
  SDLoc DL(Node);
  SDValue Value = Node->getOperand(0);

  // Transform FABS(x) => FCOPYSIGN(x, 0.0) if FCOPYSIGN is legal.
  EVT FloatVT = Value.getValueType();
  if (TLI.isOperationLegalOrCustom(ISD::FCOPYSIGN, FloatVT)) {
    SDValue Zero = DAG.getConstantFP(0.0, DL, FloatVT);
    return DAG.getNode(ISD::FCOPYSIGN, DL, FloatVT, Value, Zero);
  }

  // Transform value to integer, clear the sign bit and transform back.
  FloatSignAsInt ValueAsInt;
  getSignAsIntValue(ValueAsInt, DL, Value);
  EVT IntVT = ValueAsInt.IntValue.getValueType();
  SDValue ClearSignMask = DAG.getConstant(~ValueAsInt.SignMask, DL, IntVT);
  SDValue ClearedSign = DAG.getNode(ISD::AND, DL, IntVT, ValueAsInt.IntValue,
                                    ClearSignMask);
  return modifySignAsInt(ValueAsInt, DL, ClearedSign);
}

void SelectionDAGLegalize::ExpandDYNAMIC_STACKALLOC(SDNode* Node,
                                           SmallVectorImpl<SDValue> &Results) {
  Register SPReg = TLI.getStackPointerRegisterToSaveRestore();
  assert(SPReg && "Target cannot require DYNAMIC_STACKALLOC expansion and"
          " not tell us which reg is the stack pointer!");
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  SDValue Tmp1 = SDValue(Node, 0);
  SDValue Tmp2 = SDValue(Node, 1);
  SDValue Tmp3 = Node->getOperand(2);
  SDValue Chain = Tmp1.getOperand(0);

  // Chain the dynamic stack allocation so that it doesn't modify the stack
  // pointer when other instructions are using the stack.
  Chain = DAG.getCALLSEQ_START(Chain, 0, 0, dl);

  SDValue Size  = Tmp2.getOperand(1);
  SDValue SP = DAG.getCopyFromReg(Chain, dl, SPReg, VT);
  Chain = SP.getValue(1);
  Align Alignment = cast<ConstantSDNode>(Tmp3)->getAlignValue();
  const TargetFrameLowering *TFL = DAG.getSubtarget().getFrameLowering();
  unsigned Opc =
    TFL->getStackGrowthDirection() == TargetFrameLowering::StackGrowsUp ?
    ISD::ADD : ISD::SUB;

  Align StackAlign = TFL->getStackAlign();
  Tmp1 = DAG.getNode(Opc, dl, VT, SP, Size);       // Value
  if (Alignment > StackAlign)
    Tmp1 = DAG.getNode(ISD::AND, dl, VT, Tmp1,
                       DAG.getConstant(-Alignment.value(), dl, VT));
  Chain = DAG.getCopyToReg(Chain, dl, SPReg, Tmp1);     // Output chain

  Tmp2 = DAG.getCALLSEQ_END(Chain, 0, 0, SDValue(), dl);

  Results.push_back(Tmp1);
  Results.push_back(Tmp2);
}

/// Emit a store/load combination to the stack.  This stores
/// SrcOp to a stack slot of type SlotVT, truncating it if needed.  It then does
/// a load from the stack slot to DestVT, extending it if needed.
/// The resultant code need not be legal.
SDValue SelectionDAGLegalize::EmitStackConvert(SDValue SrcOp, EVT SlotVT,
                                               EVT DestVT, const SDLoc &dl) {
  return EmitStackConvert(SrcOp, SlotVT, DestVT, dl, DAG.getEntryNode());
}

SDValue SelectionDAGLegalize::EmitStackConvert(SDValue SrcOp, EVT SlotVT,
                                               EVT DestVT, const SDLoc &dl,
                                               SDValue Chain) {
  EVT SrcVT = SrcOp.getValueType();
  Type *DestType = DestVT.getTypeForEVT(*DAG.getContext());
  Align DestAlign = DAG.getDataLayout().getPrefTypeAlign(DestType);

  // Don't convert with stack if the load/store is expensive.
  if ((SrcVT.bitsGT(SlotVT) &&
       !TLI.isTruncStoreLegalOrCustom(SrcOp.getValueType(), SlotVT)) ||
      (SlotVT.bitsLT(DestVT) &&
       !TLI.isLoadExtLegalOrCustom(ISD::EXTLOAD, DestVT, SlotVT)))
    return SDValue();

  // Create the stack frame object.
  Align SrcAlign = DAG.getDataLayout().getPrefTypeAlign(
      SrcOp.getValueType().getTypeForEVT(*DAG.getContext()));
  SDValue FIPtr = DAG.CreateStackTemporary(SlotVT.getStoreSize(), SrcAlign);

  FrameIndexSDNode *StackPtrFI = cast<FrameIndexSDNode>(FIPtr);
  int SPFI = StackPtrFI->getIndex();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI);

  // Emit a store to the stack slot.  Use a truncstore if the input value is
  // later than DestVT.
  SDValue Store;

  if (SrcVT.bitsGT(SlotVT))
    Store = DAG.getTruncStore(Chain, dl, SrcOp, FIPtr, PtrInfo,
                              SlotVT, SrcAlign);
  else {
    assert(SrcVT.bitsEq(SlotVT) && "Invalid store");
    Store = DAG.getStore(Chain, dl, SrcOp, FIPtr, PtrInfo, SrcAlign);
  }

  // Result is a load from the stack slot.
  if (SlotVT.bitsEq(DestVT))
    return DAG.getLoad(DestVT, dl, Store, FIPtr, PtrInfo, DestAlign);

  assert(SlotVT.bitsLT(DestVT) && "Unknown extension!");
  return DAG.getExtLoad(ISD::EXTLOAD, dl, DestVT, Store, FIPtr, PtrInfo, SlotVT,
                        DestAlign);
}

SDValue SelectionDAGLegalize::ExpandSCALAR_TO_VECTOR(SDNode *Node) {
  SDLoc dl(Node);
  // Create a vector sized/aligned stack slot, store the value to element #0,
  // then load the whole vector back out.
  SDValue StackPtr = DAG.CreateStackTemporary(Node->getValueType(0));

  FrameIndexSDNode *StackPtrFI = cast<FrameIndexSDNode>(StackPtr);
  int SPFI = StackPtrFI->getIndex();

  SDValue Ch = DAG.getTruncStore(
      DAG.getEntryNode(), dl, Node->getOperand(0), StackPtr,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI),
      Node->getValueType(0).getVectorElementType());
  return DAG.getLoad(
      Node->getValueType(0), dl, Ch, StackPtr,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI));
}

static bool
ExpandBVWithShuffles(SDNode *Node, SelectionDAG &DAG,
                     const TargetLowering &TLI, SDValue &Res) {
  unsigned NumElems = Node->getNumOperands();
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);

  // Try to group the scalars into pairs, shuffle the pairs together, then
  // shuffle the pairs of pairs together, etc. until the vector has
  // been built. This will work only if all of the necessary shuffle masks
  // are legal.

  // We do this in two phases; first to check the legality of the shuffles,
  // and next, assuming that all shuffles are legal, to create the new nodes.
  for (int Phase = 0; Phase < 2; ++Phase) {
    SmallVector<std::pair<SDValue, SmallVector<int, 16>>, 16> IntermedVals,
                                                              NewIntermedVals;
    for (unsigned i = 0; i < NumElems; ++i) {
      SDValue V = Node->getOperand(i);
      if (V.isUndef())
        continue;

      SDValue Vec;
      if (Phase)
        Vec = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, V);
      IntermedVals.push_back(std::make_pair(Vec, SmallVector<int, 16>(1, i)));
    }

    while (IntermedVals.size() > 2) {
      NewIntermedVals.clear();
      for (unsigned i = 0, e = (IntermedVals.size() & ~1u); i < e; i += 2) {
        // This vector and the next vector are shuffled together (simply to
        // append the one to the other).
        SmallVector<int, 16> ShuffleVec(NumElems, -1);

        SmallVector<int, 16> FinalIndices;
        FinalIndices.reserve(IntermedVals[i].second.size() +
                             IntermedVals[i+1].second.size());

        int k = 0;
        for (unsigned j = 0, f = IntermedVals[i].second.size(); j != f;
             ++j, ++k) {
          ShuffleVec[k] = j;
          FinalIndices.push_back(IntermedVals[i].second[j]);
        }
        for (unsigned j = 0, f = IntermedVals[i+1].second.size(); j != f;
             ++j, ++k) {
          ShuffleVec[k] = NumElems + j;
          FinalIndices.push_back(IntermedVals[i+1].second[j]);
        }

        SDValue Shuffle;
        if (Phase)
          Shuffle = DAG.getVectorShuffle(VT, dl, IntermedVals[i].first,
                                         IntermedVals[i+1].first,
                                         ShuffleVec);
        else if (!TLI.isShuffleMaskLegal(ShuffleVec, VT))
          return false;
        NewIntermedVals.push_back(
            std::make_pair(Shuffle, std::move(FinalIndices)));
      }

      // If we had an odd number of defined values, then append the last
      // element to the array of new vectors.
      if ((IntermedVals.size() & 1) != 0)
        NewIntermedVals.push_back(IntermedVals.back());

      IntermedVals.swap(NewIntermedVals);
    }

    assert(IntermedVals.size() <= 2 && IntermedVals.size() > 0 &&
           "Invalid number of intermediate vectors");
    SDValue Vec1 = IntermedVals[0].first;
    SDValue Vec2;
    if (IntermedVals.size() > 1)
      Vec2 = IntermedVals[1].first;
    else if (Phase)
      Vec2 = DAG.getUNDEF(VT);

    SmallVector<int, 16> ShuffleVec(NumElems, -1);
    for (unsigned i = 0, e = IntermedVals[0].second.size(); i != e; ++i)
      ShuffleVec[IntermedVals[0].second[i]] = i;
    for (unsigned i = 0, e = IntermedVals[1].second.size(); i != e; ++i)
      ShuffleVec[IntermedVals[1].second[i]] = NumElems + i;

    if (Phase)
      Res = DAG.getVectorShuffle(VT, dl, Vec1, Vec2, ShuffleVec);
    else if (!TLI.isShuffleMaskLegal(ShuffleVec, VT))
      return false;
  }

  return true;
}

/// Expand a BUILD_VECTOR node on targets that don't
/// support the operation, but do support the resultant vector type.
SDValue SelectionDAGLegalize::ExpandBUILD_VECTOR(SDNode *Node) {
  unsigned NumElems = Node->getNumOperands();
  SDValue Value1, Value2;
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  EVT OpVT = Node->getOperand(0).getValueType();
  EVT EltVT = VT.getVectorElementType();

  // If the only non-undef value is the low element, turn this into a
  // SCALAR_TO_VECTOR node.  If this is { X, X, X, X }, determine X.
  bool isOnlyLowElement = true;
  bool MoreThanTwoValues = false;
  bool isConstant = true;
  for (unsigned i = 0; i < NumElems; ++i) {
    SDValue V = Node->getOperand(i);
    if (V.isUndef())
      continue;
    if (i > 0)
      isOnlyLowElement = false;
    if (!isa<ConstantFPSDNode>(V) && !isa<ConstantSDNode>(V))
      isConstant = false;

    if (!Value1.getNode()) {
      Value1 = V;
    } else if (!Value2.getNode()) {
      if (V != Value1)
        Value2 = V;
    } else if (V != Value1 && V != Value2) {
      MoreThanTwoValues = true;
    }
  }

  if (!Value1.getNode())
    return DAG.getUNDEF(VT);

  if (isOnlyLowElement)
    return DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Node->getOperand(0));

  // If all elements are constants, create a load from the constant pool.
  if (isConstant) {
    SmallVector<Constant*, 16> CV;
    for (unsigned i = 0, e = NumElems; i != e; ++i) {
      if (ConstantFPSDNode *V =
          dyn_cast<ConstantFPSDNode>(Node->getOperand(i))) {
        CV.push_back(const_cast<ConstantFP *>(V->getConstantFPValue()));
      } else if (ConstantSDNode *V =
                 dyn_cast<ConstantSDNode>(Node->getOperand(i))) {
        if (OpVT==EltVT)
          CV.push_back(const_cast<ConstantInt *>(V->getConstantIntValue()));
        else {
          // If OpVT and EltVT don't match, EltVT is not legal and the
          // element values have been promoted/truncated earlier.  Undo this;
          // we don't want a v16i8 to become a v16i32 for example.
          const ConstantInt *CI = V->getConstantIntValue();
          CV.push_back(ConstantInt::get(EltVT.getTypeForEVT(*DAG.getContext()),
                                        CI->getZExtValue()));
        }
      } else {
        assert(Node->getOperand(i).isUndef());
        Type *OpNTy = EltVT.getTypeForEVT(*DAG.getContext());
        CV.push_back(UndefValue::get(OpNTy));
      }
    }
    Constant *CP = ConstantVector::get(CV);
    SDValue CPIdx =
        DAG.getConstantPool(CP, TLI.getPointerTy(DAG.getDataLayout()));
    Align Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlign();
    return DAG.getLoad(
        VT, dl, DAG.getEntryNode(), CPIdx,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()),
        Alignment);
  }

  SmallSet<SDValue, 16> DefinedValues;
  for (unsigned i = 0; i < NumElems; ++i) {
    if (Node->getOperand(i).isUndef())
      continue;
    DefinedValues.insert(Node->getOperand(i));
  }

  if (TLI.shouldExpandBuildVectorWithShuffles(VT, DefinedValues.size())) {
    if (!MoreThanTwoValues) {
      SmallVector<int, 8> ShuffleVec(NumElems, -1);
      for (unsigned i = 0; i < NumElems; ++i) {
        SDValue V = Node->getOperand(i);
        if (V.isUndef())
          continue;
        ShuffleVec[i] = V == Value1 ? 0 : NumElems;
      }
      if (TLI.isShuffleMaskLegal(ShuffleVec, Node->getValueType(0))) {
        // Get the splatted value into the low element of a vector register.
        SDValue Vec1 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Value1);
        SDValue Vec2;
        if (Value2.getNode())
          Vec2 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Value2);
        else
          Vec2 = DAG.getUNDEF(VT);

        // Return shuffle(LowValVec, undef, <0,0,0,0>)
        return DAG.getVectorShuffle(VT, dl, Vec1, Vec2, ShuffleVec);
      }
    } else {
      SDValue Res;
      if (ExpandBVWithShuffles(Node, DAG, TLI, Res))
        return Res;
    }
  }

  // Otherwise, we can't handle this case efficiently.
  return ExpandVectorBuildThroughStack(Node);
}

SDValue SelectionDAGLegalize::ExpandSPLAT_VECTOR(SDNode *Node) {
  SDLoc DL(Node);
  EVT VT = Node->getValueType(0);
  SDValue SplatVal = Node->getOperand(0);

  return DAG.getSplatBuildVector(VT, DL, SplatVal);
}

// Expand a node into a call to a libcall, returning the value as the first
// result and the chain as the second.  If the result value does not fit into a
// register, return the lo part and set the hi part to the by-reg argument in
// the first.  If it does fit into a single register, return the result and
// leave the Hi part unset.
std::pair<SDValue, SDValue> SelectionDAGLegalize::ExpandLibCall(RTLIB::Libcall LC, SDNode *Node,
                                            TargetLowering::ArgListTy &&Args,
                                            bool isSigned) {
  EVT CodePtrTy = TLI.getPointerTy(DAG.getDataLayout());
  SDValue Callee;
  if (const char *LibcallName = TLI.getLibcallName(LC))
    Callee = DAG.getExternalSymbol(LibcallName, CodePtrTy);
  else {
    Callee = DAG.getUNDEF(CodePtrTy);
    DAG.getContext()->emitError(Twine("no libcall available for ") +
                                Node->getOperationName(&DAG));
  }

  EVT RetVT = Node->getValueType(0);
  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());

  // By default, the input chain to this libcall is the entry node of the
  // function. If the libcall is going to be emitted as a tail call then
  // TLI.isUsedByReturnOnly will change it to the right chain if the return
  // node which is being folded has a non-entry input chain.
  SDValue InChain = DAG.getEntryNode();

  // isTailCall may be true since the callee does not reference caller stack
  // frame. Check if it's in the right position and that the return types match.
  SDValue TCChain = InChain;
  const Function &F = DAG.getMachineFunction().getFunction();
  bool isTailCall =
      TLI.isInTailCallPosition(DAG, Node, TCChain) &&
      (RetTy == F.getReturnType() || F.getReturnType()->isVoidTy());
  if (isTailCall)
    InChain = TCChain;

  TargetLowering::CallLoweringInfo CLI(DAG);
  bool signExtend = TLI.shouldSignExtendTypeInLibCall(RetVT, isSigned);
  CLI.setDebugLoc(SDLoc(Node))
      .setChain(InChain)
      .setLibCallee(TLI.getLibcallCallingConv(LC), RetTy, Callee,
                    std::move(Args))
      .setTailCall(isTailCall)
      .setSExtResult(signExtend)
      .setZExtResult(!signExtend)
      .setIsPostTypeLegalization(true);

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  if (!CallInfo.second.getNode()) {
    LLVM_DEBUG(dbgs() << "Created tailcall: "; DAG.getRoot().dump(&DAG));
    // It's a tailcall, return the chain (which is the DAG root).
    return {DAG.getRoot(), DAG.getRoot()};
  }

  LLVM_DEBUG(dbgs() << "Created libcall: "; CallInfo.first.dump(&DAG));
  return CallInfo;
}

std::pair<SDValue, SDValue> SelectionDAGLegalize::ExpandLibCall(RTLIB::Libcall LC, SDNode *Node,
                                            bool isSigned) {
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (const SDValue &Op : Node->op_values()) {
    EVT ArgVT = Op.getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = Op;
    Entry.Ty = ArgTy;
    Entry.IsSExt = TLI.shouldSignExtendTypeInLibCall(ArgVT, isSigned);
    Entry.IsZExt = !Entry.IsSExt;
    Args.push_back(Entry);
  }

  return ExpandLibCall(LC, Node, std::move(Args), isSigned);
}

void SelectionDAGLegalize::ExpandFrexpLibCall(
    SDNode *Node, SmallVectorImpl<SDValue> &Results) {
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  EVT ExpVT = Node->getValueType(1);

  SDValue FPOp = Node->getOperand(0);

  EVT ArgVT = FPOp.getValueType();
  Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());

  TargetLowering::ArgListEntry FPArgEntry;
  FPArgEntry.Node = FPOp;
  FPArgEntry.Ty = ArgTy;

  SDValue StackSlot = DAG.CreateStackTemporary(ExpVT);
  TargetLowering::ArgListEntry PtrArgEntry;
  PtrArgEntry.Node = StackSlot;
  PtrArgEntry.Ty = PointerType::get(*DAG.getContext(),
                                    DAG.getDataLayout().getAllocaAddrSpace());

  TargetLowering::ArgListTy Args = {FPArgEntry, PtrArgEntry};

  RTLIB::Libcall LC = RTLIB::getFREXP(VT);
  auto [Call, Chain] = ExpandLibCall(LC, Node, std::move(Args), false);

  // FIXME: Get type of int for libcall declaration and cast

  int FrameIdx = cast<FrameIndexSDNode>(StackSlot)->getIndex();
  auto PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx);

  SDValue LoadExp = DAG.getLoad(ExpVT, dl, Chain, StackSlot, PtrInfo);
  SDValue OutputChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                    LoadExp.getValue(1), DAG.getRoot());
  DAG.setRoot(OutputChain);

  Results.push_back(Call);
  Results.push_back(LoadExp);
}

void SelectionDAGLegalize::ExpandFPLibCall(SDNode* Node,
                                           RTLIB::Libcall LC,
                                           SmallVectorImpl<SDValue> &Results) {
  if (LC == RTLIB::UNKNOWN_LIBCALL)
    llvm_unreachable("Can't create an unknown libcall!");

  if (Node->isStrictFPOpcode()) {
    EVT RetVT = Node->getValueType(0);
    SmallVector<SDValue, 4> Ops(drop_begin(Node->ops()));
    TargetLowering::MakeLibCallOptions CallOptions;
    // FIXME: This doesn't support tail calls.
    std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, RetVT,
                                                      Ops, CallOptions,
                                                      SDLoc(Node),
                                                      Node->getOperand(0));
    Results.push_back(Tmp.first);
    Results.push_back(Tmp.second);
  } else {
    bool IsSignedArgument = Node->getOpcode() == ISD::FLDEXP;
    SDValue Tmp = ExpandLibCall(LC, Node, IsSignedArgument).first;
    Results.push_back(Tmp);
  }
}

/// Expand the node to a libcall based on the result type.
void SelectionDAGLegalize::ExpandFPLibCall(SDNode* Node,
                                           RTLIB::Libcall Call_F32,
                                           RTLIB::Libcall Call_F64,
                                           RTLIB::Libcall Call_F80,
                                           RTLIB::Libcall Call_F128,
                                           RTLIB::Libcall Call_PPCF128,
                                           SmallVectorImpl<SDValue> &Results) {
  RTLIB::Libcall LC = RTLIB::getFPLibCall(Node->getSimpleValueType(0),
                                          Call_F32, Call_F64, Call_F80,
                                          Call_F128, Call_PPCF128);
  ExpandFPLibCall(Node, LC, Results);
}

SDValue SelectionDAGLegalize::ExpandIntLibCall(SDNode* Node, bool isSigned,
                                               RTLIB::Libcall Call_I8,
                                               RTLIB::Libcall Call_I16,
                                               RTLIB::Libcall Call_I32,
                                               RTLIB::Libcall Call_I64,
                                               RTLIB::Libcall Call_I128) {
  RTLIB::Libcall LC;
  switch (Node->getSimpleValueType(0).SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:   LC = Call_I8; break;
  case MVT::i16:  LC = Call_I16; break;
  case MVT::i32:  LC = Call_I32; break;
  case MVT::i64:  LC = Call_I64; break;
  case MVT::i128: LC = Call_I128; break;
  }
  return ExpandLibCall(LC, Node, isSigned).first;
}

/// Expand the node to a libcall based on first argument type (for instance
/// lround and its variant).
void SelectionDAGLegalize::ExpandArgFPLibCall(SDNode* Node,
                                            RTLIB::Libcall Call_F32,
                                            RTLIB::Libcall Call_F64,
                                            RTLIB::Libcall Call_F80,
                                            RTLIB::Libcall Call_F128,
                                            RTLIB::Libcall Call_PPCF128,
                                            SmallVectorImpl<SDValue> &Results) {
  EVT InVT = Node->getOperand(Node->isStrictFPOpcode() ? 1 : 0).getValueType();
  RTLIB::Libcall LC = RTLIB::getFPLibCall(InVT.getSimpleVT(),
                                          Call_F32, Call_F64, Call_F80,
                                          Call_F128, Call_PPCF128);
  ExpandFPLibCall(Node, LC, Results);
}

/// Issue libcalls to __{u}divmod to compute div / rem pairs.
void
SelectionDAGLegalize::ExpandDivRemLibCall(SDNode *Node,
                                          SmallVectorImpl<SDValue> &Results) {
  unsigned Opcode = Node->getOpcode();
  bool isSigned = Opcode == ISD::SDIVREM;

  RTLIB::Libcall LC;
  switch (Node->getSimpleValueType(0).SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:   LC= isSigned ? RTLIB::SDIVREM_I8  : RTLIB::UDIVREM_I8;  break;
  case MVT::i16:  LC= isSigned ? RTLIB::SDIVREM_I16 : RTLIB::UDIVREM_I16; break;
  case MVT::i32:  LC= isSigned ? RTLIB::SDIVREM_I32 : RTLIB::UDIVREM_I32; break;
  case MVT::i64:  LC= isSigned ? RTLIB::SDIVREM_I64 : RTLIB::UDIVREM_I64; break;
  case MVT::i128: LC= isSigned ? RTLIB::SDIVREM_I128:RTLIB::UDIVREM_I128; break;
  }

  // The input chain to this libcall is the entry node of the function.
  // Legalizing the call will automatically add the previous call to the
  // dependence.
  SDValue InChain = DAG.getEntryNode();

  EVT RetVT = Node->getValueType(0);
  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (const SDValue &Op : Node->op_values()) {
    EVT ArgVT = Op.getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = Op;
    Entry.Ty = ArgTy;
    Entry.IsSExt = isSigned;
    Entry.IsZExt = !isSigned;
    Args.push_back(Entry);
  }

  // Also pass the return address of the remainder.
  SDValue FIPtr = DAG.CreateStackTemporary(RetVT);
  Entry.Node = FIPtr;
  Entry.Ty = PointerType::getUnqual(RetTy->getContext());
  Entry.IsSExt = isSigned;
  Entry.IsZExt = !isSigned;
  Args.push_back(Entry);

  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy(DAG.getDataLayout()));

  SDLoc dl(Node);
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(InChain)
      .setLibCallee(TLI.getLibcallCallingConv(LC), RetTy, Callee,
                    std::move(Args))
      .setSExtResult(isSigned)
      .setZExtResult(!isSigned);

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  // Remainder is loaded back from the stack frame.
  SDValue Rem =
      DAG.getLoad(RetVT, dl, CallInfo.second, FIPtr, MachinePointerInfo());
  Results.push_back(CallInfo.first);
  Results.push_back(Rem);
}

/// Return true if sincos libcall is available.
static bool isSinCosLibcallAvailable(SDNode *Node, const TargetLowering &TLI) {
  RTLIB::Libcall LC;
  switch (Node->getSimpleValueType(0).SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::f32:     LC = RTLIB::SINCOS_F32; break;
  case MVT::f64:     LC = RTLIB::SINCOS_F64; break;
  case MVT::f80:     LC = RTLIB::SINCOS_F80; break;
  case MVT::f128:    LC = RTLIB::SINCOS_F128; break;
  case MVT::ppcf128: LC = RTLIB::SINCOS_PPCF128; break;
  }
  return TLI.getLibcallName(LC) != nullptr;
}

/// Only issue sincos libcall if both sin and cos are needed.
static bool useSinCos(SDNode *Node) {
  unsigned OtherOpcode = Node->getOpcode() == ISD::FSIN
    ? ISD::FCOS : ISD::FSIN;

  SDValue Op0 = Node->getOperand(0);
  for (const SDNode *User : Op0.getNode()->uses()) {
    if (User == Node)
      continue;
    // The other user might have been turned into sincos already.
    if (User->getOpcode() == OtherOpcode || User->getOpcode() == ISD::FSINCOS)
      return true;
  }
  return false;
}

/// Issue libcalls to sincos to compute sin / cos pairs.
void
SelectionDAGLegalize::ExpandSinCosLibCall(SDNode *Node,
                                          SmallVectorImpl<SDValue> &Results) {
  RTLIB::Libcall LC;
  switch (Node->getSimpleValueType(0).SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::f32:     LC = RTLIB::SINCOS_F32; break;
  case MVT::f64:     LC = RTLIB::SINCOS_F64; break;
  case MVT::f80:     LC = RTLIB::SINCOS_F80; break;
  case MVT::f128:    LC = RTLIB::SINCOS_F128; break;
  case MVT::ppcf128: LC = RTLIB::SINCOS_PPCF128; break;
  }

  // The input chain to this libcall is the entry node of the function.
  // Legalizing the call will automatically add the previous call to the
  // dependence.
  SDValue InChain = DAG.getEntryNode();

  EVT RetVT = Node->getValueType(0);
  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  // Pass the argument.
  Entry.Node = Node->getOperand(0);
  Entry.Ty = RetTy;
  Entry.IsSExt = false;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  // Pass the return address of sin.
  SDValue SinPtr = DAG.CreateStackTemporary(RetVT);
  Entry.Node = SinPtr;
  Entry.Ty = PointerType::getUnqual(RetTy->getContext());
  Entry.IsSExt = false;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  // Also pass the return address of the cos.
  SDValue CosPtr = DAG.CreateStackTemporary(RetVT);
  Entry.Node = CosPtr;
  Entry.Ty = PointerType::getUnqual(RetTy->getContext());
  Entry.IsSExt = false;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy(DAG.getDataLayout()));

  SDLoc dl(Node);
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl).setChain(InChain).setLibCallee(
      TLI.getLibcallCallingConv(LC), Type::getVoidTy(*DAG.getContext()), Callee,
      std::move(Args));

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  Results.push_back(
      DAG.getLoad(RetVT, dl, CallInfo.second, SinPtr, MachinePointerInfo()));
  Results.push_back(
      DAG.getLoad(RetVT, dl, CallInfo.second, CosPtr, MachinePointerInfo()));
}

SDValue SelectionDAGLegalize::expandLdexp(SDNode *Node) const {
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  SDValue X = Node->getOperand(0);
  SDValue N = Node->getOperand(1);
  EVT ExpVT = N.getValueType();
  EVT AsIntVT = VT.changeTypeToInteger();
  if (AsIntVT == EVT()) // TODO: How to handle f80?
    return SDValue();

  if (Node->getOpcode() == ISD::STRICT_FLDEXP) // TODO
    return SDValue();

  SDNodeFlags NSW;
  NSW.setNoSignedWrap(true);
  SDNodeFlags NUW_NSW;
  NUW_NSW.setNoUnsignedWrap(true);
  NUW_NSW.setNoSignedWrap(true);

  EVT SetCCVT =
      TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), ExpVT);
  const fltSemantics &FltSem = SelectionDAG::EVTToAPFloatSemantics(VT);

  const APFloat::ExponentType MaxExpVal = APFloat::semanticsMaxExponent(FltSem);
  const APFloat::ExponentType MinExpVal = APFloat::semanticsMinExponent(FltSem);
  const int Precision = APFloat::semanticsPrecision(FltSem);

  const SDValue MaxExp = DAG.getConstant(MaxExpVal, dl, ExpVT);
  const SDValue MinExp = DAG.getConstant(MinExpVal, dl, ExpVT);

  const SDValue DoubleMaxExp = DAG.getConstant(2 * MaxExpVal, dl, ExpVT);

  const APFloat One(FltSem, "1.0");
  APFloat ScaleUpK = scalbn(One, MaxExpVal, APFloat::rmNearestTiesToEven);

  // Offset by precision to avoid denormal range.
  APFloat ScaleDownK =
      scalbn(One, MinExpVal + Precision, APFloat::rmNearestTiesToEven);

  // TODO: Should really introduce control flow and use a block for the >
  // MaxExp, < MinExp cases

  // First, handle exponents Exp > MaxExp and scale down.
  SDValue NGtMaxExp = DAG.getSetCC(dl, SetCCVT, N, MaxExp, ISD::SETGT);

  SDValue DecN0 = DAG.getNode(ISD::SUB, dl, ExpVT, N, MaxExp, NSW);
  SDValue ClampMaxVal = DAG.getConstant(3 * MaxExpVal, dl, ExpVT);
  SDValue ClampN_Big = DAG.getNode(ISD::SMIN, dl, ExpVT, N, ClampMaxVal);
  SDValue DecN1 =
      DAG.getNode(ISD::SUB, dl, ExpVT, ClampN_Big, DoubleMaxExp, NSW);

  SDValue ScaleUpTwice =
      DAG.getSetCC(dl, SetCCVT, N, DoubleMaxExp, ISD::SETUGT);

  const SDValue ScaleUpVal = DAG.getConstantFP(ScaleUpK, dl, VT);
  SDValue ScaleUp0 = DAG.getNode(ISD::FMUL, dl, VT, X, ScaleUpVal);
  SDValue ScaleUp1 = DAG.getNode(ISD::FMUL, dl, VT, ScaleUp0, ScaleUpVal);

  SDValue SelectN_Big =
      DAG.getNode(ISD::SELECT, dl, ExpVT, ScaleUpTwice, DecN1, DecN0);
  SDValue SelectX_Big =
      DAG.getNode(ISD::SELECT, dl, VT, ScaleUpTwice, ScaleUp1, ScaleUp0);

  // Now handle exponents Exp < MinExp
  SDValue NLtMinExp = DAG.getSetCC(dl, SetCCVT, N, MinExp, ISD::SETLT);

  SDValue Increment0 = DAG.getConstant(-(MinExpVal + Precision), dl, ExpVT);
  SDValue Increment1 = DAG.getConstant(-2 * (MinExpVal + Precision), dl, ExpVT);

  SDValue IncN0 = DAG.getNode(ISD::ADD, dl, ExpVT, N, Increment0, NUW_NSW);

  SDValue ClampMinVal =
      DAG.getConstant(3 * MinExpVal + 2 * Precision, dl, ExpVT);
  SDValue ClampN_Small = DAG.getNode(ISD::SMAX, dl, ExpVT, N, ClampMinVal);
  SDValue IncN1 =
      DAG.getNode(ISD::ADD, dl, ExpVT, ClampN_Small, Increment1, NSW);

  const SDValue ScaleDownVal = DAG.getConstantFP(ScaleDownK, dl, VT);
  SDValue ScaleDown0 = DAG.getNode(ISD::FMUL, dl, VT, X, ScaleDownVal);
  SDValue ScaleDown1 = DAG.getNode(ISD::FMUL, dl, VT, ScaleDown0, ScaleDownVal);

  SDValue ScaleDownTwice = DAG.getSetCC(
      dl, SetCCVT, N, DAG.getConstant(2 * MinExpVal + Precision, dl, ExpVT),
      ISD::SETULT);

  SDValue SelectN_Small =
      DAG.getNode(ISD::SELECT, dl, ExpVT, ScaleDownTwice, IncN1, IncN0);
  SDValue SelectX_Small =
      DAG.getNode(ISD::SELECT, dl, VT, ScaleDownTwice, ScaleDown1, ScaleDown0);

  // Now combine the two out of range exponent handling cases with the base
  // case.
  SDValue NewX = DAG.getNode(
      ISD::SELECT, dl, VT, NGtMaxExp, SelectX_Big,
      DAG.getNode(ISD::SELECT, dl, VT, NLtMinExp, SelectX_Small, X));

  SDValue NewN = DAG.getNode(
      ISD::SELECT, dl, ExpVT, NGtMaxExp, SelectN_Big,
      DAG.getNode(ISD::SELECT, dl, ExpVT, NLtMinExp, SelectN_Small, N));

  SDValue BiasedN = DAG.getNode(ISD::ADD, dl, ExpVT, NewN, MaxExp, NSW);

  SDValue ExponentShiftAmt =
      DAG.getShiftAmountConstant(Precision - 1, ExpVT, dl);
  SDValue CastExpToValTy = DAG.getZExtOrTrunc(BiasedN, dl, AsIntVT);

  SDValue AsInt = DAG.getNode(ISD::SHL, dl, AsIntVT, CastExpToValTy,
                              ExponentShiftAmt, NUW_NSW);
  SDValue AsFP = DAG.getNode(ISD::BITCAST, dl, VT, AsInt);
  return DAG.getNode(ISD::FMUL, dl, VT, NewX, AsFP);
}

SDValue SelectionDAGLegalize::expandFrexp(SDNode *Node) const {
  SDLoc dl(Node);
  SDValue Val = Node->getOperand(0);
  EVT VT = Val.getValueType();
  EVT ExpVT = Node->getValueType(1);
  EVT AsIntVT = VT.changeTypeToInteger();
  if (AsIntVT == EVT()) // TODO: How to handle f80?
    return SDValue();

  const fltSemantics &FltSem = SelectionDAG::EVTToAPFloatSemantics(VT);
  const APFloat::ExponentType MinExpVal = APFloat::semanticsMinExponent(FltSem);
  const unsigned Precision = APFloat::semanticsPrecision(FltSem);
  const unsigned BitSize = VT.getScalarSizeInBits();

  // TODO: Could introduce control flow and skip over the denormal handling.

  // scale_up = fmul value, scalbn(1.0, precision + 1)
  // extracted_exp = (bitcast value to uint) >> precision - 1
  // biased_exp = extracted_exp + min_exp
  // extracted_fract = (bitcast value to uint) & (fract_mask | sign_mask)
  //
  // is_denormal = val < smallest_normalized
  // computed_fract = is_denormal ? scale_up : extracted_fract
  // computed_exp = is_denormal ? biased_exp + (-precision - 1) : biased_exp
  //
  // result_0 =  (!isfinite(val) || iszero(val)) ? val : computed_fract
  // result_1 =  (!isfinite(val) || iszero(val)) ? 0 : computed_exp

  SDValue NegSmallestNormalizedInt = DAG.getConstant(
      APFloat::getSmallestNormalized(FltSem, true).bitcastToAPInt(), dl,
      AsIntVT);

  SDValue SmallestNormalizedInt = DAG.getConstant(
      APFloat::getSmallestNormalized(FltSem, false).bitcastToAPInt(), dl,
      AsIntVT);

  // Masks out the exponent bits.
  SDValue ExpMask =
      DAG.getConstant(APFloat::getInf(FltSem).bitcastToAPInt(), dl, AsIntVT);

  // Mask out the exponent part of the value.
  //
  // e.g, for f32 FractSignMaskVal = 0x807fffff
  APInt FractSignMaskVal = APInt::getBitsSet(BitSize, 0, Precision - 1);
  FractSignMaskVal.setBit(BitSize - 1); // Set the sign bit

  APInt SignMaskVal = APInt::getSignedMaxValue(BitSize);
  SDValue SignMask = DAG.getConstant(SignMaskVal, dl, AsIntVT);

  SDValue FractSignMask = DAG.getConstant(FractSignMaskVal, dl, AsIntVT);

  const APFloat One(FltSem, "1.0");
  // Scale a possible denormal input.
  // e.g., for f64, 0x1p+54
  APFloat ScaleUpKVal =
      scalbn(One, Precision + 1, APFloat::rmNearestTiesToEven);

  SDValue ScaleUpK = DAG.getConstantFP(ScaleUpKVal, dl, VT);
  SDValue ScaleUp = DAG.getNode(ISD::FMUL, dl, VT, Val, ScaleUpK);

  EVT SetCCVT =
      TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  SDValue AsInt = DAG.getNode(ISD::BITCAST, dl, AsIntVT, Val);

  SDValue Abs = DAG.getNode(ISD::AND, dl, AsIntVT, AsInt, SignMask);

  SDValue AddNegSmallestNormal =
      DAG.getNode(ISD::ADD, dl, AsIntVT, Abs, NegSmallestNormalizedInt);
  SDValue DenormOrZero = DAG.getSetCC(dl, SetCCVT, AddNegSmallestNormal,
                                      NegSmallestNormalizedInt, ISD::SETULE);

  SDValue IsDenormal =
      DAG.getSetCC(dl, SetCCVT, Abs, SmallestNormalizedInt, ISD::SETULT);

  SDValue MinExp = DAG.getConstant(MinExpVal, dl, ExpVT);
  SDValue Zero = DAG.getConstant(0, dl, ExpVT);

  SDValue ScaledAsInt = DAG.getNode(ISD::BITCAST, dl, AsIntVT, ScaleUp);
  SDValue ScaledSelect =
      DAG.getNode(ISD::SELECT, dl, AsIntVT, IsDenormal, ScaledAsInt, AsInt);

  SDValue ExpMaskScaled =
      DAG.getNode(ISD::AND, dl, AsIntVT, ScaledAsInt, ExpMask);

  SDValue ScaledValue =
      DAG.getNode(ISD::SELECT, dl, AsIntVT, IsDenormal, ExpMaskScaled, Abs);

  // Extract the exponent bits.
  SDValue ExponentShiftAmt =
      DAG.getShiftAmountConstant(Precision - 1, AsIntVT, dl);
  SDValue ShiftedExp =
      DAG.getNode(ISD::SRL, dl, AsIntVT, ScaledValue, ExponentShiftAmt);
  SDValue Exp = DAG.getSExtOrTrunc(ShiftedExp, dl, ExpVT);

  SDValue NormalBiasedExp = DAG.getNode(ISD::ADD, dl, ExpVT, Exp, MinExp);
  SDValue DenormalOffset = DAG.getConstant(-Precision - 1, dl, ExpVT);
  SDValue DenormalExpBias =
      DAG.getNode(ISD::SELECT, dl, ExpVT, IsDenormal, DenormalOffset, Zero);

  SDValue MaskedFractAsInt =
      DAG.getNode(ISD::AND, dl, AsIntVT, ScaledSelect, FractSignMask);
  const APFloat Half(FltSem, "0.5");
  SDValue FPHalf = DAG.getConstant(Half.bitcastToAPInt(), dl, AsIntVT);
  SDValue Or = DAG.getNode(ISD::OR, dl, AsIntVT, MaskedFractAsInt, FPHalf);
  SDValue MaskedFract = DAG.getNode(ISD::BITCAST, dl, VT, Or);

  SDValue ComputedExp =
      DAG.getNode(ISD::ADD, dl, ExpVT, NormalBiasedExp, DenormalExpBias);

  SDValue Result0 =
      DAG.getNode(ISD::SELECT, dl, VT, DenormOrZero, Val, MaskedFract);

  SDValue Result1 =
      DAG.getNode(ISD::SELECT, dl, ExpVT, DenormOrZero, Zero, ComputedExp);

  return DAG.getMergeValues({Result0, Result1}, dl);
}

/// This function is responsible for legalizing a
/// INT_TO_FP operation of the specified operand when the target requests that
/// we expand it.  At this point, we know that the result and operand types are
/// legal for the target.
SDValue SelectionDAGLegalize::ExpandLegalINT_TO_FP(SDNode *Node,
                                                   SDValue &Chain) {
  bool isSigned = (Node->getOpcode() == ISD::STRICT_SINT_TO_FP ||
                   Node->getOpcode() == ISD::SINT_TO_FP);
  EVT DestVT = Node->getValueType(0);
  SDLoc dl(Node);
  unsigned OpNo = Node->isStrictFPOpcode() ? 1 : 0;
  SDValue Op0 = Node->getOperand(OpNo);
  EVT SrcVT = Op0.getValueType();

  // TODO: Should any fast-math-flags be set for the created nodes?
  LLVM_DEBUG(dbgs() << "Legalizing INT_TO_FP\n");
  if (SrcVT == MVT::i32 && TLI.isTypeLegal(MVT::f64) &&
      (DestVT.bitsLE(MVT::f64) ||
       TLI.isOperationLegal(Node->isStrictFPOpcode() ? ISD::STRICT_FP_EXTEND
                                                     : ISD::FP_EXTEND,
                            DestVT))) {
    LLVM_DEBUG(dbgs() << "32-bit [signed|unsigned] integer to float/double "
                         "expansion\n");

    // Get the stack frame index of a 8 byte buffer.
    SDValue StackSlot = DAG.CreateStackTemporary(MVT::f64);

    SDValue Lo = Op0;
    // if signed map to unsigned space
    if (isSigned) {
      // Invert sign bit (signed to unsigned mapping).
      Lo = DAG.getNode(ISD::XOR, dl, MVT::i32, Lo,
                       DAG.getConstant(0x80000000u, dl, MVT::i32));
    }
    // Initial hi portion of constructed double.
    SDValue Hi = DAG.getConstant(0x43300000u, dl, MVT::i32);

    // If this a big endian target, swap the lo and high data.
    if (DAG.getDataLayout().isBigEndian())
      std::swap(Lo, Hi);

    SDValue MemChain = DAG.getEntryNode();

    // Store the lo of the constructed double.
    SDValue Store1 = DAG.getStore(MemChain, dl, Lo, StackSlot,
                                  MachinePointerInfo());
    // Store the hi of the constructed double.
    SDValue HiPtr =
        DAG.getMemBasePlusOffset(StackSlot, TypeSize::getFixed(4), dl);
    SDValue Store2 =
        DAG.getStore(MemChain, dl, Hi, HiPtr, MachinePointerInfo());
    MemChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Store1, Store2);

    // load the constructed double
    SDValue Load =
        DAG.getLoad(MVT::f64, dl, MemChain, StackSlot, MachinePointerInfo());
    // FP constant to bias correct the final result
    SDValue Bias = DAG.getConstantFP(
        isSigned ? llvm::bit_cast<double>(0x4330000080000000ULL)
                 : llvm::bit_cast<double>(0x4330000000000000ULL),
        dl, MVT::f64);
    // Subtract the bias and get the final result.
    SDValue Sub;
    SDValue Result;
    if (Node->isStrictFPOpcode()) {
      Sub = DAG.getNode(ISD::STRICT_FSUB, dl, {MVT::f64, MVT::Other},
                        {Node->getOperand(0), Load, Bias});
      Chain = Sub.getValue(1);
      if (DestVT != Sub.getValueType()) {
        std::pair<SDValue, SDValue> ResultPair;
        ResultPair =
            DAG.getStrictFPExtendOrRound(Sub, Chain, dl, DestVT);
        Result = ResultPair.first;
        Chain = ResultPair.second;
      }
      else
        Result = Sub;
    } else {
      Sub = DAG.getNode(ISD::FSUB, dl, MVT::f64, Load, Bias);
      Result = DAG.getFPExtendOrRound(Sub, dl, DestVT);
    }
    return Result;
  }

  if (isSigned)
    return SDValue();

  // TODO: Generalize this for use with other types.
  if (((SrcVT == MVT::i32 || SrcVT == MVT::i64) && DestVT == MVT::f32) ||
      (SrcVT == MVT::i64 && DestVT == MVT::f64)) {
    LLVM_DEBUG(dbgs() << "Converting unsigned i32/i64 to f32/f64\n");
    // For unsigned conversions, convert them to signed conversions using the
    // algorithm from the x86_64 __floatundisf in compiler_rt. That method
    // should be valid for i32->f32 as well.

    // More generally this transform should be valid if there are 3 more bits
    // in the integer type than the significand. Rounding uses the first bit
    // after the width of the significand and the OR of all bits after that. So
    // we need to be able to OR the shifted out bit into one of the bits that
    // participate in the OR.

    // TODO: This really should be implemented using a branch rather than a
    // select.  We happen to get lucky and machinesink does the right
    // thing most of the time.  This would be a good candidate for a
    // pseudo-op, or, even better, for whole-function isel.
    EVT SetCCVT = getSetCCResultType(SrcVT);

    SDValue SignBitTest = DAG.getSetCC(
        dl, SetCCVT, Op0, DAG.getConstant(0, dl, SrcVT), ISD::SETLT);

    EVT ShiftVT = TLI.getShiftAmountTy(SrcVT, DAG.getDataLayout());
    SDValue ShiftConst = DAG.getConstant(1, dl, ShiftVT);
    SDValue Shr = DAG.getNode(ISD::SRL, dl, SrcVT, Op0, ShiftConst);
    SDValue AndConst = DAG.getConstant(1, dl, SrcVT);
    SDValue And = DAG.getNode(ISD::AND, dl, SrcVT, Op0, AndConst);
    SDValue Or = DAG.getNode(ISD::OR, dl, SrcVT, And, Shr);

    SDValue Slow, Fast;
    if (Node->isStrictFPOpcode()) {
      // In strict mode, we must avoid spurious exceptions, and therefore
      // must make sure to only emit a single STRICT_SINT_TO_FP.
      SDValue InCvt = DAG.getSelect(dl, SrcVT, SignBitTest, Or, Op0);
      Fast = DAG.getNode(ISD::STRICT_SINT_TO_FP, dl, { DestVT, MVT::Other },
                         { Node->getOperand(0), InCvt });
      Slow = DAG.getNode(ISD::STRICT_FADD, dl, { DestVT, MVT::Other },
                         { Fast.getValue(1), Fast, Fast });
      Chain = Slow.getValue(1);
      // The STRICT_SINT_TO_FP inherits the exception mode from the
      // incoming STRICT_UINT_TO_FP node; the STRICT_FADD node can
      // never raise any exception.
      SDNodeFlags Flags;
      Flags.setNoFPExcept(Node->getFlags().hasNoFPExcept());
      Fast->setFlags(Flags);
      Flags.setNoFPExcept(true);
      Slow->setFlags(Flags);
    } else {
      SDValue SignCvt = DAG.getNode(ISD::SINT_TO_FP, dl, DestVT, Or);
      Slow = DAG.getNode(ISD::FADD, dl, DestVT, SignCvt, SignCvt);
      Fast = DAG.getNode(ISD::SINT_TO_FP, dl, DestVT, Op0);
    }

    return DAG.getSelect(dl, DestVT, SignBitTest, Slow, Fast);
  }

  // Don't expand it if there isn't cheap fadd.
  if (!TLI.isOperationLegalOrCustom(
          Node->isStrictFPOpcode() ? ISD::STRICT_FADD : ISD::FADD, DestVT))
    return SDValue();

  // The following optimization is valid only if every value in SrcVT (when
  // treated as signed) is representable in DestVT.  Check that the mantissa
  // size of DestVT is >= than the number of bits in SrcVT -1.
  assert(APFloat::semanticsPrecision(DAG.EVTToAPFloatSemantics(DestVT)) >=
             SrcVT.getSizeInBits() - 1 &&
         "Cannot perform lossless SINT_TO_FP!");

  SDValue Tmp1;
  if (Node->isStrictFPOpcode()) {
    Tmp1 = DAG.getNode(ISD::STRICT_SINT_TO_FP, dl, { DestVT, MVT::Other },
                       { Node->getOperand(0), Op0 });
  } else
    Tmp1 = DAG.getNode(ISD::SINT_TO_FP, dl, DestVT, Op0);

  SDValue SignSet = DAG.getSetCC(dl, getSetCCResultType(SrcVT), Op0,
                                 DAG.getConstant(0, dl, SrcVT), ISD::SETLT);
  SDValue Zero = DAG.getIntPtrConstant(0, dl),
          Four = DAG.getIntPtrConstant(4, dl);
  SDValue CstOffset = DAG.getSelect(dl, Zero.getValueType(),
                                    SignSet, Four, Zero);

  // If the sign bit of the integer is set, the large number will be treated
  // as a negative number.  To counteract this, the dynamic code adds an
  // offset depending on the data type.
  uint64_t FF;
  switch (SrcVT.getSimpleVT().SimpleTy) {
  default:
    return SDValue();
  case MVT::i8 : FF = 0x43800000ULL; break;  // 2^8  (as a float)
  case MVT::i16: FF = 0x47800000ULL; break;  // 2^16 (as a float)
  case MVT::i32: FF = 0x4F800000ULL; break;  // 2^32 (as a float)
  case MVT::i64: FF = 0x5F800000ULL; break;  // 2^64 (as a float)
  }
  if (DAG.getDataLayout().isLittleEndian())
    FF <<= 32;
  Constant *FudgeFactor = ConstantInt::get(
                                       Type::getInt64Ty(*DAG.getContext()), FF);

  SDValue CPIdx =
      DAG.getConstantPool(FudgeFactor, TLI.getPointerTy(DAG.getDataLayout()));
  Align Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlign();
  CPIdx = DAG.getNode(ISD::ADD, dl, CPIdx.getValueType(), CPIdx, CstOffset);
  Alignment = commonAlignment(Alignment, 4);
  SDValue FudgeInReg;
  if (DestVT == MVT::f32)
    FudgeInReg = DAG.getLoad(
        MVT::f32, dl, DAG.getEntryNode(), CPIdx,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()),
        Alignment);
  else {
    SDValue Load = DAG.getExtLoad(
        ISD::EXTLOAD, dl, DestVT, DAG.getEntryNode(), CPIdx,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), MVT::f32,
        Alignment);
    HandleSDNode Handle(Load);
    LegalizeOp(Load.getNode());
    FudgeInReg = Handle.getValue();
  }

  if (Node->isStrictFPOpcode()) {
    SDValue Result = DAG.getNode(ISD::STRICT_FADD, dl, { DestVT, MVT::Other },
                                 { Tmp1.getValue(1), Tmp1, FudgeInReg });
    Chain = Result.getValue(1);
    return Result;
  }

  return DAG.getNode(ISD::FADD, dl, DestVT, Tmp1, FudgeInReg);
}

/// This function is responsible for legalizing a
/// *INT_TO_FP operation of the specified operand when the target requests that
/// we promote it.  At this point, we know that the result and operand types are
/// legal for the target, and that there is a legal UINT_TO_FP or SINT_TO_FP
/// operation that takes a larger input.
void SelectionDAGLegalize::PromoteLegalINT_TO_FP(
    SDNode *N, const SDLoc &dl, SmallVectorImpl<SDValue> &Results) {
  bool IsStrict = N->isStrictFPOpcode();
  bool IsSigned = N->getOpcode() == ISD::SINT_TO_FP ||
                  N->getOpcode() == ISD::STRICT_SINT_TO_FP;
  EVT DestVT = N->getValueType(0);
  SDValue LegalOp = N->getOperand(IsStrict ? 1 : 0);
  unsigned UIntOp = IsStrict ? ISD::STRICT_UINT_TO_FP : ISD::UINT_TO_FP;
  unsigned SIntOp = IsStrict ? ISD::STRICT_SINT_TO_FP : ISD::SINT_TO_FP;

  // First step, figure out the appropriate *INT_TO_FP operation to use.
  EVT NewInTy = LegalOp.getValueType();

  unsigned OpToUse = 0;

  // Scan for the appropriate larger type to use.
  while (true) {
    NewInTy = (MVT::SimpleValueType)(NewInTy.getSimpleVT().SimpleTy+1);
    assert(NewInTy.isInteger() && "Ran out of possibilities!");

    // If the target supports SINT_TO_FP of this type, use it.
    if (TLI.isOperationLegalOrCustom(SIntOp, NewInTy)) {
      OpToUse = SIntOp;
      break;
    }
    if (IsSigned)
      continue;

    // If the target supports UINT_TO_FP of this type, use it.
    if (TLI.isOperationLegalOrCustom(UIntOp, NewInTy)) {
      OpToUse = UIntOp;
      break;
    }

    // Otherwise, try a larger type.
  }

  // Okay, we found the operation and type to use.  Zero extend our input to the
  // desired type then run the operation on it.
  if (IsStrict) {
    SDValue Res =
        DAG.getNode(OpToUse, dl, {DestVT, MVT::Other},
                    {N->getOperand(0),
                     DAG.getNode(IsSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND,
                                 dl, NewInTy, LegalOp)});
    Results.push_back(Res);
    Results.push_back(Res.getValue(1));
    return;
  }

  Results.push_back(
      DAG.getNode(OpToUse, dl, DestVT,
                  DAG.getNode(IsSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND,
                              dl, NewInTy, LegalOp)));
}

/// This function is responsible for legalizing a
/// FP_TO_*INT operation of the specified operand when the target requests that
/// we promote it.  At this point, we know that the result and operand types are
/// legal for the target, and that there is a legal FP_TO_UINT or FP_TO_SINT
/// operation that returns a larger result.
void SelectionDAGLegalize::PromoteLegalFP_TO_INT(SDNode *N, const SDLoc &dl,
                                                 SmallVectorImpl<SDValue> &Results) {
  bool IsStrict = N->isStrictFPOpcode();
  bool IsSigned = N->getOpcode() == ISD::FP_TO_SINT ||
                  N->getOpcode() == ISD::STRICT_FP_TO_SINT;
  EVT DestVT = N->getValueType(0);
  SDValue LegalOp = N->getOperand(IsStrict ? 1 : 0);
  // First step, figure out the appropriate FP_TO*INT operation to use.
  EVT NewOutTy = DestVT;

  unsigned OpToUse = 0;

  // Scan for the appropriate larger type to use.
  while (true) {
    NewOutTy = (MVT::SimpleValueType)(NewOutTy.getSimpleVT().SimpleTy+1);
    assert(NewOutTy.isInteger() && "Ran out of possibilities!");

    // A larger signed type can hold all unsigned values of the requested type,
    // so using FP_TO_SINT is valid
    OpToUse = IsStrict ? ISD::STRICT_FP_TO_SINT : ISD::FP_TO_SINT;
    if (TLI.isOperationLegalOrCustom(OpToUse, NewOutTy))
      break;

    // However, if the value may be < 0.0, we *must* use some FP_TO_SINT.
    OpToUse = IsStrict ? ISD::STRICT_FP_TO_UINT : ISD::FP_TO_UINT;
    if (!IsSigned && TLI.isOperationLegalOrCustom(OpToUse, NewOutTy))
      break;

    // Otherwise, try a larger type.
  }

  // Okay, we found the operation and type to use.
  SDValue Operation;
  if (IsStrict) {
    SDVTList VTs = DAG.getVTList(NewOutTy, MVT::Other);
    Operation = DAG.getNode(OpToUse, dl, VTs, N->getOperand(0), LegalOp);
  } else
    Operation = DAG.getNode(OpToUse, dl, NewOutTy, LegalOp);

  // Truncate the result of the extended FP_TO_*INT operation to the desired
  // size.
  SDValue Trunc = DAG.getNode(ISD::TRUNCATE, dl, DestVT, Operation);
  Results.push_back(Trunc);
  if (IsStrict)
    Results.push_back(Operation.getValue(1));
}

/// Promote FP_TO_*INT_SAT operation to a larger result type. At this point
/// the result and operand types are legal and there must be a legal
/// FP_TO_*INT_SAT operation for a larger result type.
SDValue SelectionDAGLegalize::PromoteLegalFP_TO_INT_SAT(SDNode *Node,
                                                        const SDLoc &dl) {
  unsigned Opcode = Node->getOpcode();

  // Scan for the appropriate larger type to use.
  EVT NewOutTy = Node->getValueType(0);
  while (true) {
    NewOutTy = (MVT::SimpleValueType)(NewOutTy.getSimpleVT().SimpleTy + 1);
    assert(NewOutTy.isInteger() && "Ran out of possibilities!");

    if (TLI.isOperationLegalOrCustom(Opcode, NewOutTy))
      break;
  }

  // Saturation width is determined by second operand, so we don't have to
  // perform any fixup and can directly truncate the result.
  SDValue Result = DAG.getNode(Opcode, dl, NewOutTy, Node->getOperand(0),
                               Node->getOperand(1));
  return DAG.getNode(ISD::TRUNCATE, dl, Node->getValueType(0), Result);
}

/// Open code the operations for PARITY of the specified operation.
SDValue SelectionDAGLegalize::ExpandPARITY(SDValue Op, const SDLoc &dl) {
  EVT VT = Op.getValueType();
  EVT ShVT = TLI.getShiftAmountTy(VT, DAG.getDataLayout());
  unsigned Sz = VT.getScalarSizeInBits();

  // If CTPOP is legal, use it. Otherwise use shifts and xor.
  SDValue Result;
  if (TLI.isOperationLegalOrPromote(ISD::CTPOP, VT)) {
    Result = DAG.getNode(ISD::CTPOP, dl, VT, Op);
  } else {
    Result = Op;
    for (unsigned i = Log2_32_Ceil(Sz); i != 0;) {
      SDValue Shift = DAG.getNode(ISD::SRL, dl, VT, Result,
                                  DAG.getConstant(1ULL << (--i), dl, ShVT));
      Result = DAG.getNode(ISD::XOR, dl, VT, Result, Shift);
    }
  }

  return DAG.getNode(ISD::AND, dl, VT, Result, DAG.getConstant(1, dl, VT));
}

SDValue SelectionDAGLegalize::PromoteReduction(SDNode *Node) {
  MVT VecVT = Node->getOperand(1).getSimpleValueType();
  MVT NewVecVT = TLI.getTypeToPromoteTo(Node->getOpcode(), VecVT);
  MVT ScalarVT = Node->getSimpleValueType(0);
  MVT NewScalarVT = NewVecVT.getVectorElementType();

  SDLoc DL(Node);
  SmallVector<SDValue, 4> Operands(Node->getNumOperands());

  // promote the initial value.
  // FIXME: Support integer.
  assert(Node->getOperand(0).getValueType().isFloatingPoint() &&
         "Only FP promotion is supported");
  Operands[0] =
      DAG.getNode(ISD::FP_EXTEND, DL, NewScalarVT, Node->getOperand(0));

  for (unsigned j = 1; j != Node->getNumOperands(); ++j)
    if (Node->getOperand(j).getValueType().isVector() &&
        !(ISD::isVPOpcode(Node->getOpcode()) &&
          ISD::getVPMaskIdx(Node->getOpcode()) == j)) { // Skip mask operand.
      // promote the vector operand.
      // FIXME: Support integer.
      assert(Node->getOperand(j).getValueType().isFloatingPoint() &&
             "Only FP promotion is supported");
      Operands[j] =
          DAG.getNode(ISD::FP_EXTEND, DL, NewVecVT, Node->getOperand(j));
    } else {
      Operands[j] = Node->getOperand(j); // Skip VL operand.
    }

  SDValue Res = DAG.getNode(Node->getOpcode(), DL, NewScalarVT, Operands,
                            Node->getFlags());

  assert(ScalarVT.isFloatingPoint() && "Only FP promotion is supported");
  return DAG.getNode(ISD::FP_ROUND, DL, ScalarVT, Res,
                     DAG.getIntPtrConstant(0, DL, /*isTarget=*/true));
}

bool SelectionDAGLegalize::ExpandNode(SDNode *Node) {
  LLVM_DEBUG(dbgs() << "Trying to expand node\n");
  SmallVector<SDValue, 8> Results;
  SDLoc dl(Node);
  SDValue Tmp1, Tmp2, Tmp3, Tmp4;
  bool NeedInvert;
  switch (Node->getOpcode()) {
  case ISD::ABS:
    if ((Tmp1 = TLI.expandABS(Node, DAG)))
      Results.push_back(Tmp1);
    break;
  case ISD::ABDS:
  case ISD::ABDU:
    if ((Tmp1 = TLI.expandABD(Node, DAG)))
      Results.push_back(Tmp1);
    break;
  case ISD::AVGCEILS:
  case ISD::AVGCEILU:
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU:
    if ((Tmp1 = TLI.expandAVG(Node, DAG)))
      Results.push_back(Tmp1);
    break;
  case ISD::CTPOP:
    if ((Tmp1 = TLI.expandCTPOP(Node, DAG)))
      Results.push_back(Tmp1);
    break;
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
    if ((Tmp1 = TLI.expandCTLZ(Node, DAG)))
      Results.push_back(Tmp1);
    break;
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
    if ((Tmp1 = TLI.expandCTTZ(Node, DAG)))
      Results.push_back(Tmp1);
    break;
  case ISD::BITREVERSE:
    if ((Tmp1 = TLI.expandBITREVERSE(Node, DAG)))
      Results.push_back(Tmp1);
    break;
  case ISD::BSWAP:
    if ((Tmp1 = TLI.expandBSWAP(Node, DAG)))
      Results.push_back(Tmp1);
    break;
  case ISD::PARITY:
    Results.push_back(ExpandPARITY(Node->getOperand(0), dl));
    break;
  case ISD::FRAMEADDR:
  case ISD::RETURNADDR:
  case ISD::FRAME_TO_ARGS_OFFSET:
    Results.push_back(DAG.getConstant(0, dl, Node->getValueType(0)));
    break;
  case ISD::EH_DWARF_CFA: {
    SDValue CfaArg = DAG.getSExtOrTrunc(Node->getOperand(0), dl,
                                        TLI.getPointerTy(DAG.getDataLayout()));
    SDValue Offset = DAG.getNode(ISD::ADD, dl,
                                 CfaArg.getValueType(),
                                 DAG.getNode(ISD::FRAME_TO_ARGS_OFFSET, dl,
                                             CfaArg.getValueType()),
                                 CfaArg);
    SDValue FA = DAG.getNode(
        ISD::FRAMEADDR, dl, TLI.getPointerTy(DAG.getDataLayout()),
        DAG.getConstant(0, dl, TLI.getPointerTy(DAG.getDataLayout())));
    Results.push_back(DAG.getNode(ISD::ADD, dl, FA.getValueType(),
                                  FA, Offset));
    break;
  }
  case ISD::GET_ROUNDING:
    Results.push_back(DAG.getConstant(1, dl, Node->getValueType(0)));
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::EH_RETURN:
  case ISD::EH_LABEL:
  case ISD::PREFETCH:
  case ISD::VAEND:
  case ISD::EH_SJLJ_LONGJMP:
    // If the target didn't expand these, there's nothing to do, so just
    // preserve the chain and be done.
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::READCYCLECOUNTER:
  case ISD::READSTEADYCOUNTER:
    // If the target didn't expand this, just return 'zero' and preserve the
    // chain.
    Results.append(Node->getNumValues() - 1,
                   DAG.getConstant(0, dl, Node->getValueType(0)));
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::EH_SJLJ_SETJMP:
    // If the target didn't expand this, just return 'zero' and preserve the
    // chain.
    Results.push_back(DAG.getConstant(0, dl, MVT::i32));
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::ATOMIC_LOAD: {
    // There is no libcall for atomic load; fake it with ATOMIC_CMP_SWAP.
    SDValue Zero = DAG.getConstant(0, dl, Node->getValueType(0));
    SDVTList VTs = DAG.getVTList(Node->getValueType(0), MVT::Other);
    SDValue Swap = DAG.getAtomicCmpSwap(
        ISD::ATOMIC_CMP_SWAP, dl, cast<AtomicSDNode>(Node)->getMemoryVT(), VTs,
        Node->getOperand(0), Node->getOperand(1), Zero, Zero,
        cast<AtomicSDNode>(Node)->getMemOperand());
    Results.push_back(Swap.getValue(0));
    Results.push_back(Swap.getValue(1));
    break;
  }
  case ISD::ATOMIC_STORE: {
    // There is no libcall for atomic store; fake it with ATOMIC_SWAP.
    SDValue Swap = DAG.getAtomic(
        ISD::ATOMIC_SWAP, dl, cast<AtomicSDNode>(Node)->getMemoryVT(),
        Node->getOperand(0), Node->getOperand(2), Node->getOperand(1),
        cast<AtomicSDNode>(Node)->getMemOperand());
    Results.push_back(Swap.getValue(1));
    break;
  }
  case ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS: {
    // Expanding an ATOMIC_CMP_SWAP_WITH_SUCCESS produces an ATOMIC_CMP_SWAP and
    // splits out the success value as a comparison. Expanding the resulting
    // ATOMIC_CMP_SWAP will produce a libcall.
    SDVTList VTs = DAG.getVTList(Node->getValueType(0), MVT::Other);
    SDValue Res = DAG.getAtomicCmpSwap(
        ISD::ATOMIC_CMP_SWAP, dl, cast<AtomicSDNode>(Node)->getMemoryVT(), VTs,
        Node->getOperand(0), Node->getOperand(1), Node->getOperand(2),
        Node->getOperand(3), cast<MemSDNode>(Node)->getMemOperand());

    SDValue ExtRes = Res;
    SDValue LHS = Res;
    SDValue RHS = Node->getOperand(1);

    EVT AtomicType = cast<AtomicSDNode>(Node)->getMemoryVT();
    EVT OuterType = Node->getValueType(0);
    switch (TLI.getExtendForAtomicOps()) {
    case ISD::SIGN_EXTEND:
      LHS = DAG.getNode(ISD::AssertSext, dl, OuterType, Res,
                        DAG.getValueType(AtomicType));
      RHS = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, OuterType,
                        Node->getOperand(2), DAG.getValueType(AtomicType));
      ExtRes = LHS;
      break;
    case ISD::ZERO_EXTEND:
      LHS = DAG.getNode(ISD::AssertZext, dl, OuterType, Res,
                        DAG.getValueType(AtomicType));
      RHS = DAG.getZeroExtendInReg(Node->getOperand(2), dl, AtomicType);
      ExtRes = LHS;
      break;
    case ISD::ANY_EXTEND:
      LHS = DAG.getZeroExtendInReg(Res, dl, AtomicType);
      RHS = DAG.getZeroExtendInReg(Node->getOperand(2), dl, AtomicType);
      break;
    default:
      llvm_unreachable("Invalid atomic op extension");
    }

    SDValue Success =
        DAG.getSetCC(dl, Node->getValueType(1), LHS, RHS, ISD::SETEQ);

    Results.push_back(ExtRes.getValue(0));
    Results.push_back(Success);
    Results.push_back(Res.getValue(1));
    break;
  }
  case ISD::ATOMIC_LOAD_SUB: {
    SDLoc DL(Node);
    EVT VT = Node->getValueType(0);
    SDValue RHS = Node->getOperand(2);
    AtomicSDNode *AN = cast<AtomicSDNode>(Node);
    if (RHS->getOpcode() == ISD::SIGN_EXTEND_INREG &&
        cast<VTSDNode>(RHS->getOperand(1))->getVT() == AN->getMemoryVT())
      RHS = RHS->getOperand(0);
    SDValue NewRHS =
        DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), RHS);
    SDValue Res = DAG.getAtomic(ISD::ATOMIC_LOAD_ADD, DL, AN->getMemoryVT(),
                                Node->getOperand(0), Node->getOperand(1),
                                NewRHS, AN->getMemOperand());
    Results.push_back(Res);
    Results.push_back(Res.getValue(1));
    break;
  }
  case ISD::DYNAMIC_STACKALLOC:
    ExpandDYNAMIC_STACKALLOC(Node, Results);
    break;
  case ISD::MERGE_VALUES:
    for (unsigned i = 0; i < Node->getNumValues(); i++)
      Results.push_back(Node->getOperand(i));
    break;
  case ISD::UNDEF: {
    EVT VT = Node->getValueType(0);
    if (VT.isInteger())
      Results.push_back(DAG.getConstant(0, dl, VT));
    else {
      assert(VT.isFloatingPoint() && "Unknown value type!");
      Results.push_back(DAG.getConstantFP(0, dl, VT));
    }
    break;
  }
  case ISD::STRICT_FP_ROUND:
    // When strict mode is enforced we can't do expansion because it
    // does not honor the "strict" properties. Only libcall is allowed.
    if (TLI.isStrictFPEnabled())
      break;
    // We might as well mutate to FP_ROUND when FP_ROUND operation is legal
    // since this operation is more efficient than stack operation.
    if (TLI.getStrictFPOperationAction(Node->getOpcode(),
                                       Node->getValueType(0))
        == TargetLowering::Legal)
      break;
    // We fall back to use stack operation when the FP_ROUND operation
    // isn't available.
    if ((Tmp1 = EmitStackConvert(Node->getOperand(1), Node->getValueType(0),
                                 Node->getValueType(0), dl,
                                 Node->getOperand(0)))) {
      ReplaceNode(Node, Tmp1.getNode());
      LLVM_DEBUG(dbgs() << "Successfully expanded STRICT_FP_ROUND node\n");
      return true;
    }
    break;
  case ISD::FP_ROUND: {
    if ((Tmp1 = TLI.expandFP_ROUND(Node, DAG))) {
      Results.push_back(Tmp1);
      break;
    }

    [[fallthrough]];
  }
  case ISD::BITCAST:
    if ((Tmp1 = EmitStackConvert(Node->getOperand(0), Node->getValueType(0),
                                 Node->getValueType(0), dl)))
      Results.push_back(Tmp1);
    break;
  case ISD::STRICT_FP_EXTEND:
    // When strict mode is enforced we can't do expansion because it
    // does not honor the "strict" properties. Only libcall is allowed.
    if (TLI.isStrictFPEnabled())
      break;
    // We might as well mutate to FP_EXTEND when FP_EXTEND operation is legal
    // since this operation is more efficient than stack operation.
    if (TLI.getStrictFPOperationAction(Node->getOpcode(),
                                       Node->getValueType(0))
        == TargetLowering::Legal)
      break;
    // We fall back to use stack operation when the FP_EXTEND operation
    // isn't available.
    if ((Tmp1 = EmitStackConvert(
             Node->getOperand(1), Node->getOperand(1).getValueType(),
             Node->getValueType(0), dl, Node->getOperand(0)))) {
      ReplaceNode(Node, Tmp1.getNode());
      LLVM_DEBUG(dbgs() << "Successfully expanded STRICT_FP_EXTEND node\n");
      return true;
    }
    break;
  case ISD::FP_EXTEND: {
    SDValue Op = Node->getOperand(0);
    EVT SrcVT = Op.getValueType();
    EVT DstVT = Node->getValueType(0);
    if (SrcVT.getScalarType() == MVT::bf16) {
      Results.push_back(DAG.getNode(ISD::BF16_TO_FP, SDLoc(Node), DstVT, Op));
      break;
    }

    if ((Tmp1 = EmitStackConvert(Op, SrcVT, DstVT, dl)))
      Results.push_back(Tmp1);
    break;
  }
  case ISD::BF16_TO_FP: {
    // Always expand bf16 to f32 casts, they lower to ext + shift.
    //
    // Note that the operand of this code can be bf16 or an integer type in case
    // bf16 is not supported on the target and was softened.
    SDValue Op = Node->getOperand(0);
    if (Op.getValueType() == MVT::bf16) {
      Op = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i32,
                       DAG.getNode(ISD::BITCAST, dl, MVT::i16, Op));
    } else {
      Op = DAG.getAnyExtOrTrunc(Op, dl, MVT::i32);
    }
    Op = DAG.getNode(
        ISD::SHL, dl, MVT::i32, Op,
        DAG.getConstant(16, dl,
                        TLI.getShiftAmountTy(MVT::i32, DAG.getDataLayout())));
    Op = DAG.getNode(ISD::BITCAST, dl, MVT::f32, Op);
    // Add fp_extend in case the output is bigger than f32.
    if (Node->getValueType(0) != MVT::f32)
      Op = DAG.getNode(ISD::FP_EXTEND, dl, Node->getValueType(0), Op);
    Results.push_back(Op);
    break;
  }
  case ISD::FP_TO_BF16: {
    SDValue Op = Node->getOperand(0);
    if (Op.getValueType() != MVT::f32)
      Op = DAG.getNode(ISD::FP_ROUND, dl, MVT::f32, Op,
                       DAG.getIntPtrConstant(0, dl, /*isTarget=*/true));
    // Certain SNaNs will turn into infinities if we do a simple shift right.
    if (!DAG.isKnownNeverSNaN(Op)) {
      Op = DAG.getNode(ISD::FCANONICALIZE, dl, MVT::f32, Op, Node->getFlags());
    }
    Op = DAG.getNode(
        ISD::SRL, dl, MVT::i32, DAG.getNode(ISD::BITCAST, dl, MVT::i32, Op),
        DAG.getConstant(16, dl,
                        TLI.getShiftAmountTy(MVT::i32, DAG.getDataLayout())));
    // The result of this node can be bf16 or an integer type in case bf16 is
    // not supported on the target and was softened to i16 for storage.
    if (Node->getValueType(0) == MVT::bf16) {
      Op = DAG.getNode(ISD::BITCAST, dl, MVT::bf16,
                       DAG.getNode(ISD::TRUNCATE, dl, MVT::i16, Op));
    } else {
      Op = DAG.getAnyExtOrTrunc(Op, dl, Node->getValueType(0));
    }
    Results.push_back(Op);
    break;
  }
  case ISD::SIGN_EXTEND_INREG: {
    EVT ExtraVT = cast<VTSDNode>(Node->getOperand(1))->getVT();
    EVT VT = Node->getValueType(0);

    // An in-register sign-extend of a boolean is a negation:
    // 'true' (1) sign-extended is -1.
    // 'false' (0) sign-extended is 0.
    // However, we must mask the high bits of the source operand because the
    // SIGN_EXTEND_INREG does not guarantee that the high bits are already zero.

    // TODO: Do this for vectors too?
    if (ExtraVT.isScalarInteger() && ExtraVT.getSizeInBits() == 1) {
      SDValue One = DAG.getConstant(1, dl, VT);
      SDValue And = DAG.getNode(ISD::AND, dl, VT, Node->getOperand(0), One);
      SDValue Zero = DAG.getConstant(0, dl, VT);
      SDValue Neg = DAG.getNode(ISD::SUB, dl, VT, Zero, And);
      Results.push_back(Neg);
      break;
    }

    // NOTE: we could fall back on load/store here too for targets without
    // SRA.  However, it is doubtful that any exist.
    EVT ShiftAmountTy = TLI.getShiftAmountTy(VT, DAG.getDataLayout());
    unsigned BitsDiff = VT.getScalarSizeInBits() -
                        ExtraVT.getScalarSizeInBits();
    SDValue ShiftCst = DAG.getConstant(BitsDiff, dl, ShiftAmountTy);
    Tmp1 = DAG.getNode(ISD::SHL, dl, Node->getValueType(0),
                       Node->getOperand(0), ShiftCst);
    Tmp1 = DAG.getNode(ISD::SRA, dl, Node->getValueType(0), Tmp1, ShiftCst);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::UINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
    if (TLI.expandUINT_TO_FP(Node, Tmp1, Tmp2, DAG)) {
      Results.push_back(Tmp1);
      if (Node->isStrictFPOpcode())
        Results.push_back(Tmp2);
      break;
    }
    [[fallthrough]];
  case ISD::SINT_TO_FP:
  case ISD::STRICT_SINT_TO_FP:
    if ((Tmp1 = ExpandLegalINT_TO_FP(Node, Tmp2))) {
      Results.push_back(Tmp1);
      if (Node->isStrictFPOpcode())
        Results.push_back(Tmp2);
    }
    break;
  case ISD::FP_TO_SINT:
    if (TLI.expandFP_TO_SINT(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::STRICT_FP_TO_SINT:
    if (TLI.expandFP_TO_SINT(Node, Tmp1, DAG)) {
      ReplaceNode(Node, Tmp1.getNode());
      LLVM_DEBUG(dbgs() << "Successfully expanded STRICT_FP_TO_SINT node\n");
      return true;
    }
    break;
  case ISD::FP_TO_UINT:
    if (TLI.expandFP_TO_UINT(Node, Tmp1, Tmp2, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::STRICT_FP_TO_UINT:
    if (TLI.expandFP_TO_UINT(Node, Tmp1, Tmp2, DAG)) {
      // Relink the chain.
      DAG.ReplaceAllUsesOfValueWith(SDValue(Node,1), Tmp2);
      // Replace the new UINT result.
      ReplaceNodeWithValue(SDValue(Node, 0), Tmp1);
      LLVM_DEBUG(dbgs() << "Successfully expanded STRICT_FP_TO_UINT node\n");
      return true;
    }
    break;
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
    Results.push_back(TLI.expandFP_TO_INT_SAT(Node, DAG));
    break;
  case ISD::VAARG:
    Results.push_back(DAG.expandVAArg(Node));
    Results.push_back(Results[0].getValue(1));
    break;
  case ISD::VACOPY:
    Results.push_back(DAG.expandVACopy(Node));
    break;
  case ISD::EXTRACT_VECTOR_ELT:
    if (Node->getOperand(0).getValueType().getVectorElementCount().isScalar())
      // This must be an access of the only element.  Return it.
      Tmp1 = DAG.getNode(ISD::BITCAST, dl, Node->getValueType(0),
                         Node->getOperand(0));
    else
      Tmp1 = ExpandExtractFromVectorThroughStack(SDValue(Node, 0));
    Results.push_back(Tmp1);
    break;
  case ISD::EXTRACT_SUBVECTOR:
    Results.push_back(ExpandExtractFromVectorThroughStack(SDValue(Node, 0)));
    break;
  case ISD::INSERT_SUBVECTOR:
    Results.push_back(ExpandInsertToVectorThroughStack(SDValue(Node, 0)));
    break;
  case ISD::CONCAT_VECTORS:
    Results.push_back(ExpandVectorBuildThroughStack(Node));
    break;
  case ISD::SCALAR_TO_VECTOR:
    Results.push_back(ExpandSCALAR_TO_VECTOR(Node));
    break;
  case ISD::INSERT_VECTOR_ELT:
    Results.push_back(ExpandINSERT_VECTOR_ELT(SDValue(Node, 0)));
    break;
  case ISD::VECTOR_SHUFFLE: {
    SmallVector<int, 32> NewMask;
    ArrayRef<int> Mask = cast<ShuffleVectorSDNode>(Node)->getMask();

    EVT VT = Node->getValueType(0);
    EVT EltVT = VT.getVectorElementType();
    SDValue Op0 = Node->getOperand(0);
    SDValue Op1 = Node->getOperand(1);
    if (!TLI.isTypeLegal(EltVT)) {
      EVT NewEltVT = TLI.getTypeToTransformTo(*DAG.getContext(), EltVT);

      // BUILD_VECTOR operands are allowed to be wider than the element type.
      // But if NewEltVT is smaller that EltVT the BUILD_VECTOR does not accept
      // it.
      if (NewEltVT.bitsLT(EltVT)) {
        // Convert shuffle node.
        // If original node was v4i64 and the new EltVT is i32,
        // cast operands to v8i32 and re-build the mask.

        // Calculate new VT, the size of the new VT should be equal to original.
        EVT NewVT =
            EVT::getVectorVT(*DAG.getContext(), NewEltVT,
                             VT.getSizeInBits() / NewEltVT.getSizeInBits());
        assert(NewVT.bitsEq(VT));

        // cast operands to new VT
        Op0 = DAG.getNode(ISD::BITCAST, dl, NewVT, Op0);
        Op1 = DAG.getNode(ISD::BITCAST, dl, NewVT, Op1);

        // Convert the shuffle mask
        unsigned int factor =
                         NewVT.getVectorNumElements()/VT.getVectorNumElements();

        // EltVT gets smaller
        assert(factor > 0);

        for (unsigned i = 0; i < VT.getVectorNumElements(); ++i) {
          if (Mask[i] < 0) {
            for (unsigned fi = 0; fi < factor; ++fi)
              NewMask.push_back(Mask[i]);
          }
          else {
            for (unsigned fi = 0; fi < factor; ++fi)
              NewMask.push_back(Mask[i]*factor+fi);
          }
        }
        Mask = NewMask;
        VT = NewVT;
      }
      EltVT = NewEltVT;
    }
    unsigned NumElems = VT.getVectorNumElements();
    SmallVector<SDValue, 16> Ops;
    for (unsigned i = 0; i != NumElems; ++i) {
      if (Mask[i] < 0) {
        Ops.push_back(DAG.getUNDEF(EltVT));
        continue;
      }
      unsigned Idx = Mask[i];
      if (Idx < NumElems)
        Ops.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op0,
                                  DAG.getVectorIdxConstant(Idx, dl)));
      else
        Ops.push_back(
            DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op1,
                        DAG.getVectorIdxConstant(Idx - NumElems, dl)));
    }

    Tmp1 = DAG.getBuildVector(VT, dl, Ops);
    // We may have changed the BUILD_VECTOR type. Cast it back to the Node type.
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, Node->getValueType(0), Tmp1);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::VECTOR_SPLICE: {
    Results.push_back(TLI.expandVectorSplice(Node, DAG));
    break;
  }
  case ISD::EXTRACT_ELEMENT: {
    EVT OpTy = Node->getOperand(0).getValueType();
    if (Node->getConstantOperandVal(1)) {
      // 1 -> Hi
      Tmp1 = DAG.getNode(ISD::SRL, dl, OpTy, Node->getOperand(0),
                         DAG.getConstant(OpTy.getSizeInBits() / 2, dl,
                                         TLI.getShiftAmountTy(
                                             Node->getOperand(0).getValueType(),
                                             DAG.getDataLayout())));
      Tmp1 = DAG.getNode(ISD::TRUNCATE, dl, Node->getValueType(0), Tmp1);
    } else {
      // 0 -> Lo
      Tmp1 = DAG.getNode(ISD::TRUNCATE, dl, Node->getValueType(0),
                         Node->getOperand(0));
    }
    Results.push_back(Tmp1);
    break;
  }
  case ISD::STACKSAVE:
    // Expand to CopyFromReg if the target set
    // StackPointerRegisterToSaveRestore.
    if (Register SP = TLI.getStackPointerRegisterToSaveRestore()) {
      Results.push_back(DAG.getCopyFromReg(Node->getOperand(0), dl, SP,
                                           Node->getValueType(0)));
      Results.push_back(Results[0].getValue(1));
    } else {
      Results.push_back(DAG.getUNDEF(Node->getValueType(0)));
      Results.push_back(Node->getOperand(0));
    }
    break;
  case ISD::STACKRESTORE:
    // Expand to CopyToReg if the target set
    // StackPointerRegisterToSaveRestore.
    if (Register SP = TLI.getStackPointerRegisterToSaveRestore()) {
      Results.push_back(DAG.getCopyToReg(Node->getOperand(0), dl, SP,
                                         Node->getOperand(1)));
    } else {
      Results.push_back(Node->getOperand(0));
    }
    break;
  case ISD::GET_DYNAMIC_AREA_OFFSET:
    Results.push_back(DAG.getConstant(0, dl, Node->getValueType(0)));
    Results.push_back(Results[0].getValue(0));
    break;
  case ISD::FCOPYSIGN:
    Results.push_back(ExpandFCOPYSIGN(Node));
    break;
  case ISD::FNEG:
    Results.push_back(ExpandFNEG(Node));
    break;
  case ISD::FABS:
    Results.push_back(ExpandFABS(Node));
    break;
  case ISD::IS_FPCLASS: {
    auto Test = static_cast<FPClassTest>(Node->getConstantOperandVal(1));
    if (SDValue Expanded =
            TLI.expandIS_FPCLASS(Node->getValueType(0), Node->getOperand(0),
                                 Test, Node->getFlags(), SDLoc(Node), DAG))
      Results.push_back(Expanded);
    break;
  }
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX: {
    // Expand Y = MAX(A, B) -> Y = (A > B) ? A : B
    ISD::CondCode Pred;
    switch (Node->getOpcode()) {
    default: llvm_unreachable("How did we get here?");
    case ISD::SMAX: Pred = ISD::SETGT; break;
    case ISD::SMIN: Pred = ISD::SETLT; break;
    case ISD::UMAX: Pred = ISD::SETUGT; break;
    case ISD::UMIN: Pred = ISD::SETULT; break;
    }
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    Tmp1 = DAG.getSelectCC(dl, Tmp1, Tmp2, Tmp1, Tmp2, Pred);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::FMINNUM:
  case ISD::FMAXNUM: {
    if (SDValue Expanded = TLI.expandFMINNUM_FMAXNUM(Node, DAG))
      Results.push_back(Expanded);
    break;
  }
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM: {
    if (SDValue Expanded = TLI.expandFMINIMUM_FMAXIMUM(Node, DAG))
      Results.push_back(Expanded);
    break;
  }
  case ISD::FSIN:
  case ISD::FCOS: {
    EVT VT = Node->getValueType(0);
    // Turn fsin / fcos into ISD::FSINCOS node if there are a pair of fsin /
    // fcos which share the same operand and both are used.
    if ((TLI.isOperationLegalOrCustom(ISD::FSINCOS, VT) ||
         isSinCosLibcallAvailable(Node, TLI))
        && useSinCos(Node)) {
      SDVTList VTs = DAG.getVTList(VT, VT);
      Tmp1 = DAG.getNode(ISD::FSINCOS, dl, VTs, Node->getOperand(0));
      if (Node->getOpcode() == ISD::FCOS)
        Tmp1 = Tmp1.getValue(1);
      Results.push_back(Tmp1);
    }
    break;
  }
  case ISD::FLDEXP:
  case ISD::STRICT_FLDEXP: {
    EVT VT = Node->getValueType(0);
    RTLIB::Libcall LC = RTLIB::getLDEXP(VT);
    // Use the LibCall instead, it is very likely faster
    // FIXME: Use separate LibCall action.
    if (TLI.getLibcallName(LC))
      break;

    if (SDValue Expanded = expandLdexp(Node)) {
      Results.push_back(Expanded);
      if (Node->getOpcode() == ISD::STRICT_FLDEXP)
        Results.push_back(Expanded.getValue(1));
    }

    break;
  }
  case ISD::FFREXP: {
    RTLIB::Libcall LC = RTLIB::getFREXP(Node->getValueType(0));
    // Use the LibCall instead, it is very likely faster
    // FIXME: Use separate LibCall action.
    if (TLI.getLibcallName(LC))
      break;

    if (SDValue Expanded = expandFrexp(Node)) {
      Results.push_back(Expanded);
      Results.push_back(Expanded.getValue(1));
    }
    break;
  }
  case ISD::FMAD:
    llvm_unreachable("Illegal fmad should never be formed");

  case ISD::FP16_TO_FP:
    if (Node->getValueType(0) != MVT::f32) {
      // We can extend to types bigger than f32 in two steps without changing
      // the result. Since "f16 -> f32" is much more commonly available, give
      // CodeGen the option of emitting that before resorting to a libcall.
      SDValue Res =
          DAG.getNode(ISD::FP16_TO_FP, dl, MVT::f32, Node->getOperand(0));
      Results.push_back(
          DAG.getNode(ISD::FP_EXTEND, dl, Node->getValueType(0), Res));
    }
    break;
  case ISD::STRICT_BF16_TO_FP:
  case ISD::STRICT_FP16_TO_FP:
    if (Node->getValueType(0) != MVT::f32) {
      // We can extend to types bigger than f32 in two steps without changing
      // the result. Since "f16 -> f32" is much more commonly available, give
      // CodeGen the option of emitting that before resorting to a libcall.
      SDValue Res = DAG.getNode(Node->getOpcode(), dl, {MVT::f32, MVT::Other},
                                {Node->getOperand(0), Node->getOperand(1)});
      Res = DAG.getNode(ISD::STRICT_FP_EXTEND, dl,
                        {Node->getValueType(0), MVT::Other},
                        {Res.getValue(1), Res});
      Results.push_back(Res);
      Results.push_back(Res.getValue(1));
    }
    break;
  case ISD::FP_TO_FP16:
    LLVM_DEBUG(dbgs() << "Legalizing FP_TO_FP16\n");
    if (!TLI.useSoftFloat() && TM.Options.UnsafeFPMath) {
      SDValue Op = Node->getOperand(0);
      MVT SVT = Op.getSimpleValueType();
      if ((SVT == MVT::f64 || SVT == MVT::f80) &&
          TLI.isOperationLegalOrCustom(ISD::FP_TO_FP16, MVT::f32)) {
        // Under fastmath, we can expand this node into a fround followed by
        // a float-half conversion.
        SDValue FloatVal =
            DAG.getNode(ISD::FP_ROUND, dl, MVT::f32, Op,
                        DAG.getIntPtrConstant(0, dl, /*isTarget=*/true));
        Results.push_back(
            DAG.getNode(ISD::FP_TO_FP16, dl, Node->getValueType(0), FloatVal));
      }
    }
    break;
  case ISD::ConstantFP: {
    ConstantFPSDNode *CFP = cast<ConstantFPSDNode>(Node);
    // Check to see if this FP immediate is already legal.
    // If this is a legal constant, turn it into a TargetConstantFP node.
    if (!TLI.isFPImmLegal(CFP->getValueAPF(), Node->getValueType(0),
                          DAG.shouldOptForSize()))
      Results.push_back(ExpandConstantFP(CFP, true));
    break;
  }
  case ISD::Constant: {
    ConstantSDNode *CP = cast<ConstantSDNode>(Node);
    Results.push_back(ExpandConstant(CP));
    break;
  }
  case ISD::FSUB: {
    EVT VT = Node->getValueType(0);
    if (TLI.isOperationLegalOrCustom(ISD::FADD, VT) &&
        TLI.isOperationLegalOrCustom(ISD::FNEG, VT)) {
      const SDNodeFlags Flags = Node->getFlags();
      Tmp1 = DAG.getNode(ISD::FNEG, dl, VT, Node->getOperand(1));
      Tmp1 = DAG.getNode(ISD::FADD, dl, VT, Node->getOperand(0), Tmp1, Flags);
      Results.push_back(Tmp1);
    }
    break;
  }
  case ISD::SUB: {
    EVT VT = Node->getValueType(0);
    assert(TLI.isOperationLegalOrCustom(ISD::ADD, VT) &&
           TLI.isOperationLegalOrCustom(ISD::XOR, VT) &&
           "Don't know how to expand this subtraction!");
    Tmp1 = DAG.getNOT(dl, Node->getOperand(1), VT);
    Tmp1 = DAG.getNode(ISD::ADD, dl, VT, Tmp1, DAG.getConstant(1, dl, VT));
    Results.push_back(DAG.getNode(ISD::ADD, dl, VT, Node->getOperand(0), Tmp1));
    break;
  }
  case ISD::UREM:
  case ISD::SREM:
    if (TLI.expandREM(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::UDIV:
  case ISD::SDIV: {
    bool isSigned = Node->getOpcode() == ISD::SDIV;
    unsigned DivRemOpc = isSigned ? ISD::SDIVREM : ISD::UDIVREM;
    EVT VT = Node->getValueType(0);
    if (TLI.isOperationLegalOrCustom(DivRemOpc, VT)) {
      SDVTList VTs = DAG.getVTList(VT, VT);
      Tmp1 = DAG.getNode(DivRemOpc, dl, VTs, Node->getOperand(0),
                         Node->getOperand(1));
      Results.push_back(Tmp1);
    }
    break;
  }
  case ISD::MULHU:
  case ISD::MULHS: {
    unsigned ExpandOpcode =
        Node->getOpcode() == ISD::MULHU ? ISD::UMUL_LOHI : ISD::SMUL_LOHI;
    EVT VT = Node->getValueType(0);
    SDVTList VTs = DAG.getVTList(VT, VT);

    Tmp1 = DAG.getNode(ExpandOpcode, dl, VTs, Node->getOperand(0),
                       Node->getOperand(1));
    Results.push_back(Tmp1.getValue(1));
    break;
  }
  case ISD::UMUL_LOHI:
  case ISD::SMUL_LOHI: {
    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);
    MVT VT = LHS.getSimpleValueType();
    unsigned MULHOpcode =
        Node->getOpcode() == ISD::UMUL_LOHI ? ISD::MULHU : ISD::MULHS;

    if (TLI.isOperationLegalOrCustom(MULHOpcode, VT)) {
      Results.push_back(DAG.getNode(ISD::MUL, dl, VT, LHS, RHS));
      Results.push_back(DAG.getNode(MULHOpcode, dl, VT, LHS, RHS));
      break;
    }

    SmallVector<SDValue, 4> Halves;
    EVT HalfType = EVT(VT).getHalfSizedIntegerVT(*DAG.getContext());
    assert(TLI.isTypeLegal(HalfType));
    if (TLI.expandMUL_LOHI(Node->getOpcode(), VT, dl, LHS, RHS, Halves,
                           HalfType, DAG,
                           TargetLowering::MulExpansionKind::Always)) {
      for (unsigned i = 0; i < 2; ++i) {
        SDValue Lo = DAG.getNode(ISD::ZERO_EXTEND, dl, VT, Halves[2 * i]);
        SDValue Hi = DAG.getNode(ISD::ANY_EXTEND, dl, VT, Halves[2 * i + 1]);
        SDValue Shift = DAG.getConstant(
            HalfType.getScalarSizeInBits(), dl,
            TLI.getShiftAmountTy(HalfType, DAG.getDataLayout()));
        Hi = DAG.getNode(ISD::SHL, dl, VT, Hi, Shift);
        Results.push_back(DAG.getNode(ISD::OR, dl, VT, Lo, Hi));
      }
      break;
    }
    break;
  }
  case ISD::MUL: {
    EVT VT = Node->getValueType(0);
    SDVTList VTs = DAG.getVTList(VT, VT);
    // See if multiply or divide can be lowered using two-result operations.
    // We just need the low half of the multiply; try both the signed
    // and unsigned forms. If the target supports both SMUL_LOHI and
    // UMUL_LOHI, form a preference by checking which forms of plain
    // MULH it supports.
    bool HasSMUL_LOHI = TLI.isOperationLegalOrCustom(ISD::SMUL_LOHI, VT);
    bool HasUMUL_LOHI = TLI.isOperationLegalOrCustom(ISD::UMUL_LOHI, VT);
    bool HasMULHS = TLI.isOperationLegalOrCustom(ISD::MULHS, VT);
    bool HasMULHU = TLI.isOperationLegalOrCustom(ISD::MULHU, VT);
    unsigned OpToUse = 0;
    if (HasSMUL_LOHI && !HasMULHS) {
      OpToUse = ISD::SMUL_LOHI;
    } else if (HasUMUL_LOHI && !HasMULHU) {
      OpToUse = ISD::UMUL_LOHI;
    } else if (HasSMUL_LOHI) {
      OpToUse = ISD::SMUL_LOHI;
    } else if (HasUMUL_LOHI) {
      OpToUse = ISD::UMUL_LOHI;
    }
    if (OpToUse) {
      Results.push_back(DAG.getNode(OpToUse, dl, VTs, Node->getOperand(0),
                                    Node->getOperand(1)));
      break;
    }

    SDValue Lo, Hi;
    EVT HalfType = VT.getHalfSizedIntegerVT(*DAG.getContext());
    if (TLI.isOperationLegalOrCustom(ISD::ZERO_EXTEND, VT) &&
        TLI.isOperationLegalOrCustom(ISD::ANY_EXTEND, VT) &&
        TLI.isOperationLegalOrCustom(ISD::SHL, VT) &&
        TLI.isOperationLegalOrCustom(ISD::OR, VT) &&
        TLI.expandMUL(Node, Lo, Hi, HalfType, DAG,
                      TargetLowering::MulExpansionKind::OnlyLegalOrCustom)) {
      Lo = DAG.getNode(ISD::ZERO_EXTEND, dl, VT, Lo);
      Hi = DAG.getNode(ISD::ANY_EXTEND, dl, VT, Hi);
      SDValue Shift =
          DAG.getConstant(HalfType.getSizeInBits(), dl,
                          TLI.getShiftAmountTy(HalfType, DAG.getDataLayout()));
      Hi = DAG.getNode(ISD::SHL, dl, VT, Hi, Shift);
      Results.push_back(DAG.getNode(ISD::OR, dl, VT, Lo, Hi));
    }
    break;
  }
  case ISD::FSHL:
  case ISD::FSHR:
    if (SDValue Expanded = TLI.expandFunnelShift(Node, DAG))
      Results.push_back(Expanded);
    break;
  case ISD::ROTL:
  case ISD::ROTR:
    if (SDValue Expanded = TLI.expandROT(Node, true /*AllowVectorOps*/, DAG))
      Results.push_back(Expanded);
    break;
  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT:
    Results.push_back(TLI.expandAddSubSat(Node, DAG));
    break;
  case ISD::SCMP:
  case ISD::UCMP:
    Results.push_back(TLI.expandCMP(Node, DAG));
    break;
  case ISD::SSHLSAT:
  case ISD::USHLSAT:
    Results.push_back(TLI.expandShlSat(Node, DAG));
    break;
  case ISD::SMULFIX:
  case ISD::SMULFIXSAT:
  case ISD::UMULFIX:
  case ISD::UMULFIXSAT:
    Results.push_back(TLI.expandFixedPointMul(Node, DAG));
    break;
  case ISD::SDIVFIX:
  case ISD::SDIVFIXSAT:
  case ISD::UDIVFIX:
  case ISD::UDIVFIXSAT:
    if (SDValue V = TLI.expandFixedPointDiv(Node->getOpcode(), SDLoc(Node),
                                            Node->getOperand(0),
                                            Node->getOperand(1),
                                            Node->getConstantOperandVal(2),
                                            DAG)) {
      Results.push_back(V);
      break;
    }
    // FIXME: We might want to retry here with a wider type if we fail, if that
    // type is legal.
    // FIXME: Technically, so long as we only have sdivfixes where BW+Scale is
    // <= 128 (which is the case for all of the default Embedded-C types),
    // we will only get here with types and scales that we could always expand
    // if we were allowed to generate libcalls to division functions of illegal
    // type. But we cannot do that.
    llvm_unreachable("Cannot expand DIVFIX!");
  case ISD::UADDO_CARRY:
  case ISD::USUBO_CARRY: {
    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);
    SDValue Carry = Node->getOperand(2);

    bool IsAdd = Node->getOpcode() == ISD::UADDO_CARRY;

    // Initial add of the 2 operands.
    unsigned Op = IsAdd ? ISD::ADD : ISD::SUB;
    EVT VT = LHS.getValueType();
    SDValue Sum = DAG.getNode(Op, dl, VT, LHS, RHS);

    // Initial check for overflow.
    EVT CarryType = Node->getValueType(1);
    EVT SetCCType = getSetCCResultType(Node->getValueType(0));
    ISD::CondCode CC = IsAdd ? ISD::SETULT : ISD::SETUGT;
    SDValue Overflow = DAG.getSetCC(dl, SetCCType, Sum, LHS, CC);

    // Add of the sum and the carry.
    SDValue One = DAG.getConstant(1, dl, VT);
    SDValue CarryExt =
        DAG.getNode(ISD::AND, dl, VT, DAG.getZExtOrTrunc(Carry, dl, VT), One);
    SDValue Sum2 = DAG.getNode(Op, dl, VT, Sum, CarryExt);

    // Second check for overflow. If we are adding, we can only overflow if the
    // initial sum is all 1s ang the carry is set, resulting in a new sum of 0.
    // If we are subtracting, we can only overflow if the initial sum is 0 and
    // the carry is set, resulting in a new sum of all 1s.
    SDValue Zero = DAG.getConstant(0, dl, VT);
    SDValue Overflow2 =
        IsAdd ? DAG.getSetCC(dl, SetCCType, Sum2, Zero, ISD::SETEQ)
              : DAG.getSetCC(dl, SetCCType, Sum, Zero, ISD::SETEQ);
    Overflow2 = DAG.getNode(ISD::AND, dl, SetCCType, Overflow2,
                            DAG.getZExtOrTrunc(Carry, dl, SetCCType));

    SDValue ResultCarry =
        DAG.getNode(ISD::OR, dl, SetCCType, Overflow, Overflow2);

    Results.push_back(Sum2);
    Results.push_back(DAG.getBoolExtOrTrunc(ResultCarry, dl, CarryType, VT));
    break;
  }
  case ISD::SADDO:
  case ISD::SSUBO: {
    SDValue Result, Overflow;
    TLI.expandSADDSUBO(Node, Result, Overflow, DAG);
    Results.push_back(Result);
    Results.push_back(Overflow);
    break;
  }
  case ISD::UADDO:
  case ISD::USUBO: {
    SDValue Result, Overflow;
    TLI.expandUADDSUBO(Node, Result, Overflow, DAG);
    Results.push_back(Result);
    Results.push_back(Overflow);
    break;
  }
  case ISD::UMULO:
  case ISD::SMULO: {
    SDValue Result, Overflow;
    if (TLI.expandMULO(Node, Result, Overflow, DAG)) {
      Results.push_back(Result);
      Results.push_back(Overflow);
    }
    break;
  }
  case ISD::BUILD_PAIR: {
    EVT PairTy = Node->getValueType(0);
    Tmp1 = DAG.getNode(ISD::ZERO_EXTEND, dl, PairTy, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::ANY_EXTEND, dl, PairTy, Node->getOperand(1));
    Tmp2 = DAG.getNode(
        ISD::SHL, dl, PairTy, Tmp2,
        DAG.getConstant(PairTy.getSizeInBits() / 2, dl,
                        TLI.getShiftAmountTy(PairTy, DAG.getDataLayout())));
    Results.push_back(DAG.getNode(ISD::OR, dl, PairTy, Tmp1, Tmp2));
    break;
  }
  case ISD::SELECT:
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    Tmp3 = Node->getOperand(2);
    if (Tmp1.getOpcode() == ISD::SETCC) {
      Tmp1 = DAG.getSelectCC(dl, Tmp1.getOperand(0), Tmp1.getOperand(1),
                             Tmp2, Tmp3,
                             cast<CondCodeSDNode>(Tmp1.getOperand(2))->get());
    } else {
      Tmp1 = DAG.getSelectCC(dl, Tmp1,
                             DAG.getConstant(0, dl, Tmp1.getValueType()),
                             Tmp2, Tmp3, ISD::SETNE);
    }
    Tmp1->setFlags(Node->getFlags());
    Results.push_back(Tmp1);
    break;
  case ISD::BR_JT: {
    SDValue Chain = Node->getOperand(0);
    SDValue Table = Node->getOperand(1);
    SDValue Index = Node->getOperand(2);
    int JTI = cast<JumpTableSDNode>(Table.getNode())->getIndex();

    const DataLayout &TD = DAG.getDataLayout();
    EVT PTy = TLI.getPointerTy(TD);

    unsigned EntrySize =
      DAG.getMachineFunction().getJumpTableInfo()->getEntrySize(TD);

    // For power-of-two jumptable entry sizes convert multiplication to a shift.
    // This transformation needs to be done here since otherwise the MIPS
    // backend will end up emitting a three instruction multiply sequence
    // instead of a single shift and MSP430 will call a runtime function.
    if (llvm::isPowerOf2_32(EntrySize))
      Index = DAG.getNode(
          ISD::SHL, dl, Index.getValueType(), Index,
          DAG.getConstant(llvm::Log2_32(EntrySize), dl, Index.getValueType()));
    else
      Index = DAG.getNode(ISD::MUL, dl, Index.getValueType(), Index,
                          DAG.getConstant(EntrySize, dl, Index.getValueType()));
    SDValue Addr = DAG.getNode(ISD::ADD, dl, Index.getValueType(),
                               Index, Table);

    EVT MemVT = EVT::getIntegerVT(*DAG.getContext(), EntrySize * 8);
    SDValue LD = DAG.getExtLoad(
        ISD::SEXTLOAD, dl, PTy, Chain, Addr,
        MachinePointerInfo::getJumpTable(DAG.getMachineFunction()), MemVT);
    Addr = LD;
    if (TLI.isJumpTableRelative()) {
      // For PIC, the sequence is:
      // BRIND(load(Jumptable + index) + RelocBase)
      // RelocBase can be JumpTable, GOT or some sort of global base.
      Addr = DAG.getNode(ISD::ADD, dl, PTy, Addr,
                          TLI.getPICJumpTableRelocBase(Table, DAG));
    }

    Tmp1 = TLI.expandIndirectJTBranch(dl, LD.getValue(1), Addr, JTI, DAG);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::BRCOND:
    // Expand brcond's setcc into its constituent parts and create a BR_CC
    // Node.
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    if (Tmp2.getOpcode() == ISD::SETCC &&
        TLI.isOperationLegalOrCustom(ISD::BR_CC,
                                     Tmp2.getOperand(0).getValueType())) {
      Tmp1 = DAG.getNode(ISD::BR_CC, dl, MVT::Other, Tmp1, Tmp2.getOperand(2),
                         Tmp2.getOperand(0), Tmp2.getOperand(1),
                         Node->getOperand(2));
    } else {
      // We test only the i1 bit.  Skip the AND if UNDEF or another AND.
      if (Tmp2.isUndef() ||
          (Tmp2.getOpcode() == ISD::AND && isOneConstant(Tmp2.getOperand(1))))
        Tmp3 = Tmp2;
      else
        Tmp3 = DAG.getNode(ISD::AND, dl, Tmp2.getValueType(), Tmp2,
                           DAG.getConstant(1, dl, Tmp2.getValueType()));
      Tmp1 = DAG.getNode(ISD::BR_CC, dl, MVT::Other, Tmp1,
                         DAG.getCondCode(ISD::SETNE), Tmp3,
                         DAG.getConstant(0, dl, Tmp3.getValueType()),
                         Node->getOperand(2));
    }
    Results.push_back(Tmp1);
    break;
  case ISD::SETCC:
  case ISD::VP_SETCC:
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS: {
    bool IsVP = Node->getOpcode() == ISD::VP_SETCC;
    bool IsStrict = Node->getOpcode() == ISD::STRICT_FSETCC ||
                    Node->getOpcode() == ISD::STRICT_FSETCCS;
    bool IsSignaling = Node->getOpcode() == ISD::STRICT_FSETCCS;
    SDValue Chain = IsStrict ? Node->getOperand(0) : SDValue();
    unsigned Offset = IsStrict ? 1 : 0;
    Tmp1 = Node->getOperand(0 + Offset);
    Tmp2 = Node->getOperand(1 + Offset);
    Tmp3 = Node->getOperand(2 + Offset);
    SDValue Mask, EVL;
    if (IsVP) {
      Mask = Node->getOperand(3 + Offset);
      EVL = Node->getOperand(4 + Offset);
    }
    bool Legalized = TLI.LegalizeSetCCCondCode(
        DAG, Node->getValueType(0), Tmp1, Tmp2, Tmp3, Mask, EVL, NeedInvert, dl,
        Chain, IsSignaling);

    if (Legalized) {
      // If we expanded the SETCC by swapping LHS and RHS, or by inverting the
      // condition code, create a new SETCC node.
      if (Tmp3.getNode()) {
        if (IsStrict) {
          Tmp1 = DAG.getNode(Node->getOpcode(), dl, Node->getVTList(),
                             {Chain, Tmp1, Tmp2, Tmp3}, Node->getFlags());
          Chain = Tmp1.getValue(1);
        } else if (IsVP) {
          Tmp1 = DAG.getNode(Node->getOpcode(), dl, Node->getValueType(0),
                             {Tmp1, Tmp2, Tmp3, Mask, EVL}, Node->getFlags());
        } else {
          Tmp1 = DAG.getNode(Node->getOpcode(), dl, Node->getValueType(0), Tmp1,
                             Tmp2, Tmp3, Node->getFlags());
        }
      }

      // If we expanded the SETCC by inverting the condition code, then wrap
      // the existing SETCC in a NOT to restore the intended condition.
      if (NeedInvert) {
        if (!IsVP)
          Tmp1 = DAG.getLogicalNOT(dl, Tmp1, Tmp1->getValueType(0));
        else
          Tmp1 =
              DAG.getVPLogicalNOT(dl, Tmp1, Mask, EVL, Tmp1->getValueType(0));
      }

      Results.push_back(Tmp1);
      if (IsStrict)
        Results.push_back(Chain);

      break;
    }

    // FIXME: It seems Legalized is false iff CCCode is Legal. I don't
    // understand if this code is useful for strict nodes.
    assert(!IsStrict && "Don't know how to expand for strict nodes.");

    // Otherwise, SETCC for the given comparison type must be completely
    // illegal; expand it into a SELECT_CC.
    // FIXME: This drops the mask/evl for VP_SETCC.
    EVT VT = Node->getValueType(0);
    EVT Tmp1VT = Tmp1.getValueType();
    Tmp1 = DAG.getNode(ISD::SELECT_CC, dl, VT, Tmp1, Tmp2,
                       DAG.getBoolConstant(true, dl, VT, Tmp1VT),
                       DAG.getBoolConstant(false, dl, VT, Tmp1VT), Tmp3);
    Tmp1->setFlags(Node->getFlags());
    Results.push_back(Tmp1);
    break;
  }
  case ISD::SELECT_CC: {
    // TODO: need to add STRICT_SELECT_CC and STRICT_SELECT_CCS
    Tmp1 = Node->getOperand(0);   // LHS
    Tmp2 = Node->getOperand(1);   // RHS
    Tmp3 = Node->getOperand(2);   // True
    Tmp4 = Node->getOperand(3);   // False
    EVT VT = Node->getValueType(0);
    SDValue Chain;
    SDValue CC = Node->getOperand(4);
    ISD::CondCode CCOp = cast<CondCodeSDNode>(CC)->get();

    if (TLI.isCondCodeLegalOrCustom(CCOp, Tmp1.getSimpleValueType())) {
      // If the condition code is legal, then we need to expand this
      // node using SETCC and SELECT.
      EVT CmpVT = Tmp1.getValueType();
      assert(!TLI.isOperationExpand(ISD::SELECT, VT) &&
             "Cannot expand ISD::SELECT_CC when ISD::SELECT also needs to be "
             "expanded.");
      EVT CCVT = getSetCCResultType(CmpVT);
      SDValue Cond = DAG.getNode(ISD::SETCC, dl, CCVT, Tmp1, Tmp2, CC, Node->getFlags());
      Results.push_back(
          DAG.getSelect(dl, VT, Cond, Tmp3, Tmp4, Node->getFlags()));
      break;
    }

    // SELECT_CC is legal, so the condition code must not be.
    bool Legalized = false;
    // Try to legalize by inverting the condition.  This is for targets that
    // might support an ordered version of a condition, but not the unordered
    // version (or vice versa).
    ISD::CondCode InvCC = ISD::getSetCCInverse(CCOp, Tmp1.getValueType());
    if (TLI.isCondCodeLegalOrCustom(InvCC, Tmp1.getSimpleValueType())) {
      // Use the new condition code and swap true and false
      Legalized = true;
      Tmp1 = DAG.getSelectCC(dl, Tmp1, Tmp2, Tmp4, Tmp3, InvCC);
      Tmp1->setFlags(Node->getFlags());
    } else {
      // If The inverse is not legal, then try to swap the arguments using
      // the inverse condition code.
      ISD::CondCode SwapInvCC = ISD::getSetCCSwappedOperands(InvCC);
      if (TLI.isCondCodeLegalOrCustom(SwapInvCC, Tmp1.getSimpleValueType())) {
        // The swapped inverse condition is legal, so swap true and false,
        // lhs and rhs.
        Legalized = true;
        Tmp1 = DAG.getSelectCC(dl, Tmp2, Tmp1, Tmp4, Tmp3, SwapInvCC);
        Tmp1->setFlags(Node->getFlags());
      }
    }

    if (!Legalized) {
      Legalized = TLI.LegalizeSetCCCondCode(
          DAG, getSetCCResultType(Tmp1.getValueType()), Tmp1, Tmp2, CC,
          /*Mask*/ SDValue(), /*EVL*/ SDValue(), NeedInvert, dl, Chain);

      assert(Legalized && "Can't legalize SELECT_CC with legal condition!");

      // If we expanded the SETCC by inverting the condition code, then swap
      // the True/False operands to match.
      if (NeedInvert)
        std::swap(Tmp3, Tmp4);

      // If we expanded the SETCC by swapping LHS and RHS, or by inverting the
      // condition code, create a new SELECT_CC node.
      if (CC.getNode()) {
        Tmp1 = DAG.getNode(ISD::SELECT_CC, dl, Node->getValueType(0),
                           Tmp1, Tmp2, Tmp3, Tmp4, CC);
      } else {
        Tmp2 = DAG.getConstant(0, dl, Tmp1.getValueType());
        CC = DAG.getCondCode(ISD::SETNE);
        Tmp1 = DAG.getNode(ISD::SELECT_CC, dl, Node->getValueType(0), Tmp1,
                           Tmp2, Tmp3, Tmp4, CC);
      }
      Tmp1->setFlags(Node->getFlags());
    }
    Results.push_back(Tmp1);
    break;
  }
  case ISD::BR_CC: {
    // TODO: need to add STRICT_BR_CC and STRICT_BR_CCS
    SDValue Chain;
    Tmp1 = Node->getOperand(0);              // Chain
    Tmp2 = Node->getOperand(2);              // LHS
    Tmp3 = Node->getOperand(3);              // RHS
    Tmp4 = Node->getOperand(1);              // CC

    bool Legalized = TLI.LegalizeSetCCCondCode(
        DAG, getSetCCResultType(Tmp2.getValueType()), Tmp2, Tmp3, Tmp4,
        /*Mask*/ SDValue(), /*EVL*/ SDValue(), NeedInvert, dl, Chain);
    (void)Legalized;
    assert(Legalized && "Can't legalize BR_CC with legal condition!");

    // If we expanded the SETCC by swapping LHS and RHS, create a new BR_CC
    // node.
    if (Tmp4.getNode()) {
      assert(!NeedInvert && "Don't know how to invert BR_CC!");

      Tmp1 = DAG.getNode(ISD::BR_CC, dl, Node->getValueType(0), Tmp1,
                         Tmp4, Tmp2, Tmp3, Node->getOperand(4));
    } else {
      Tmp3 = DAG.getConstant(0, dl, Tmp2.getValueType());
      Tmp4 = DAG.getCondCode(NeedInvert ? ISD::SETEQ : ISD::SETNE);
      Tmp1 = DAG.getNode(ISD::BR_CC, dl, Node->getValueType(0), Tmp1, Tmp4,
                         Tmp2, Tmp3, Node->getOperand(4));
    }
    Results.push_back(Tmp1);
    break;
  }
  case ISD::BUILD_VECTOR:
    Results.push_back(ExpandBUILD_VECTOR(Node));
    break;
  case ISD::SPLAT_VECTOR:
    Results.push_back(ExpandSPLAT_VECTOR(Node));
    break;
  case ISD::SRA:
  case ISD::SRL:
  case ISD::SHL: {
    // Scalarize vector SRA/SRL/SHL.
    EVT VT = Node->getValueType(0);
    assert(VT.isVector() && "Unable to legalize non-vector shift");
    assert(TLI.isTypeLegal(VT.getScalarType())&& "Element type must be legal");
    unsigned NumElem = VT.getVectorNumElements();

    SmallVector<SDValue, 8> Scalars;
    for (unsigned Idx = 0; Idx < NumElem; Idx++) {
      SDValue Ex =
          DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, VT.getScalarType(),
                      Node->getOperand(0), DAG.getVectorIdxConstant(Idx, dl));
      SDValue Sh =
          DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, VT.getScalarType(),
                      Node->getOperand(1), DAG.getVectorIdxConstant(Idx, dl));
      Scalars.push_back(DAG.getNode(Node->getOpcode(), dl,
                                    VT.getScalarType(), Ex, Sh));
    }

    SDValue Result = DAG.getBuildVector(Node->getValueType(0), dl, Scalars);
    Results.push_back(Result);
    break;
  }
  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_FMUL:
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMINIMUM:
    Results.push_back(TLI.expandVecReduce(Node, DAG));
    break;
  case ISD::VP_CTTZ_ELTS:
  case ISD::VP_CTTZ_ELTS_ZERO_UNDEF:
    Results.push_back(TLI.expandVPCTTZElements(Node, DAG));
    break;
  case ISD::CLEAR_CACHE:
    // The default expansion of llvm.clear_cache is simply a no-op for those
    // targets where it is not needed.
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::GLOBAL_OFFSET_TABLE:
  case ISD::GlobalAddress:
  case ISD::GlobalTLSAddress:
  case ISD::ExternalSymbol:
  case ISD::ConstantPool:
  case ISD::JumpTable:
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_WO_CHAIN:
  case ISD::INTRINSIC_VOID:
    // FIXME: Custom lowering for these operations shouldn't return null!
    // Return true so that we don't call ConvertNodeToLibcall which also won't
    // do anything.
    return true;
  }

  if (!TLI.isStrictFPEnabled() && Results.empty() && Node->isStrictFPOpcode()) {
    // FIXME: We were asked to expand a strict floating-point operation,
    // but there is currently no expansion implemented that would preserve
    // the "strict" properties.  For now, we just fall back to the non-strict
    // version if that is legal on the target.  The actual mutation of the
    // operation will happen in SelectionDAGISel::DoInstructionSelection.
    switch (Node->getOpcode()) {
    default:
      if (TLI.getStrictFPOperationAction(Node->getOpcode(),
                                         Node->getValueType(0))
          == TargetLowering::Legal)
        return true;
      break;
    case ISD::STRICT_FSUB: {
      if (TLI.getStrictFPOperationAction(
              ISD::STRICT_FSUB, Node->getValueType(0)) == TargetLowering::Legal)
        return true;
      if (TLI.getStrictFPOperationAction(
              ISD::STRICT_FADD, Node->getValueType(0)) != TargetLowering::Legal)
        break;

      EVT VT = Node->getValueType(0);
      const SDNodeFlags Flags = Node->getFlags();
      SDValue Neg = DAG.getNode(ISD::FNEG, dl, VT, Node->getOperand(2), Flags);
      SDValue Fadd = DAG.getNode(ISD::STRICT_FADD, dl, Node->getVTList(),
                                 {Node->getOperand(0), Node->getOperand(1), Neg},
                         Flags);

      Results.push_back(Fadd);
      Results.push_back(Fadd.getValue(1));
      break;
    }
    case ISD::STRICT_SINT_TO_FP:
    case ISD::STRICT_UINT_TO_FP:
    case ISD::STRICT_LRINT:
    case ISD::STRICT_LLRINT:
    case ISD::STRICT_LROUND:
    case ISD::STRICT_LLROUND:
      // These are registered by the operand type instead of the value
      // type. Reflect that here.
      if (TLI.getStrictFPOperationAction(Node->getOpcode(),
                                         Node->getOperand(1).getValueType())
          == TargetLowering::Legal)
        return true;
      break;
    }
  }

  // Replace the original node with the legalized result.
  if (Results.empty()) {
    LLVM_DEBUG(dbgs() << "Cannot expand node\n");
    return false;
  }

  LLVM_DEBUG(dbgs() << "Successfully expanded node\n");
  ReplaceNode(Node, Results.data());
  return true;
}

void SelectionDAGLegalize::ConvertNodeToLibcall(SDNode *Node) {
  LLVM_DEBUG(dbgs() << "Trying to convert node to libcall\n");
  SmallVector<SDValue, 8> Results;
  SDLoc dl(Node);
  // FIXME: Check flags on the node to see if we can use a finite call.
  unsigned Opc = Node->getOpcode();
  switch (Opc) {
  case ISD::ATOMIC_FENCE: {
    // If the target didn't lower this, lower it to '__sync_synchronize()' call
    // FIXME: handle "fence singlethread" more efficiently.
    TargetLowering::ArgListTy Args;

    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(dl)
        .setChain(Node->getOperand(0))
        .setLibCallee(
            CallingConv::C, Type::getVoidTy(*DAG.getContext()),
            DAG.getExternalSymbol("__sync_synchronize",
                                  TLI.getPointerTy(DAG.getDataLayout())),
            std::move(Args));

    std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);

    Results.push_back(CallResult.second);
    break;
  }
  // By default, atomic intrinsics are marked Legal and lowered. Targets
  // which don't support them directly, however, may want libcalls, in which
  // case they mark them Expand, and we get here.
  case ISD::ATOMIC_SWAP:
  case ISD::ATOMIC_LOAD_ADD:
  case ISD::ATOMIC_LOAD_SUB:
  case ISD::ATOMIC_LOAD_AND:
  case ISD::ATOMIC_LOAD_CLR:
  case ISD::ATOMIC_LOAD_OR:
  case ISD::ATOMIC_LOAD_XOR:
  case ISD::ATOMIC_LOAD_NAND:
  case ISD::ATOMIC_LOAD_MIN:
  case ISD::ATOMIC_LOAD_MAX:
  case ISD::ATOMIC_LOAD_UMIN:
  case ISD::ATOMIC_LOAD_UMAX:
  case ISD::ATOMIC_CMP_SWAP: {
    MVT VT = cast<AtomicSDNode>(Node)->getMemoryVT().getSimpleVT();
    AtomicOrdering Order = cast<AtomicSDNode>(Node)->getMergedOrdering();
    RTLIB::Libcall LC = RTLIB::getOUTLINE_ATOMIC(Opc, Order, VT);
    EVT RetVT = Node->getValueType(0);
    TargetLowering::MakeLibCallOptions CallOptions;
    SmallVector<SDValue, 4> Ops;
    if (TLI.getLibcallName(LC)) {
      // If outline atomic available, prepare its arguments and expand.
      Ops.append(Node->op_begin() + 2, Node->op_end());
      Ops.push_back(Node->getOperand(1));

    } else {
      LC = RTLIB::getSYNC(Opc, VT);
      assert(LC != RTLIB::UNKNOWN_LIBCALL &&
             "Unexpected atomic op or value type!");
      // Arguments for expansion to sync libcall
      Ops.append(Node->op_begin() + 1, Node->op_end());
    }
    std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, RetVT,
                                                      Ops, CallOptions,
                                                      SDLoc(Node),
                                                      Node->getOperand(0));
    Results.push_back(Tmp.first);
    Results.push_back(Tmp.second);
    break;
  }
  case ISD::TRAP: {
    // If this operation is not supported, lower it to 'abort()' call
    TargetLowering::ArgListTy Args;
    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(dl)
        .setChain(Node->getOperand(0))
        .setLibCallee(CallingConv::C, Type::getVoidTy(*DAG.getContext()),
                      DAG.getExternalSymbol(
                          "abort", TLI.getPointerTy(DAG.getDataLayout())),
                      std::move(Args));
    std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);

    Results.push_back(CallResult.second);
    break;
  }
  case ISD::CLEAR_CACHE: {
    TargetLowering::MakeLibCallOptions CallOptions;
    SDValue InputChain = Node->getOperand(0);
    SDValue StartVal = Node->getOperand(1);
    SDValue EndVal = Node->getOperand(2);
    std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(
        DAG, RTLIB::CLEAR_CACHE, MVT::isVoid, {StartVal, EndVal}, CallOptions,
        SDLoc(Node), InputChain);
    Results.push_back(Tmp.second);
    break;
  }
  case ISD::FMINNUM:
  case ISD::STRICT_FMINNUM:
    ExpandFPLibCall(Node, RTLIB::FMIN_F32, RTLIB::FMIN_F64,
                    RTLIB::FMIN_F80, RTLIB::FMIN_F128,
                    RTLIB::FMIN_PPCF128, Results);
    break;
  // FIXME: We do not have libcalls for FMAXIMUM and FMINIMUM. So, we cannot use
  // libcall legalization for these nodes, but there is no default expasion for
  // these nodes either (see PR63267 for example).
  case ISD::FMAXNUM:
  case ISD::STRICT_FMAXNUM:
    ExpandFPLibCall(Node, RTLIB::FMAX_F32, RTLIB::FMAX_F64,
                    RTLIB::FMAX_F80, RTLIB::FMAX_F128,
                    RTLIB::FMAX_PPCF128, Results);
    break;
  case ISD::FSQRT:
  case ISD::STRICT_FSQRT:
    ExpandFPLibCall(Node, RTLIB::SQRT_F32, RTLIB::SQRT_F64,
                    RTLIB::SQRT_F80, RTLIB::SQRT_F128,
                    RTLIB::SQRT_PPCF128, Results);
    break;
  case ISD::FCBRT:
    ExpandFPLibCall(Node, RTLIB::CBRT_F32, RTLIB::CBRT_F64,
                    RTLIB::CBRT_F80, RTLIB::CBRT_F128,
                    RTLIB::CBRT_PPCF128, Results);
    break;
  case ISD::FSIN:
  case ISD::STRICT_FSIN:
    ExpandFPLibCall(Node, RTLIB::SIN_F32, RTLIB::SIN_F64,
                    RTLIB::SIN_F80, RTLIB::SIN_F128,
                    RTLIB::SIN_PPCF128, Results);
    break;
  case ISD::FCOS:
  case ISD::STRICT_FCOS:
    ExpandFPLibCall(Node, RTLIB::COS_F32, RTLIB::COS_F64,
                    RTLIB::COS_F80, RTLIB::COS_F128,
                    RTLIB::COS_PPCF128, Results);
    break;
  case ISD::FTAN:
  case ISD::STRICT_FTAN:
    ExpandFPLibCall(Node, RTLIB::TAN_F32, RTLIB::TAN_F64, RTLIB::TAN_F80,
                    RTLIB::TAN_F128, RTLIB::TAN_PPCF128, Results);
    break;
  case ISD::FASIN:
  case ISD::STRICT_FASIN:
    ExpandFPLibCall(Node, RTLIB::ASIN_F32, RTLIB::ASIN_F64, RTLIB::ASIN_F80,
                    RTLIB::ASIN_F128, RTLIB::ASIN_PPCF128, Results);
    break;
  case ISD::FACOS:
  case ISD::STRICT_FACOS:
    ExpandFPLibCall(Node, RTLIB::ACOS_F32, RTLIB::ACOS_F64, RTLIB::ACOS_F80,
                    RTLIB::ACOS_F128, RTLIB::ACOS_PPCF128, Results);
    break;
  case ISD::FATAN:
  case ISD::STRICT_FATAN:
    ExpandFPLibCall(Node, RTLIB::ATAN_F32, RTLIB::ATAN_F64, RTLIB::ATAN_F80,
                    RTLIB::ATAN_F128, RTLIB::ATAN_PPCF128, Results);
    break;
  case ISD::FSINH:
  case ISD::STRICT_FSINH:
    ExpandFPLibCall(Node, RTLIB::SINH_F32, RTLIB::SINH_F64, RTLIB::SINH_F80,
                    RTLIB::SINH_F128, RTLIB::SINH_PPCF128, Results);
    break;
  case ISD::FCOSH:
  case ISD::STRICT_FCOSH:
    ExpandFPLibCall(Node, RTLIB::COSH_F32, RTLIB::COSH_F64, RTLIB::COSH_F80,
                    RTLIB::COSH_F128, RTLIB::COSH_PPCF128, Results);
    break;
  case ISD::FTANH:
  case ISD::STRICT_FTANH:
    ExpandFPLibCall(Node, RTLIB::TANH_F32, RTLIB::TANH_F64, RTLIB::TANH_F80,
                    RTLIB::TANH_F128, RTLIB::TANH_PPCF128, Results);
    break;
  case ISD::FSINCOS:
    // Expand into sincos libcall.
    ExpandSinCosLibCall(Node, Results);
    break;
  case ISD::FLOG:
  case ISD::STRICT_FLOG:
    ExpandFPLibCall(Node, RTLIB::LOG_F32, RTLIB::LOG_F64, RTLIB::LOG_F80,
                    RTLIB::LOG_F128, RTLIB::LOG_PPCF128, Results);
    break;
  case ISD::FLOG2:
  case ISD::STRICT_FLOG2:
    ExpandFPLibCall(Node, RTLIB::LOG2_F32, RTLIB::LOG2_F64, RTLIB::LOG2_F80,
                    RTLIB::LOG2_F128, RTLIB::LOG2_PPCF128, Results);
    break;
  case ISD::FLOG10:
  case ISD::STRICT_FLOG10:
    ExpandFPLibCall(Node, RTLIB::LOG10_F32, RTLIB::LOG10_F64, RTLIB::LOG10_F80,
                    RTLIB::LOG10_F128, RTLIB::LOG10_PPCF128, Results);
    break;
  case ISD::FEXP:
  case ISD::STRICT_FEXP:
    ExpandFPLibCall(Node, RTLIB::EXP_F32, RTLIB::EXP_F64, RTLIB::EXP_F80,
                    RTLIB::EXP_F128, RTLIB::EXP_PPCF128, Results);
    break;
  case ISD::FEXP2:
  case ISD::STRICT_FEXP2:
    ExpandFPLibCall(Node, RTLIB::EXP2_F32, RTLIB::EXP2_F64, RTLIB::EXP2_F80,
                    RTLIB::EXP2_F128, RTLIB::EXP2_PPCF128, Results);
    break;
  case ISD::FEXP10:
    ExpandFPLibCall(Node, RTLIB::EXP10_F32, RTLIB::EXP10_F64, RTLIB::EXP10_F80,
                    RTLIB::EXP10_F128, RTLIB::EXP10_PPCF128, Results);
    break;
  case ISD::FTRUNC:
  case ISD::STRICT_FTRUNC:
    ExpandFPLibCall(Node, RTLIB::TRUNC_F32, RTLIB::TRUNC_F64,
                    RTLIB::TRUNC_F80, RTLIB::TRUNC_F128,
                    RTLIB::TRUNC_PPCF128, Results);
    break;
  case ISD::FFLOOR:
  case ISD::STRICT_FFLOOR:
    ExpandFPLibCall(Node, RTLIB::FLOOR_F32, RTLIB::FLOOR_F64,
                    RTLIB::FLOOR_F80, RTLIB::FLOOR_F128,
                    RTLIB::FLOOR_PPCF128, Results);
    break;
  case ISD::FCEIL:
  case ISD::STRICT_FCEIL:
    ExpandFPLibCall(Node, RTLIB::CEIL_F32, RTLIB::CEIL_F64,
                    RTLIB::CEIL_F80, RTLIB::CEIL_F128,
                    RTLIB::CEIL_PPCF128, Results);
    break;
  case ISD::FRINT:
  case ISD::STRICT_FRINT:
    ExpandFPLibCall(Node, RTLIB::RINT_F32, RTLIB::RINT_F64,
                    RTLIB::RINT_F80, RTLIB::RINT_F128,
                    RTLIB::RINT_PPCF128, Results);
    break;
  case ISD::FNEARBYINT:
  case ISD::STRICT_FNEARBYINT:
    ExpandFPLibCall(Node, RTLIB::NEARBYINT_F32,
                    RTLIB::NEARBYINT_F64,
                    RTLIB::NEARBYINT_F80,
                    RTLIB::NEARBYINT_F128,
                    RTLIB::NEARBYINT_PPCF128, Results);
    break;
  case ISD::FROUND:
  case ISD::STRICT_FROUND:
    ExpandFPLibCall(Node, RTLIB::ROUND_F32,
                    RTLIB::ROUND_F64,
                    RTLIB::ROUND_F80,
                    RTLIB::ROUND_F128,
                    RTLIB::ROUND_PPCF128, Results);
    break;
  case ISD::FROUNDEVEN:
  case ISD::STRICT_FROUNDEVEN:
    ExpandFPLibCall(Node, RTLIB::ROUNDEVEN_F32,
                    RTLIB::ROUNDEVEN_F64,
                    RTLIB::ROUNDEVEN_F80,
                    RTLIB::ROUNDEVEN_F128,
                    RTLIB::ROUNDEVEN_PPCF128, Results);
    break;
  case ISD::FLDEXP:
  case ISD::STRICT_FLDEXP:
    ExpandFPLibCall(Node, RTLIB::LDEXP_F32, RTLIB::LDEXP_F64, RTLIB::LDEXP_F80,
                    RTLIB::LDEXP_F128, RTLIB::LDEXP_PPCF128, Results);
    break;
  case ISD::FFREXP: {
    ExpandFrexpLibCall(Node, Results);
    break;
  }
  case ISD::FPOWI:
  case ISD::STRICT_FPOWI: {
    RTLIB::Libcall LC = RTLIB::getPOWI(Node->getSimpleValueType(0));
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unexpected fpowi.");
    if (!TLI.getLibcallName(LC)) {
      // Some targets don't have a powi libcall; use pow instead.
      if (Node->isStrictFPOpcode()) {
        SDValue Exponent =
            DAG.getNode(ISD::STRICT_SINT_TO_FP, SDLoc(Node),
                        {Node->getValueType(0), Node->getValueType(1)},
                        {Node->getOperand(0), Node->getOperand(2)});
        SDValue FPOW =
            DAG.getNode(ISD::STRICT_FPOW, SDLoc(Node),
                        {Node->getValueType(0), Node->getValueType(1)},
                        {Exponent.getValue(1), Node->getOperand(1), Exponent});
        Results.push_back(FPOW);
        Results.push_back(FPOW.getValue(1));
      } else {
        SDValue Exponent =
            DAG.getNode(ISD::SINT_TO_FP, SDLoc(Node), Node->getValueType(0),
                        Node->getOperand(1));
        Results.push_back(DAG.getNode(ISD::FPOW, SDLoc(Node),
                                      Node->getValueType(0),
                                      Node->getOperand(0), Exponent));
      }
      break;
    }
    unsigned Offset = Node->isStrictFPOpcode() ? 1 : 0;
    bool ExponentHasSizeOfInt =
        DAG.getLibInfo().getIntSize() ==
        Node->getOperand(1 + Offset).getValueType().getSizeInBits();
    if (!ExponentHasSizeOfInt) {
      // If the exponent does not match with sizeof(int) a libcall to
      // RTLIB::POWI would use the wrong type for the argument.
      DAG.getContext()->emitError("POWI exponent does not match sizeof(int)");
      Results.push_back(DAG.getUNDEF(Node->getValueType(0)));
      break;
    }
    ExpandFPLibCall(Node, LC, Results);
    break;
  }
  case ISD::FPOW:
  case ISD::STRICT_FPOW:
    ExpandFPLibCall(Node, RTLIB::POW_F32, RTLIB::POW_F64, RTLIB::POW_F80,
                    RTLIB::POW_F128, RTLIB::POW_PPCF128, Results);
    break;
  case ISD::LROUND:
  case ISD::STRICT_LROUND:
    ExpandArgFPLibCall(Node, RTLIB::LROUND_F32,
                       RTLIB::LROUND_F64, RTLIB::LROUND_F80,
                       RTLIB::LROUND_F128,
                       RTLIB::LROUND_PPCF128, Results);
    break;
  case ISD::LLROUND:
  case ISD::STRICT_LLROUND:
    ExpandArgFPLibCall(Node, RTLIB::LLROUND_F32,
                       RTLIB::LLROUND_F64, RTLIB::LLROUND_F80,
                       RTLIB::LLROUND_F128,
                       RTLIB::LLROUND_PPCF128, Results);
    break;
  case ISD::LRINT:
  case ISD::STRICT_LRINT:
    ExpandArgFPLibCall(Node, RTLIB::LRINT_F32,
                       RTLIB::LRINT_F64, RTLIB::LRINT_F80,
                       RTLIB::LRINT_F128,
                       RTLIB::LRINT_PPCF128, Results);
    break;
  case ISD::LLRINT:
  case ISD::STRICT_LLRINT:
    ExpandArgFPLibCall(Node, RTLIB::LLRINT_F32,
                       RTLIB::LLRINT_F64, RTLIB::LLRINT_F80,
                       RTLIB::LLRINT_F128,
                       RTLIB::LLRINT_PPCF128, Results);
    break;
  case ISD::FDIV:
  case ISD::STRICT_FDIV:
    ExpandFPLibCall(Node, RTLIB::DIV_F32, RTLIB::DIV_F64,
                    RTLIB::DIV_F80, RTLIB::DIV_F128,
                    RTLIB::DIV_PPCF128, Results);
    break;
  case ISD::FREM:
  case ISD::STRICT_FREM:
    ExpandFPLibCall(Node, RTLIB::REM_F32, RTLIB::REM_F64,
                    RTLIB::REM_F80, RTLIB::REM_F128,
                    RTLIB::REM_PPCF128, Results);
    break;
  case ISD::FMA:
  case ISD::STRICT_FMA:
    ExpandFPLibCall(Node, RTLIB::FMA_F32, RTLIB::FMA_F64,
                    RTLIB::FMA_F80, RTLIB::FMA_F128,
                    RTLIB::FMA_PPCF128, Results);
    break;
  case ISD::FADD:
  case ISD::STRICT_FADD:
    ExpandFPLibCall(Node, RTLIB::ADD_F32, RTLIB::ADD_F64,
                    RTLIB::ADD_F80, RTLIB::ADD_F128,
                    RTLIB::ADD_PPCF128, Results);
    break;
  case ISD::FMUL:
  case ISD::STRICT_FMUL:
    ExpandFPLibCall(Node, RTLIB::MUL_F32, RTLIB::MUL_F64,
                    RTLIB::MUL_F80, RTLIB::MUL_F128,
                    RTLIB::MUL_PPCF128, Results);
    break;
  case ISD::FP16_TO_FP:
    if (Node->getValueType(0) == MVT::f32) {
      Results.push_back(ExpandLibCall(RTLIB::FPEXT_F16_F32, Node, false).first);
    }
    break;
  case ISD::STRICT_BF16_TO_FP:
    if (Node->getValueType(0) == MVT::f32) {
      TargetLowering::MakeLibCallOptions CallOptions;
      std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(
          DAG, RTLIB::FPEXT_BF16_F32, MVT::f32, Node->getOperand(1),
          CallOptions, SDLoc(Node), Node->getOperand(0));
      Results.push_back(Tmp.first);
      Results.push_back(Tmp.second);
    }
    break;
  case ISD::STRICT_FP16_TO_FP: {
    if (Node->getValueType(0) == MVT::f32) {
      TargetLowering::MakeLibCallOptions CallOptions;
      std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(
          DAG, RTLIB::FPEXT_F16_F32, MVT::f32, Node->getOperand(1), CallOptions,
          SDLoc(Node), Node->getOperand(0));
      Results.push_back(Tmp.first);
      Results.push_back(Tmp.second);
    }
    break;
  }
  case ISD::FP_TO_FP16: {
    RTLIB::Libcall LC =
        RTLIB::getFPROUND(Node->getOperand(0).getValueType(), MVT::f16);
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unable to expand fp_to_fp16");
    Results.push_back(ExpandLibCall(LC, Node, false).first);
    break;
  }
  case ISD::FP_TO_BF16: {
    RTLIB::Libcall LC =
        RTLIB::getFPROUND(Node->getOperand(0).getValueType(), MVT::bf16);
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unable to expand fp_to_bf16");
    Results.push_back(ExpandLibCall(LC, Node, false).first);
    break;
  }
  case ISD::STRICT_SINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP: {
    // TODO - Common the code with DAGTypeLegalizer::SoftenFloatRes_XINT_TO_FP
    bool IsStrict = Node->isStrictFPOpcode();
    bool Signed = Node->getOpcode() == ISD::SINT_TO_FP ||
                  Node->getOpcode() == ISD::STRICT_SINT_TO_FP;
    EVT SVT = Node->getOperand(IsStrict ? 1 : 0).getValueType();
    EVT RVT = Node->getValueType(0);
    EVT NVT = EVT();
    SDLoc dl(Node);

    // Even if the input is legal, no libcall may exactly match, eg. we don't
    // have i1 -> fp conversions. So, it needs to be promoted to a larger type,
    // eg: i13 -> fp. Then, look for an appropriate libcall.
    RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
    for (unsigned t = MVT::FIRST_INTEGER_VALUETYPE;
         t <= MVT::LAST_INTEGER_VALUETYPE && LC == RTLIB::UNKNOWN_LIBCALL;
         ++t) {
      NVT = (MVT::SimpleValueType)t;
      // The source needs to big enough to hold the operand.
      if (NVT.bitsGE(SVT))
        LC = Signed ? RTLIB::getSINTTOFP(NVT, RVT)
                    : RTLIB::getUINTTOFP(NVT, RVT);
    }
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unable to legalize as libcall");

    SDValue Chain = IsStrict ? Node->getOperand(0) : SDValue();
    // Sign/zero extend the argument if the libcall takes a larger type.
    SDValue Op = DAG.getNode(Signed ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, dl,
                             NVT, Node->getOperand(IsStrict ? 1 : 0));
    TargetLowering::MakeLibCallOptions CallOptions;
    CallOptions.setSExt(Signed);
    std::pair<SDValue, SDValue> Tmp =
        TLI.makeLibCall(DAG, LC, RVT, Op, CallOptions, dl, Chain);
    Results.push_back(Tmp.first);
    if (IsStrict)
      Results.push_back(Tmp.second);
    break;
  }
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT: {
    // TODO - Common the code with DAGTypeLegalizer::SoftenFloatOp_FP_TO_XINT.
    bool IsStrict = Node->isStrictFPOpcode();
    bool Signed = Node->getOpcode() == ISD::FP_TO_SINT ||
                  Node->getOpcode() == ISD::STRICT_FP_TO_SINT;

    SDValue Op = Node->getOperand(IsStrict ? 1 : 0);
    EVT SVT = Op.getValueType();
    EVT RVT = Node->getValueType(0);
    EVT NVT = EVT();
    SDLoc dl(Node);

    // Even if the result is legal, no libcall may exactly match, eg. we don't
    // have fp -> i1 conversions. So, it needs to be promoted to a larger type,
    // eg: fp -> i32. Then, look for an appropriate libcall.
    RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
    for (unsigned IntVT = MVT::FIRST_INTEGER_VALUETYPE;
         IntVT <= MVT::LAST_INTEGER_VALUETYPE && LC == RTLIB::UNKNOWN_LIBCALL;
         ++IntVT) {
      NVT = (MVT::SimpleValueType)IntVT;
      // The type needs to big enough to hold the result.
      if (NVT.bitsGE(RVT))
        LC = Signed ? RTLIB::getFPTOSINT(SVT, NVT)
                    : RTLIB::getFPTOUINT(SVT, NVT);
    }
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unable to legalize as libcall");

    SDValue Chain = IsStrict ? Node->getOperand(0) : SDValue();
    TargetLowering::MakeLibCallOptions CallOptions;
    std::pair<SDValue, SDValue> Tmp =
        TLI.makeLibCall(DAG, LC, NVT, Op, CallOptions, dl, Chain);

    // Truncate the result if the libcall returns a larger type.
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, RVT, Tmp.first));
    if (IsStrict)
      Results.push_back(Tmp.second);
    break;
  }

  case ISD::FP_ROUND:
  case ISD::STRICT_FP_ROUND: {
    // X = FP_ROUND(Y, TRUNC)
    // TRUNC is a flag, which is always an integer that is zero or one.
    // If TRUNC is 0, this is a normal rounding, if it is 1, this FP_ROUND
    // is known to not change the value of Y.
    // We can only expand it into libcall if the TRUNC is 0.
    bool IsStrict = Node->isStrictFPOpcode();
    SDValue Op = Node->getOperand(IsStrict ? 1 : 0);
    SDValue Chain = IsStrict ? Node->getOperand(0) : SDValue();
    EVT VT = Node->getValueType(0);
    assert(cast<ConstantSDNode>(Node->getOperand(IsStrict ? 2 : 1))->isZero() &&
           "Unable to expand as libcall if it is not normal rounding");

    RTLIB::Libcall LC = RTLIB::getFPROUND(Op.getValueType(), VT);
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unable to legalize as libcall");

    TargetLowering::MakeLibCallOptions CallOptions;
    std::pair<SDValue, SDValue> Tmp =
        TLI.makeLibCall(DAG, LC, VT, Op, CallOptions, SDLoc(Node), Chain);
    Results.push_back(Tmp.first);
    if (IsStrict)
      Results.push_back(Tmp.second);
    break;
  }
  case ISD::FP_EXTEND: {
    Results.push_back(
        ExpandLibCall(RTLIB::getFPEXT(Node->getOperand(0).getValueType(),
                                      Node->getValueType(0)),
                      Node, false).first);
    break;
  }
  case ISD::STRICT_FP_EXTEND:
  case ISD::STRICT_FP_TO_FP16:
  case ISD::STRICT_FP_TO_BF16: {
    RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
    if (Node->getOpcode() == ISD::STRICT_FP_TO_FP16)
      LC = RTLIB::getFPROUND(Node->getOperand(1).getValueType(), MVT::f16);
    else if (Node->getOpcode() == ISD::STRICT_FP_TO_BF16)
      LC = RTLIB::getFPROUND(Node->getOperand(1).getValueType(), MVT::bf16);
    else
      LC = RTLIB::getFPEXT(Node->getOperand(1).getValueType(),
                           Node->getValueType(0));

    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unable to legalize as libcall");

    TargetLowering::MakeLibCallOptions CallOptions;
    std::pair<SDValue, SDValue> Tmp =
        TLI.makeLibCall(DAG, LC, Node->getValueType(0), Node->getOperand(1),
                        CallOptions, SDLoc(Node), Node->getOperand(0));
    Results.push_back(Tmp.first);
    Results.push_back(Tmp.second);
    break;
  }
  case ISD::FSUB:
  case ISD::STRICT_FSUB:
    ExpandFPLibCall(Node, RTLIB::SUB_F32, RTLIB::SUB_F64,
                    RTLIB::SUB_F80, RTLIB::SUB_F128,
                    RTLIB::SUB_PPCF128, Results);
    break;
  case ISD::SREM:
    Results.push_back(ExpandIntLibCall(Node, true,
                                       RTLIB::SREM_I8,
                                       RTLIB::SREM_I16, RTLIB::SREM_I32,
                                       RTLIB::SREM_I64, RTLIB::SREM_I128));
    break;
  case ISD::UREM:
    Results.push_back(ExpandIntLibCall(Node, false,
                                       RTLIB::UREM_I8,
                                       RTLIB::UREM_I16, RTLIB::UREM_I32,
                                       RTLIB::UREM_I64, RTLIB::UREM_I128));
    break;
  case ISD::SDIV:
    Results.push_back(ExpandIntLibCall(Node, true,
                                       RTLIB::SDIV_I8,
                                       RTLIB::SDIV_I16, RTLIB::SDIV_I32,
                                       RTLIB::SDIV_I64, RTLIB::SDIV_I128));
    break;
  case ISD::UDIV:
    Results.push_back(ExpandIntLibCall(Node, false,
                                       RTLIB::UDIV_I8,
                                       RTLIB::UDIV_I16, RTLIB::UDIV_I32,
                                       RTLIB::UDIV_I64, RTLIB::UDIV_I128));
    break;
  case ISD::SDIVREM:
  case ISD::UDIVREM:
    // Expand into divrem libcall
    ExpandDivRemLibCall(Node, Results);
    break;
  case ISD::MUL:
    Results.push_back(ExpandIntLibCall(Node, false,
                                       RTLIB::MUL_I8,
                                       RTLIB::MUL_I16, RTLIB::MUL_I32,
                                       RTLIB::MUL_I64, RTLIB::MUL_I128));
    break;
  case ISD::CTLZ_ZERO_UNDEF:
    switch (Node->getSimpleValueType(0).SimpleTy) {
    default:
      llvm_unreachable("LibCall explicitly requested, but not available");
    case MVT::i32:
      Results.push_back(ExpandLibCall(RTLIB::CTLZ_I32, Node, false).first);
      break;
    case MVT::i64:
      Results.push_back(ExpandLibCall(RTLIB::CTLZ_I64, Node, false).first);
      break;
    case MVT::i128:
      Results.push_back(ExpandLibCall(RTLIB::CTLZ_I128, Node, false).first);
      break;
    }
    break;
  case ISD::RESET_FPENV: {
    // It is legalized to call 'fesetenv(FE_DFL_ENV)'. On most targets
    // FE_DFL_ENV is defined as '((const fenv_t *) -1)' in glibc.
    SDValue Ptr = DAG.getIntPtrConstant(-1LL, dl);
    SDValue Chain = Node->getOperand(0);
    Results.push_back(
        DAG.makeStateFunctionCall(RTLIB::FESETENV, Ptr, Chain, dl));
    break;
  }
  case ISD::GET_FPENV_MEM: {
    SDValue Chain = Node->getOperand(0);
    SDValue EnvPtr = Node->getOperand(1);
    Results.push_back(
        DAG.makeStateFunctionCall(RTLIB::FEGETENV, EnvPtr, Chain, dl));
    break;
  }
  case ISD::SET_FPENV_MEM: {
    SDValue Chain = Node->getOperand(0);
    SDValue EnvPtr = Node->getOperand(1);
    Results.push_back(
        DAG.makeStateFunctionCall(RTLIB::FESETENV, EnvPtr, Chain, dl));
    break;
  }
  case ISD::GET_FPMODE: {
    // Call fegetmode, which saves control modes into a stack slot. Then load
    // the value to return from the stack.
    EVT ModeVT = Node->getValueType(0);
    SDValue StackPtr = DAG.CreateStackTemporary(ModeVT);
    int SPFI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
    SDValue Chain = DAG.makeStateFunctionCall(RTLIB::FEGETMODE, StackPtr,
                                              Node->getOperand(0), dl);
    SDValue LdInst = DAG.getLoad(
        ModeVT, dl, Chain, StackPtr,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI));
    Results.push_back(LdInst);
    Results.push_back(LdInst.getValue(1));
    break;
  }
  case ISD::SET_FPMODE: {
    // Move control modes to stack slot and then call fesetmode with the pointer
    // to the slot as argument.
    SDValue Mode = Node->getOperand(1);
    EVT ModeVT = Mode.getValueType();
    SDValue StackPtr = DAG.CreateStackTemporary(ModeVT);
    int SPFI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
    SDValue StInst = DAG.getStore(
        Node->getOperand(0), dl, Mode, StackPtr,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI));
    Results.push_back(
        DAG.makeStateFunctionCall(RTLIB::FESETMODE, StackPtr, StInst, dl));
    break;
  }
  case ISD::RESET_FPMODE: {
    // It is legalized to a call 'fesetmode(FE_DFL_MODE)'. On most targets
    // FE_DFL_MODE is defined as '((const femode_t *) -1)' in glibc. If not, the
    // target must provide custom lowering.
    const DataLayout &DL = DAG.getDataLayout();
    EVT PtrTy = TLI.getPointerTy(DL);
    SDValue Mode = DAG.getConstant(-1LL, dl, PtrTy);
    Results.push_back(DAG.makeStateFunctionCall(RTLIB::FESETMODE, Mode,
                                                Node->getOperand(0), dl));
    break;
  }
  }

  // Replace the original node with the legalized result.
  if (!Results.empty()) {
    LLVM_DEBUG(dbgs() << "Successfully converted node to libcall\n");
    ReplaceNode(Node, Results.data());
  } else
    LLVM_DEBUG(dbgs() << "Could not convert node to libcall\n");
}

// Determine the vector type to use in place of an original scalar element when
// promoting equally sized vectors.
static MVT getPromotedVectorElementType(const TargetLowering &TLI,
                                        MVT EltVT, MVT NewEltVT) {
  unsigned OldEltsPerNewElt = EltVT.getSizeInBits() / NewEltVT.getSizeInBits();
  MVT MidVT = OldEltsPerNewElt == 1
                  ? NewEltVT
                  : MVT::getVectorVT(NewEltVT, OldEltsPerNewElt);
  assert(TLI.isTypeLegal(MidVT) && "unexpected");
  return MidVT;
}

void SelectionDAGLegalize::PromoteNode(SDNode *Node) {
  LLVM_DEBUG(dbgs() << "Trying to promote node\n");
  SmallVector<SDValue, 8> Results;
  MVT OVT = Node->getSimpleValueType(0);
  if (Node->getOpcode() == ISD::UINT_TO_FP ||
      Node->getOpcode() == ISD::SINT_TO_FP ||
      Node->getOpcode() == ISD::SETCC ||
      Node->getOpcode() == ISD::EXTRACT_VECTOR_ELT ||
      Node->getOpcode() == ISD::INSERT_VECTOR_ELT) {
    OVT = Node->getOperand(0).getSimpleValueType();
  }
  if (Node->getOpcode() == ISD::ATOMIC_STORE ||
      Node->getOpcode() == ISD::STRICT_UINT_TO_FP ||
      Node->getOpcode() == ISD::STRICT_SINT_TO_FP ||
      Node->getOpcode() == ISD::STRICT_FSETCC ||
      Node->getOpcode() == ISD::STRICT_FSETCCS ||
      Node->getOpcode() == ISD::VP_REDUCE_FADD ||
      Node->getOpcode() == ISD::VP_REDUCE_FMUL ||
      Node->getOpcode() == ISD::VP_REDUCE_FMAX ||
      Node->getOpcode() == ISD::VP_REDUCE_FMIN ||
      Node->getOpcode() == ISD::VP_REDUCE_FMAXIMUM ||
      Node->getOpcode() == ISD::VP_REDUCE_FMINIMUM ||
      Node->getOpcode() == ISD::VP_REDUCE_SEQ_FADD)
    OVT = Node->getOperand(1).getSimpleValueType();
  if (Node->getOpcode() == ISD::BR_CC ||
      Node->getOpcode() == ISD::SELECT_CC)
    OVT = Node->getOperand(2).getSimpleValueType();
  MVT NVT = TLI.getTypeToPromoteTo(Node->getOpcode(), OVT);
  SDLoc dl(Node);
  SDValue Tmp1, Tmp2, Tmp3, Tmp4;
  switch (Node->getOpcode()) {
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTLZ:
  case ISD::CTPOP: {
    // Zero extend the argument unless its cttz, then use any_extend.
    if (Node->getOpcode() == ISD::CTTZ ||
        Node->getOpcode() == ISD::CTTZ_ZERO_UNDEF)
      Tmp1 = DAG.getNode(ISD::ANY_EXTEND, dl, NVT, Node->getOperand(0));
    else
      Tmp1 = DAG.getNode(ISD::ZERO_EXTEND, dl, NVT, Node->getOperand(0));

    unsigned NewOpc = Node->getOpcode();
    if (NewOpc == ISD::CTTZ) {
      // The count is the same in the promoted type except if the original
      // value was zero.  This can be handled by setting the bit just off
      // the top of the original type.
      auto TopBit = APInt::getOneBitSet(NVT.getSizeInBits(),
                                        OVT.getSizeInBits());
      Tmp1 = DAG.getNode(ISD::OR, dl, NVT, Tmp1,
                         DAG.getConstant(TopBit, dl, NVT));
      NewOpc = ISD::CTTZ_ZERO_UNDEF;
    }
    // Perform the larger operation. For CTPOP and CTTZ_ZERO_UNDEF, this is
    // already the correct result.
    Tmp1 = DAG.getNode(NewOpc, dl, NVT, Tmp1);
    if (NewOpc == ISD::CTLZ) {
      // Tmp1 = Tmp1 - (sizeinbits(NVT) - sizeinbits(Old VT))
      Tmp1 = DAG.getNode(ISD::SUB, dl, NVT, Tmp1,
                          DAG.getConstant(NVT.getSizeInBits() -
                                          OVT.getSizeInBits(), dl, NVT));
    }
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp1));
    break;
  }
  case ISD::CTLZ_ZERO_UNDEF: {
    // We know that the argument is unlikely to be zero, hence we can take a
    // different approach as compared to ISD::CTLZ

    // Any Extend the argument
    auto AnyExtendedNode =
        DAG.getNode(ISD::ANY_EXTEND, dl, NVT, Node->getOperand(0));

    // Tmp1 = Tmp1 << (sizeinbits(NVT) - sizeinbits(Old VT))
    auto ShiftConstant = DAG.getShiftAmountConstant(
        NVT.getSizeInBits() - OVT.getSizeInBits(), NVT, dl);
    auto LeftShiftResult =
        DAG.getNode(ISD::SHL, dl, NVT, AnyExtendedNode, ShiftConstant);

    // Perform the larger operation
    auto CTLZResult = DAG.getNode(Node->getOpcode(), dl, NVT, LeftShiftResult);
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, CTLZResult));
    break;
  }
  case ISD::BITREVERSE:
  case ISD::BSWAP: {
    unsigned DiffBits = NVT.getSizeInBits() - OVT.getSizeInBits();
    Tmp1 = DAG.getNode(ISD::ZERO_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp1 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1);
    Tmp1 = DAG.getNode(
        ISD::SRL, dl, NVT, Tmp1,
        DAG.getConstant(DiffBits, dl,
                        TLI.getShiftAmountTy(NVT, DAG.getDataLayout())));

    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp1));
    break;
  }
  case ISD::FP_TO_UINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::FP_TO_SINT:
  case ISD::STRICT_FP_TO_SINT:
    PromoteLegalFP_TO_INT(Node, dl, Results);
    break;
  case ISD::FP_TO_UINT_SAT:
  case ISD::FP_TO_SINT_SAT:
    Results.push_back(PromoteLegalFP_TO_INT_SAT(Node, dl));
    break;
  case ISD::UINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
  case ISD::SINT_TO_FP:
  case ISD::STRICT_SINT_TO_FP:
    PromoteLegalINT_TO_FP(Node, dl, Results);
    break;
  case ISD::VAARG: {
    SDValue Chain = Node->getOperand(0); // Get the chain.
    SDValue Ptr = Node->getOperand(1); // Get the pointer.

    unsigned TruncOp;
    if (OVT.isVector()) {
      TruncOp = ISD::BITCAST;
    } else {
      assert(OVT.isInteger()
        && "VAARG promotion is supported only for vectors or integer types");
      TruncOp = ISD::TRUNCATE;
    }

    // Perform the larger operation, then convert back
    Tmp1 = DAG.getVAArg(NVT, dl, Chain, Ptr, Node->getOperand(2),
             Node->getConstantOperandVal(3));
    Chain = Tmp1.getValue(1);

    Tmp2 = DAG.getNode(TruncOp, dl, OVT, Tmp1);

    // Modified the chain result - switch anything that used the old chain to
    // use the new one.
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 0), Tmp2);
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), Chain);
    if (UpdatedNodes) {
      UpdatedNodes->insert(Tmp2.getNode());
      UpdatedNodes->insert(Chain.getNode());
    }
    ReplacedNode(Node);
    break;
  }
  case ISD::MUL:
  case ISD::SDIV:
  case ISD::SREM:
  case ISD::UDIV:
  case ISD::UREM:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR: {
    unsigned ExtOp, TruncOp;
    if (OVT.isVector()) {
      ExtOp   = ISD::BITCAST;
      TruncOp = ISD::BITCAST;
    } else {
      assert(OVT.isInteger() && "Cannot promote logic operation");

      switch (Node->getOpcode()) {
      default:
        ExtOp = ISD::ANY_EXTEND;
        break;
      case ISD::SDIV:
      case ISD::SREM:
      case ISD::SMIN:
      case ISD::SMAX:
        ExtOp = ISD::SIGN_EXTEND;
        break;
      case ISD::UDIV:
      case ISD::UREM:
        ExtOp = ISD::ZERO_EXTEND;
        break;
      case ISD::UMIN:
      case ISD::UMAX:
        if (TLI.isSExtCheaperThanZExt(OVT, NVT))
          ExtOp = ISD::SIGN_EXTEND;
        else
          ExtOp = ISD::ZERO_EXTEND;
        break;
      }
      TruncOp = ISD::TRUNCATE;
    }
    // Promote each of the values to the new type.
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    // Perform the larger operation, then convert back
    Tmp1 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2);
    Results.push_back(DAG.getNode(TruncOp, dl, OVT, Tmp1));
    break;
  }
  case ISD::UMUL_LOHI:
  case ISD::SMUL_LOHI: {
    // Promote to a multiply in a wider integer type.
    unsigned ExtOp = Node->getOpcode() == ISD::UMUL_LOHI ? ISD::ZERO_EXTEND
                                                         : ISD::SIGN_EXTEND;
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    Tmp1 = DAG.getNode(ISD::MUL, dl, NVT, Tmp1, Tmp2);

    auto &DL = DAG.getDataLayout();
    unsigned OriginalSize = OVT.getScalarSizeInBits();
    Tmp2 = DAG.getNode(
        ISD::SRL, dl, NVT, Tmp1,
        DAG.getConstant(OriginalSize, dl, TLI.getScalarShiftAmountTy(DL, NVT)));
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp1));
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp2));
    break;
  }
  case ISD::SELECT: {
    unsigned ExtOp, TruncOp;
    if (Node->getValueType(0).isVector() ||
        Node->getValueType(0).getSizeInBits() == NVT.getSizeInBits()) {
      ExtOp   = ISD::BITCAST;
      TruncOp = ISD::BITCAST;
    } else if (Node->getValueType(0).isInteger()) {
      ExtOp   = ISD::ANY_EXTEND;
      TruncOp = ISD::TRUNCATE;
    } else {
      ExtOp   = ISD::FP_EXTEND;
      TruncOp = ISD::FP_ROUND;
    }
    Tmp1 = Node->getOperand(0);
    // Promote each of the values to the new type.
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    Tmp3 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(2));
    // Perform the larger operation, then round down.
    Tmp1 = DAG.getSelect(dl, NVT, Tmp1, Tmp2, Tmp3);
    Tmp1->setFlags(Node->getFlags());
    if (TruncOp != ISD::FP_ROUND)
      Tmp1 = DAG.getNode(TruncOp, dl, Node->getValueType(0), Tmp1);
    else
      Tmp1 = DAG.getNode(TruncOp, dl, Node->getValueType(0), Tmp1,
                         DAG.getIntPtrConstant(0, dl));
    Results.push_back(Tmp1);
    break;
  }
  case ISD::VECTOR_SHUFFLE: {
    ArrayRef<int> Mask = cast<ShuffleVectorSDNode>(Node)->getMask();

    // Cast the two input vectors.
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::BITCAST, dl, NVT, Node->getOperand(1));

    // Convert the shuffle mask to the right # elements.
    Tmp1 = ShuffleWithNarrowerEltType(NVT, OVT, dl, Tmp1, Tmp2, Mask);
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, OVT, Tmp1);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::VECTOR_SPLICE: {
    Tmp1 = DAG.getNode(ISD::ANY_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::ANY_EXTEND, dl, NVT, Node->getOperand(1));
    Tmp3 = DAG.getNode(ISD::VECTOR_SPLICE, dl, NVT, Tmp1, Tmp2,
                       Node->getOperand(2));
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp3));
    break;
  }
  case ISD::SELECT_CC: {
    SDValue Cond = Node->getOperand(4);
    ISD::CondCode CCCode = cast<CondCodeSDNode>(Cond)->get();
    // Type of the comparison operands.
    MVT CVT = Node->getSimpleValueType(0);
    assert(CVT == OVT && "not handled");

    unsigned ExtOp = ISD::FP_EXTEND;
    if (NVT.isInteger()) {
      ExtOp = isSignedIntSetCC(CCCode) ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    }

    // Promote the comparison operands, if needed.
    if (TLI.isCondCodeLegal(CCCode, CVT)) {
      Tmp1 = Node->getOperand(0);
      Tmp2 = Node->getOperand(1);
    } else {
      Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(0));
      Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    }
    // Cast the true/false operands.
    Tmp3 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(2));
    Tmp4 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(3));

    Tmp1 = DAG.getNode(ISD::SELECT_CC, dl, NVT, {Tmp1, Tmp2, Tmp3, Tmp4, Cond},
                       Node->getFlags());

    // Cast the result back to the original type.
    if (ExtOp != ISD::FP_EXTEND)
      Tmp1 = DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp1);
    else
      Tmp1 = DAG.getNode(ISD::FP_ROUND, dl, OVT, Tmp1,
                         DAG.getIntPtrConstant(0, dl, /*isTarget=*/true));

    Results.push_back(Tmp1);
    break;
  }
  case ISD::SETCC:
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS: {
    unsigned ExtOp = ISD::FP_EXTEND;
    if (NVT.isInteger()) {
      ISD::CondCode CCCode = cast<CondCodeSDNode>(Node->getOperand(2))->get();
      if (isSignedIntSetCC(CCCode) ||
          TLI.isSExtCheaperThanZExt(Node->getOperand(0).getValueType(), NVT))
        ExtOp = ISD::SIGN_EXTEND;
      else
        ExtOp = ISD::ZERO_EXTEND;
    }
    if (Node->isStrictFPOpcode()) {
      SDValue InChain = Node->getOperand(0);
      std::tie(Tmp1, std::ignore) =
          DAG.getStrictFPExtendOrRound(Node->getOperand(1), InChain, dl, NVT);
      std::tie(Tmp2, std::ignore) =
          DAG.getStrictFPExtendOrRound(Node->getOperand(2), InChain, dl, NVT);
      SmallVector<SDValue, 2> TmpChains = {Tmp1.getValue(1), Tmp2.getValue(1)};
      SDValue OutChain = DAG.getTokenFactor(dl, TmpChains);
      SDVTList VTs = DAG.getVTList(Node->getValueType(0), MVT::Other);
      Results.push_back(DAG.getNode(Node->getOpcode(), dl, VTs,
                                    {OutChain, Tmp1, Tmp2, Node->getOperand(3)},
                                    Node->getFlags()));
      Results.push_back(Results.back().getValue(1));
      break;
    }
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    Results.push_back(DAG.getNode(ISD::SETCC, dl, Node->getValueType(0), Tmp1,
                                  Tmp2, Node->getOperand(2), Node->getFlags()));
    break;
  }
  case ISD::BR_CC: {
    unsigned ExtOp = ISD::FP_EXTEND;
    if (NVT.isInteger()) {
      ISD::CondCode CCCode =
        cast<CondCodeSDNode>(Node->getOperand(1))->get();
      ExtOp = isSignedIntSetCC(CCCode) ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    }
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(2));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(3));
    Results.push_back(DAG.getNode(ISD::BR_CC, dl, Node->getValueType(0),
                                  Node->getOperand(0), Node->getOperand(1),
                                  Tmp1, Tmp2, Node->getOperand(4)));
    break;
  }
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FDIV:
  case ISD::FREM:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::FPOW:
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(1));
    Tmp3 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2,
                       Node->getFlags());
    Results.push_back(
        DAG.getNode(ISD::FP_ROUND, dl, OVT, Tmp3,
                    DAG.getIntPtrConstant(0, dl, /*isTarget=*/true)));
    break;
  case ISD::STRICT_FADD:
  case ISD::STRICT_FSUB:
  case ISD::STRICT_FMUL:
  case ISD::STRICT_FDIV:
  case ISD::STRICT_FMINNUM:
  case ISD::STRICT_FMAXNUM:
  case ISD::STRICT_FREM:
  case ISD::STRICT_FPOW:
    Tmp1 = DAG.getNode(ISD::STRICT_FP_EXTEND, dl, {NVT, MVT::Other},
                       {Node->getOperand(0), Node->getOperand(1)});
    Tmp2 = DAG.getNode(ISD::STRICT_FP_EXTEND, dl, {NVT, MVT::Other},
                       {Node->getOperand(0), Node->getOperand(2)});
    Tmp3 = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Tmp1.getValue(1),
                       Tmp2.getValue(1));
    Tmp1 = DAG.getNode(Node->getOpcode(), dl, {NVT, MVT::Other},
                       {Tmp3, Tmp1, Tmp2});
    Tmp1 = DAG.getNode(ISD::STRICT_FP_ROUND, dl, {OVT, MVT::Other},
                       {Tmp1.getValue(1), Tmp1, DAG.getIntPtrConstant(0, dl)});
    Results.push_back(Tmp1);
    Results.push_back(Tmp1.getValue(1));
    break;
  case ISD::FMA:
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(1));
    Tmp3 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(2));
    Results.push_back(
        DAG.getNode(ISD::FP_ROUND, dl, OVT,
                    DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2, Tmp3),
                    DAG.getIntPtrConstant(0, dl, /*isTarget=*/true)));
    break;
  case ISD::STRICT_FMA:
    Tmp1 = DAG.getNode(ISD::STRICT_FP_EXTEND, dl, {NVT, MVT::Other},
                       {Node->getOperand(0), Node->getOperand(1)});
    Tmp2 = DAG.getNode(ISD::STRICT_FP_EXTEND, dl, {NVT, MVT::Other},
                       {Node->getOperand(0), Node->getOperand(2)});
    Tmp3 = DAG.getNode(ISD::STRICT_FP_EXTEND, dl, {NVT, MVT::Other},
                       {Node->getOperand(0), Node->getOperand(3)});
    Tmp4 = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Tmp1.getValue(1),
                       Tmp2.getValue(1), Tmp3.getValue(1));
    Tmp4 = DAG.getNode(Node->getOpcode(), dl, {NVT, MVT::Other},
                       {Tmp4, Tmp1, Tmp2, Tmp3});
    Tmp4 = DAG.getNode(ISD::STRICT_FP_ROUND, dl, {OVT, MVT::Other},
                       {Tmp4.getValue(1), Tmp4, DAG.getIntPtrConstant(0, dl)});
    Results.push_back(Tmp4);
    Results.push_back(Tmp4.getValue(1));
    break;
  case ISD::FCOPYSIGN:
  case ISD::FLDEXP:
  case ISD::FPOWI: {
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = Node->getOperand(1);
    Tmp3 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2);

    // fcopysign doesn't change anything but the sign bit, so
    //   (fp_round (fcopysign (fpext a), b))
    // is as precise as
    //   (fp_round (fpext a))
    // which is a no-op. Mark it as a TRUNCating FP_ROUND.
    const bool isTrunc = (Node->getOpcode() == ISD::FCOPYSIGN);
    Results.push_back(
        DAG.getNode(ISD::FP_ROUND, dl, OVT, Tmp3,
                    DAG.getIntPtrConstant(isTrunc, dl, /*isTarget=*/true)));
    break;
  }
  case ISD::STRICT_FPOWI:
    Tmp1 = DAG.getNode(ISD::STRICT_FP_EXTEND, dl, {NVT, MVT::Other},
                       {Node->getOperand(0), Node->getOperand(1)});
    Tmp2 = DAG.getNode(Node->getOpcode(), dl, {NVT, MVT::Other},
                       {Tmp1.getValue(1), Tmp1, Node->getOperand(2)});
    Tmp3 = DAG.getNode(ISD::STRICT_FP_ROUND, dl, {OVT, MVT::Other},
                       {Tmp2.getValue(1), Tmp2, DAG.getIntPtrConstant(0, dl)});
    Results.push_back(Tmp3);
    Results.push_back(Tmp3.getValue(1));
    break;
  case ISD::FFREXP: {
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::FFREXP, dl, {NVT, Node->getValueType(1)}, Tmp1);

    Results.push_back(
        DAG.getNode(ISD::FP_ROUND, dl, OVT, Tmp2,
                    DAG.getIntPtrConstant(0, dl, /*isTarget=*/true)));

    Results.push_back(Tmp2.getValue(1));
    break;
  }
  case ISD::FFLOOR:
  case ISD::FCEIL:
  case ISD::FRINT:
  case ISD::FNEARBYINT:
  case ISD::FROUND:
  case ISD::FROUNDEVEN:
  case ISD::FTRUNC:
  case ISD::FNEG:
  case ISD::FSQRT:
  case ISD::FSIN:
  case ISD::FCOS:
  case ISD::FTAN:
  case ISD::FASIN:
  case ISD::FACOS:
  case ISD::FATAN:
  case ISD::FSINH:
  case ISD::FCOSH:
  case ISD::FTANH:
  case ISD::FLOG:
  case ISD::FLOG2:
  case ISD::FLOG10:
  case ISD::FABS:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FEXP10:
  case ISD::FCANONICALIZE:
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1);
    Results.push_back(
        DAG.getNode(ISD::FP_ROUND, dl, OVT, Tmp2,
                    DAG.getIntPtrConstant(0, dl, /*isTarget=*/true)));
    break;
  case ISD::STRICT_FFLOOR:
  case ISD::STRICT_FCEIL:
  case ISD::STRICT_FRINT:
  case ISD::STRICT_FNEARBYINT:
  case ISD::STRICT_FROUND:
  case ISD::STRICT_FROUNDEVEN:
  case ISD::STRICT_FTRUNC:
  case ISD::STRICT_FSQRT:
  case ISD::STRICT_FSIN:
  case ISD::STRICT_FCOS:
  case ISD::STRICT_FTAN:
  case ISD::STRICT_FASIN:
  case ISD::STRICT_FACOS:
  case ISD::STRICT_FATAN:
  case ISD::STRICT_FSINH:
  case ISD::STRICT_FCOSH:
  case ISD::STRICT_FTANH:
  case ISD::STRICT_FLOG:
  case ISD::STRICT_FLOG2:
  case ISD::STRICT_FLOG10:
  case ISD::STRICT_FEXP:
  case ISD::STRICT_FEXP2:
    Tmp1 = DAG.getNode(ISD::STRICT_FP_EXTEND, dl, {NVT, MVT::Other},
                       {Node->getOperand(0), Node->getOperand(1)});
    Tmp2 = DAG.getNode(Node->getOpcode(), dl, {NVT, MVT::Other},
                       {Tmp1.getValue(1), Tmp1});
    Tmp3 = DAG.getNode(ISD::STRICT_FP_ROUND, dl, {OVT, MVT::Other},
                       {Tmp2.getValue(1), Tmp2, DAG.getIntPtrConstant(0, dl)});
    Results.push_back(Tmp3);
    Results.push_back(Tmp3.getValue(1));
    break;
  case ISD::BUILD_VECTOR: {
    MVT EltVT = OVT.getVectorElementType();
    MVT NewEltVT = NVT.getVectorElementType();

    // Handle bitcasts to a different vector type with the same total bit size
    //
    // e.g. v2i64 = build_vector i64:x, i64:y => v4i32
    //  =>
    //  v4i32 = concat_vectors (v2i32 (bitcast i64:x)), (v2i32 (bitcast i64:y))

    assert(NVT.isVector() && OVT.getSizeInBits() == NVT.getSizeInBits() &&
           "Invalid promote type for build_vector");
    assert(NewEltVT.bitsLE(EltVT) && "not handled");

    MVT MidVT = getPromotedVectorElementType(TLI, EltVT, NewEltVT);

    SmallVector<SDValue, 8> NewOps;
    for (const SDValue &Op : Node->op_values())
      NewOps.push_back(DAG.getNode(ISD::BITCAST, SDLoc(Op), MidVT, Op));

    SDLoc SL(Node);
    SDValue Concat =
        DAG.getNode(MidVT == NewEltVT ? ISD::BUILD_VECTOR : ISD::CONCAT_VECTORS,
                    SL, NVT, NewOps);
    SDValue CvtVec = DAG.getNode(ISD::BITCAST, SL, OVT, Concat);
    Results.push_back(CvtVec);
    break;
  }
  case ISD::EXTRACT_VECTOR_ELT: {
    MVT EltVT = OVT.getVectorElementType();
    MVT NewEltVT = NVT.getVectorElementType();

    // Handle bitcasts to a different vector type with the same total bit size.
    //
    // e.g. v2i64 = extract_vector_elt x:v2i64, y:i32
    //  =>
    //  v4i32:castx = bitcast x:v2i64
    //
    // i64 = bitcast
    //   (v2i32 build_vector (i32 (extract_vector_elt castx, (2 * y))),
    //                       (i32 (extract_vector_elt castx, (2 * y + 1)))
    //

    assert(NVT.isVector() && OVT.getSizeInBits() == NVT.getSizeInBits() &&
           "Invalid promote type for extract_vector_elt");
    assert(NewEltVT.bitsLT(EltVT) && "not handled");

    MVT MidVT = getPromotedVectorElementType(TLI, EltVT, NewEltVT);
    unsigned NewEltsPerOldElt = MidVT.getVectorNumElements();

    SDValue Idx = Node->getOperand(1);
    EVT IdxVT = Idx.getValueType();
    SDLoc SL(Node);
    SDValue Factor = DAG.getConstant(NewEltsPerOldElt, SL, IdxVT);
    SDValue NewBaseIdx = DAG.getNode(ISD::MUL, SL, IdxVT, Idx, Factor);

    SDValue CastVec = DAG.getNode(ISD::BITCAST, SL, NVT, Node->getOperand(0));

    SmallVector<SDValue, 8> NewOps;
    for (unsigned I = 0; I < NewEltsPerOldElt; ++I) {
      SDValue IdxOffset = DAG.getConstant(I, SL, IdxVT);
      SDValue TmpIdx = DAG.getNode(ISD::ADD, SL, IdxVT, NewBaseIdx, IdxOffset);

      SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, NewEltVT,
                                CastVec, TmpIdx);
      NewOps.push_back(Elt);
    }

    SDValue NewVec = DAG.getBuildVector(MidVT, SL, NewOps);
    Results.push_back(DAG.getNode(ISD::BITCAST, SL, EltVT, NewVec));
    break;
  }
  case ISD::INSERT_VECTOR_ELT: {
    MVT EltVT = OVT.getVectorElementType();
    MVT NewEltVT = NVT.getVectorElementType();

    // Handle bitcasts to a different vector type with the same total bit size
    //
    // e.g. v2i64 = insert_vector_elt x:v2i64, y:i64, z:i32
    //  =>
    //  v4i32:castx = bitcast x:v2i64
    //  v2i32:casty = bitcast y:i64
    //
    // v2i64 = bitcast
    //   (v4i32 insert_vector_elt
    //       (v4i32 insert_vector_elt v4i32:castx,
    //                                (extract_vector_elt casty, 0), 2 * z),
    //        (extract_vector_elt casty, 1), (2 * z + 1))

    assert(NVT.isVector() && OVT.getSizeInBits() == NVT.getSizeInBits() &&
           "Invalid promote type for insert_vector_elt");
    assert(NewEltVT.bitsLT(EltVT) && "not handled");

    MVT MidVT = getPromotedVectorElementType(TLI, EltVT, NewEltVT);
    unsigned NewEltsPerOldElt = MidVT.getVectorNumElements();

    SDValue Val = Node->getOperand(1);
    SDValue Idx = Node->getOperand(2);
    EVT IdxVT = Idx.getValueType();
    SDLoc SL(Node);

    SDValue Factor = DAG.getConstant(NewEltsPerOldElt, SDLoc(), IdxVT);
    SDValue NewBaseIdx = DAG.getNode(ISD::MUL, SL, IdxVT, Idx, Factor);

    SDValue CastVec = DAG.getNode(ISD::BITCAST, SL, NVT, Node->getOperand(0));
    SDValue CastVal = DAG.getNode(ISD::BITCAST, SL, MidVT, Val);

    SDValue NewVec = CastVec;
    for (unsigned I = 0; I < NewEltsPerOldElt; ++I) {
      SDValue IdxOffset = DAG.getConstant(I, SL, IdxVT);
      SDValue InEltIdx = DAG.getNode(ISD::ADD, SL, IdxVT, NewBaseIdx, IdxOffset);

      SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, NewEltVT,
                                CastVal, IdxOffset);

      NewVec = DAG.getNode(ISD::INSERT_VECTOR_ELT, SL, NVT,
                           NewVec, Elt, InEltIdx);
    }

    Results.push_back(DAG.getNode(ISD::BITCAST, SL, OVT, NewVec));
    break;
  }
  case ISD::SCALAR_TO_VECTOR: {
    MVT EltVT = OVT.getVectorElementType();
    MVT NewEltVT = NVT.getVectorElementType();

    // Handle bitcasts to different vector type with the same total bit size.
    //
    // e.g. v2i64 = scalar_to_vector x:i64
    //   =>
    //  concat_vectors (v2i32 bitcast x:i64), (v2i32 undef)
    //

    MVT MidVT = getPromotedVectorElementType(TLI, EltVT, NewEltVT);
    SDValue Val = Node->getOperand(0);
    SDLoc SL(Node);

    SDValue CastVal = DAG.getNode(ISD::BITCAST, SL, MidVT, Val);
    SDValue Undef = DAG.getUNDEF(MidVT);

    SmallVector<SDValue, 8> NewElts;
    NewElts.push_back(CastVal);
    for (unsigned I = 1, NElts = OVT.getVectorNumElements(); I != NElts; ++I)
      NewElts.push_back(Undef);

    SDValue Concat = DAG.getNode(ISD::CONCAT_VECTORS, SL, NVT, NewElts);
    SDValue CvtVec = DAG.getNode(ISD::BITCAST, SL, OVT, Concat);
    Results.push_back(CvtVec);
    break;
  }
  case ISD::ATOMIC_SWAP:
  case ISD::ATOMIC_STORE: {
    AtomicSDNode *AM = cast<AtomicSDNode>(Node);
    SDLoc SL(Node);
    SDValue CastVal = DAG.getNode(ISD::BITCAST, SL, NVT, AM->getVal());
    assert(NVT.getSizeInBits() == OVT.getSizeInBits() &&
           "unexpected promotion type");
    assert(AM->getMemoryVT().getSizeInBits() == NVT.getSizeInBits() &&
           "unexpected atomic_swap with illegal type");

    SDValue Op0 = AM->getBasePtr();
    SDValue Op1 = CastVal;

    // ATOMIC_STORE uses a swapped operand order from every other AtomicSDNode,
    // but really it should merge with ISD::STORE.
    if (AM->getOpcode() == ISD::ATOMIC_STORE)
      std::swap(Op0, Op1);

    SDValue NewAtomic = DAG.getAtomic(AM->getOpcode(), SL, NVT, AM->getChain(),
                                      Op0, Op1, AM->getMemOperand());

    if (AM->getOpcode() != ISD::ATOMIC_STORE) {
      Results.push_back(DAG.getNode(ISD::BITCAST, SL, OVT, NewAtomic));
      Results.push_back(NewAtomic.getValue(1));
    } else
      Results.push_back(NewAtomic);
    break;
  }
  case ISD::ATOMIC_LOAD: {
    AtomicSDNode *AM = cast<AtomicSDNode>(Node);
    SDLoc SL(Node);
    assert(NVT.getSizeInBits() == OVT.getSizeInBits() &&
           "unexpected promotion type");
    assert(AM->getMemoryVT().getSizeInBits() == NVT.getSizeInBits() &&
           "unexpected atomic_load with illegal type");

    SDValue NewAtomic =
        DAG.getAtomic(ISD::ATOMIC_LOAD, SL, NVT, DAG.getVTList(NVT, MVT::Other),
                      {AM->getChain(), AM->getBasePtr()}, AM->getMemOperand());
    Results.push_back(DAG.getNode(ISD::BITCAST, SL, OVT, NewAtomic));
    Results.push_back(NewAtomic.getValue(1));
    break;
  }
  case ISD::SPLAT_VECTOR: {
    SDValue Scalar = Node->getOperand(0);
    MVT ScalarType = Scalar.getSimpleValueType();
    MVT NewScalarType = NVT.getVectorElementType();
    if (ScalarType.isInteger()) {
      Tmp1 = DAG.getNode(ISD::ANY_EXTEND, dl, NewScalarType, Scalar);
      Tmp2 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1);
      Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp2));
      break;
    }
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NewScalarType, Scalar);
    Tmp2 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1);
    Results.push_back(
        DAG.getNode(ISD::FP_ROUND, dl, OVT, Tmp2,
                    DAG.getIntPtrConstant(0, dl, /*isTarget=*/true)));
    break;
  }
  case ISD::VP_REDUCE_FADD:
  case ISD::VP_REDUCE_FMUL:
  case ISD::VP_REDUCE_FMAX:
  case ISD::VP_REDUCE_FMIN:
  case ISD::VP_REDUCE_FMAXIMUM:
  case ISD::VP_REDUCE_FMINIMUM:
  case ISD::VP_REDUCE_SEQ_FADD:
    Results.push_back(PromoteReduction(Node));
    break;
  }

  // Replace the original node with the legalized result.
  if (!Results.empty()) {
    LLVM_DEBUG(dbgs() << "Successfully promoted node\n");
    ReplaceNode(Node, Results.data());
  } else
    LLVM_DEBUG(dbgs() << "Could not promote node\n");
}

/// This is the entry point for the file.
void SelectionDAG::Legalize() {
  AssignTopologicalOrder();

  SmallPtrSet<SDNode *, 16> LegalizedNodes;
  // Use a delete listener to remove nodes which were deleted during
  // legalization from LegalizeNodes. This is needed to handle the situation
  // where a new node is allocated by the object pool to the same address of a
  // previously deleted node.
  DAGNodeDeletedListener DeleteListener(
      *this,
      [&LegalizedNodes](SDNode *N, SDNode *E) { LegalizedNodes.erase(N); });

  SelectionDAGLegalize Legalizer(*this, LegalizedNodes);

  // Visit all the nodes. We start in topological order, so that we see
  // nodes with their original operands intact. Legalization can produce
  // new nodes which may themselves need to be legalized. Iterate until all
  // nodes have been legalized.
  while (true) {
    bool AnyLegalized = false;
    for (auto NI = allnodes_end(); NI != allnodes_begin();) {
      --NI;

      SDNode *N = &*NI;
      if (N->use_empty() && N != getRoot().getNode()) {
        ++NI;
        DeleteNode(N);
        continue;
      }

      if (LegalizedNodes.insert(N).second) {
        AnyLegalized = true;
        Legalizer.LegalizeOp(N);

        if (N->use_empty() && N != getRoot().getNode()) {
          ++NI;
          DeleteNode(N);
        }
      }
    }
    if (!AnyLegalized)
      break;

  }

  // Remove dead nodes now.
  RemoveDeadNodes();
}

bool SelectionDAG::LegalizeOp(SDNode *N,
                              SmallSetVector<SDNode *, 16> &UpdatedNodes) {
  SmallPtrSet<SDNode *, 16> LegalizedNodes;
  SelectionDAGLegalize Legalizer(*this, LegalizedNodes, &UpdatedNodes);

  // Directly insert the node in question, and legalize it. This will recurse
  // as needed through operands.
  LegalizedNodes.insert(N);
  Legalizer.LegalizeOp(N);

  return LegalizedNodes.count(N);
}
