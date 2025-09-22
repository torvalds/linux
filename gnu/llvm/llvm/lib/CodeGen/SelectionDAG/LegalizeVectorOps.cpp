//===- LegalizeVectorOps.cpp - Implement SelectionDAG::LegalizeVectors ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SelectionDAG::LegalizeVectors method.
//
// The vector legalizer looks for vector operations which might need to be
// scalarized and legalizes them. This is a separate step from Legalize because
// scalarizing can introduce illegal types.  For example, suppose we have an
// ISD::SDIV of type v2i64 on x86-32.  The type is legal (for example, addition
// on a v2i64 is legal), but ISD::SDIV isn't legal, so we have to unroll the
// operation, which introduces nodes with the illegal type i64 which must be
// expanded.  Similarly, suppose we have an ISD::SRA of type v16i8 on PowerPC;
// the operation must be unrolled, which introduces nodes with the illegal
// type i8 which must be promoted.
//
// This does not legalize vector manipulations like ISD::BUILD_VECTOR,
// or operations that happen to take a vector which are custom-lowered;
// the legalization for such operations never produces nodes
// with illegal types, so it's okay to put off legalizing them until
// SelectionDAG::Legalize runs.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "legalizevectorops"

namespace {

class VectorLegalizer {
  SelectionDAG& DAG;
  const TargetLowering &TLI;
  bool Changed = false; // Keep track of whether anything changed

  /// For nodes that are of legal width, and that have more than one use, this
  /// map indicates what regularized operand to use.  This allows us to avoid
  /// legalizing the same thing more than once.
  SmallDenseMap<SDValue, SDValue, 64> LegalizedNodes;

  /// Adds a node to the translation cache.
  void AddLegalizedOperand(SDValue From, SDValue To) {
    LegalizedNodes.insert(std::make_pair(From, To));
    // If someone requests legalization of the new node, return itself.
    if (From != To)
      LegalizedNodes.insert(std::make_pair(To, To));
  }

  /// Legalizes the given node.
  SDValue LegalizeOp(SDValue Op);

  /// Assuming the node is legal, "legalize" the results.
  SDValue TranslateLegalizeResults(SDValue Op, SDNode *Result);

  /// Make sure Results are legal and update the translation cache.
  SDValue RecursivelyLegalizeResults(SDValue Op,
                                     MutableArrayRef<SDValue> Results);

  /// Wrapper to interface LowerOperation with a vector of Results.
  /// Returns false if the target wants to use default expansion. Otherwise
  /// returns true. If return is true and the Results are empty, then the
  /// target wants to keep the input node as is.
  bool LowerOperationWrapper(SDNode *N, SmallVectorImpl<SDValue> &Results);

  /// Implements unrolling a VSETCC.
  SDValue UnrollVSETCC(SDNode *Node);

