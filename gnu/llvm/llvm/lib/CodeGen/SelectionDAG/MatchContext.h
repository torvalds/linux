//===---------------- llvm/CodeGen/MatchContext.h  --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the EmptyMatchContext class and VPMatchContext class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_SELECTIONDAG_MATCHCONTEXT_H
#define LLVM_LIB_CODEGEN_SELECTIONDAG_MATCHCONTEXT_H

#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

using namespace llvm;

namespace {
class EmptyMatchContext {
  SelectionDAG &DAG;
  const TargetLowering &TLI;
  SDNode *Root;

public:
  EmptyMatchContext(SelectionDAG &DAG, const TargetLowering &TLI, SDNode *Root)
      : DAG(DAG), TLI(TLI), Root(Root) {}

  unsigned getRootBaseOpcode() { return Root->getOpcode(); }
  bool match(SDValue OpN, unsigned Opcode) const {
    return Opcode == OpN->getOpcode();
  }

  // Same as SelectionDAG::getNode().
  template <typename... ArgT> SDValue getNode(ArgT &&...Args) {
    return DAG.getNode(std::forward<ArgT>(Args)...);
  }

  bool isOperationLegal(unsigned Op, EVT VT) const {
    return TLI.isOperationLegal(Op, VT);
  }

  bool isOperationLegalOrCustom(unsigned Op, EVT VT,
                                bool LegalOnly = false) const {
    return TLI.isOperationLegalOrCustom(Op, VT, LegalOnly);
  }
};

class VPMatchContext {
  SelectionDAG &DAG;
  const TargetLowering &TLI;
  SDValue RootMaskOp;
  SDValue RootVectorLenOp;
  SDNode *Root;

public:
  VPMatchContext(SelectionDAG &DAG, const TargetLowering &TLI, SDNode *_Root)
      : DAG(DAG), TLI(TLI), RootMaskOp(), RootVectorLenOp() {
    Root = _Root;
    assert(Root->isVPOpcode());
    if (auto RootMaskPos = ISD::getVPMaskIdx(Root->getOpcode()))
      RootMaskOp = Root->getOperand(*RootMaskPos);
    else if (Root->getOpcode() == ISD::VP_SELECT)
      RootMaskOp = DAG.getAllOnesConstant(SDLoc(Root),
                                          Root->getOperand(0).getValueType());

    if (auto RootVLenPos = ISD::getVPExplicitVectorLengthIdx(Root->getOpcode()))
      RootVectorLenOp = Root->getOperand(*RootVLenPos);
  }

  unsigned getRootBaseOpcode() {
    std::optional<unsigned> Opcode = ISD::getBaseOpcodeForVP(
        Root->getOpcode(), !Root->getFlags().hasNoFPExcept());
    assert(Opcode.has_value());
    return *Opcode;
  }

  /// whether \p OpVal is a node that is functionally compatible with the
  /// NodeType \p Opc
  bool match(SDValue OpVal, unsigned Opc) const {
    if (!OpVal->isVPOpcode())
      return OpVal->getOpcode() == Opc;

    auto BaseOpc = ISD::getBaseOpcodeForVP(OpVal->getOpcode(),
                                           !OpVal->getFlags().hasNoFPExcept());
    if (BaseOpc != Opc)
      return false;

    // Make sure the mask of OpVal is true mask or is same as Root's.
    unsigned VPOpcode = OpVal->getOpcode();
    if (auto MaskPos = ISD::getVPMaskIdx(VPOpcode)) {
      SDValue MaskOp = OpVal.getOperand(*MaskPos);
      if (RootMaskOp != MaskOp &&
          !ISD::isConstantSplatVectorAllOnes(MaskOp.getNode()))
        return false;
    }

    // Make sure the EVL of OpVal is same as Root's.
    if (auto VLenPos = ISD::getVPExplicitVectorLengthIdx(VPOpcode))
      if (RootVectorLenOp != OpVal.getOperand(*VLenPos))
        return false;
    return true;
  }

  // Specialize based on number of operands.
  // TODO emit VP intrinsics where MaskOp/VectorLenOp != null
  // SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT) { return
  // DAG.getNode(Opcode, DL, VT); }
  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue Operand) {
    unsigned VPOpcode = ISD::getVPForBaseOpcode(Opcode);
    assert(ISD::getVPMaskIdx(VPOpcode) == 1 &&
           ISD::getVPExplicitVectorLengthIdx(VPOpcode) == 2);
    return DAG.getNode(VPOpcode, DL, VT,
                       {Operand, RootMaskOp, RootVectorLenOp});
  }

  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2) {
    unsigned VPOpcode = ISD::getVPForBaseOpcode(Opcode);
    assert(ISD::getVPMaskIdx(VPOpcode) == 2 &&
           ISD::getVPExplicitVectorLengthIdx(VPOpcode) == 3);
    return DAG.getNode(VPOpcode, DL, VT, {N1, N2, RootMaskOp, RootVectorLenOp});
  }

  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDValue N3) {
    unsigned VPOpcode = ISD::getVPForBaseOpcode(Opcode);
    assert(ISD::getVPMaskIdx(VPOpcode) == 3 &&
           ISD::getVPExplicitVectorLengthIdx(VPOpcode) == 4);
    return DAG.getNode(VPOpcode, DL, VT,
                       {N1, N2, N3, RootMaskOp, RootVectorLenOp});
  }

  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue Operand,
                  SDNodeFlags Flags) {
    unsigned VPOpcode = ISD::getVPForBaseOpcode(Opcode);
    assert(ISD::getVPMaskIdx(VPOpcode) == 1 &&
           ISD::getVPExplicitVectorLengthIdx(VPOpcode) == 2);
    return DAG.getNode(VPOpcode, DL, VT, {Operand, RootMaskOp, RootVectorLenOp},
                       Flags);
  }

  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDNodeFlags Flags) {
    unsigned VPOpcode = ISD::getVPForBaseOpcode(Opcode);
    assert(ISD::getVPMaskIdx(VPOpcode) == 2 &&
           ISD::getVPExplicitVectorLengthIdx(VPOpcode) == 3);
    return DAG.getNode(VPOpcode, DL, VT, {N1, N2, RootMaskOp, RootVectorLenOp},
                       Flags);
  }

  SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                  SDValue N2, SDValue N3, SDNodeFlags Flags) {
    unsigned VPOpcode = ISD::getVPForBaseOpcode(Opcode);
    assert(ISD::getVPMaskIdx(VPOpcode) == 3 &&
           ISD::getVPExplicitVectorLengthIdx(VPOpcode) == 4);
    return DAG.getNode(VPOpcode, DL, VT,
                       {N1, N2, N3, RootMaskOp, RootVectorLenOp}, Flags);
  }

  bool isOperationLegal(unsigned Op, EVT VT) const {
    unsigned VPOp = ISD::getVPForBaseOpcode(Op);
    return TLI.isOperationLegal(VPOp, VT);
  }

  bool isOperationLegalOrCustom(unsigned Op, EVT VT,
                                bool LegalOnly = false) const {
    unsigned VPOp = ISD::getVPForBaseOpcode(Op);
    return TLI.isOperationLegalOrCustom(VPOp, VT, LegalOnly);
  }
};
} // end anonymous namespace
#endif