  /// Implement expand-based legalization of vector operations.
  ///
  /// This is just a high-level routine to dispatch to specific code paths for
  /// operations to legalize them.
  void Expand(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  /// Implements expansion for FP_TO_UINT; falls back to UnrollVectorOp if
  /// FP_TO_SINT isn't legal.
  void ExpandFP_TO_UINT(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  /// Implements expansion for UINT_TO_FLOAT; falls back to UnrollVectorOp if
  /// SINT_TO_FLOAT and SHR on vectors isn't legal.
  void ExpandUINT_TO_FLOAT(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  /// Implement expansion for SIGN_EXTEND_INREG using SRL and SRA.
  SDValue ExpandSEXTINREG(SDNode *Node);

  /// Implement expansion for ANY_EXTEND_VECTOR_INREG.
  ///
  /// Shuffles the low lanes of the operand into place and bitcasts to the proper
  /// type. The contents of the bits in the extended part of each element are
  /// undef.
  SDValue ExpandANY_EXTEND_VECTOR_INREG(SDNode *Node);

  /// Implement expansion for SIGN_EXTEND_VECTOR_INREG.
  ///
  /// Shuffles the low lanes of the operand into place, bitcasts to the proper
  /// type, then shifts left and arithmetic shifts right to introduce a sign
  /// extension.
  SDValue ExpandSIGN_EXTEND_VECTOR_INREG(SDNode *Node);

  /// Implement expansion for ZERO_EXTEND_VECTOR_INREG.
  ///
  /// Shuffles the low lanes of the operand into place and blends zeros into
  /// the remaining lanes, finally bitcasting to the proper type.
  SDValue ExpandZERO_EXTEND_VECTOR_INREG(SDNode *Node);

  /// Expand bswap of vectors into a shuffle if legal.
  SDValue ExpandBSWAP(SDNode *Node);

  /// Implement vselect in terms of XOR, AND, OR when blend is not
  /// supported by the target.
  SDValue ExpandVSELECT(SDNode *Node);
  SDValue ExpandVP_SELECT(SDNode *Node);
  SDValue ExpandVP_MERGE(SDNode *Node);
  SDValue ExpandVP_REM(SDNode *Node);
  SDValue ExpandSELECT(SDNode *Node);
  std::pair<SDValue, SDValue> ExpandLoad(SDNode *N);
  SDValue ExpandStore(SDNode *N);
  SDValue ExpandFNEG(SDNode *Node);
  void ExpandFSUB(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandSETCC(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandBITREVERSE(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandUADDSUBO(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandSADDSUBO(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandMULO(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandFixedPointDiv(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandStrictFPOp(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandREM(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  bool tryExpandVecMathCall(SDNode *Node, RTLIB::Libcall LC,
                            SmallVectorImpl<SDValue> &Results);
  bool tryExpandVecMathCall(SDNode *Node, RTLIB::Libcall Call_F32,
                            RTLIB::Libcall Call_F64, RTLIB::Libcall Call_F80,
                            RTLIB::Libcall Call_F128,
                            RTLIB::Libcall Call_PPCF128,
                            SmallVectorImpl<SDValue> &Results);

  void UnrollStrictFPOp(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  /// Implements vector promotion.
  ///
  /// This is essentially just bitcasting the operands to a different type and
  /// bitcasting the result back to the original type.
  void Promote(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  /// Implements [SU]INT_TO_FP vector promotion.
  ///
  /// This is a [zs]ext of the input operand to a larger integer type.
  void PromoteINT_TO_FP(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  /// Implements FP_TO_[SU]INT vector promotion of the result type.
  ///
  /// It is promoted to a larger integer type.  The result is then
  /// truncated back to the original type.
  void PromoteFP_TO_INT(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  /// Implements vector setcc operation promotion.
  ///
  /// All vector operands are promoted to a vector type with larger element
  /// type.
  void PromoteSETCC(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  void PromoteSTRICT(SDNode *Node, SmallVectorImpl<SDValue> &Results);

public:
  VectorLegalizer(SelectionDAG& dag) :
      DAG(dag), TLI(dag.getTargetLoweringInfo()) {}

  /// Begin legalizer the vector operations in the DAG.
  bool Run();
};

} // end anonymous namespace

bool VectorLegalizer::Run() {
  // Before we start legalizing vector nodes, check if there are any vectors.
  bool HasVectors = false;
  for (SelectionDAG::allnodes_iterator I = DAG.allnodes_begin(),
       E = std::prev(DAG.allnodes_end()); I != std::next(E); ++I) {
    // Check if the values of the nodes contain vectors. We don't need to check
    // the operands because we are going to check their values at some point.
    HasVectors = llvm::any_of(I->values(), [](EVT T) { return T.isVector(); });

    // If we found a vector node we can start the legalization.
    if (HasVectors)
      break;
  }

  // If this basic block has no vectors then no need to legalize vectors.
  if (!HasVectors)
    return false;

  // The legalize process is inherently a bottom-up recursive process (users
  // legalize their uses before themselves).  Given infinite stack space, we
  // could just start legalizing on the root and traverse the whole graph.  In
  // practice however, this causes us to run out of stack space on large basic
  // blocks.  To avoid this problem, compute an ordering of the nodes where each
  // node is only legalized after all of its operands are legalized.
  DAG.AssignTopologicalOrder();
  for (SelectionDAG::allnodes_iterator I = DAG.allnodes_begin(),
       E = std::prev(DAG.allnodes_end()); I != std::next(E); ++I)
    LegalizeOp(SDValue(&*I, 0));

  // Finally, it's possible the root changed.  Get the new root.
  SDValue OldRoot = DAG.getRoot();
  assert(LegalizedNodes.count(OldRoot) && "Root didn't get legalized?");
  DAG.setRoot(LegalizedNodes[OldRoot]);

  LegalizedNodes.clear();

  // Remove dead nodes now.
  DAG.RemoveDeadNodes();

  return Changed;
}

SDValue VectorLegalizer::TranslateLegalizeResults(SDValue Op, SDNode *Result) {
  assert(Op->getNumValues() == Result->getNumValues() &&
         "Unexpected number of results");
  // Generic legalization: just pass the operand through.
  for (unsigned i = 0, e = Op->getNumValues(); i != e; ++i)
    AddLegalizedOperand(Op.getValue(i), SDValue(Result, i));
  return SDValue(Result, Op.getResNo());
}

SDValue
VectorLegalizer::RecursivelyLegalizeResults(SDValue Op,
                                            MutableArrayRef<SDValue> Results) {
  assert(Results.size() == Op->getNumValues() &&
         "Unexpected number of results");
  // Make sure that the generated code is itself legal.
  for (unsigned i = 0, e = Results.size(); i != e; ++i) {
    Results[i] = LegalizeOp(Results[i]);
    AddLegalizedOperand(Op.getValue(i), Results[i]);
  }

  return Results[Op.getResNo()];
}

SDValue VectorLegalizer::LegalizeOp(SDValue Op) {
  // Note that LegalizeOp may be reentered even from single-use nodes, which
  // means that we always must cache transformed nodes.
  DenseMap<SDValue, SDValue>::iterator I = LegalizedNodes.find(Op);
  if (I != LegalizedNodes.end()) return I->second;

  // Legalize the operands
  SmallVector<SDValue, 8> Ops;
  for (const SDValue &Oper : Op->op_values())
    Ops.push_back(LegalizeOp(Oper));

  SDNode *Node = DAG.UpdateNodeOperands(Op.getNode(), Ops);

  bool HasVectorValueOrOp =
      llvm::any_of(Node->values(), [](EVT T) { return T.isVector(); }) ||
      llvm::any_of(Node->op_values(),
                   [](SDValue O) { return O.getValueType().isVector(); });
  if (!HasVectorValueOrOp)
    return TranslateLegalizeResults(Op, Node);

  TargetLowering::LegalizeAction Action = TargetLowering::Legal;
  EVT ValVT;
  switch (Op.getOpcode()) {
  default:
    return TranslateLegalizeResults(Op, Node);
  case ISD::LOAD: {
    LoadSDNode *LD = cast<LoadSDNode>(Node);
    ISD::LoadExtType ExtType = LD->getExtensionType();
    EVT LoadedVT = LD->getMemoryVT();
    if (LoadedVT.isVector() && ExtType != ISD::NON_EXTLOAD)
      Action = TLI.getLoadExtAction(ExtType, LD->getValueType(0), LoadedVT);
    break;
  }
  case ISD::STORE: {
    StoreSDNode *ST = cast<StoreSDNode>(Node);
    EVT StVT = ST->getMemoryVT();
    MVT ValVT = ST->getValue().getSimpleValueType();
    if (StVT.isVector() && ST->isTruncatingStore())
      Action = TLI.getTruncStoreAction(ValVT, StVT);
    break;
  }
  case ISD::MERGE_VALUES:
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    // This operation lies about being legal: when it claims to be legal,
    // it should actually be expanded.
    if (Action == TargetLowering::Legal)
      Action = TargetLowering::Expand;
    break;
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  case ISD::STRICT_##DAGN:
#include "llvm/IR/ConstrainedOps.def"
    ValVT = Node->getValueType(0);
    if (Op.getOpcode() == ISD::STRICT_SINT_TO_FP ||
        Op.getOpcode() == ISD::STRICT_UINT_TO_FP)
      ValVT = Node->getOperand(1).getValueType();
    if (Op.getOpcode() == ISD::STRICT_FSETCC ||
        Op.getOpcode() == ISD::STRICT_FSETCCS) {
      MVT OpVT = Node->getOperand(1).getSimpleValueType();
      ISD::CondCode CCCode = cast<CondCodeSDNode>(Node->getOperand(3))->get();
      Action = TLI.getCondCodeAction(CCCode, OpVT);
      if (Action == TargetLowering::Legal)
        Action = TLI.getOperationAction(Node->getOpcode(), OpVT);
    } else {
      Action = TLI.getOperationAction(Node->getOpcode(), ValVT);
    }
    // If we're asked to expand a strict vector floating-point operation,
    // by default we're going to simply unroll it.  That is usually the
    // best approach, except in the case where the resulting strict (scalar)
    // operations would themselves use the fallback mutation to non-strict.
    // In that specific case, just do the fallback on the vector op.
    if (Action == TargetLowering::Expand && !TLI.isStrictFPEnabled() &&
        TLI.getStrictFPOperationAction(Node->getOpcode(), ValVT) ==
            TargetLowering::Legal) {
      EVT EltVT = ValVT.getVectorElementType();
      if (TLI.getOperationAction(Node->getOpcode(), EltVT)
          == TargetLowering::Expand &&
          TLI.getStrictFPOperationAction(Node->getOpcode(), EltVT)
          == TargetLowering::Legal)
        Action = TargetLowering::Legal;
    }
    break;
  case ISD::ADD:
  case ISD::SUB:
  case ISD::MUL:
  case ISD::MULHS:
  case ISD::MULHU:
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::SREM:
  case ISD::UREM:
  case ISD::SDIVREM:
  case ISD::UDIVREM:
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FDIV:
  case ISD::FREM:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
  case ISD::FSHL:
  case ISD::FSHR:
  case ISD::ROTL:
  case ISD::ROTR:
  case ISD::ABS:
  case ISD::ABDS:
  case ISD::ABDU:
  case ISD::AVGCEILS:
  case ISD::AVGCEILU:
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU:
  case ISD::BSWAP:
  case ISD::BITREVERSE:
  case ISD::CTLZ:
  case ISD::CTTZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTPOP:
  case ISD::SELECT:
  case ISD::VSELECT:
  case ISD::SELECT_CC:
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
  case ISD::TRUNCATE:
  case ISD::SIGN_EXTEND:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::FNEG:
  case ISD::FABS:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINNUM_IEEE:
  case ISD::FMAXNUM_IEEE:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::FCOPYSIGN:
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
  case ISD::FLDEXP:
  case ISD::FPOWI:
  case ISD::FPOW:
  case ISD::FLOG:
  case ISD::FLOG2:
  case ISD::FLOG10:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FEXP10:
  case ISD::FCEIL:
  case ISD::FTRUNC:
  case ISD::FRINT:
  case ISD::FNEARBYINT:
  case ISD::FROUND:
  case ISD::FROUNDEVEN:
  case ISD::FFLOOR:
  case ISD::FP_ROUND:
  case ISD::FP_EXTEND:
  case ISD::FPTRUNC_ROUND:
  case ISD::FMA:
  case ISD::SIGN_EXTEND_INREG:
  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:
  case ISD::SMUL_LOHI:
  case ISD::UMUL_LOHI:
  case ISD::SADDO:
  case ISD::UADDO:
  case ISD::SSUBO:
  case ISD::USUBO:
  case ISD::SMULO:
  case ISD::UMULO:
  case ISD::FCANONICALIZE:
  case ISD::FFREXP:
  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT:
  case ISD::SSHLSAT:
  case ISD::USHLSAT:
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
  case ISD::MGATHER:
  case ISD::VECTOR_COMPRESS:
  case ISD::SCMP:
  case ISD::UCMP:
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
  case ISD::LRINT:
  case ISD::LLRINT:
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_FMUL:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMINIMUM:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(0).getValueType());
    break;
  case ISD::VECREDUCE_SEQ_FADD:
  case ISD::VECREDUCE_SEQ_FMUL:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(1).getValueType());
    break;
  case ISD::SETCC: {
    MVT OpVT = Node->getOperand(0).getSimpleValueType();
    ISD::CondCode CCCode = cast<CondCodeSDNode>(Node->getOperand(2))->get();
    Action = TLI.getCondCodeAction(CCCode, OpVT);
    if (Action == TargetLowering::Legal)
      Action = TLI.getOperationAction(Node->getOpcode(), OpVT);
    break;
  }

#define BEGIN_REGISTER_VP_SDNODE(VPID, LEGALPOS, ...)                          \
  case ISD::VPID: {                                                            \
    EVT LegalizeVT = LEGALPOS < 0 ? Node->getValueType(-(1 + LEGALPOS))        \
                                  : Node->getOperand(LEGALPOS).getValueType(); \
    if (ISD::VPID == ISD::VP_SETCC) {                                          \
      ISD::CondCode CCCode = cast<CondCodeSDNode>(Node->getOperand(2))->get(); \
      Action = TLI.getCondCodeAction(CCCode, LegalizeVT.getSimpleVT());        \
      if (Action != TargetLowering::Legal)                                     \
        break;                                                                 \
    }                                                                          \
    /* Defer non-vector results to LegalizeDAG. */                             \
    if (!Node->getValueType(0).isVector() &&                                   \
        Node->getValueType(0) != MVT::Other) {                                 \
      Action = TargetLowering::Legal;                                          \
      break;                                                                   \
    }                                                                          \
    Action = TLI.getOperationAction(Node->getOpcode(), LegalizeVT);            \
  } break;
#include "llvm/IR/VPIntrinsics.def"
  }

  LLVM_DEBUG(dbgs() << "\nLegalizing vector op: "; Node->dump(&DAG));

  SmallVector<SDValue, 8> ResultVals;
  switch (Action) {
  default: llvm_unreachable("This action is not supported yet!");
  case TargetLowering::Promote:
    assert((Op.getOpcode() != ISD::LOAD && Op.getOpcode() != ISD::STORE) &&
           "This action is not supported yet!");
    LLVM_DEBUG(dbgs() << "Promoting\n");
    Promote(Node, ResultVals);
    assert(!ResultVals.empty() && "No results for promotion?");
    break;
  case TargetLowering::Legal:
    LLVM_DEBUG(dbgs() << "Legal node: nothing to do\n");
    break;
  case TargetLowering::Custom:
    LLVM_DEBUG(dbgs() << "Trying custom legalization\n");
    if (LowerOperationWrapper(Node, ResultVals))
      break;
    LLVM_DEBUG(dbgs() << "Could not custom legalize node\n");
    [[fallthrough]];
  case TargetLowering::Expand:
    LLVM_DEBUG(dbgs() << "Expanding\n");
    Expand(Node, ResultVals);
    break;
  }

  if (ResultVals.empty())
    return TranslateLegalizeResults(Op, Node);

  Changed = true;
  return RecursivelyLegalizeResults(Op, ResultVals);
}

// FIXME: This is very similar to TargetLowering::LowerOperationWrapper. Can we
// merge them somehow?
bool VectorLegalizer::LowerOperationWrapper(SDNode *Node,
                                            SmallVectorImpl<SDValue> &Results) {
  SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG);

  if (!Res.getNode())
    return false;

  if (Res == SDValue(Node, 0))
    return true;

  // If the original node has one result, take the return value from
  // LowerOperation as is. It might not be result number 0.
  if (Node->getNumValues() == 1) {
    Results.push_back(Res);
    return true;
  }

  // If the original node has multiple results, then the return node should
  // have the same number of results.
  assert((Node->getNumValues() == Res->getNumValues()) &&
         "Lowering returned the wrong number of results!");

  // Places new result values base on N result number.
  for (unsigned I = 0, E = Node->getNumValues(); I != E; ++I)
    Results.push_back(Res.getValue(I));

  return true;
}

void VectorLegalizer::PromoteSETCC(SDNode *Node,
                                   SmallVectorImpl<SDValue> &Results) {
  MVT VecVT = Node->getOperand(0).getSimpleValueType();
  MVT NewVecVT = TLI.getTypeToPromoteTo(Node->getOpcode(), VecVT);

  unsigned ExtOp = VecVT.isFloatingPoint() ? ISD::FP_EXTEND : ISD::ANY_EXTEND;

  SDLoc DL(Node);
  SmallVector<SDValue, 5> Operands(Node->getNumOperands());

  Operands[0] = DAG.getNode(ExtOp, DL, NewVecVT, Node->getOperand(0));
  Operands[1] = DAG.getNode(ExtOp, DL, NewVecVT, Node->getOperand(1));
  Operands[2] = Node->getOperand(2);

  if (Node->getOpcode() == ISD::VP_SETCC) {
    Operands[3] = Node->getOperand(3); // mask
    Operands[4] = Node->getOperand(4); // evl
  }

  SDValue Res = DAG.getNode(Node->getOpcode(), DL, Node->getSimpleValueType(0),
                            Operands, Node->getFlags());

  Results.push_back(Res);
}

void VectorLegalizer::PromoteSTRICT(SDNode *Node,
                                    SmallVectorImpl<SDValue> &Results) {
  MVT VecVT = Node->getOperand(1).getSimpleValueType();
  MVT NewVecVT = TLI.getTypeToPromoteTo(Node->getOpcode(), VecVT);

  assert(VecVT.isFloatingPoint());

  SDLoc DL(Node);
  SmallVector<SDValue, 5> Operands(Node->getNumOperands());
  SmallVector<SDValue, 2> Chains;

  for (unsigned j = 1; j != Node->getNumOperands(); ++j)
    if (Node->getOperand(j).getValueType().isVector() &&
        !(ISD::isVPOpcode(Node->getOpcode()) &&
          ISD::getVPMaskIdx(Node->getOpcode()) == j)) // Skip mask operand.
    {
      // promote the vector operand.
      SDValue Ext =
          DAG.getNode(ISD::STRICT_FP_EXTEND, DL, {NewVecVT, MVT::Other},
                      {Node->getOperand(0), Node->getOperand(j)});
      Operands[j] = Ext.getValue(0);
      Chains.push_back(Ext.getValue(1));
    } else
      Operands[j] = Node->getOperand(j); // Skip no vector operand.

  SDVTList VTs = DAG.getVTList(NewVecVT, Node->getValueType(1));

  Operands[0] = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);

  SDValue Res =
      DAG.getNode(Node->getOpcode(), DL, VTs, Operands, Node->getFlags());

  SDValue Round =
      DAG.getNode(ISD::STRICT_FP_ROUND, DL, {VecVT, MVT::Other},
                  {Res.getValue(1), Res.getValue(0),
                   DAG.getIntPtrConstant(0, DL, /*isTarget=*/true)});

  Results.push_back(Round.getValue(0));
  Results.push_back(Round.getValue(1));
}

void VectorLegalizer::Promote(SDNode *Node, SmallVectorImpl<SDValue> &Results) {
  // For a few operations there is a specific concept for promotion based on
  // the operand's type.
  switch (Node->getOpcode()) {
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::STRICT_SINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
    // "Promote" the operation by extending the operand.
    PromoteINT_TO_FP(Node, Results);
    return;
  case ISD::FP_TO_UINT:
  case ISD::FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::STRICT_FP_TO_SINT:
    // Promote the operation by extending the operand.
    PromoteFP_TO_INT(Node, Results);
    return;
  case ISD::VP_SETCC:
  case ISD::SETCC:
    // Promote the operation by extending the operand.
    PromoteSETCC(Node, Results);
    return;
  case ISD::STRICT_FADD:
  case ISD::STRICT_FSUB:
  case ISD::STRICT_FMUL:
  case ISD::STRICT_FDIV:
  case ISD::STRICT_FSQRT:
  case ISD::STRICT_FMA:
    PromoteSTRICT(Node, Results);
    return;
  case ISD::FP_ROUND:
  case ISD::FP_EXTEND:
    // These operations are used to do promotion so they can't be promoted
    // themselves.
    llvm_unreachable("Don't know how to promote this operation!");
  }

  // There are currently two cases of vector promotion:
  // 1) Bitcasting a vector of integers to a different type to a vector of the
  //    same overall length. For example, x86 promotes ISD::AND v2i32 to v1i64.
  // 2) Extending a vector of floats to a vector of the same number of larger
  //    floats. For example, AArch64 promotes ISD::FADD on v4f16 to v4f32.
  assert(Node->getNumValues() == 1 &&
         "Can't promote a vector with multiple results!");
  MVT VT = Node->getSimpleValueType(0);
  MVT NVT = TLI.getTypeToPromoteTo(Node->getOpcode(), VT);
  SDLoc dl(Node);
  SmallVector<SDValue, 4> Operands(Node->getNumOperands());

  for (unsigned j = 0; j != Node->getNumOperands(); ++j) {
    // Do not promote the mask operand of a VP OP.
    bool SkipPromote = ISD::isVPOpcode(Node->getOpcode()) &&
                       ISD::getVPMaskIdx(Node->getOpcode()) == j;
    if (Node->getOperand(j).getValueType().isVector() && !SkipPromote)
      if (Node->getOperand(j)
              .getValueType()
              .getVectorElementType()
              .isFloatingPoint() &&
          NVT.isVector() && NVT.getVectorElementType().isFloatingPoint())
        Operands[j] = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(j));
      else
        Operands[j] = DAG.getNode(ISD::BITCAST, dl, NVT, Node->getOperand(j));
    else
      Operands[j] = Node->getOperand(j);
  }

  SDValue Res =
      DAG.getNode(Node->getOpcode(), dl, NVT, Operands, Node->getFlags());

  if ((VT.isFloatingPoint() && NVT.isFloatingPoint()) ||
      (VT.isVector() && VT.getVectorElementType().isFloatingPoint() &&
       NVT.isVector() && NVT.getVectorElementType().isFloatingPoint()))
    Res = DAG.getNode(ISD::FP_ROUND, dl, VT, Res,
                      DAG.getIntPtrConstant(0, dl, /*isTarget=*/true));
  else
    Res = DAG.getNode(ISD::BITCAST, dl, VT, Res);

  Results.push_back(Res);
}

void VectorLegalizer::PromoteINT_TO_FP(SDNode *Node,
                                       SmallVectorImpl<SDValue> &Results) {
  // INT_TO_FP operations may require the input operand be promoted even
  // when the type is otherwise legal.
  bool IsStrict = Node->isStrictFPOpcode();
  MVT VT = Node->getOperand(IsStrict ? 1 : 0).getSimpleValueType();
  MVT NVT = TLI.getTypeToPromoteTo(Node->getOpcode(), VT);
  assert(NVT.getVectorNumElements() == VT.getVectorNumElements() &&
         "Vectors have different number of elements!");

  SDLoc dl(Node);
  SmallVector<SDValue, 4> Operands(Node->getNumOperands());

  unsigned Opc = (Node->getOpcode() == ISD::UINT_TO_FP ||
                  Node->getOpcode() == ISD::STRICT_UINT_TO_FP)
                     ? ISD::ZERO_EXTEND
                     : ISD::SIGN_EXTEND;
  for (unsigned j = 0; j != Node->getNumOperands(); ++j) {
    if (Node->getOperand(j).getValueType().isVector())
      Operands[j] = DAG.getNode(Opc, dl, NVT, Node->getOperand(j));
    else
      Operands[j] = Node->getOperand(j);
  }

  if (IsStrict) {
    SDValue Res = DAG.getNode(Node->getOpcode(), dl,
                              {Node->getValueType(0), MVT::Other}, Operands);
    Results.push_back(Res);
    Results.push_back(Res.getValue(1));
    return;
  }

  SDValue Res =
      DAG.getNode(Node->getOpcode(), dl, Node->getValueType(0), Operands);
  Results.push_back(Res);
}

// For FP_TO_INT we promote the result type to a vector type with wider
// elements and then truncate the result.  This is different from the default
// PromoteVector which uses bitcast to promote thus assumning that the
// promoted vector type has the same overall size.
void VectorLegalizer::PromoteFP_TO_INT(SDNode *Node,
                                       SmallVectorImpl<SDValue> &Results) {
  MVT VT = Node->getSimpleValueType(0);
  MVT NVT = TLI.getTypeToPromoteTo(Node->getOpcode(), VT);
  bool IsStrict = Node->isStrictFPOpcode();
  assert(NVT.getVectorNumElements() == VT.getVectorNumElements() &&
         "Vectors have different number of elements!");

  unsigned NewOpc = Node->getOpcode();
  // Change FP_TO_UINT to FP_TO_SINT if possible.
  // TODO: Should we only do this if FP_TO_UINT itself isn't legal?
  if (NewOpc == ISD::FP_TO_UINT &&
      TLI.isOperationLegalOrCustom(ISD::FP_TO_SINT, NVT))
    NewOpc = ISD::FP_TO_SINT;

  if (NewOpc == ISD::STRICT_FP_TO_UINT &&
      TLI.isOperationLegalOrCustom(ISD::STRICT_FP_TO_SINT, NVT))
    NewOpc = ISD::STRICT_FP_TO_SINT;

  SDLoc dl(Node);
  SDValue Promoted, Chain;
  if (IsStrict) {
    Promoted = DAG.getNode(NewOpc, dl, {NVT, MVT::Other},
                           {Node->getOperand(0), Node->getOperand(1)});
    Chain = Promoted.getValue(1);
  } else
    Promoted = DAG.getNode(NewOpc, dl, NVT, Node->getOperand(0));

  // Assert that the converted value fits in the original type.  If it doesn't
  // (eg: because the value being converted is too big), then the result of the
  // original operation was undefined anyway, so the assert is still correct.
  if (Node->getOpcode() == ISD::FP_TO_UINT ||
      Node->getOpcode() == ISD::STRICT_FP_TO_UINT)
    NewOpc = ISD::AssertZext;
  else
    NewOpc = ISD::AssertSext;

  Promoted = DAG.getNode(NewOpc, dl, NVT, Promoted,
                         DAG.getValueType(VT.getScalarType()));
  Promoted = DAG.getNode(ISD::TRUNCATE, dl, VT, Promoted);
  Results.push_back(Promoted);
  if (IsStrict)
    Results.push_back(Chain);
}

std::pair<SDValue, SDValue> VectorLegalizer::ExpandLoad(SDNode *N) {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  return TLI.scalarizeVectorLoad(LD, DAG);
}

SDValue VectorLegalizer::ExpandStore(SDNode *N) {
  StoreSDNode *ST = cast<StoreSDNode>(N);
  SDValue TF = TLI.scalarizeVectorStore(ST, DAG);
  return TF;
}

void VectorLegalizer::Expand(SDNode *Node, SmallVectorImpl<SDValue> &Results) {
  switch (Node->getOpcode()) {
  case ISD::LOAD: {
    std::pair<SDValue, SDValue> Tmp = ExpandLoad(Node);
    Results.push_back(Tmp.first);
    Results.push_back(Tmp.second);
    return;
  }
  case ISD::STORE:
    Results.push_back(ExpandStore(Node));
    return;
  case ISD::MERGE_VALUES:
    for (unsigned i = 0, e = Node->getNumValues(); i != e; ++i)
      Results.push_back(Node->getOperand(i));
    return;
  case ISD::SIGN_EXTEND_INREG:
    Results.push_back(ExpandSEXTINREG(Node));
    return;
  case ISD::ANY_EXTEND_VECTOR_INREG:
    Results.push_back(ExpandANY_EXTEND_VECTOR_INREG(Node));
    return;
  case ISD::SIGN_EXTEND_VECTOR_INREG:
    Results.push_back(ExpandSIGN_EXTEND_VECTOR_INREG(Node));
    return;
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    Results.push_back(ExpandZERO_EXTEND_VECTOR_INREG(Node));
    return;
  case ISD::BSWAP:
    Results.push_back(ExpandBSWAP(Node));
    return;
  case ISD::VP_BSWAP:
    Results.push_back(TLI.expandVPBSWAP(Node, DAG));
    return;
  case ISD::VSELECT:
    Results.push_back(ExpandVSELECT(Node));
    return;
  case ISD::VP_SELECT:
    Results.push_back(ExpandVP_SELECT(Node));
    return;
  case ISD::VP_SREM:
  case ISD::VP_UREM:
    if (SDValue Expanded = ExpandVP_REM(Node)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::SELECT:
    Results.push_back(ExpandSELECT(Node));
    return;
  case ISD::SELECT_CC: {
    if (Node->getValueType(0).isScalableVector()) {
      EVT CondVT = TLI.getSetCCResultType(
          DAG.getDataLayout(), *DAG.getContext(), Node->getValueType(0));
      SDValue SetCC =
          DAG.getNode(ISD::SETCC, SDLoc(Node), CondVT, Node->getOperand(0),
                      Node->getOperand(1), Node->getOperand(4));
      Results.push_back(DAG.getSelect(SDLoc(Node), Node->getValueType(0), SetCC,
                                      Node->getOperand(2),
                                      Node->getOperand(3)));
      return;
    }
    break;
  }
  case ISD::FP_TO_UINT:
    ExpandFP_TO_UINT(Node, Results);
    return;
  case ISD::UINT_TO_FP:
    ExpandUINT_TO_FLOAT(Node, Results);
    return;
  case ISD::FNEG:
    Results.push_back(ExpandFNEG(Node));
    return;
  case ISD::FSUB:
    ExpandFSUB(Node, Results);
    return;
  case ISD::SETCC:
  case ISD::VP_SETCC:
    ExpandSETCC(Node, Results);
    return;
  case ISD::ABS:
    if (SDValue Expanded = TLI.expandABS(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::ABDS:
  case ISD::ABDU:
    if (SDValue Expanded = TLI.expandABD(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::AVGCEILS:
  case ISD::AVGCEILU:
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU:
    if (SDValue Expanded = TLI.expandAVG(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::BITREVERSE:
    ExpandBITREVERSE(Node, Results);
    return;
  case ISD::VP_BITREVERSE:
    if (SDValue Expanded = TLI.expandVPBITREVERSE(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::CTPOP:
    if (SDValue Expanded = TLI.expandCTPOP(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::VP_CTPOP:
    if (SDValue Expanded = TLI.expandVPCTPOP(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
    if (SDValue Expanded = TLI.expandCTLZ(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::VP_CTLZ:
  case ISD::VP_CTLZ_ZERO_UNDEF:
    if (SDValue Expanded = TLI.expandVPCTLZ(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
    if (SDValue Expanded = TLI.expandCTTZ(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::VP_CTTZ:
  case ISD::VP_CTTZ_ZERO_UNDEF:
    if (SDValue Expanded = TLI.expandVPCTTZ(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::FSHL:
  case ISD::VP_FSHL:
  case ISD::FSHR:
  case ISD::VP_FSHR:
    if (SDValue Expanded = TLI.expandFunnelShift(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::ROTL:
  case ISD::ROTR:
    if (SDValue Expanded = TLI.expandROT(Node, false /*AllowVectorOps*/, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
    if (SDValue Expanded = TLI.expandFMINNUM_FMAXNUM(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
    Results.push_back(TLI.expandFMINIMUM_FMAXIMUM(Node, DAG));
    return;
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:
    if (SDValue Expanded = TLI.expandIntMINMAX(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::UADDO:
  case ISD::USUBO:
    ExpandUADDSUBO(Node, Results);
    return;
  case ISD::SADDO:
  case ISD::SSUBO:
    ExpandSADDSUBO(Node, Results);
    return;
  case ISD::UMULO:
  case ISD::SMULO:
    ExpandMULO(Node, Results);
    return;
  case ISD::USUBSAT:
  case ISD::SSUBSAT:
  case ISD::UADDSAT:
  case ISD::SADDSAT:
    if (SDValue Expanded = TLI.expandAddSubSat(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::USHLSAT:
  case ISD::SSHLSAT:
    if (SDValue Expanded = TLI.expandShlSat(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
    // Expand the fpsosisat if it is scalable to prevent it from unrolling below.
    if (Node->getValueType(0).isScalableVector()) {
      if (SDValue Expanded = TLI.expandFP_TO_INT_SAT(Node, DAG)) {
        Results.push_back(Expanded);
        return;
      }
    }
    break;
  case ISD::SMULFIX:
  case ISD::UMULFIX:
    if (SDValue Expanded = TLI.expandFixedPointMul(Node, DAG)) {
      Results.push_back(Expanded);
      return;
    }
    break;
  case ISD::SMULFIXSAT:
  case ISD::UMULFIXSAT:
    // FIXME: We do not expand SMULFIXSAT/UMULFIXSAT here yet, not sure exactly
    // why. Maybe it results in worse codegen compared to the unroll for some
    // targets? This should probably be investigated. And if we still prefer to
    // unroll an explanation could be helpful.
    break;
  case ISD::SDIVFIX:
  case ISD::UDIVFIX:
    ExpandFixedPointDiv(Node, Results);
    return;
  case ISD::SDIVFIXSAT:
  case ISD::UDIVFIXSAT:
    break;
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  case ISD::STRICT_##DAGN:
#include "llvm/IR/ConstrainedOps.def"
    ExpandStrictFPOp(Node, Results);
    return;
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_FMUL:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMINIMUM:
    Results.push_back(TLI.expandVecReduce(Node, DAG));
    return;
  case ISD::VECREDUCE_SEQ_FADD:
  case ISD::VECREDUCE_SEQ_FMUL:
    Results.push_back(TLI.expandVecReduceSeq(Node, DAG));
    return;
  case ISD::SREM:
  case ISD::UREM:
    ExpandREM(Node, Results);
    return;
  case ISD::VP_MERGE:
    Results.push_back(ExpandVP_MERGE(Node));
    return;
  case ISD::FREM:
    if (tryExpandVecMathCall(Node, RTLIB::REM_F32, RTLIB::REM_F64,
                             RTLIB::REM_F80, RTLIB::REM_F128,
                             RTLIB::REM_PPCF128, Results))
      return;

    break;
  case ISD::VECTOR_COMPRESS:
    Results.push_back(TLI.expandVECTOR_COMPRESS(Node, DAG));
    return;
  }

  SDValue Unrolled = DAG.UnrollVectorOp(Node);
  if (Node->getNumValues() == 1) {
    Results.push_back(Unrolled);
  } else {
    assert(Node->getNumValues() == Unrolled->getNumValues() &&
      "VectorLegalizer Expand returned wrong number of results!");
    for (unsigned I = 0, E = Unrolled->getNumValues(); I != E; ++I)
      Results.push_back(Unrolled.getValue(I));
  }
}

SDValue VectorLegalizer::ExpandSELECT(SDNode *Node) {
  // Lower a select instruction where the condition is a scalar and the
  // operands are vectors. Lower this select to VSELECT and implement it
  // using XOR AND OR. The selector bit is broadcasted.
  EVT VT = Node->getValueType(0);
  SDLoc DL(Node);

  SDValue Mask = Node->getOperand(0);
  SDValue Op1 = Node->getOperand(1);
  SDValue Op2 = Node->getOperand(2);

  assert(VT.isVector() && !Mask.getValueType().isVector()
         && Op1.getValueType() == Op2.getValueType() && "Invalid type");

  // If we can't even use the basic vector operations of
  // AND,OR,XOR, we will have to scalarize the op.
  // Notice that the operation may be 'promoted' which means that it is
  // 'bitcasted' to another type which is handled.
  // Also, we need to be able to construct a splat vector using either
  // BUILD_VECTOR or SPLAT_VECTOR.
  // FIXME: Should we also permit fixed-length SPLAT_VECTOR as a fallback to
  // BUILD_VECTOR?
  if (TLI.getOperationAction(ISD::AND, VT) == TargetLowering::Expand ||
      TLI.getOperationAction(ISD::XOR, VT) == TargetLowering::Expand ||
      TLI.getOperationAction(ISD::OR, VT) == TargetLowering::Expand ||
      TLI.getOperationAction(VT.isFixedLengthVector() ? ISD::BUILD_VECTOR
                                                      : ISD::SPLAT_VECTOR,
                             VT) == TargetLowering::Expand)
    return DAG.UnrollVectorOp(Node);

  // Generate a mask operand.
  EVT MaskTy = VT.changeVectorElementTypeToInteger();

  // What is the size of each element in the vector mask.
  EVT BitTy = MaskTy.getScalarType();

  Mask = DAG.getSelect(DL, BitTy, Mask, DAG.getAllOnesConstant(DL, BitTy),
                       DAG.getConstant(0, DL, BitTy));

  // Broadcast the mask so that the entire vector is all one or all zero.
  Mask = DAG.getSplat(MaskTy, DL, Mask);

  // Bitcast the operands to be the same type as the mask.
  // This is needed when we select between FP types because
  // the mask is a vector of integers.
  Op1 = DAG.getNode(ISD::BITCAST, DL, MaskTy, Op1);
  Op2 = DAG.getNode(ISD::BITCAST, DL, MaskTy, Op2);

  SDValue NotMask = DAG.getNOT(DL, Mask, MaskTy);

  Op1 = DAG.getNode(ISD::AND, DL, MaskTy, Op1, Mask);
  Op2 = DAG.getNode(ISD::AND, DL, MaskTy, Op2, NotMask);
  SDValue Val = DAG.getNode(ISD::OR, DL, MaskTy, Op1, Op2);
  return DAG.getNode(ISD::BITCAST, DL, Node->getValueType(0), Val);
}

SDValue VectorLegalizer::ExpandSEXTINREG(SDNode *Node) {
  EVT VT = Node->getValueType(0);

  // Make sure that the SRA and SHL instructions are available.
  if (TLI.getOperationAction(ISD::SRA, VT) == TargetLowering::Expand ||
      TLI.getOperationAction(ISD::SHL, VT) == TargetLowering::Expand)
    return DAG.UnrollVectorOp(Node);

  SDLoc DL(Node);
  EVT OrigTy = cast<VTSDNode>(Node->getOperand(1))->getVT();

  unsigned BW = VT.getScalarSizeInBits();
  unsigned OrigBW = OrigTy.getScalarSizeInBits();
  SDValue ShiftSz = DAG.getConstant(BW - OrigBW, DL, VT);

  SDValue Op = DAG.getNode(ISD::SHL, DL, VT, Node->getOperand(0), ShiftSz);
  return DAG.getNode(ISD::SRA, DL, VT, Op, ShiftSz);
}

// Generically expand a vector anyext in register to a shuffle of the relevant
// lanes into the appropriate locations, with other lanes left undef.
SDValue VectorLegalizer::ExpandANY_EXTEND_VECTOR_INREG(SDNode *Node) {
  SDLoc DL(Node);
  EVT VT = Node->getValueType(0);
  int NumElements = VT.getVectorNumElements();
  SDValue Src = Node->getOperand(0);
  EVT SrcVT = Src.getValueType();
  int NumSrcElements = SrcVT.getVectorNumElements();

  // *_EXTEND_VECTOR_INREG SrcVT can be smaller than VT - so insert the vector
  // into a larger vector type.
  if (SrcVT.bitsLE(VT)) {
    assert((VT.getSizeInBits() % SrcVT.getScalarSizeInBits()) == 0 &&
           "ANY_EXTEND_VECTOR_INREG vector size mismatch");
    NumSrcElements = VT.getSizeInBits() / SrcVT.getScalarSizeInBits();
    SrcVT = EVT::getVectorVT(*DAG.getContext(), SrcVT.getScalarType(),
                             NumSrcElements);
    Src = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, SrcVT, DAG.getUNDEF(SrcVT),
                      Src, DAG.getVectorIdxConstant(0, DL));
  }

  // Build a base mask of undef shuffles.
  SmallVector<int, 16> ShuffleMask;
  ShuffleMask.resize(NumSrcElements, -1);

  // Place the extended lanes into the correct locations.
  int ExtLaneScale = NumSrcElements / NumElements;
  int EndianOffset = DAG.getDataLayout().isBigEndian() ? ExtLaneScale - 1 : 0;
  for (int i = 0; i < NumElements; ++i)
    ShuffleMask[i * ExtLaneScale + EndianOffset] = i;

  return DAG.getNode(
      ISD::BITCAST, DL, VT,
      DAG.getVectorShuffle(SrcVT, DL, Src, DAG.getUNDEF(SrcVT), ShuffleMask));
}

SDValue VectorLegalizer::ExpandSIGN_EXTEND_VECTOR_INREG(SDNode *Node) {
  SDLoc DL(Node);
  EVT VT = Node->getValueType(0);
  SDValue Src = Node->getOperand(0);
  EVT SrcVT = Src.getValueType();

  // First build an any-extend node which can be legalized above when we
  // recurse through it.
  SDValue Op = DAG.getNode(ISD::ANY_EXTEND_VECTOR_INREG, DL, VT, Src);

  // Now we need sign extend. Do this by shifting the elements. Even if these
  // aren't legal operations, they have a better chance of being legalized
  // without full scalarization than the sign extension does.
  unsigned EltWidth = VT.getScalarSizeInBits();
  unsigned SrcEltWidth = SrcVT.getScalarSizeInBits();
  SDValue ShiftAmount = DAG.getConstant(EltWidth - SrcEltWidth, DL, VT);
  return DAG.getNode(ISD::SRA, DL, VT,
                     DAG.getNode(ISD::SHL, DL, VT, Op, ShiftAmount),
                     ShiftAmount);
}

// Generically expand a vector zext in register to a shuffle of the relevant
// lanes into the appropriate locations, a blend of zero into the high bits,
// and a bitcast to the wider element type.
SDValue VectorLegalizer::ExpandZERO_EXTEND_VECTOR_INREG(SDNode *Node) {
  SDLoc DL(Node);
  EVT VT = Node->getValueType(0);
  int NumElements = VT.getVectorNumElements();
  SDValue Src = Node->getOperand(0);
  EVT SrcVT = Src.getValueType();
  int NumSrcElements = SrcVT.getVectorNumElements();

  // *_EXTEND_VECTOR_INREG SrcVT can be smaller than VT - so insert the vector
  // into a larger vector type.
  if (SrcVT.bitsLE(VT)) {
    assert((VT.getSizeInBits() % SrcVT.getScalarSizeInBits()) == 0 &&
           "ZERO_EXTEND_VECTOR_INREG vector size mismatch");
    NumSrcElements = VT.getSizeInBits() / SrcVT.getScalarSizeInBits();
    SrcVT = EVT::getVectorVT(*DAG.getContext(), SrcVT.getScalarType(),
                             NumSrcElements);
    Src = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, SrcVT, DAG.getUNDEF(SrcVT),
                      Src, DAG.getVectorIdxConstant(0, DL));
  }

  // Build up a zero vector to blend into this one.
  SDValue Zero = DAG.getConstant(0, DL, SrcVT);

  // Shuffle the incoming lanes into the correct position, and pull all other
  // lanes from the zero vector.
  auto ShuffleMask = llvm::to_vector<16>(llvm::seq<int>(0, NumSrcElements));

  int ExtLaneScale = NumSrcElements / NumElements;
  int EndianOffset = DAG.getDataLayout().isBigEndian() ? ExtLaneScale - 1 : 0;
  for (int i = 0; i < NumElements; ++i)
    ShuffleMask[i * ExtLaneScale + EndianOffset] = NumSrcElements + i;

  return DAG.getNode(ISD::BITCAST, DL, VT,
                     DAG.getVectorShuffle(SrcVT, DL, Zero, Src, ShuffleMask));
}

static void createBSWAPShuffleMask(EVT VT, SmallVectorImpl<int> &ShuffleMask) {
  int ScalarSizeInBytes = VT.getScalarSizeInBits() / 8;
  for (int I = 0, E = VT.getVectorNumElements(); I != E; ++I)
    for (int J = ScalarSizeInBytes - 1; J >= 0; --J)
      ShuffleMask.push_back((I * ScalarSizeInBytes) + J);
}

SDValue VectorLegalizer::ExpandBSWAP(SDNode *Node) {
  EVT VT = Node->getValueType(0);

  // Scalable vectors can't use shuffle expansion.
  if (VT.isScalableVector())
    return TLI.expandBSWAP(Node, DAG);

  // Generate a byte wise shuffle mask for the BSWAP.
  SmallVector<int, 16> ShuffleMask;
  createBSWAPShuffleMask(VT, ShuffleMask);
  EVT ByteVT = EVT::getVectorVT(*DAG.getContext(), MVT::i8, ShuffleMask.size());

  // Only emit a shuffle if the mask is legal.
  if (TLI.isShuffleMaskLegal(ShuffleMask, ByteVT)) {
    SDLoc DL(Node);
    SDValue Op = DAG.getNode(ISD::BITCAST, DL, ByteVT, Node->getOperand(0));
    Op = DAG.getVectorShuffle(ByteVT, DL, Op, DAG.getUNDEF(ByteVT), ShuffleMask);
    return DAG.getNode(ISD::BITCAST, DL, VT, Op);
  }

  // If we have the appropriate vector bit operations, it is better to use them
  // than unrolling and expanding each component.
  if (TLI.isOperationLegalOrCustom(ISD::SHL, VT) &&
      TLI.isOperationLegalOrCustom(ISD::SRL, VT) &&
      TLI.isOperationLegalOrCustomOrPromote(ISD::AND, VT) &&
      TLI.isOperationLegalOrCustomOrPromote(ISD::OR, VT))
    return TLI.expandBSWAP(Node, DAG);

  // Otherwise unroll.
  return DAG.UnrollVectorOp(Node);
}

void VectorLegalizer::ExpandBITREVERSE(SDNode *Node,
                                       SmallVectorImpl<SDValue> &Results) {
  EVT VT = Node->getValueType(0);

  // We can't unroll or use shuffles for scalable vectors.
  if (VT.isScalableVector()) {
    Results.push_back(TLI.expandBITREVERSE(Node, DAG));
    return;
  }

  // If we have the scalar operation, it's probably cheaper to unroll it.
  if (TLI.isOperationLegalOrCustom(ISD::BITREVERSE, VT.getScalarType())) {
    SDValue Tmp = DAG.UnrollVectorOp(Node);
    Results.push_back(Tmp);
    return;
  }

  // If the vector element width is a whole number of bytes, test if its legal
  // to BSWAP shuffle the bytes and then perform the BITREVERSE on the byte
  // vector. This greatly reduces the number of bit shifts necessary.
  unsigned ScalarSizeInBits = VT.getScalarSizeInBits();
  if (ScalarSizeInBits > 8 && (ScalarSizeInBits % 8) == 0) {
    SmallVector<int, 16> BSWAPMask;
    createBSWAPShuffleMask(VT, BSWAPMask);

    EVT ByteVT = EVT::getVectorVT(*DAG.getContext(), MVT::i8, BSWAPMask.size());
    if (TLI.isShuffleMaskLegal(BSWAPMask, ByteVT) &&
        (TLI.isOperationLegalOrCustom(ISD::BITREVERSE, ByteVT) ||
         (TLI.isOperationLegalOrCustom(ISD::SHL, ByteVT) &&
          TLI.isOperationLegalOrCustom(ISD::SRL, ByteVT) &&
          TLI.isOperationLegalOrCustomOrPromote(ISD::AND, ByteVT) &&
          TLI.isOperationLegalOrCustomOrPromote(ISD::OR, ByteVT)))) {
      SDLoc DL(Node);
      SDValue Op = DAG.getNode(ISD::BITCAST, DL, ByteVT, Node->getOperand(0));
      Op = DAG.getVectorShuffle(ByteVT, DL, Op, DAG.getUNDEF(ByteVT),
                                BSWAPMask);
      Op = DAG.getNode(ISD::BITREVERSE, DL, ByteVT, Op);
      Op = DAG.getNode(ISD::BITCAST, DL, VT, Op);
      Results.push_back(Op);
      return;
    }
  }

  // If we have the appropriate vector bit operations, it is better to use them
  // than unrolling and expanding each component.
  if (TLI.isOperationLegalOrCustom(ISD::SHL, VT) &&
      TLI.isOperationLegalOrCustom(ISD::SRL, VT) &&
      TLI.isOperationLegalOrCustomOrPromote(ISD::AND, VT) &&
      TLI.isOperationLegalOrCustomOrPromote(ISD::OR, VT)) {
    Results.push_back(TLI.expandBITREVERSE(Node, DAG));
    return;
  }

  // Otherwise unroll.
  SDValue Tmp = DAG.UnrollVectorOp(Node);
  Results.push_back(Tmp);
}

SDValue VectorLegalizer::ExpandVSELECT(SDNode *Node) {
  // Implement VSELECT in terms of XOR, AND, OR
  // on platforms which do not support blend natively.
  SDLoc DL(Node);

  SDValue Mask = Node->getOperand(0);
  SDValue Op1 = Node->getOperand(1);
  SDValue Op2 = Node->getOperand(2);

  EVT VT = Mask.getValueType();

  // If we can't even use the basic vector operations of
  // AND,OR,XOR, we will have to scalarize the op.
  // Notice that the operation may be 'promoted' which means that it is
  // 'bitcasted' to another type which is handled.
  if (TLI.getOperationAction(ISD::AND, VT) == TargetLowering::Expand ||
      TLI.getOperationAction(ISD::XOR, VT) == TargetLowering::Expand ||
      TLI.getOperationAction(ISD::OR, VT) == TargetLowering::Expand)
    return DAG.UnrollVectorOp(Node);

  // This operation also isn't safe with AND, OR, XOR when the boolean type is
  // 0/1 and the select operands aren't also booleans, as we need an all-ones
  // vector constant to mask with.
  // FIXME: Sign extend 1 to all ones if that's legal on the target.
  auto BoolContents = TLI.getBooleanContents(Op1.getValueType());
  if (BoolContents != TargetLowering::ZeroOrNegativeOneBooleanContent &&
      !(BoolContents == TargetLowering::ZeroOrOneBooleanContent &&
        Op1.getValueType().getVectorElementType() == MVT::i1))
    return DAG.UnrollVectorOp(Node);

  // If the mask and the type are different sizes, unroll the vector op. This
  // can occur when getSetCCResultType returns something that is different in
  // size from the operand types. For example, v4i8 = select v4i32, v4i8, v4i8.
  if (VT.getSizeInBits() != Op1.getValueSizeInBits())
    return DAG.UnrollVectorOp(Node);

  // Bitcast the operands to be the same type as the mask.
  // This is needed when we select between FP types because
  // the mask is a vector of integers.
  Op1 = DAG.getNode(ISD::BITCAST, DL, VT, Op1);
  Op2 = DAG.getNode(ISD::BITCAST, DL, VT, Op2);

  SDValue NotMask = DAG.getNOT(DL, Mask, VT);

  Op1 = DAG.getNode(ISD::AND, DL, VT, Op1, Mask);
  Op2 = DAG.getNode(ISD::AND, DL, VT, Op2, NotMask);
  SDValue Val = DAG.getNode(ISD::OR, DL, VT, Op1, Op2);
  return DAG.getNode(ISD::BITCAST, DL, Node->getValueType(0), Val);
}

SDValue VectorLegalizer::ExpandVP_SELECT(SDNode *Node) {
  // Implement VP_SELECT in terms of VP_XOR, VP_AND and VP_OR on platforms which
  // do not support it natively.
  SDLoc DL(Node);

  SDValue Mask = Node->getOperand(0);
  SDValue Op1 = Node->getOperand(1);
  SDValue Op2 = Node->getOperand(2);
  SDValue EVL = Node->getOperand(3);

  EVT VT = Mask.getValueType();

  // If we can't even use the basic vector operations of
  // VP_AND,VP_OR,VP_XOR, we will have to scalarize the op.
  if (TLI.getOperationAction(ISD::VP_AND, VT) == TargetLowering::Expand ||
      TLI.getOperationAction(ISD::VP_XOR, VT) == TargetLowering::Expand ||
      TLI.getOperationAction(ISD::VP_OR, VT) == TargetLowering::Expand)
    return DAG.UnrollVectorOp(Node);

  // This operation also isn't safe when the operands aren't also booleans.
  if (Op1.getValueType().getVectorElementType() != MVT::i1)
    return DAG.UnrollVectorOp(Node);

  SDValue Ones = DAG.getAllOnesConstant(DL, VT);
  SDValue NotMask = DAG.getNode(ISD::VP_XOR, DL, VT, Mask, Ones, Ones, EVL);

  Op1 = DAG.getNode(ISD::VP_AND, DL, VT, Op1, Mask, Ones, EVL);
  Op2 = DAG.getNode(ISD::VP_AND, DL, VT, Op2, NotMask, Ones, EVL);
  return DAG.getNode(ISD::VP_OR, DL, VT, Op1, Op2, Ones, EVL);
}

SDValue VectorLegalizer::ExpandVP_MERGE(SDNode *Node) {
  // Implement VP_MERGE in terms of VSELECT. Construct a mask where vector
  // indices less than the EVL/pivot are true. Combine that with the original
  // mask for a full-length mask. Use a full-length VSELECT to select between
  // the true and false values.
  SDLoc DL(Node);

  SDValue Mask = Node->getOperand(0);
  SDValue Op1 = Node->getOperand(1);
  SDValue Op2 = Node->getOperand(2);
  SDValue EVL = Node->getOperand(3);

  EVT MaskVT = Mask.getValueType();
  bool IsFixedLen = MaskVT.isFixedLengthVector();

  EVT EVLVecVT = EVT::getVectorVT(*DAG.getContext(), EVL.getValueType(),
                                  MaskVT.getVectorElementCount());

  // If we can't construct the EVL mask efficiently, it's better to unroll.
  if ((IsFixedLen &&
       !TLI.isOperationLegalOrCustom(ISD::BUILD_VECTOR, EVLVecVT)) ||
      (!IsFixedLen &&
       (!TLI.isOperationLegalOrCustom(ISD::STEP_VECTOR, EVLVecVT) ||
        !TLI.isOperationLegalOrCustom(ISD::SPLAT_VECTOR, EVLVecVT))))
    return DAG.UnrollVectorOp(Node);

  // If using a SETCC would result in a different type than the mask type,
  // unroll.
  if (TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                             EVLVecVT) != MaskVT)
    return DAG.UnrollVectorOp(Node);

  SDValue StepVec = DAG.getStepVector(DL, EVLVecVT);
  SDValue SplatEVL = DAG.getSplat(EVLVecVT, DL, EVL);
  SDValue EVLMask =
      DAG.getSetCC(DL, MaskVT, StepVec, SplatEVL, ISD::CondCode::SETULT);

  SDValue FullMask = DAG.getNode(ISD::AND, DL, MaskVT, Mask, EVLMask);
  return DAG.getSelect(DL, Node->getValueType(0), FullMask, Op1, Op2);
}

SDValue VectorLegalizer::ExpandVP_REM(SDNode *Node) {
  // Implement VP_SREM/UREM in terms of VP_SDIV/VP_UDIV, VP_MUL, VP_SUB.
  EVT VT = Node->getValueType(0);

  unsigned DivOpc = Node->getOpcode() == ISD::VP_SREM ? ISD::VP_SDIV : ISD::VP_UDIV;

  if (!TLI.isOperationLegalOrCustom(DivOpc, VT) ||
      !TLI.isOperationLegalOrCustom(ISD::VP_MUL, VT) ||
      !TLI.isOperationLegalOrCustom(ISD::VP_SUB, VT))
    return SDValue();

  SDLoc DL(Node);

  SDValue Dividend = Node->getOperand(0);
  SDValue Divisor = Node->getOperand(1);
  SDValue Mask = Node->getOperand(2);
  SDValue EVL = Node->getOperand(3);

  // X % Y -> X-X/Y*Y
  SDValue Div = DAG.getNode(DivOpc, DL, VT, Dividend, Divisor, Mask, EVL);
  SDValue Mul = DAG.getNode(ISD::VP_MUL, DL, VT, Divisor, Div, Mask, EVL);
  return DAG.getNode(ISD::VP_SUB, DL, VT, Dividend, Mul, Mask, EVL);
}

void VectorLegalizer::ExpandFP_TO_UINT(SDNode *Node,
                                       SmallVectorImpl<SDValue> &Results) {
  // Attempt to expand using TargetLowering.
  SDValue Result, Chain;
  if (TLI.expandFP_TO_UINT(Node, Result, Chain, DAG)) {
    Results.push_back(Result);
    if (Node->isStrictFPOpcode())
      Results.push_back(Chain);
    return;
  }

  // Otherwise go ahead and unroll.
  if (Node->isStrictFPOpcode()) {
    UnrollStrictFPOp(Node, Results);
    return;
  }

  Results.push_back(DAG.UnrollVectorOp(Node));
}

void VectorLegalizer::ExpandUINT_TO_FLOAT(SDNode *Node,
                                          SmallVectorImpl<SDValue> &Results) {
  bool IsStrict = Node->isStrictFPOpcode();
  unsigned OpNo = IsStrict ? 1 : 0;
  SDValue Src = Node->getOperand(OpNo);
  EVT VT = Src.getValueType();
  SDLoc DL(Node);

  // Attempt to expand using TargetLowering.
  SDValue Result;
  SDValue Chain;
  if (TLI.expandUINT_TO_FP(Node, Result, Chain, DAG)) {
    Results.push_back(Result);
    if (IsStrict)
      Results.push_back(Chain);
    return;
  }

  // Make sure that the SINT_TO_FP and SRL instructions are available.
  if (((!IsStrict && TLI.getOperationAction(ISD::SINT_TO_FP, VT) ==
                         TargetLowering::Expand) ||
       (IsStrict && TLI.getOperationAction(ISD::STRICT_SINT_TO_FP, VT) ==
                        TargetLowering::Expand)) ||
      TLI.getOperationAction(ISD::SRL, VT) == TargetLowering::Expand) {
    if (IsStrict) {
      UnrollStrictFPOp(Node, Results);
      return;
    }

    Results.push_back(DAG.UnrollVectorOp(Node));
    return;
  }

  unsigned BW = VT.getScalarSizeInBits();
  assert((BW == 64 || BW == 32) &&
         "Elements in vector-UINT_TO_FP must be 32 or 64 bits wide");

  SDValue HalfWord = DAG.getConstant(BW / 2, DL, VT);

  // Constants to clear the upper part of the word.
  // Notice that we can also use SHL+SHR, but using a constant is slightly
  // faster on x86.
  uint64_t HWMask = (BW == 64) ? 0x00000000FFFFFFFF : 0x0000FFFF;
  SDValue HalfWordMask = DAG.getConstant(HWMask, DL, VT);

  // Two to the power of half-word-size.
  SDValue TWOHW =
      DAG.getConstantFP(1ULL << (BW / 2), DL, Node->getValueType(0));

  // Clear upper part of LO, lower HI
  SDValue HI = DAG.getNode(ISD::SRL, DL, VT, Src, HalfWord);
  SDValue LO = DAG.getNode(ISD::AND, DL, VT, Src, HalfWordMask);

  if (IsStrict) {
    // Convert hi and lo to floats
    // Convert the hi part back to the upper values
    // TODO: Can any fast-math-flags be set on these nodes?
    SDValue fHI = DAG.getNode(ISD::STRICT_SINT_TO_FP, DL,
                              {Node->getValueType(0), MVT::Other},
                              {Node->getOperand(0), HI});
    fHI = DAG.getNode(ISD::STRICT_FMUL, DL, {Node->getValueType(0), MVT::Other},
                      {fHI.getValue(1), fHI, TWOHW});
    SDValue fLO = DAG.getNode(ISD::STRICT_SINT_TO_FP, DL,
                              {Node->getValueType(0), MVT::Other},
                              {Node->getOperand(0), LO});

    SDValue TF = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, fHI.getValue(1),
                             fLO.getValue(1));

    // Add the two halves
    SDValue Result =
        DAG.getNode(ISD::STRICT_FADD, DL, {Node->getValueType(0), MVT::Other},
                    {TF, fHI, fLO});

    Results.push_back(Result);
    Results.push_back(Result.getValue(1));
    return;
  }

  // Convert hi and lo to floats
  // Convert the hi part back to the upper values
  // TODO: Can any fast-math-flags be set on these nodes?
  SDValue fHI = DAG.getNode(ISD::SINT_TO_FP, DL, Node->getValueType(0), HI);
  fHI = DAG.getNode(ISD::FMUL, DL, Node->getValueType(0), fHI, TWOHW);
  SDValue fLO = DAG.getNode(ISD::SINT_TO_FP, DL, Node->getValueType(0), LO);

  // Add the two halves
  Results.push_back(
      DAG.getNode(ISD::FADD, DL, Node->getValueType(0), fHI, fLO));
}

SDValue VectorLegalizer::ExpandFNEG(SDNode *Node) {
  if (TLI.isOperationLegalOrCustom(ISD::FSUB, Node->getValueType(0))) {
    SDLoc DL(Node);
    SDValue Zero = DAG.getConstantFP(-0.0, DL, Node->getValueType(0));
    // TODO: If FNEG had fast-math-flags, they'd get propagated to this FSUB.
    return DAG.getNode(ISD::FSUB, DL, Node->getValueType(0), Zero,
                       Node->getOperand(0));
  }
  return DAG.UnrollVectorOp(Node);
}

void VectorLegalizer::ExpandFSUB(SDNode *Node,
                                 SmallVectorImpl<SDValue> &Results) {
  // For floating-point values, (a-b) is the same as a+(-b). If FNEG is legal,
  // we can defer this to operation legalization where it will be lowered as
  // a+(-b).
  EVT VT = Node->getValueType(0);
  if (TLI.isOperationLegalOrCustom(ISD::FNEG, VT) &&
      TLI.isOperationLegalOrCustom(ISD::FADD, VT))
    return; // Defer to LegalizeDAG

  SDValue Tmp = DAG.UnrollVectorOp(Node);
  Results.push_back(Tmp);
}

void VectorLegalizer::ExpandSETCC(SDNode *Node,
                                  SmallVectorImpl<SDValue> &Results) {
  bool NeedInvert = false;
  bool IsVP = Node->getOpcode() == ISD::VP_SETCC;
  bool IsStrict = Node->getOpcode() == ISD::STRICT_FSETCC ||
                  Node->getOpcode() == ISD::STRICT_FSETCCS;
  bool IsSignaling = Node->getOpcode() == ISD::STRICT_FSETCCS;
  unsigned Offset = IsStrict ? 1 : 0;

  SDValue Chain = IsStrict ? Node->getOperand(0) : SDValue();
  SDValue LHS = Node->getOperand(0 + Offset);
  SDValue RHS = Node->getOperand(1 + Offset);
  SDValue CC = Node->getOperand(2 + Offset);

  MVT OpVT = LHS.getSimpleValueType();
  ISD::CondCode CCCode = cast<CondCodeSDNode>(CC)->get();

  if (TLI.getCondCodeAction(CCCode, OpVT) != TargetLowering::Expand) {
    if (IsStrict) {
      UnrollStrictFPOp(Node, Results);
      return;
    }
    Results.push_back(UnrollVSETCC(Node));
    return;
  }

  SDValue Mask, EVL;
  if (IsVP) {
    Mask = Node->getOperand(3 + Offset);
    EVL = Node->getOperand(4 + Offset);
  }

  SDLoc dl(Node);
  bool Legalized =
      TLI.LegalizeSetCCCondCode(DAG, Node->getValueType(0), LHS, RHS, CC, Mask,
                                EVL, NeedInvert, dl, Chain, IsSignaling);

  if (Legalized) {
    // If we expanded the SETCC by swapping LHS and RHS, or by inverting the
    // condition code, create a new SETCC node.
    if (CC.getNode()) {
      if (IsStrict) {
        LHS = DAG.getNode(Node->getOpcode(), dl, Node->getVTList(),
                          {Chain, LHS, RHS, CC}, Node->getFlags());
        Chain = LHS.getValue(1);
      } else if (IsVP) {
        LHS = DAG.getNode(ISD::VP_SETCC, dl, Node->getValueType(0),
                          {LHS, RHS, CC, Mask, EVL}, Node->getFlags());
      } else {
        LHS = DAG.getNode(ISD::SETCC, dl, Node->getValueType(0), LHS, RHS, CC,
                          Node->getFlags());
      }
    }

    // If we expanded the SETCC by inverting the condition code, then wrap
    // the existing SETCC in a NOT to restore the intended condition.
    if (NeedInvert) {
      if (!IsVP)
        LHS = DAG.getLogicalNOT(dl, LHS, LHS->getValueType(0));
      else
        LHS = DAG.getVPLogicalNOT(dl, LHS, Mask, EVL, LHS->getValueType(0));
    }
  } else {
    assert(!IsStrict && "Don't know how to expand for strict nodes.");

    // Otherwise, SETCC for the given comparison type must be completely
    // illegal; expand it into a SELECT_CC.
    EVT VT = Node->getValueType(0);
    LHS =
        DAG.getNode(ISD::SELECT_CC, dl, VT, LHS, RHS,
                    DAG.getBoolConstant(true, dl, VT, LHS.getValueType()),
                    DAG.getBoolConstant(false, dl, VT, LHS.getValueType()), CC);
    LHS->setFlags(Node->getFlags());
  }

  Results.push_back(LHS);
  if (IsStrict)
    Results.push_back(Chain);
}

void VectorLegalizer::ExpandUADDSUBO(SDNode *Node,
                                     SmallVectorImpl<SDValue> &Results) {
  SDValue Result, Overflow;
  TLI.expandUADDSUBO(Node, Result, Overflow, DAG);
  Results.push_back(Result);
  Results.push_back(Overflow);
}

void VectorLegalizer::ExpandSADDSUBO(SDNode *Node,
                                     SmallVectorImpl<SDValue> &Results) {
  SDValue Result, Overflow;
  TLI.expandSADDSUBO(Node, Result, Overflow, DAG);
  Results.push_back(Result);
  Results.push_back(Overflow);
}

void VectorLegalizer::ExpandMULO(SDNode *Node,
                                 SmallVectorImpl<SDValue> &Results) {
  SDValue Result, Overflow;
  if (!TLI.expandMULO(Node, Result, Overflow, DAG))
    std::tie(Result, Overflow) = DAG.UnrollVectorOverflowOp(Node);

  Results.push_back(Result);
  Results.push_back(Overflow);
}

void VectorLegalizer::ExpandFixedPointDiv(SDNode *Node,
                                          SmallVectorImpl<SDValue> &Results) {
  SDNode *N = Node;
  if (SDValue Expanded = TLI.expandFixedPointDiv(N->getOpcode(), SDLoc(N),
          N->getOperand(0), N->getOperand(1), N->getConstantOperandVal(2), DAG))
    Results.push_back(Expanded);
}

void VectorLegalizer::ExpandStrictFPOp(SDNode *Node,
                                       SmallVectorImpl<SDValue> &Results) {
  if (Node->getOpcode() == ISD::STRICT_UINT_TO_FP) {
    ExpandUINT_TO_FLOAT(Node, Results);
    return;
  }
  if (Node->getOpcode() == ISD::STRICT_FP_TO_UINT) {
    ExpandFP_TO_UINT(Node, Results);
    return;
  }

  if (Node->getOpcode() == ISD::STRICT_FSETCC ||
      Node->getOpcode() == ISD::STRICT_FSETCCS) {
    ExpandSETCC(Node, Results);
    return;
  }

  UnrollStrictFPOp(Node, Results);
}

void VectorLegalizer::ExpandREM(SDNode *Node,
                                SmallVectorImpl<SDValue> &Results) {
  assert((Node->getOpcode() == ISD::SREM || Node->getOpcode() == ISD::UREM) &&
         "Expected REM node");

  SDValue Result;
  if (!TLI.expandREM(Node, Result, DAG))
    Result = DAG.UnrollVectorOp(Node);
  Results.push_back(Result);
}

// Try to expand libm nodes into vector math routine calls. Callers provide the
// LibFunc equivalent of the passed in Node, which is used to lookup mappings
// within TargetLibraryInfo. The only mappings considered are those where the
// result and all operands are the same vector type. While predicated nodes are
// not supported, we will emit calls to masked routines by passing in an all
// true mask.
bool VectorLegalizer::tryExpandVecMathCall(SDNode *Node, RTLIB::Libcall LC,
                                           SmallVectorImpl<SDValue> &Results) {
  // Chain must be propagated but currently strict fp operations are down
  // converted to their none strict counterpart.
  assert(!Node->isStrictFPOpcode() && "Unexpected strict fp operation!");

  const char *LCName = TLI.getLibcallName(LC);
  if (!LCName)
    return false;
  LLVM_DEBUG(dbgs() << "Looking for vector variant of " << LCName << "\n");

  EVT VT = Node->getValueType(0);
  ElementCount VL = VT.getVectorElementCount();

  // Lookup a vector function equivalent to the specified libcall. Prefer
  // unmasked variants but we will generate a mask if need be.
  const TargetLibraryInfo &TLibInfo = DAG.getLibInfo();
  const VecDesc *VD = TLibInfo.getVectorMappingInfo(LCName, VL, false);
  if (!VD)
    VD = TLibInfo.getVectorMappingInfo(LCName, VL, /*Masked=*/true);
  if (!VD)
    return false;

  LLVMContext *Ctx = DAG.getContext();
  Type *Ty = VT.getTypeForEVT(*Ctx);
  Type *ScalarTy = Ty->getScalarType();

  // Construct a scalar function type based on Node's operands.
  SmallVector<Type *, 8> ArgTys;
  for (unsigned i = 0; i < Node->getNumOperands(); ++i) {
    assert(Node->getOperand(i).getValueType() == VT &&
           "Expected matching vector types!");
    ArgTys.push_back(ScalarTy);
  }
  FunctionType *ScalarFTy = FunctionType::get(ScalarTy, ArgTys, false);

  // Generate call information for the vector function.
  const std::string MangledName = VD->getVectorFunctionABIVariantString();
  auto OptVFInfo = VFABI::tryDemangleForVFABI(MangledName, ScalarFTy);
  if (!OptVFInfo)
    return false;

  LLVM_DEBUG(dbgs() << "Found vector variant " << VD->getVectorFnName()
                    << "\n");

  // Sanity check just in case OptVFInfo has unexpected parameters.
  if (OptVFInfo->Shape.Parameters.size() !=
      Node->getNumOperands() + VD->isMasked())
    return false;

  // Collect vector call operands.

  SDLoc DL(Node);
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  Entry.IsSExt = false;
  Entry.IsZExt = false;

  unsigned OpNum = 0;
  for (auto &VFParam : OptVFInfo->Shape.Parameters) {
    if (VFParam.ParamKind == VFParamKind::GlobalPredicate) {
      EVT MaskVT = TLI.getSetCCResultType(DAG.getDataLayout(), *Ctx, VT);
      Entry.Node = DAG.getBoolConstant(true, DL, MaskVT, VT);
      Entry.Ty = MaskVT.getTypeForEVT(*Ctx);
      Args.push_back(Entry);
      continue;
    }

    // Only vector operands are supported.
    if (VFParam.ParamKind != VFParamKind::Vector)
      return false;

    Entry.Node = Node->getOperand(OpNum++);
    Entry.Ty = Ty;
    Args.push_back(Entry);
  }

  // Emit a call to the vector function.
  SDValue Callee = DAG.getExternalSymbol(VD->getVectorFnName().data(),
                                         TLI.getPointerTy(DAG.getDataLayout()));
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, Ty, Callee, std::move(Args));

  std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);
  Results.push_back(CallResult.first);
  return true;
}

/// Try to expand the node to a vector libcall based on the result type.
bool VectorLegalizer::tryExpandVecMathCall(
    SDNode *Node, RTLIB::Libcall Call_F32, RTLIB::Libcall Call_F64,
    RTLIB::Libcall Call_F80, RTLIB::Libcall Call_F128,
    RTLIB::Libcall Call_PPCF128, SmallVectorImpl<SDValue> &Results) {
  RTLIB::Libcall LC = RTLIB::getFPLibCall(
      Node->getValueType(0).getVectorElementType(), Call_F32, Call_F64,
      Call_F80, Call_F128, Call_PPCF128);

  if (LC == RTLIB::UNKNOWN_LIBCALL)
    return false;

  return tryExpandVecMathCall(Node, LC, Results);
}

void VectorLegalizer::UnrollStrictFPOp(SDNode *Node,
                                       SmallVectorImpl<SDValue> &Results) {
  EVT VT = Node->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  unsigned NumElems = VT.getVectorNumElements();
  unsigned NumOpers = Node->getNumOperands();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  EVT TmpEltVT = EltVT;
  if (Node->getOpcode() == ISD::STRICT_FSETCC ||
      Node->getOpcode() == ISD::STRICT_FSETCCS)
    TmpEltVT = TLI.getSetCCResultType(DAG.getDataLayout(),
                                      *DAG.getContext(), TmpEltVT);

  EVT ValueVTs[] = {TmpEltVT, MVT::Other};
  SDValue Chain = Node->getOperand(0);
  SDLoc dl(Node);

  SmallVector<SDValue, 32> OpValues;
  SmallVector<SDValue, 32> OpChains;
  for (unsigned i = 0; i < NumElems; ++i) {
    SmallVector<SDValue, 4> Opers;
    SDValue Idx = DAG.getVectorIdxConstant(i, dl);

    // The Chain is the first operand.
    Opers.push_back(Chain);

    // Now process the remaining operands.
    for (unsigned j = 1; j < NumOpers; ++j) {
      SDValue Oper = Node->getOperand(j);
      EVT OperVT = Oper.getValueType();

      if (OperVT.isVector())
        Oper = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl,
                           OperVT.getVectorElementType(), Oper, Idx);

      Opers.push_back(Oper);
    }

    SDValue ScalarOp = DAG.getNode(Node->getOpcode(), dl, ValueVTs, Opers);
    SDValue ScalarResult = ScalarOp.getValue(0);
    SDValue ScalarChain = ScalarOp.getValue(1);

    if (Node->getOpcode() == ISD::STRICT_FSETCC ||
        Node->getOpcode() == ISD::STRICT_FSETCCS)
      ScalarResult = DAG.getSelect(dl, EltVT, ScalarResult,
                                   DAG.getAllOnesConstant(dl, EltVT),
                                   DAG.getConstant(0, dl, EltVT));

    OpValues.push_back(ScalarResult);
    OpChains.push_back(ScalarChain);
  }

  SDValue Result = DAG.getBuildVector(VT, dl, OpValues);
  SDValue NewChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OpChains);

  Results.push_back(Result);
  Results.push_back(NewChain);
}

SDValue VectorLegalizer::UnrollVSETCC(SDNode *Node) {
  EVT VT = Node->getValueType(0);
  unsigned NumElems = VT.getVectorNumElements();
  EVT EltVT = VT.getVectorElementType();
  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  SDValue CC = Node->getOperand(2);
  EVT TmpEltVT = LHS.getValueType().getVectorElementType();
  SDLoc dl(Node);
  SmallVector<SDValue, 8> Ops(NumElems);
  for (unsigned i = 0; i < NumElems; ++i) {
    SDValue LHSElem = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, TmpEltVT, LHS,
                                  DAG.getVectorIdxConstant(i, dl));
    SDValue RHSElem = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, TmpEltVT, RHS,
                                  DAG.getVectorIdxConstant(i, dl));
    Ops[i] = DAG.getNode(ISD::SETCC, dl,
                         TLI.getSetCCResultType(DAG.getDataLayout(),
                                                *DAG.getContext(), TmpEltVT),
                         LHSElem, RHSElem, CC);
    Ops[i] = DAG.getSelect(dl, EltVT, Ops[i], DAG.getAllOnesConstant(dl, EltVT),
                           DAG.getConstant(0, dl, EltVT));
  }
  return DAG.getBuildVector(VT, dl, Ops);
}

bool SelectionDAG::LegalizeVectors() {
  return VectorLegalizer(*this).Run();
}
