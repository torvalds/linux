//===-- TargetLowering.cpp - Implement the TargetLowering class -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the TargetLowering class.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/CodeGenCommonISel.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/DivisionByConstantInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include <cctype>
using namespace llvm;

/// NOTE: The TargetMachine owns TLOF.
TargetLowering::TargetLowering(const TargetMachine &tm)
    : TargetLoweringBase(tm) {}

const char *TargetLowering::getTargetNodeName(unsigned Opcode) const {
  return nullptr;
}

bool TargetLowering::isPositionIndependent() const {
  return getTargetMachine().isPositionIndependent();
}

/// Check whether a given call node is in tail position within its function. If
/// so, it sets Chain to the input chain of the tail call.
bool TargetLowering::isInTailCallPosition(SelectionDAG &DAG, SDNode *Node,
                                          SDValue &Chain) const {
  const Function &F = DAG.getMachineFunction().getFunction();

  // First, check if tail calls have been disabled in this function.
  if (F.getFnAttribute("disable-tail-calls").getValueAsBool())
    return false;

  // Conservatively require the attributes of the call to match those of
  // the return. Ignore following attributes because they don't affect the
  // call sequence.
  AttrBuilder CallerAttrs(F.getContext(), F.getAttributes().getRetAttrs());
  for (const auto &Attr :
       {Attribute::Alignment, Attribute::Dereferenceable,
        Attribute::DereferenceableOrNull, Attribute::NoAlias,
        Attribute::NonNull, Attribute::NoUndef, Attribute::Range})
    CallerAttrs.removeAttribute(Attr);

  if (CallerAttrs.hasAttributes())
    return false;

  // It's not safe to eliminate the sign / zero extension of the return value.
  if (CallerAttrs.contains(Attribute::ZExt) ||
      CallerAttrs.contains(Attribute::SExt))
    return false;

  // Check if the only use is a function return node.
  return isUsedByReturnOnly(Node, Chain);
}

bool TargetLowering::parametersInCSRMatch(const MachineRegisterInfo &MRI,
    const uint32_t *CallerPreservedMask,
    const SmallVectorImpl<CCValAssign> &ArgLocs,
    const SmallVectorImpl<SDValue> &OutVals) const {
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &ArgLoc = ArgLocs[I];
    if (!ArgLoc.isRegLoc())
      continue;
    MCRegister Reg = ArgLoc.getLocReg();
    // Only look at callee saved registers.
    if (MachineOperand::clobbersPhysReg(CallerPreservedMask, Reg))
      continue;
    // Check that we pass the value used for the caller.
    // (We look for a CopyFromReg reading a virtual register that is used
    //  for the function live-in value of register Reg)
    SDValue Value = OutVals[I];
    if (Value->getOpcode() == ISD::AssertZext)
      Value = Value.getOperand(0);
    if (Value->getOpcode() != ISD::CopyFromReg)
      return false;
    Register ArgReg = cast<RegisterSDNode>(Value->getOperand(1))->getReg();
    if (MRI.getLiveInPhysReg(ArgReg) != Reg)
      return false;
  }
  return true;
}

/// Set CallLoweringInfo attribute flags based on a call instruction
/// and called function attributes.
void TargetLoweringBase::ArgListEntry::setAttributes(const CallBase *Call,
                                                     unsigned ArgIdx) {
  IsSExt = Call->paramHasAttr(ArgIdx, Attribute::SExt);
  IsZExt = Call->paramHasAttr(ArgIdx, Attribute::ZExt);
  IsInReg = Call->paramHasAttr(ArgIdx, Attribute::InReg);
  IsSRet = Call->paramHasAttr(ArgIdx, Attribute::StructRet);
  IsNest = Call->paramHasAttr(ArgIdx, Attribute::Nest);
  IsByVal = Call->paramHasAttr(ArgIdx, Attribute::ByVal);
  IsPreallocated = Call->paramHasAttr(ArgIdx, Attribute::Preallocated);
  IsInAlloca = Call->paramHasAttr(ArgIdx, Attribute::InAlloca);
  IsReturned = Call->paramHasAttr(ArgIdx, Attribute::Returned);
  IsSwiftSelf = Call->paramHasAttr(ArgIdx, Attribute::SwiftSelf);
  IsSwiftAsync = Call->paramHasAttr(ArgIdx, Attribute::SwiftAsync);
  IsSwiftError = Call->paramHasAttr(ArgIdx, Attribute::SwiftError);
  Alignment = Call->getParamStackAlign(ArgIdx);
  IndirectType = nullptr;
  assert(IsByVal + IsPreallocated + IsInAlloca + IsSRet <= 1 &&
         "multiple ABI attributes?");
  if (IsByVal) {
    IndirectType = Call->getParamByValType(ArgIdx);
    if (!Alignment)
      Alignment = Call->getParamAlign(ArgIdx);
  }
  if (IsPreallocated)
    IndirectType = Call->getParamPreallocatedType(ArgIdx);
  if (IsInAlloca)
    IndirectType = Call->getParamInAllocaType(ArgIdx);
  if (IsSRet)
    IndirectType = Call->getParamStructRetType(ArgIdx);
}

/// Generate a libcall taking the given operands as arguments and returning a
/// result of type RetVT.
std::pair<SDValue, SDValue>
TargetLowering::makeLibCall(SelectionDAG &DAG, RTLIB::Libcall LC, EVT RetVT,
                            ArrayRef<SDValue> Ops,
                            MakeLibCallOptions CallOptions,
                            const SDLoc &dl,
                            SDValue InChain) const {
  if (!InChain)
    InChain = DAG.getEntryNode();

  TargetLowering::ArgListTy Args;
  Args.reserve(Ops.size());

  TargetLowering::ArgListEntry Entry;
  for (unsigned i = 0; i < Ops.size(); ++i) {
    SDValue NewOp = Ops[i];
    Entry.Node = NewOp;
    Entry.Ty = Entry.Node.getValueType().getTypeForEVT(*DAG.getContext());
    Entry.IsSExt = shouldSignExtendTypeInLibCall(NewOp.getValueType(),
                                                 CallOptions.IsSExt);
    Entry.IsZExt = !Entry.IsSExt;

    if (CallOptions.IsSoften &&
        !shouldExtendTypeInLibCall(CallOptions.OpsVTBeforeSoften[i])) {
      Entry.IsSExt = Entry.IsZExt = false;
    }
    Args.push_back(Entry);
  }

  if (LC == RTLIB::UNKNOWN_LIBCALL)
    report_fatal_error("Unsupported library call operation!");
  SDValue Callee = DAG.getExternalSymbol(getLibcallName(LC),
                                         getPointerTy(DAG.getDataLayout()));

  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());
  TargetLowering::CallLoweringInfo CLI(DAG);
  bool signExtend = shouldSignExtendTypeInLibCall(RetVT, CallOptions.IsSExt);
  bool zeroExtend = !signExtend;

  if (CallOptions.IsSoften &&
      !shouldExtendTypeInLibCall(CallOptions.RetVTBeforeSoften)) {
    signExtend = zeroExtend = false;
  }

  CLI.setDebugLoc(dl)
      .setChain(InChain)
      .setLibCallee(getLibcallCallingConv(LC), RetTy, Callee, std::move(Args))
      .setNoReturn(CallOptions.DoesNotReturn)
      .setDiscardResult(!CallOptions.IsReturnValueUsed)
      .setIsPostTypeLegalization(CallOptions.IsPostTypeLegalization)
      .setSExtResult(signExtend)
      .setZExtResult(zeroExtend);
  return LowerCallTo(CLI);
}

bool TargetLowering::findOptimalMemOpLowering(
    std::vector<EVT> &MemOps, unsigned Limit, const MemOp &Op, unsigned DstAS,
    unsigned SrcAS, const AttributeList &FuncAttributes) const {
  if (Limit != ~unsigned(0) && Op.isMemcpyWithFixedDstAlign() &&
      Op.getSrcAlign() < Op.getDstAlign())
    return false;

  EVT VT = getOptimalMemOpType(Op, FuncAttributes);

  if (VT == MVT::Other) {
    // Use the largest integer type whose alignment constraints are satisfied.
    // We only need to check DstAlign here as SrcAlign is always greater or
    // equal to DstAlign (or zero).
    VT = MVT::LAST_INTEGER_VALUETYPE;
    if (Op.isFixedDstAlign())
      while (Op.getDstAlign() < (VT.getSizeInBits() / 8) &&
             !allowsMisalignedMemoryAccesses(VT, DstAS, Op.getDstAlign()))
        VT = (MVT::SimpleValueType)(VT.getSimpleVT().SimpleTy - 1);
    assert(VT.isInteger());

    // Find the largest legal integer type.
    MVT LVT = MVT::LAST_INTEGER_VALUETYPE;
    while (!isTypeLegal(LVT))
      LVT = (MVT::SimpleValueType)(LVT.SimpleTy - 1);
    assert(LVT.isInteger());

    // If the type we've chosen is larger than the largest legal integer type
    // then use that instead.
    if (VT.bitsGT(LVT))
      VT = LVT;
  }

  unsigned NumMemOps = 0;
  uint64_t Size = Op.size();
  while (Size) {
    unsigned VTSize = VT.getSizeInBits() / 8;
    while (VTSize > Size) {
      // For now, only use non-vector load / store's for the left-over pieces.
      EVT NewVT = VT;
      unsigned NewVTSize;

      bool Found = false;
      if (VT.isVector() || VT.isFloatingPoint()) {
        NewVT = (VT.getSizeInBits() > 64) ? MVT::i64 : MVT::i32;
        if (isOperationLegalOrCustom(ISD::STORE, NewVT) &&
            isSafeMemOpType(NewVT.getSimpleVT()))
          Found = true;
        else if (NewVT == MVT::i64 &&
                 isOperationLegalOrCustom(ISD::STORE, MVT::f64) &&
                 isSafeMemOpType(MVT::f64)) {
          // i64 is usually not legal on 32-bit targets, but f64 may be.
          NewVT = MVT::f64;
          Found = true;
        }
      }

      if (!Found) {
        do {
          NewVT = (MVT::SimpleValueType)(NewVT.getSimpleVT().SimpleTy - 1);
          if (NewVT == MVT::i8)
            break;
        } while (!isSafeMemOpType(NewVT.getSimpleVT()));
      }
      NewVTSize = NewVT.getSizeInBits() / 8;

      // If the new VT cannot cover all of the remaining bits, then consider
      // issuing a (or a pair of) unaligned and overlapping load / store.
      unsigned Fast;
      if (NumMemOps && Op.allowOverlap() && NewVTSize < Size &&
          allowsMisalignedMemoryAccesses(
              VT, DstAS, Op.isFixedDstAlign() ? Op.getDstAlign() : Align(1),
              MachineMemOperand::MONone, &Fast) &&
          Fast)
        VTSize = Size;
      else {
        VT = NewVT;
        VTSize = NewVTSize;
      }
    }

    if (++NumMemOps > Limit)
      return false;

    MemOps.push_back(VT);
    Size -= VTSize;
  }

  return true;
}

/// Soften the operands of a comparison. This code is shared among BR_CC,
/// SELECT_CC, and SETCC handlers.
void TargetLowering::softenSetCCOperands(SelectionDAG &DAG, EVT VT,
                                         SDValue &NewLHS, SDValue &NewRHS,
                                         ISD::CondCode &CCCode,
                                         const SDLoc &dl, const SDValue OldLHS,
                                         const SDValue OldRHS) const {
  SDValue Chain;
  return softenSetCCOperands(DAG, VT, NewLHS, NewRHS, CCCode, dl, OldLHS,
                             OldRHS, Chain);
}

void TargetLowering::softenSetCCOperands(SelectionDAG &DAG, EVT VT,
                                         SDValue &NewLHS, SDValue &NewRHS,
                                         ISD::CondCode &CCCode,
                                         const SDLoc &dl, const SDValue OldLHS,
                                         const SDValue OldRHS,
                                         SDValue &Chain,
                                         bool IsSignaling) const {
  // FIXME: Currently we cannot really respect all IEEE predicates due to libgcc
  // not supporting it. We can update this code when libgcc provides such
  // functions.

  assert((VT == MVT::f32 || VT == MVT::f64 || VT == MVT::f128 || VT == MVT::ppcf128)
         && "Unsupported setcc type!");

  // Expand into one or more soft-fp libcall(s).
  RTLIB::Libcall LC1 = RTLIB::UNKNOWN_LIBCALL, LC2 = RTLIB::UNKNOWN_LIBCALL;
  bool ShouldInvertCC = false;
  switch (CCCode) {
  case ISD::SETEQ:
  case ISD::SETOEQ:
    LC1 = (VT == MVT::f32) ? RTLIB::OEQ_F32 :
          (VT == MVT::f64) ? RTLIB::OEQ_F64 :
          (VT == MVT::f128) ? RTLIB::OEQ_F128 : RTLIB::OEQ_PPCF128;
    break;
  case ISD::SETNE:
  case ISD::SETUNE:
    LC1 = (VT == MVT::f32) ? RTLIB::UNE_F32 :
          (VT == MVT::f64) ? RTLIB::UNE_F64 :
          (VT == MVT::f128) ? RTLIB::UNE_F128 : RTLIB::UNE_PPCF128;
    break;
  case ISD::SETGE:
  case ISD::SETOGE:
    LC1 = (VT == MVT::f32) ? RTLIB::OGE_F32 :
          (VT == MVT::f64) ? RTLIB::OGE_F64 :
          (VT == MVT::f128) ? RTLIB::OGE_F128 : RTLIB::OGE_PPCF128;
    break;
  case ISD::SETLT:
  case ISD::SETOLT:
    LC1 = (VT == MVT::f32) ? RTLIB::OLT_F32 :
          (VT == MVT::f64) ? RTLIB::OLT_F64 :
          (VT == MVT::f128) ? RTLIB::OLT_F128 : RTLIB::OLT_PPCF128;
    break;
  case ISD::SETLE:
  case ISD::SETOLE:
    LC1 = (VT == MVT::f32) ? RTLIB::OLE_F32 :
          (VT == MVT::f64) ? RTLIB::OLE_F64 :
          (VT == MVT::f128) ? RTLIB::OLE_F128 : RTLIB::OLE_PPCF128;
    break;
  case ISD::SETGT:
  case ISD::SETOGT:
    LC1 = (VT == MVT::f32) ? RTLIB::OGT_F32 :
          (VT == MVT::f64) ? RTLIB::OGT_F64 :
          (VT == MVT::f128) ? RTLIB::OGT_F128 : RTLIB::OGT_PPCF128;
    break;
  case ISD::SETO:
    ShouldInvertCC = true;
    [[fallthrough]];
  case ISD::SETUO:
    LC1 = (VT == MVT::f32) ? RTLIB::UO_F32 :
          (VT == MVT::f64) ? RTLIB::UO_F64 :
          (VT == MVT::f128) ? RTLIB::UO_F128 : RTLIB::UO_PPCF128;
    break;
  case ISD::SETONE:
    // SETONE = O && UNE
    ShouldInvertCC = true;
    [[fallthrough]];
  case ISD::SETUEQ:
    LC1 = (VT == MVT::f32) ? RTLIB::UO_F32 :
          (VT == MVT::f64) ? RTLIB::UO_F64 :
          (VT == MVT::f128) ? RTLIB::UO_F128 : RTLIB::UO_PPCF128;
    LC2 = (VT == MVT::f32) ? RTLIB::OEQ_F32 :
          (VT == MVT::f64) ? RTLIB::OEQ_F64 :
          (VT == MVT::f128) ? RTLIB::OEQ_F128 : RTLIB::OEQ_PPCF128;
    break;
  default:
    // Invert CC for unordered comparisons
    ShouldInvertCC = true;
    switch (CCCode) {
    case ISD::SETULT:
      LC1 = (VT == MVT::f32) ? RTLIB::OGE_F32 :
            (VT == MVT::f64) ? RTLIB::OGE_F64 :
            (VT == MVT::f128) ? RTLIB::OGE_F128 : RTLIB::OGE_PPCF128;
      break;
    case ISD::SETULE:
      LC1 = (VT == MVT::f32) ? RTLIB::OGT_F32 :
            (VT == MVT::f64) ? RTLIB::OGT_F64 :
            (VT == MVT::f128) ? RTLIB::OGT_F128 : RTLIB::OGT_PPCF128;
      break;
    case ISD::SETUGT:
      LC1 = (VT == MVT::f32) ? RTLIB::OLE_F32 :
            (VT == MVT::f64) ? RTLIB::OLE_F64 :
            (VT == MVT::f128) ? RTLIB::OLE_F128 : RTLIB::OLE_PPCF128;
      break;
    case ISD::SETUGE:
      LC1 = (VT == MVT::f32) ? RTLIB::OLT_F32 :
            (VT == MVT::f64) ? RTLIB::OLT_F64 :
            (VT == MVT::f128) ? RTLIB::OLT_F128 : RTLIB::OLT_PPCF128;
      break;
    default: llvm_unreachable("Do not know how to soften this setcc!");
    }
  }

  // Use the target specific return value for comparison lib calls.
  EVT RetVT = getCmpLibcallReturnType();
  SDValue Ops[2] = {NewLHS, NewRHS};
  TargetLowering::MakeLibCallOptions CallOptions;
  EVT OpsVT[2] = { OldLHS.getValueType(),
                   OldRHS.getValueType() };
  CallOptions.setTypeListBeforeSoften(OpsVT, RetVT, true);
  auto Call = makeLibCall(DAG, LC1, RetVT, Ops, CallOptions, dl, Chain);
  NewLHS = Call.first;
  NewRHS = DAG.getConstant(0, dl, RetVT);

  CCCode = getCmpLibcallCC(LC1);
  if (ShouldInvertCC) {
    assert(RetVT.isInteger());
    CCCode = getSetCCInverse(CCCode, RetVT);
  }

  if (LC2 == RTLIB::UNKNOWN_LIBCALL) {
    // Update Chain.
    Chain = Call.second;
  } else {
    EVT SetCCVT =
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), RetVT);
    SDValue Tmp = DAG.getSetCC(dl, SetCCVT, NewLHS, NewRHS, CCCode);
    auto Call2 = makeLibCall(DAG, LC2, RetVT, Ops, CallOptions, dl, Chain);
    CCCode = getCmpLibcallCC(LC2);
    if (ShouldInvertCC)
      CCCode = getSetCCInverse(CCCode, RetVT);
    NewLHS = DAG.getSetCC(dl, SetCCVT, Call2.first, NewRHS, CCCode);
    if (Chain)
      Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Call.second,
                          Call2.second);
    NewLHS = DAG.getNode(ShouldInvertCC ? ISD::AND : ISD::OR, dl,
                         Tmp.getValueType(), Tmp, NewLHS);
    NewRHS = SDValue();
  }
}

/// Return the entry encoding for a jump table in the current function. The
/// returned value is a member of the MachineJumpTableInfo::JTEntryKind enum.
unsigned TargetLowering::getJumpTableEncoding() const {
  // In non-pic modes, just use the address of a block.
  if (!isPositionIndependent())
    return MachineJumpTableInfo::EK_BlockAddress;

  // In PIC mode, if the target supports a GPRel32 directive, use it.
  if (getTargetMachine().getMCAsmInfo()->getGPRel32Directive() != nullptr)
    return MachineJumpTableInfo::EK_GPRel32BlockAddress;

  // Otherwise, use a label difference.
  return MachineJumpTableInfo::EK_LabelDifference32;
}

SDValue TargetLowering::getPICJumpTableRelocBase(SDValue Table,
                                                 SelectionDAG &DAG) const {
  // If our PIC model is GP relative, use the global offset table as the base.
  unsigned JTEncoding = getJumpTableEncoding();

  if ((JTEncoding == MachineJumpTableInfo::EK_GPRel64BlockAddress) ||
      (JTEncoding == MachineJumpTableInfo::EK_GPRel32BlockAddress))
    return DAG.getGLOBAL_OFFSET_TABLE(getPointerTy(DAG.getDataLayout()));

  return Table;
}

/// This returns the relocation base for the given PIC jumptable, the same as
/// getPICJumpTableRelocBase, but as an MCExpr.
const MCExpr *
TargetLowering::getPICJumpTableRelocBaseExpr(const MachineFunction *MF,
                                             unsigned JTI,MCContext &Ctx) const{
  // The normal PIC reloc base is the label at the start of the jump table.
  return MCSymbolRefExpr::create(MF->getJTISymbol(JTI, Ctx), Ctx);
}

SDValue TargetLowering::expandIndirectJTBranch(const SDLoc &dl, SDValue Value,
                                               SDValue Addr, int JTI,
                                               SelectionDAG &DAG) const {
  SDValue Chain = Value;
  // Jump table debug info is only needed if CodeView is enabled.
  if (DAG.getTarget().getTargetTriple().isOSBinFormatCOFF()) {
    Chain = DAG.getJumpTableDebugInfo(JTI, Chain, dl);
  }
  return DAG.getNode(ISD::BRIND, dl, MVT::Other, Chain, Addr);
}

bool
TargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  const TargetMachine &TM = getTargetMachine();
  const GlobalValue *GV = GA->getGlobal();

  // If the address is not even local to this DSO we will have to load it from
  // a got and then add the offset.
  if (!TM.shouldAssumeDSOLocal(GV))
    return false;

  // If the code is position independent we will have to add a base register.
  if (isPositionIndependent())
    return false;

  // Otherwise we can do it.
  return true;
}

//===----------------------------------------------------------------------===//
//  Optimization Methods
//===----------------------------------------------------------------------===//

/// If the specified instruction has a constant integer operand and there are
/// bits set in that constant that are not demanded, then clear those bits and
/// return true.
bool TargetLowering::ShrinkDemandedConstant(SDValue Op,
                                            const APInt &DemandedBits,
                                            const APInt &DemandedElts,
                                            TargetLoweringOpt &TLO) const {
  SDLoc DL(Op);
  unsigned Opcode = Op.getOpcode();

  // Early-out if we've ended up calling an undemanded node, leave this to
  // constant folding.
  if (DemandedBits.isZero() || DemandedElts.isZero())
    return false;

  // Do target-specific constant optimization.
  if (targetShrinkDemandedConstant(Op, DemandedBits, DemandedElts, TLO))
    return TLO.New.getNode();

  // FIXME: ISD::SELECT, ISD::SELECT_CC
  switch (Opcode) {
  default:
    break;
  case ISD::XOR:
  case ISD::AND:
  case ISD::OR: {
    auto *Op1C = dyn_cast<ConstantSDNode>(Op.getOperand(1));
    if (!Op1C || Op1C->isOpaque())
      return false;

    // If this is a 'not' op, don't touch it because that's a canonical form.
    const APInt &C = Op1C->getAPIntValue();
    if (Opcode == ISD::XOR && DemandedBits.isSubsetOf(C))
      return false;

    if (!C.isSubsetOf(DemandedBits)) {
      EVT VT = Op.getValueType();
      SDValue NewC = TLO.DAG.getConstant(DemandedBits & C, DL, VT);
      SDValue NewOp = TLO.DAG.getNode(Opcode, DL, VT, Op.getOperand(0), NewC,
                                      Op->getFlags());
      return TLO.CombineTo(Op, NewOp);
    }

    break;
  }
  }

  return false;
}

bool TargetLowering::ShrinkDemandedConstant(SDValue Op,
                                            const APInt &DemandedBits,
                                            TargetLoweringOpt &TLO) const {
  EVT VT = Op.getValueType();
  APInt DemandedElts = VT.isVector()
                           ? APInt::getAllOnes(VT.getVectorNumElements())
                           : APInt(1, 1);
  return ShrinkDemandedConstant(Op, DemandedBits, DemandedElts, TLO);
}

/// Convert x+y to (VT)((SmallVT)x+(SmallVT)y) if the casts are free.
/// This uses isTruncateFree/isZExtFree and ANY_EXTEND for the widening cast,
/// but it could be generalized for targets with other types of implicit
/// widening casts.
bool TargetLowering::ShrinkDemandedOp(SDValue Op, unsigned BitWidth,
                                      const APInt &DemandedBits,
                                      TargetLoweringOpt &TLO) const {
  assert(Op.getNumOperands() == 2 &&
         "ShrinkDemandedOp only supports binary operators!");
  assert(Op.getNode()->getNumValues() == 1 &&
         "ShrinkDemandedOp only supports nodes with one result!");

  EVT VT = Op.getValueType();
  SelectionDAG &DAG = TLO.DAG;
  SDLoc dl(Op);

  // Early return, as this function cannot handle vector types.
  if (VT.isVector())
    return false;

  assert(Op.getOperand(0).getValueType().getScalarSizeInBits() == BitWidth &&
         Op.getOperand(1).getValueType().getScalarSizeInBits() == BitWidth &&
         "ShrinkDemandedOp only supports operands that have the same size!");

  // Don't do this if the node has another user, which may require the
  // full value.
  if (!Op.getNode()->hasOneUse())
    return false;

  // Search for the smallest integer type with free casts to and from
  // Op's type. For expedience, just check power-of-2 integer types.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  unsigned DemandedSize = DemandedBits.getActiveBits();
  for (unsigned SmallVTBits = llvm::bit_ceil(DemandedSize);
       SmallVTBits < BitWidth; SmallVTBits = NextPowerOf2(SmallVTBits)) {
    EVT SmallVT = EVT::getIntegerVT(*DAG.getContext(), SmallVTBits);
    if (TLI.isTruncateFree(VT, SmallVT) && TLI.isZExtFree(SmallVT, VT)) {
      // We found a type with free casts.
      SDValue X = DAG.getNode(
          Op.getOpcode(), dl, SmallVT,
          DAG.getNode(ISD::TRUNCATE, dl, SmallVT, Op.getOperand(0)),
          DAG.getNode(ISD::TRUNCATE, dl, SmallVT, Op.getOperand(1)));
      assert(DemandedSize <= SmallVTBits && "Narrowed below demanded bits?");
      SDValue Z = DAG.getNode(ISD::ANY_EXTEND, dl, VT, X);
      return TLO.CombineTo(Op, Z);
    }
  }
  return false;
}

bool TargetLowering::SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                                          DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  TargetLoweringOpt TLO(DAG, !DCI.isBeforeLegalize(),
                        !DCI.isBeforeLegalizeOps());
  KnownBits Known;

  bool Simplified = SimplifyDemandedBits(Op, DemandedBits, Known, TLO);
  if (Simplified) {
    DCI.AddToWorklist(Op.getNode());
    DCI.CommitTargetLoweringOpt(TLO);
  }
  return Simplified;
}

bool TargetLowering::SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                                          const APInt &DemandedElts,
                                          DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  TargetLoweringOpt TLO(DAG, !DCI.isBeforeLegalize(),
                        !DCI.isBeforeLegalizeOps());
  KnownBits Known;

  bool Simplified =
      SimplifyDemandedBits(Op, DemandedBits, DemandedElts, Known, TLO);
  if (Simplified) {
    DCI.AddToWorklist(Op.getNode());
    DCI.CommitTargetLoweringOpt(TLO);
  }
  return Simplified;
}

bool TargetLowering::SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                                          KnownBits &Known,
                                          TargetLoweringOpt &TLO,
                                          unsigned Depth,
                                          bool AssumeSingleUse) const {
  EVT VT = Op.getValueType();

  // Since the number of lanes in a scalable vector is unknown at compile time,
  // we track one bit which is implicitly broadcast to all lanes.  This means
  // that all lanes in a scalable vector are considered demanded.
  APInt DemandedElts = VT.isFixedLengthVector()
                           ? APInt::getAllOnes(VT.getVectorNumElements())
                           : APInt(1, 1);
  return SimplifyDemandedBits(Op, DemandedBits, DemandedElts, Known, TLO, Depth,
                              AssumeSingleUse);
}

// TODO: Under what circumstances can we create nodes? Constant folding?
SDValue TargetLowering::SimplifyMultipleUseDemandedBits(
    SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
    SelectionDAG &DAG, unsigned Depth) const {
  EVT VT = Op.getValueType();

  // Limit search depth.
  if (Depth >= SelectionDAG::MaxRecursionDepth)
    return SDValue();

  // Ignore UNDEFs.
  if (Op.isUndef())
    return SDValue();

  // Not demanding any bits/elts from Op.
  if (DemandedBits == 0 || DemandedElts == 0)
    return DAG.getUNDEF(VT);

  bool IsLE = DAG.getDataLayout().isLittleEndian();
  unsigned NumElts = DemandedElts.getBitWidth();
  unsigned BitWidth = DemandedBits.getBitWidth();
  KnownBits LHSKnown, RHSKnown;
  switch (Op.getOpcode()) {
  case ISD::BITCAST: {
    if (VT.isScalableVector())
      return SDValue();

    SDValue Src = peekThroughBitcasts(Op.getOperand(0));
    EVT SrcVT = Src.getValueType();
    EVT DstVT = Op.getValueType();
    if (SrcVT == DstVT)
      return Src;

    unsigned NumSrcEltBits = SrcVT.getScalarSizeInBits();
    unsigned NumDstEltBits = DstVT.getScalarSizeInBits();
    if (NumSrcEltBits == NumDstEltBits)
      if (SDValue V = SimplifyMultipleUseDemandedBits(
              Src, DemandedBits, DemandedElts, DAG, Depth + 1))
        return DAG.getBitcast(DstVT, V);

    if (SrcVT.isVector() && (NumDstEltBits % NumSrcEltBits) == 0) {
      unsigned Scale = NumDstEltBits / NumSrcEltBits;
      unsigned NumSrcElts = SrcVT.getVectorNumElements();
      APInt DemandedSrcBits = APInt::getZero(NumSrcEltBits);
      APInt DemandedSrcElts = APInt::getZero(NumSrcElts);
      for (unsigned i = 0; i != Scale; ++i) {
        unsigned EltOffset = IsLE ? i : (Scale - 1 - i);
        unsigned BitOffset = EltOffset * NumSrcEltBits;
        APInt Sub = DemandedBits.extractBits(NumSrcEltBits, BitOffset);
        if (!Sub.isZero()) {
          DemandedSrcBits |= Sub;
          for (unsigned j = 0; j != NumElts; ++j)
            if (DemandedElts[j])
              DemandedSrcElts.setBit((j * Scale) + i);
        }
      }

      if (SDValue V = SimplifyMultipleUseDemandedBits(
              Src, DemandedSrcBits, DemandedSrcElts, DAG, Depth + 1))
        return DAG.getBitcast(DstVT, V);
    }

    // TODO - bigendian once we have test coverage.
    if (IsLE && (NumSrcEltBits % NumDstEltBits) == 0) {
      unsigned Scale = NumSrcEltBits / NumDstEltBits;
      unsigned NumSrcElts = SrcVT.isVector() ? SrcVT.getVectorNumElements() : 1;
      APInt DemandedSrcBits = APInt::getZero(NumSrcEltBits);
      APInt DemandedSrcElts = APInt::getZero(NumSrcElts);
      for (unsigned i = 0; i != NumElts; ++i)
        if (DemandedElts[i]) {
          unsigned Offset = (i % Scale) * NumDstEltBits;
          DemandedSrcBits.insertBits(DemandedBits, Offset);
          DemandedSrcElts.setBit(i / Scale);
        }

      if (SDValue V = SimplifyMultipleUseDemandedBits(
              Src, DemandedSrcBits, DemandedSrcElts, DAG, Depth + 1))
        return DAG.getBitcast(DstVT, V);
    }

    break;
  }
  case ISD::FREEZE: {
    SDValue N0 = Op.getOperand(0);
    if (DAG.isGuaranteedNotToBeUndefOrPoison(N0, DemandedElts,
                                             /*PoisonOnly=*/false))
      return N0;
    break;
  }
  case ISD::AND: {
    LHSKnown = DAG.computeKnownBits(Op.getOperand(0), DemandedElts, Depth + 1);
    RHSKnown = DAG.computeKnownBits(Op.getOperand(1), DemandedElts, Depth + 1);

    // If all of the demanded bits are known 1 on one side, return the other.
    // These bits cannot contribute to the result of the 'and' in this
    // context.
    if (DemandedBits.isSubsetOf(LHSKnown.Zero | RHSKnown.One))
      return Op.getOperand(0);
    if (DemandedBits.isSubsetOf(RHSKnown.Zero | LHSKnown.One))
      return Op.getOperand(1);
    break;
  }
  case ISD::OR: {
    LHSKnown = DAG.computeKnownBits(Op.getOperand(0), DemandedElts, Depth + 1);
    RHSKnown = DAG.computeKnownBits(Op.getOperand(1), DemandedElts, Depth + 1);

    // If all of the demanded bits are known zero on one side, return the
    // other.  These bits cannot contribute to the result of the 'or' in this
    // context.
    if (DemandedBits.isSubsetOf(LHSKnown.One | RHSKnown.Zero))
      return Op.getOperand(0);
    if (DemandedBits.isSubsetOf(RHSKnown.One | LHSKnown.Zero))
      return Op.getOperand(1);
    break;
  }
  case ISD::XOR: {
    LHSKnown = DAG.computeKnownBits(Op.getOperand(0), DemandedElts, Depth + 1);
    RHSKnown = DAG.computeKnownBits(Op.getOperand(1), DemandedElts, Depth + 1);

    // If all of the demanded bits are known zero on one side, return the
    // other.
    if (DemandedBits.isSubsetOf(RHSKnown.Zero))
      return Op.getOperand(0);
    if (DemandedBits.isSubsetOf(LHSKnown.Zero))
      return Op.getOperand(1);
    break;
  }
  case ISD::SHL: {
    // If we are only demanding sign bits then we can use the shift source
    // directly.
    if (std::optional<uint64_t> MaxSA =
            DAG.getValidMaximumShiftAmount(Op, DemandedElts, Depth + 1)) {
      SDValue Op0 = Op.getOperand(0);
      unsigned ShAmt = *MaxSA;
      unsigned NumSignBits =
          DAG.ComputeNumSignBits(Op0, DemandedElts, Depth + 1);
      unsigned UpperDemandedBits = BitWidth - DemandedBits.countr_zero();
      if (NumSignBits > ShAmt && (NumSignBits - ShAmt) >= (UpperDemandedBits))
        return Op0;
    }
    break;
  }
  case ISD::SETCC: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
    // If (1) we only need the sign-bit, (2) the setcc operands are the same
    // width as the setcc result, and (3) the result of a setcc conforms to 0 or
    // -1, we may be able to bypass the setcc.
    if (DemandedBits.isSignMask() &&
        Op0.getScalarValueSizeInBits() == BitWidth &&
        getBooleanContents(Op0.getValueType()) ==
            BooleanContent::ZeroOrNegativeOneBooleanContent) {
      // If we're testing X < 0, then this compare isn't needed - just use X!
      // FIXME: We're limiting to integer types here, but this should also work
      // if we don't care about FP signed-zero. The use of SETLT with FP means
      // that we don't care about NaNs.
      if (CC == ISD::SETLT && Op1.getValueType().isInteger() &&
          (isNullConstant(Op1) || ISD::isBuildVectorAllZeros(Op1.getNode())))
        return Op0;
    }
    break;
  }
  case ISD::SIGN_EXTEND_INREG: {
    // If none of the extended bits are demanded, eliminate the sextinreg.
    SDValue Op0 = Op.getOperand(0);
    EVT ExVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
    unsigned ExBits = ExVT.getScalarSizeInBits();
    if (DemandedBits.getActiveBits() <= ExBits &&
        shouldRemoveRedundantExtend(Op))
      return Op0;
    // If the input is already sign extended, just drop the extension.
    unsigned NumSignBits = DAG.ComputeNumSignBits(Op0, DemandedElts, Depth + 1);
    if (NumSignBits >= (BitWidth - ExBits + 1))
      return Op0;
    break;
  }
  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG: {
    if (VT.isScalableVector())
      return SDValue();

    // If we only want the lowest element and none of extended bits, then we can
    // return the bitcasted source vector.
    SDValue Src = Op.getOperand(0);
    EVT SrcVT = Src.getValueType();
    EVT DstVT = Op.getValueType();
    if (IsLE && DemandedElts == 1 &&
        DstVT.getSizeInBits() == SrcVT.getSizeInBits() &&
        DemandedBits.getActiveBits() <= SrcVT.getScalarSizeInBits()) {
      return DAG.getBitcast(DstVT, Src);
    }
    break;
  }
  case ISD::INSERT_VECTOR_ELT: {
    if (VT.isScalableVector())
      return SDValue();

    // If we don't demand the inserted element, return the base vector.
    SDValue Vec = Op.getOperand(0);
    auto *CIdx = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    EVT VecVT = Vec.getValueType();
    if (CIdx && CIdx->getAPIntValue().ult(VecVT.getVectorNumElements()) &&
        !DemandedElts[CIdx->getZExtValue()])
      return Vec;
    break;
  }
  case ISD::INSERT_SUBVECTOR: {
    if (VT.isScalableVector())
      return SDValue();

    SDValue Vec = Op.getOperand(0);
    SDValue Sub = Op.getOperand(1);
    uint64_t Idx = Op.getConstantOperandVal(2);
    unsigned NumSubElts = Sub.getValueType().getVectorNumElements();
    APInt DemandedSubElts = DemandedElts.extractBits(NumSubElts, Idx);
    // If we don't demand the inserted subvector, return the base vector.
    if (DemandedSubElts == 0)
      return Vec;
    break;
  }
  case ISD::VECTOR_SHUFFLE: {
    assert(!VT.isScalableVector());
    ArrayRef<int> ShuffleMask = cast<ShuffleVectorSDNode>(Op)->getMask();

    // If all the demanded elts are from one operand and are inline,
    // then we can use the operand directly.
    bool AllUndef = true, IdentityLHS = true, IdentityRHS = true;
    for (unsigned i = 0; i != NumElts; ++i) {
      int M = ShuffleMask[i];
      if (M < 0 || !DemandedElts[i])
        continue;
      AllUndef = false;
      IdentityLHS &= (M == (int)i);
      IdentityRHS &= ((M - NumElts) == i);
    }

    if (AllUndef)
      return DAG.getUNDEF(Op.getValueType());
    if (IdentityLHS)
      return Op.getOperand(0);
    if (IdentityRHS)
      return Op.getOperand(1);
    break;
  }
  default:
    // TODO: Probably okay to remove after audit; here to reduce change size
    // in initial enablement patch for scalable vectors
    if (VT.isScalableVector())
      return SDValue();

    if (Op.getOpcode() >= ISD::BUILTIN_OP_END)
      if (SDValue V = SimplifyMultipleUseDemandedBitsForTargetNode(
              Op, DemandedBits, DemandedElts, DAG, Depth))
        return V;
    break;
  }
  return SDValue();
}

SDValue TargetLowering::SimplifyMultipleUseDemandedBits(
    SDValue Op, const APInt &DemandedBits, SelectionDAG &DAG,
    unsigned Depth) const {
  EVT VT = Op.getValueType();
  // Since the number of lanes in a scalable vector is unknown at compile time,
  // we track one bit which is implicitly broadcast to all lanes.  This means
  // that all lanes in a scalable vector are considered demanded.
  APInt DemandedElts = VT.isFixedLengthVector()
                           ? APInt::getAllOnes(VT.getVectorNumElements())
                           : APInt(1, 1);
  return SimplifyMultipleUseDemandedBits(Op, DemandedBits, DemandedElts, DAG,
                                         Depth);
}

SDValue TargetLowering::SimplifyMultipleUseDemandedVectorElts(
    SDValue Op, const APInt &DemandedElts, SelectionDAG &DAG,
    unsigned Depth) const {
  APInt DemandedBits = APInt::getAllOnes(Op.getScalarValueSizeInBits());
  return SimplifyMultipleUseDemandedBits(Op, DemandedBits, DemandedElts, DAG,
                                         Depth);
}

// Attempt to form ext(avgfloor(A, B)) from shr(add(ext(A), ext(B)), 1).
//      or to form ext(avgceil(A, B)) from shr(add(ext(A), ext(B), 1), 1).
static SDValue combineShiftToAVG(SDValue Op,
                                 TargetLowering::TargetLoweringOpt &TLO,
                                 const TargetLowering &TLI,
                                 const APInt &DemandedBits,
                                 const APInt &DemandedElts, unsigned Depth) {
  assert((Op.getOpcode() == ISD::SRL || Op.getOpcode() == ISD::SRA) &&
         "SRL or SRA node is required here!");
  // Is the right shift using an immediate value of 1?
  ConstantSDNode *N1C = isConstOrConstSplat(Op.getOperand(1), DemandedElts);
  if (!N1C || !N1C->isOne())
    return SDValue();

  // We are looking for an avgfloor
  // add(ext, ext)
  // or one of these as a avgceil
  // add(add(ext, ext), 1)
  // add(add(ext, 1), ext)
  // add(ext, add(ext, 1))
  SDValue Add = Op.getOperand(0);
  if (Add.getOpcode() != ISD::ADD)
    return SDValue();

  SDValue ExtOpA = Add.getOperand(0);
  SDValue ExtOpB = Add.getOperand(1);
  SDValue Add2;
  auto MatchOperands = [&](SDValue Op1, SDValue Op2, SDValue Op3, SDValue A) {
    ConstantSDNode *ConstOp;
    if ((ConstOp = isConstOrConstSplat(Op2, DemandedElts)) &&
        ConstOp->isOne()) {
      ExtOpA = Op1;
      ExtOpB = Op3;
      Add2 = A;
      return true;
    }
    if ((ConstOp = isConstOrConstSplat(Op3, DemandedElts)) &&
        ConstOp->isOne()) {
      ExtOpA = Op1;
      ExtOpB = Op2;
      Add2 = A;
      return true;
    }
    return false;
  };
  bool IsCeil =
      (ExtOpA.getOpcode() == ISD::ADD &&
       MatchOperands(ExtOpA.getOperand(0), ExtOpA.getOperand(1), ExtOpB, ExtOpA)) ||
      (ExtOpB.getOpcode() == ISD::ADD &&
       MatchOperands(ExtOpB.getOperand(0), ExtOpB.getOperand(1), ExtOpA, ExtOpB));

  // If the shift is signed (sra):
  //  - Needs >= 2 sign bit for both operands.
  //  - Needs >= 2 zero bits.
  // If the shift is unsigned (srl):
  //  - Needs >= 1 zero bit for both operands.
  //  - Needs 1 demanded bit zero and >= 2 sign bits.
  SelectionDAG &DAG = TLO.DAG;
  unsigned ShiftOpc = Op.getOpcode();
  bool IsSigned = false;
  unsigned KnownBits;
  unsigned NumSignedA = DAG.ComputeNumSignBits(ExtOpA, DemandedElts, Depth);
  unsigned NumSignedB = DAG.ComputeNumSignBits(ExtOpB, DemandedElts, Depth);
  unsigned NumSigned = std::min(NumSignedA, NumSignedB) - 1;
  unsigned NumZeroA =
      DAG.computeKnownBits(ExtOpA, DemandedElts, Depth).countMinLeadingZeros();
  unsigned NumZeroB =
      DAG.computeKnownBits(ExtOpB, DemandedElts, Depth).countMinLeadingZeros();
  unsigned NumZero = std::min(NumZeroA, NumZeroB);

  switch (ShiftOpc) {
  default:
    llvm_unreachable("Unexpected ShiftOpc in combineShiftToAVG");
  case ISD::SRA: {
    if (NumZero >= 2 && NumSigned < NumZero) {
      IsSigned = false;
      KnownBits = NumZero;
      break;
    }
    if (NumSigned >= 1) {
      IsSigned = true;
      KnownBits = NumSigned;
      break;
    }
    return SDValue();
  }
  case ISD::SRL: {
    if (NumZero >= 1 && NumSigned < NumZero) {
      IsSigned = false;
      KnownBits = NumZero;
      break;
    }
    if (NumSigned >= 1 && DemandedBits.isSignBitClear()) {
      IsSigned = true;
      KnownBits = NumSigned;
      break;
    }
    return SDValue();
  }
  }

  unsigned AVGOpc = IsCeil ? (IsSigned ? ISD::AVGCEILS : ISD::AVGCEILU)
                           : (IsSigned ? ISD::AVGFLOORS : ISD::AVGFLOORU);

  // Find the smallest power-2 type that is legal for this vector size and
  // operation, given the original type size and the number of known sign/zero
  // bits.
  EVT VT = Op.getValueType();
  unsigned MinWidth =
      std::max<unsigned>(VT.getScalarSizeInBits() - KnownBits, 8);
  EVT NVT = EVT::getIntegerVT(*DAG.getContext(), llvm::bit_ceil(MinWidth));
  if (NVT.getScalarSizeInBits() > VT.getScalarSizeInBits())
    return SDValue();
  if (VT.isVector())
    NVT = EVT::getVectorVT(*DAG.getContext(), NVT, VT.getVectorElementCount());
  if (TLO.LegalTypes() && !TLI.isOperationLegal(AVGOpc, NVT)) {
    // If we could not transform, and (both) adds are nuw/nsw, we can use the
    // larger type size to do the transform.
    if (TLO.LegalOperations() && !TLI.isOperationLegal(AVGOpc, VT))
      return SDValue();
    if (DAG.willNotOverflowAdd(IsSigned, Add.getOperand(0),
                               Add.getOperand(1)) &&
        (!Add2 || DAG.willNotOverflowAdd(IsSigned, Add2.getOperand(0),
                                         Add2.getOperand(1))))
      NVT = VT;
    else
      return SDValue();
  }

  // Don't create a AVGFLOOR node with a scalar constant unless its legal as
  // this is likely to stop other folds (reassociation, value tracking etc.)
  if (!IsCeil && !TLI.isOperationLegal(AVGOpc, NVT) &&
      (isa<ConstantSDNode>(ExtOpA) || isa<ConstantSDNode>(ExtOpB)))
    return SDValue();

  SDLoc DL(Op);
  SDValue ResultAVG =
      DAG.getNode(AVGOpc, DL, NVT, DAG.getExtOrTrunc(IsSigned, ExtOpA, DL, NVT),
                  DAG.getExtOrTrunc(IsSigned, ExtOpB, DL, NVT));
  return DAG.getExtOrTrunc(IsSigned, ResultAVG, DL, VT);
}

/// Look at Op. At this point, we know that only the OriginalDemandedBits of the
/// result of Op are ever used downstream. If we can use this information to
/// simplify Op, create a new simplified DAG node and return true, returning the
/// original and new nodes in Old and New. Otherwise, analyze the expression and
/// return a mask of Known bits for the expression (used to simplify the
/// caller).  The Known bits may only be accurate for those bits in the
/// OriginalDemandedBits and OriginalDemandedElts.
bool TargetLowering::SimplifyDemandedBits(
    SDValue Op, const APInt &OriginalDemandedBits,
    const APInt &OriginalDemandedElts, KnownBits &Known, TargetLoweringOpt &TLO,
    unsigned Depth, bool AssumeSingleUse) const {
  unsigned BitWidth = OriginalDemandedBits.getBitWidth();
  assert(Op.getScalarValueSizeInBits() == BitWidth &&
         "Mask size mismatches value type size!");

  // Don't know anything.
  Known = KnownBits(BitWidth);

  EVT VT = Op.getValueType();
  bool IsLE = TLO.DAG.getDataLayout().isLittleEndian();
  unsigned NumElts = OriginalDemandedElts.getBitWidth();
  assert((!VT.isFixedLengthVector() || NumElts == VT.getVectorNumElements()) &&
         "Unexpected vector size");

  APInt DemandedBits = OriginalDemandedBits;
  APInt DemandedElts = OriginalDemandedElts;
  SDLoc dl(Op);

  // Undef operand.
  if (Op.isUndef())
    return false;

  // We can't simplify target constants.
  if (Op.getOpcode() == ISD::TargetConstant)
    return false;

  if (Op.getOpcode() == ISD::Constant) {
    // We know all of the bits for a constant!
    Known = KnownBits::makeConstant(Op->getAsAPIntVal());
    return false;
  }

  if (Op.getOpcode() == ISD::ConstantFP) {
    // We know all of the bits for a floating point constant!
    Known = KnownBits::makeConstant(
        cast<ConstantFPSDNode>(Op)->getValueAPF().bitcastToAPInt());
    return false;
  }

  // Other users may use these bits.
  bool HasMultiUse = false;
  if (!AssumeSingleUse && !Op.getNode()->hasOneUse()) {
    if (Depth >= SelectionDAG::MaxRecursionDepth) {
      // Limit search depth.
      return false;
    }
    // Allow multiple uses, just set the DemandedBits/Elts to all bits.
    DemandedBits = APInt::getAllOnes(BitWidth);
    DemandedElts = APInt::getAllOnes(NumElts);
    HasMultiUse = true;
  } else if (OriginalDemandedBits == 0 || OriginalDemandedElts == 0) {
    // Not demanding any bits/elts from Op.
    return TLO.CombineTo(Op, TLO.DAG.getUNDEF(VT));
  } else if (Depth >= SelectionDAG::MaxRecursionDepth) {
    // Limit search depth.
    return false;
  }

  KnownBits Known2;
  switch (Op.getOpcode()) {
  case ISD::SCALAR_TO_VECTOR: {
    if (VT.isScalableVector())
      return false;
    if (!DemandedElts[0])
      return TLO.CombineTo(Op, TLO.DAG.getUNDEF(VT));

    KnownBits SrcKnown;
    SDValue Src = Op.getOperand(0);
    unsigned SrcBitWidth = Src.getScalarValueSizeInBits();
    APInt SrcDemandedBits = DemandedBits.zext(SrcBitWidth);
    if (SimplifyDemandedBits(Src, SrcDemandedBits, SrcKnown, TLO, Depth + 1))
      return true;

    // Upper elements are undef, so only get the knownbits if we just demand
    // the bottom element.
    if (DemandedElts == 1)
      Known = SrcKnown.anyextOrTrunc(BitWidth);
    break;
  }
  case ISD::BUILD_VECTOR:
    // Collect the known bits that are shared by every demanded element.
    // TODO: Call SimplifyDemandedBits for non-constant demanded elements.
    Known = TLO.DAG.computeKnownBits(Op, DemandedElts, Depth);
    return false; // Don't fall through, will infinitely loop.
  case ISD::SPLAT_VECTOR: {
    SDValue Scl = Op.getOperand(0);
    APInt DemandedSclBits = DemandedBits.zextOrTrunc(Scl.getValueSizeInBits());
    KnownBits KnownScl;
    if (SimplifyDemandedBits(Scl, DemandedSclBits, KnownScl, TLO, Depth + 1))
      return true;

    // Implicitly truncate the bits to match the official semantics of
    // SPLAT_VECTOR.
    Known = KnownScl.trunc(BitWidth);
    break;
  }
  case ISD::LOAD: {
    auto *LD = cast<LoadSDNode>(Op);
    if (getTargetConstantFromLoad(LD)) {
      Known = TLO.DAG.computeKnownBits(Op, DemandedElts, Depth);
      return false; // Don't fall through, will infinitely loop.
    }
    if (ISD::isZEXTLoad(Op.getNode()) && Op.getResNo() == 0) {
      // If this is a ZEXTLoad and we are looking at the loaded value.
      EVT MemVT = LD->getMemoryVT();
      unsigned MemBits = MemVT.getScalarSizeInBits();
      Known.Zero.setBitsFrom(MemBits);
      return false; // Don't fall through, will infinitely loop.
    }
    break;
  }
  case ISD::INSERT_VECTOR_ELT: {
    if (VT.isScalableVector())
      return false;
    SDValue Vec = Op.getOperand(0);
    SDValue Scl = Op.getOperand(1);
    auto *CIdx = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    EVT VecVT = Vec.getValueType();

    // If index isn't constant, assume we need all vector elements AND the
    // inserted element.
    APInt DemandedVecElts(DemandedElts);
    if (CIdx && CIdx->getAPIntValue().ult(VecVT.getVectorNumElements())) {
      unsigned Idx = CIdx->getZExtValue();
      DemandedVecElts.clearBit(Idx);

      // Inserted element is not required.
      if (!DemandedElts[Idx])
        return TLO.CombineTo(Op, Vec);
    }

    KnownBits KnownScl;
    unsigned NumSclBits = Scl.getScalarValueSizeInBits();
    APInt DemandedSclBits = DemandedBits.zextOrTrunc(NumSclBits);
    if (SimplifyDemandedBits(Scl, DemandedSclBits, KnownScl, TLO, Depth + 1))
      return true;

    Known = KnownScl.anyextOrTrunc(BitWidth);

    KnownBits KnownVec;
    if (SimplifyDemandedBits(Vec, DemandedBits, DemandedVecElts, KnownVec, TLO,
                             Depth + 1))
      return true;

    if (!!DemandedVecElts)
      Known = Known.intersectWith(KnownVec);

    return false;
  }
  case ISD::INSERT_SUBVECTOR: {
    if (VT.isScalableVector())
      return false;
    // Demand any elements from the subvector and the remainder from the src its
    // inserted into.
    SDValue Src = Op.getOperand(0);
    SDValue Sub = Op.getOperand(1);
    uint64_t Idx = Op.getConstantOperandVal(2);
    unsigned NumSubElts = Sub.getValueType().getVectorNumElements();
    APInt DemandedSubElts = DemandedElts.extractBits(NumSubElts, Idx);
    APInt DemandedSrcElts = DemandedElts;
    DemandedSrcElts.insertBits(APInt::getZero(NumSubElts), Idx);

    KnownBits KnownSub, KnownSrc;
    if (SimplifyDemandedBits(Sub, DemandedBits, DemandedSubElts, KnownSub, TLO,
                             Depth + 1))
      return true;
    if (SimplifyDemandedBits(Src, DemandedBits, DemandedSrcElts, KnownSrc, TLO,
                             Depth + 1))
      return true;

    Known.Zero.setAllBits();
    Known.One.setAllBits();
    if (!!DemandedSubElts)
      Known = Known.intersectWith(KnownSub);
    if (!!DemandedSrcElts)
      Known = Known.intersectWith(KnownSrc);

    // Attempt to avoid multi-use src if we don't need anything from it.
    if (!DemandedBits.isAllOnes() || !DemandedSubElts.isAllOnes() ||
        !DemandedSrcElts.isAllOnes()) {
      SDValue NewSub = SimplifyMultipleUseDemandedBits(
          Sub, DemandedBits, DemandedSubElts, TLO.DAG, Depth + 1);
      SDValue NewSrc = SimplifyMultipleUseDemandedBits(
          Src, DemandedBits, DemandedSrcElts, TLO.DAG, Depth + 1);
      if (NewSub || NewSrc) {
        NewSub = NewSub ? NewSub : Sub;
        NewSrc = NewSrc ? NewSrc : Src;
        SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), dl, VT, NewSrc, NewSub,
                                        Op.getOperand(2));
        return TLO.CombineTo(Op, NewOp);
      }
    }
    break;
  }
  case ISD::EXTRACT_SUBVECTOR: {
    if (VT.isScalableVector())
      return false;
    // Offset the demanded elts by the subvector index.
    SDValue Src = Op.getOperand(0);
    if (Src.getValueType().isScalableVector())
      break;
    uint64_t Idx = Op.getConstantOperandVal(1);
    unsigned NumSrcElts = Src.getValueType().getVectorNumElements();
    APInt DemandedSrcElts = DemandedElts.zext(NumSrcElts).shl(Idx);

    if (SimplifyDemandedBits(Src, DemandedBits, DemandedSrcElts, Known, TLO,
                             Depth + 1))
      return true;

    // Attempt to avoid multi-use src if we don't need anything from it.
    if (!DemandedBits.isAllOnes() || !DemandedSrcElts.isAllOnes()) {
      SDValue DemandedSrc = SimplifyMultipleUseDemandedBits(
          Src, DemandedBits, DemandedSrcElts, TLO.DAG, Depth + 1);
      if (DemandedSrc) {
        SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), dl, VT, DemandedSrc,
                                        Op.getOperand(1));
        return TLO.CombineTo(Op, NewOp);
      }
    }
    break;
  }
  case ISD::CONCAT_VECTORS: {
    if (VT.isScalableVector())
      return false;
    Known.Zero.setAllBits();
    Known.One.setAllBits();
    EVT SubVT = Op.getOperand(0).getValueType();
    unsigned NumSubVecs = Op.getNumOperands();
    unsigned NumSubElts = SubVT.getVectorNumElements();
    for (unsigned i = 0; i != NumSubVecs; ++i) {
      APInt DemandedSubElts =
          DemandedElts.extractBits(NumSubElts, i * NumSubElts);
      if (SimplifyDemandedBits(Op.getOperand(i), DemandedBits, DemandedSubElts,
                               Known2, TLO, Depth + 1))
        return true;
      // Known bits are shared by every demanded subvector element.
      if (!!DemandedSubElts)
        Known = Known.intersectWith(Known2);
    }
    break;
  }
  case ISD::VECTOR_SHUFFLE: {
    assert(!VT.isScalableVector());
    ArrayRef<int> ShuffleMask = cast<ShuffleVectorSDNode>(Op)->getMask();

    // Collect demanded elements from shuffle operands..
    APInt DemandedLHS, DemandedRHS;
    if (!getShuffleDemandedElts(NumElts, ShuffleMask, DemandedElts, DemandedLHS,
                                DemandedRHS))
      break;

    if (!!DemandedLHS || !!DemandedRHS) {
      SDValue Op0 = Op.getOperand(0);
      SDValue Op1 = Op.getOperand(1);

      Known.Zero.setAllBits();
      Known.One.setAllBits();
      if (!!DemandedLHS) {
        if (SimplifyDemandedBits(Op0, DemandedBits, DemandedLHS, Known2, TLO,
                                 Depth + 1))
          return true;
        Known = Known.intersectWith(Known2);
      }
      if (!!DemandedRHS) {
        if (SimplifyDemandedBits(Op1, DemandedBits, DemandedRHS, Known2, TLO,
                                 Depth + 1))
          return true;
        Known = Known.intersectWith(Known2);
      }

      // Attempt to avoid multi-use ops if we don't need anything from them.
      SDValue DemandedOp0 = SimplifyMultipleUseDemandedBits(
          Op0, DemandedBits, DemandedLHS, TLO.DAG, Depth + 1);
      SDValue DemandedOp1 = SimplifyMultipleUseDemandedBits(
          Op1, DemandedBits, DemandedRHS, TLO.DAG, Depth + 1);
      if (DemandedOp0 || DemandedOp1) {
        Op0 = DemandedOp0 ? DemandedOp0 : Op0;
        Op1 = DemandedOp1 ? DemandedOp1 : Op1;
        SDValue NewOp = TLO.DAG.getVectorShuffle(VT, dl, Op0, Op1, ShuffleMask);
        return TLO.CombineTo(Op, NewOp);
      }
    }
    break;
  }
  case ISD::AND: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);

    // If the RHS is a constant, check to see if the LHS would be zero without
    // using the bits from the RHS.  Below, we use knowledge about the RHS to
    // simplify the LHS, here we're using information from the LHS to simplify
    // the RHS.
    if (ConstantSDNode *RHSC = isConstOrConstSplat(Op1, DemandedElts)) {
      // Do not increment Depth here; that can cause an infinite loop.
      KnownBits LHSKnown = TLO.DAG.computeKnownBits(Op0, DemandedElts, Depth);
      // If the LHS already has zeros where RHSC does, this 'and' is dead.
      if ((LHSKnown.Zero & DemandedBits) ==
          (~RHSC->getAPIntValue() & DemandedBits))
        return TLO.CombineTo(Op, Op0);

      // If any of the set bits in the RHS are known zero on the LHS, shrink
      // the constant.
      if (ShrinkDemandedConstant(Op, ~LHSKnown.Zero & DemandedBits,
                                 DemandedElts, TLO))
        return true;

      // Bitwise-not (xor X, -1) is a special case: we don't usually shrink its
      // constant, but if this 'and' is only clearing bits that were just set by
      // the xor, then this 'and' can be eliminated by shrinking the mask of
      // the xor. For example, for a 32-bit X:
      // and (xor (srl X, 31), -1), 1 --> xor (srl X, 31), 1
      if (isBitwiseNot(Op0) && Op0.hasOneUse() &&
          LHSKnown.One == ~RHSC->getAPIntValue()) {
        SDValue Xor = TLO.DAG.getNode(ISD::XOR, dl, VT, Op0.getOperand(0), Op1);
        return TLO.CombineTo(Op, Xor);
      }
    }

    // AND(INSERT_SUBVECTOR(C,X,I),M) -> INSERT_SUBVECTOR(AND(C,M),X,I)
    // iff 'C' is Undef/Constant and AND(X,M) == X (for DemandedBits).
    if (Op0.getOpcode() == ISD::INSERT_SUBVECTOR && !VT.isScalableVector() &&
        (Op0.getOperand(0).isUndef() ||
         ISD::isBuildVectorOfConstantSDNodes(Op0.getOperand(0).getNode())) &&
        Op0->hasOneUse()) {
      unsigned NumSubElts =
          Op0.getOperand(1).getValueType().getVectorNumElements();
      unsigned SubIdx = Op0.getConstantOperandVal(2);
      APInt DemandedSub =
          APInt::getBitsSet(NumElts, SubIdx, SubIdx + NumSubElts);
      KnownBits KnownSubMask =
          TLO.DAG.computeKnownBits(Op1, DemandedSub & DemandedElts, Depth + 1);
      if (DemandedBits.isSubsetOf(KnownSubMask.One)) {
        SDValue NewAnd =
            TLO.DAG.getNode(ISD::AND, dl, VT, Op0.getOperand(0), Op1);
        SDValue NewInsert =
            TLO.DAG.getNode(ISD::INSERT_SUBVECTOR, dl, VT, NewAnd,
                            Op0.getOperand(1), Op0.getOperand(2));
        return TLO.CombineTo(Op, NewInsert);
      }
    }

    if (SimplifyDemandedBits(Op1, DemandedBits, DemandedElts, Known, TLO,
                             Depth + 1))
      return true;
    if (SimplifyDemandedBits(Op0, ~Known.Zero & DemandedBits, DemandedElts,
                             Known2, TLO, Depth + 1))
      return true;

    // If all of the demanded bits are known one on one side, return the other.
    // These bits cannot contribute to the result of the 'and'.
    if (DemandedBits.isSubsetOf(Known2.Zero | Known.One))
      return TLO.CombineTo(Op, Op0);
    if (DemandedBits.isSubsetOf(Known.Zero | Known2.One))
      return TLO.CombineTo(Op, Op1);
    // If all of the demanded bits in the inputs are known zeros, return zero.
    if (DemandedBits.isSubsetOf(Known.Zero | Known2.Zero))
      return TLO.CombineTo(Op, TLO.DAG.getConstant(0, dl, VT));
    // If the RHS is a constant, see if we can simplify it.
    if (ShrinkDemandedConstant(Op, ~Known2.Zero & DemandedBits, DemandedElts,
                               TLO))
      return true;
    // If the operation can be done in a smaller type, do so.
    if (ShrinkDemandedOp(Op, BitWidth, DemandedBits, TLO))
      return true;

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (!DemandedBits.isAllOnes() || !DemandedElts.isAllOnes()) {
      SDValue DemandedOp0 = SimplifyMultipleUseDemandedBits(
          Op0, DemandedBits, DemandedElts, TLO.DAG, Depth + 1);
      SDValue DemandedOp1 = SimplifyMultipleUseDemandedBits(
          Op1, DemandedBits, DemandedElts, TLO.DAG, Depth + 1);
      if (DemandedOp0 || DemandedOp1) {
        Op0 = DemandedOp0 ? DemandedOp0 : Op0;
        Op1 = DemandedOp1 ? DemandedOp1 : Op1;
        SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), dl, VT, Op0, Op1);
        return TLO.CombineTo(Op, NewOp);
      }
    }

    Known &= Known2;
    break;
  }
  case ISD::OR: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);
    SDNodeFlags Flags = Op.getNode()->getFlags();
    if (SimplifyDemandedBits(Op1, DemandedBits, DemandedElts, Known, TLO,
                             Depth + 1)) {
      if (Flags.hasDisjoint()) {
        Flags.setDisjoint(false);
        Op->setFlags(Flags);
      }
      return true;
    }

    if (SimplifyDemandedBits(Op0, ~Known.One & DemandedBits, DemandedElts,
                             Known2, TLO, Depth + 1)) {
      if (Flags.hasDisjoint()) {
        Flags.setDisjoint(false);
        Op->setFlags(Flags);
      }
      return true;
    }

    // If all of the demanded bits are known zero on one side, return the other.
    // These bits cannot contribute to the result of the 'or'.
    if (DemandedBits.isSubsetOf(Known2.One | Known.Zero))
      return TLO.CombineTo(Op, Op0);
    if (DemandedBits.isSubsetOf(Known.One | Known2.Zero))
      return TLO.CombineTo(Op, Op1);
    // If the RHS is a constant, see if we can simplify it.
    if (ShrinkDemandedConstant(Op, DemandedBits, DemandedElts, TLO))
      return true;
    // If the operation can be done in a smaller type, do so.
    if (ShrinkDemandedOp(Op, BitWidth, DemandedBits, TLO))
      return true;

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (!DemandedBits.isAllOnes() || !DemandedElts.isAllOnes()) {
      SDValue DemandedOp0 = SimplifyMultipleUseDemandedBits(
          Op0, DemandedBits, DemandedElts, TLO.DAG, Depth + 1);
      SDValue DemandedOp1 = SimplifyMultipleUseDemandedBits(
          Op1, DemandedBits, DemandedElts, TLO.DAG, Depth + 1);
      if (DemandedOp0 || DemandedOp1) {
        Op0 = DemandedOp0 ? DemandedOp0 : Op0;
        Op1 = DemandedOp1 ? DemandedOp1 : Op1;
        SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), dl, VT, Op0, Op1);
        return TLO.CombineTo(Op, NewOp);
      }
    }

    // (or (and X, C1), (and (or X, Y), C2)) -> (or (and X, C1|C2), (and Y, C2))
    // TODO: Use SimplifyMultipleUseDemandedBits to peek through masks.
    if (Op0.getOpcode() == ISD::AND && Op1.getOpcode() == ISD::AND &&
        Op0->hasOneUse() && Op1->hasOneUse()) {
      // Attempt to match all commutations - m_c_Or would've been useful!
      for (int I = 0; I != 2; ++I) {
        SDValue X = Op.getOperand(I).getOperand(0);
        SDValue C1 = Op.getOperand(I).getOperand(1);
        SDValue Alt = Op.getOperand(1 - I).getOperand(0);
        SDValue C2 = Op.getOperand(1 - I).getOperand(1);
        if (Alt.getOpcode() == ISD::OR) {
          for (int J = 0; J != 2; ++J) {
            if (X == Alt.getOperand(J)) {
              SDValue Y = Alt.getOperand(1 - J);
              if (SDValue C12 = TLO.DAG.FoldConstantArithmetic(ISD::OR, dl, VT,
                                                               {C1, C2})) {
                SDValue MaskX = TLO.DAG.getNode(ISD::AND, dl, VT, X, C12);
                SDValue MaskY = TLO.DAG.getNode(ISD::AND, dl, VT, Y, C2);
                return TLO.CombineTo(
                    Op, TLO.DAG.getNode(ISD::OR, dl, VT, MaskX, MaskY));
              }
            }
          }
        }
      }
    }

    Known |= Known2;
    break;
  }
  case ISD::XOR: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);

    if (SimplifyDemandedBits(Op1, DemandedBits, DemandedElts, Known, TLO,
                             Depth + 1))
      return true;
    if (SimplifyDemandedBits(Op0, DemandedBits, DemandedElts, Known2, TLO,
                             Depth + 1))
      return true;

    // If all of the demanded bits are known zero on one side, return the other.
    // These bits cannot contribute to the result of the 'xor'.
    if (DemandedBits.isSubsetOf(Known.Zero))
      return TLO.CombineTo(Op, Op0);
    if (DemandedBits.isSubsetOf(Known2.Zero))
      return TLO.CombineTo(Op, Op1);
    // If the operation can be done in a smaller type, do so.
    if (ShrinkDemandedOp(Op, BitWidth, DemandedBits, TLO))
      return true;

    // If all of the unknown bits are known to be zero on one side or the other
    // turn this into an *inclusive* or.
    //    e.g. (A & C1)^(B & C2) -> (A & C1)|(B & C2) iff C1&C2 == 0
    if (DemandedBits.isSubsetOf(Known.Zero | Known2.Zero))
      return TLO.CombineTo(Op, TLO.DAG.getNode(ISD::OR, dl, VT, Op0, Op1));

    ConstantSDNode *C = isConstOrConstSplat(Op1, DemandedElts);
    if (C) {
      // If one side is a constant, and all of the set bits in the constant are
      // also known set on the other side, turn this into an AND, as we know
      // the bits will be cleared.
      //    e.g. (X | C1) ^ C2 --> (X | C1) & ~C2 iff (C1&C2) == C2
      // NB: it is okay if more bits are known than are requested
      if (C->getAPIntValue() == Known2.One) {
        SDValue ANDC =
            TLO.DAG.getConstant(~C->getAPIntValue() & DemandedBits, dl, VT);
        return TLO.CombineTo(Op, TLO.DAG.getNode(ISD::AND, dl, VT, Op0, ANDC));
      }

      // If the RHS is a constant, see if we can change it. Don't alter a -1
      // constant because that's a 'not' op, and that is better for combining
      // and codegen.
      if (!C->isAllOnes() && DemandedBits.isSubsetOf(C->getAPIntValue())) {
        // We're flipping all demanded bits. Flip the undemanded bits too.
        SDValue New = TLO.DAG.getNOT(dl, Op0, VT);
        return TLO.CombineTo(Op, New);
      }

      unsigned Op0Opcode = Op0.getOpcode();
      if ((Op0Opcode == ISD::SRL || Op0Opcode == ISD::SHL) && Op0.hasOneUse()) {
        if (ConstantSDNode *ShiftC =
                isConstOrConstSplat(Op0.getOperand(1), DemandedElts)) {
          // Don't crash on an oversized shift. We can not guarantee that a
          // bogus shift has been simplified to undef.
          if (ShiftC->getAPIntValue().ult(BitWidth)) {
            uint64_t ShiftAmt = ShiftC->getZExtValue();
            APInt Ones = APInt::getAllOnes(BitWidth);
            Ones = Op0Opcode == ISD::SHL ? Ones.shl(ShiftAmt)
                                         : Ones.lshr(ShiftAmt);
            const TargetLowering &TLI = TLO.DAG.getTargetLoweringInfo();
            if ((DemandedBits & C->getAPIntValue()) == (DemandedBits & Ones) &&
                TLI.isDesirableToCommuteXorWithShift(Op.getNode())) {
              // If the xor constant is a demanded mask, do a 'not' before the
              // shift:
              // xor (X << ShiftC), XorC --> (not X) << ShiftC
              // xor (X >> ShiftC), XorC --> (not X) >> ShiftC
              SDValue Not = TLO.DAG.getNOT(dl, Op0.getOperand(0), VT);
              return TLO.CombineTo(Op, TLO.DAG.getNode(Op0Opcode, dl, VT, Not,
                                                       Op0.getOperand(1)));
            }
          }
        }
      }
    }

    // If we can't turn this into a 'not', try to shrink the constant.
    if (!C || !C->isAllOnes())
      if (ShrinkDemandedConstant(Op, DemandedBits, DemandedElts, TLO))
        return true;

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (!DemandedBits.isAllOnes() || !DemandedElts.isAllOnes()) {
      SDValue DemandedOp0 = SimplifyMultipleUseDemandedBits(
          Op0, DemandedBits, DemandedElts, TLO.DAG, Depth + 1);
      SDValue DemandedOp1 = SimplifyMultipleUseDemandedBits(
          Op1, DemandedBits, DemandedElts, TLO.DAG, Depth + 1);
      if (DemandedOp0 || DemandedOp1) {
        Op0 = DemandedOp0 ? DemandedOp0 : Op0;
        Op1 = DemandedOp1 ? DemandedOp1 : Op1;
        SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), dl, VT, Op0, Op1);
        return TLO.CombineTo(Op, NewOp);
      }
    }

    Known ^= Known2;
    break;
  }
  case ISD::SELECT:
    if (SimplifyDemandedBits(Op.getOperand(2), DemandedBits, DemandedElts,
                             Known, TLO, Depth + 1))
      return true;
    if (SimplifyDemandedBits(Op.getOperand(1), DemandedBits, DemandedElts,
                             Known2, TLO, Depth + 1))
      return true;

    // If the operands are constants, see if we can simplify them.
    if (ShrinkDemandedConstant(Op, DemandedBits, DemandedElts, TLO))
      return true;

    // Only known if known in both the LHS and RHS.
    Known = Known.intersectWith(Known2);
    break;
  case ISD::VSELECT:
    if (SimplifyDemandedBits(Op.getOperand(2), DemandedBits, DemandedElts,
                             Known, TLO, Depth + 1))
      return true;
    if (SimplifyDemandedBits(Op.getOperand(1), DemandedBits, DemandedElts,
                             Known2, TLO, Depth + 1))
      return true;

    // Only known if known in both the LHS and RHS.
    Known = Known.intersectWith(Known2);
    break;
  case ISD::SELECT_CC:
    if (SimplifyDemandedBits(Op.getOperand(3), DemandedBits, DemandedElts,
                             Known, TLO, Depth + 1))
      return true;
    if (SimplifyDemandedBits(Op.getOperand(2), DemandedBits, DemandedElts,
                             Known2, TLO, Depth + 1))
      return true;

    // If the operands are constants, see if we can simplify them.
    if (ShrinkDemandedConstant(Op, DemandedBits, DemandedElts, TLO))
      return true;

    // Only known if known in both the LHS and RHS.
    Known = Known.intersectWith(Known2);
    break;
  case ISD::SETCC: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
    // If (1) we only need the sign-bit, (2) the setcc operands are the same
    // width as the setcc result, and (3) the result of a setcc conforms to 0 or
    // -1, we may be able to bypass the setcc.
    if (DemandedBits.isSignMask() &&
        Op0.getScalarValueSizeInBits() == BitWidth &&
        getBooleanContents(Op0.getValueType()) ==
            BooleanContent::ZeroOrNegativeOneBooleanContent) {
      // If we're testing X < 0, then this compare isn't needed - just use X!
      // FIXME: We're limiting to integer types here, but this should also work
      // if we don't care about FP signed-zero. The use of SETLT with FP means
      // that we don't care about NaNs.
      if (CC == ISD::SETLT && Op1.getValueType().isInteger() &&
          (isNullConstant(Op1) || ISD::isBuildVectorAllZeros(Op1.getNode())))
        return TLO.CombineTo(Op, Op0);

      // TODO: Should we check for other forms of sign-bit comparisons?
      // Examples: X <= -1, X >= 0
    }
    if (getBooleanContents(Op0.getValueType()) ==
            TargetLowering::ZeroOrOneBooleanContent &&
        BitWidth > 1)
      Known.Zero.setBitsFrom(1);
    break;
  }
  case ISD::SHL: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);
    EVT ShiftVT = Op1.getValueType();

    if (std::optional<uint64_t> KnownSA =
            TLO.DAG.getValidShiftAmount(Op, DemandedElts, Depth + 1)) {
      unsigned ShAmt = *KnownSA;
      if (ShAmt == 0)
        return TLO.CombineTo(Op, Op0);

      // If this is ((X >>u C1) << ShAmt), see if we can simplify this into a
      // single shift.  We can do this if the bottom bits (which are shifted
      // out) are never demanded.
      // TODO - support non-uniform vector amounts.
      if (Op0.getOpcode() == ISD::SRL) {
        if (!DemandedBits.intersects(APInt::getLowBitsSet(BitWidth, ShAmt))) {
          if (std::optional<uint64_t> InnerSA =
                  TLO.DAG.getValidShiftAmount(Op0, DemandedElts, Depth + 2)) {
            unsigned C1 = *InnerSA;
            unsigned Opc = ISD::SHL;
            int Diff = ShAmt - C1;
            if (Diff < 0) {
              Diff = -Diff;
              Opc = ISD::SRL;
            }
            SDValue NewSA = TLO.DAG.getConstant(Diff, dl, ShiftVT);
            return TLO.CombineTo(
                Op, TLO.DAG.getNode(Opc, dl, VT, Op0.getOperand(0), NewSA));
          }
        }
      }

      // Convert (shl (anyext x, c)) to (anyext (shl x, c)) if the high bits
      // are not demanded. This will likely allow the anyext to be folded away.
      // TODO - support non-uniform vector amounts.
      if (Op0.getOpcode() == ISD::ANY_EXTEND) {
        SDValue InnerOp = Op0.getOperand(0);
        EVT InnerVT = InnerOp.getValueType();
        unsigned InnerBits = InnerVT.getScalarSizeInBits();
        if (ShAmt < InnerBits && DemandedBits.getActiveBits() <= InnerBits &&
            isTypeDesirableForOp(ISD::SHL, InnerVT)) {
          SDValue NarrowShl = TLO.DAG.getNode(
              ISD::SHL, dl, InnerVT, InnerOp,
              TLO.DAG.getShiftAmountConstant(ShAmt, InnerVT, dl));
          return TLO.CombineTo(
              Op, TLO.DAG.getNode(ISD::ANY_EXTEND, dl, VT, NarrowShl));
        }

        // Repeat the SHL optimization above in cases where an extension
        // intervenes: (shl (anyext (shr x, c1)), c2) to
        // (shl (anyext x), c2-c1).  This requires that the bottom c1 bits
        // aren't demanded (as above) and that the shifted upper c1 bits of
        // x aren't demanded.
        // TODO - support non-uniform vector amounts.
        if (InnerOp.getOpcode() == ISD::SRL && Op0.hasOneUse() &&
            InnerOp.hasOneUse()) {
          if (std::optional<uint64_t> SA2 = TLO.DAG.getValidShiftAmount(
                  InnerOp, DemandedElts, Depth + 2)) {
            unsigned InnerShAmt = *SA2;
            if (InnerShAmt < ShAmt && InnerShAmt < InnerBits &&
                DemandedBits.getActiveBits() <=
                    (InnerBits - InnerShAmt + ShAmt) &&
                DemandedBits.countr_zero() >= ShAmt) {
              SDValue NewSA =
                  TLO.DAG.getConstant(ShAmt - InnerShAmt, dl, ShiftVT);
              SDValue NewExt = TLO.DAG.getNode(ISD::ANY_EXTEND, dl, VT,
                                               InnerOp.getOperand(0));
              return TLO.CombineTo(
                  Op, TLO.DAG.getNode(ISD::SHL, dl, VT, NewExt, NewSA));
            }
          }
        }
      }

      APInt InDemandedMask = DemandedBits.lshr(ShAmt);
      if (SimplifyDemandedBits(Op0, InDemandedMask, DemandedElts, Known, TLO,
                               Depth + 1)) {
        SDNodeFlags Flags = Op.getNode()->getFlags();
        if (Flags.hasNoSignedWrap() || Flags.hasNoUnsignedWrap()) {
          // Disable the nsw and nuw flags. We can no longer guarantee that we
          // won't wrap after simplification.
          Flags.setNoSignedWrap(false);
          Flags.setNoUnsignedWrap(false);
          Op->setFlags(Flags);
        }
        return true;
      }
      Known.Zero <<= ShAmt;
      Known.One <<= ShAmt;
      // low bits known zero.
      Known.Zero.setLowBits(ShAmt);

      // Attempt to avoid multi-use ops if we don't need anything from them.
      if (!InDemandedMask.isAllOnes() || !DemandedElts.isAllOnes()) {
        SDValue DemandedOp0 = SimplifyMultipleUseDemandedBits(
            Op0, InDemandedMask, DemandedElts, TLO.DAG, Depth + 1);
        if (DemandedOp0) {
          SDValue NewOp = TLO.DAG.getNode(ISD::SHL, dl, VT, DemandedOp0, Op1);
          return TLO.CombineTo(Op, NewOp);
        }
      }

      // TODO: Can we merge this fold with the one below?
      // Try shrinking the operation as long as the shift amount will still be
      // in range.
      if (ShAmt < DemandedBits.getActiveBits() && !VT.isVector() &&
          Op.getNode()->hasOneUse()) {
        // Search for the smallest integer type with free casts to and from
        // Op's type. For expedience, just check power-of-2 integer types.
        unsigned DemandedSize = DemandedBits.getActiveBits();
        for (unsigned SmallVTBits = llvm::bit_ceil(DemandedSize);
             SmallVTBits < BitWidth; SmallVTBits = NextPowerOf2(SmallVTBits)) {
          EVT SmallVT = EVT::getIntegerVT(*TLO.DAG.getContext(), SmallVTBits);
          if (isNarrowingProfitable(VT, SmallVT) &&
              isTypeDesirableForOp(ISD::SHL, SmallVT) &&
              isTruncateFree(VT, SmallVT) && isZExtFree(SmallVT, VT) &&
              (!TLO.LegalOperations() || isOperationLegal(ISD::SHL, SmallVT))) {
            assert(DemandedSize <= SmallVTBits &&
                   "Narrowed below demanded bits?");
            // We found a type with free casts.
            SDValue NarrowShl = TLO.DAG.getNode(
                ISD::SHL, dl, SmallVT,
                TLO.DAG.getNode(ISD::TRUNCATE, dl, SmallVT, Op.getOperand(0)),
                TLO.DAG.getShiftAmountConstant(ShAmt, SmallVT, dl));
            return TLO.CombineTo(
                Op, TLO.DAG.getNode(ISD::ANY_EXTEND, dl, VT, NarrowShl));
          }
        }
      }

      // Narrow shift to lower half - similar to ShrinkDemandedOp.
      // (shl i64:x, K) -> (i64 zero_extend (shl (i32 (trunc i64:x)), K))
      // Only do this if we demand the upper half so the knownbits are correct.
      unsigned HalfWidth = BitWidth / 2;
      if ((BitWidth % 2) == 0 && !VT.isVector() && ShAmt < HalfWidth &&
          DemandedBits.countLeadingOnes() >= HalfWidth) {
        EVT HalfVT = EVT::getIntegerVT(*TLO.DAG.getContext(), HalfWidth);
        if (isNarrowingProfitable(VT, HalfVT) &&
            isTypeDesirableForOp(ISD::SHL, HalfVT) &&
            isTruncateFree(VT, HalfVT) && isZExtFree(HalfVT, VT) &&
            (!TLO.LegalOperations() || isOperationLegal(ISD::SHL, HalfVT))) {
          // If we're demanding the upper bits at all, we must ensure
          // that the upper bits of the shift result are known to be zero,
          // which is equivalent to the narrow shift being NUW.
          if (bool IsNUW = (Known.countMinLeadingZeros() >= HalfWidth)) {
            bool IsNSW = Known.countMinSignBits() > HalfWidth;
            SDNodeFlags Flags;
            Flags.setNoSignedWrap(IsNSW);
            Flags.setNoUnsignedWrap(IsNUW);
            SDValue NewOp = TLO.DAG.getNode(ISD::TRUNCATE, dl, HalfVT, Op0);
            SDValue NewShiftAmt =
                TLO.DAG.getShiftAmountConstant(ShAmt, HalfVT, dl);
            SDValue NewShift = TLO.DAG.getNode(ISD::SHL, dl, HalfVT, NewOp,
                                               NewShiftAmt, Flags);
            SDValue NewExt =
                TLO.DAG.getNode(ISD::ZERO_EXTEND, dl, VT, NewShift);
            return TLO.CombineTo(Op, NewExt);
          }
        }
      }
    } else {
      // This is a variable shift, so we can't shift the demand mask by a known
      // amount. But if we are not demanding high bits, then we are not
      // demanding those bits from the pre-shifted operand either.
      if (unsigned CTLZ = DemandedBits.countl_zero()) {
        APInt DemandedFromOp(APInt::getLowBitsSet(BitWidth, BitWidth - CTLZ));
        if (SimplifyDemandedBits(Op0, DemandedFromOp, DemandedElts, Known, TLO,
                                 Depth + 1)) {
          SDNodeFlags Flags = Op.getNode()->getFlags();
          if (Flags.hasNoSignedWrap() || Flags.hasNoUnsignedWrap()) {
            // Disable the nsw and nuw flags. We can no longer guarantee that we
            // won't wrap after simplification.
            Flags.setNoSignedWrap(false);
            Flags.setNoUnsignedWrap(false);
            Op->setFlags(Flags);
          }
          return true;
        }
        Known.resetAll();
      }
    }

    // If we are only demanding sign bits then we can use the shift source
    // directly.
    if (std::optional<uint64_t> MaxSA =
            TLO.DAG.getValidMaximumShiftAmount(Op, DemandedElts, Depth + 1)) {
      unsigned ShAmt = *MaxSA;
      unsigned NumSignBits =
          TLO.DAG.ComputeNumSignBits(Op0, DemandedElts, Depth + 1);
      unsigned UpperDemandedBits = BitWidth - DemandedBits.countr_zero();
      if (NumSignBits > ShAmt && (NumSignBits - ShAmt) >= (UpperDemandedBits))
        return TLO.CombineTo(Op, Op0);
    }
    break;
  }
  case ISD::SRL: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);
    EVT ShiftVT = Op1.getValueType();

    if (std::optional<uint64_t> KnownSA =
            TLO.DAG.getValidShiftAmount(Op, DemandedElts, Depth + 1)) {
      unsigned ShAmt = *KnownSA;
      if (ShAmt == 0)
        return TLO.CombineTo(Op, Op0);

      // If this is ((X << C1) >>u ShAmt), see if we can simplify this into a
      // single shift.  We can do this if the top bits (which are shifted out)
      // are never demanded.
      // TODO - support non-uniform vector amounts.
      if (Op0.getOpcode() == ISD::SHL) {
        if (!DemandedBits.intersects(APInt::getHighBitsSet(BitWidth, ShAmt))) {
          if (std::optional<uint64_t> InnerSA =
                  TLO.DAG.getValidShiftAmount(Op0, DemandedElts, Depth + 2)) {
            unsigned C1 = *InnerSA;
            unsigned Opc = ISD::SRL;
            int Diff = ShAmt - C1;
            if (Diff < 0) {
              Diff = -Diff;
              Opc = ISD::SHL;
            }
            SDValue NewSA = TLO.DAG.getConstant(Diff, dl, ShiftVT);
            return TLO.CombineTo(
                Op, TLO.DAG.getNode(Opc, dl, VT, Op0.getOperand(0), NewSA));
          }
        }
      }

      APInt InDemandedMask = (DemandedBits << ShAmt);

      // If the shift is exact, then it does demand the low bits (and knows that
      // they are zero).
      if (Op->getFlags().hasExact())
        InDemandedMask.setLowBits(ShAmt);

      // Narrow shift to lower half - similar to ShrinkDemandedOp.
      // (srl i64:x, K) -> (i64 zero_extend (srl (i32 (trunc i64:x)), K))
      if ((BitWidth % 2) == 0 && !VT.isVector()) {
        APInt HiBits = APInt::getHighBitsSet(BitWidth, BitWidth / 2);
        EVT HalfVT = EVT::getIntegerVT(*TLO.DAG.getContext(), BitWidth / 2);
        if (isNarrowingProfitable(VT, HalfVT) &&
            isTypeDesirableForOp(ISD::SRL, HalfVT) &&
            isTruncateFree(VT, HalfVT) && isZExtFree(HalfVT, VT) &&
            (!TLO.LegalOperations() || isOperationLegal(ISD::SRL, HalfVT)) &&
            ((InDemandedMask.countLeadingZeros() >= (BitWidth / 2)) ||
             TLO.DAG.MaskedValueIsZero(Op0, HiBits))) {
          SDValue NewOp = TLO.DAG.getNode(ISD::TRUNCATE, dl, HalfVT, Op0);
          SDValue NewShiftAmt =
              TLO.DAG.getShiftAmountConstant(ShAmt, HalfVT, dl);
          SDValue NewShift =
              TLO.DAG.getNode(ISD::SRL, dl, HalfVT, NewOp, NewShiftAmt);
          return TLO.CombineTo(
              Op, TLO.DAG.getNode(ISD::ZERO_EXTEND, dl, VT, NewShift));
        }
      }

      // Compute the new bits that are at the top now.
      if (SimplifyDemandedBits(Op0, InDemandedMask, DemandedElts, Known, TLO,
                               Depth + 1))
        return true;
      Known.Zero.lshrInPlace(ShAmt);
      Known.One.lshrInPlace(ShAmt);
      // High bits known zero.
      Known.Zero.setHighBits(ShAmt);

      // Attempt to avoid multi-use ops if we don't need anything from them.
      if (!InDemandedMask.isAllOnes() || !DemandedElts.isAllOnes()) {
        SDValue DemandedOp0 = SimplifyMultipleUseDemandedBits(
            Op0, InDemandedMask, DemandedElts, TLO.DAG, Depth + 1);
        if (DemandedOp0) {
          SDValue NewOp = TLO.DAG.getNode(ISD::SRL, dl, VT, DemandedOp0, Op1);
          return TLO.CombineTo(Op, NewOp);
        }
      }
    } else {
      // Use generic knownbits computation as it has support for non-uniform
      // shift amounts.
      Known = TLO.DAG.computeKnownBits(Op, DemandedElts, Depth);
    }

    // Try to match AVG patterns (after shift simplification).
    if (SDValue AVG = combineShiftToAVG(Op, TLO, *this, DemandedBits,
                                        DemandedElts, Depth + 1))
      return TLO.CombineTo(Op, AVG);

    break;
  }
  case ISD::SRA: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);
    EVT ShiftVT = Op1.getValueType();

    // If we only want bits that already match the signbit then we don't need
    // to shift.
    unsigned NumHiDemandedBits = BitWidth - DemandedBits.countr_zero();
    if (TLO.DAG.ComputeNumSignBits(Op0, DemandedElts, Depth + 1) >=
        NumHiDemandedBits)
      return TLO.CombineTo(Op, Op0);

    // If this is an arithmetic shift right and only the low-bit is set, we can
    // always convert this into a logical shr, even if the shift amount is
    // variable.  The low bit of the shift cannot be an input sign bit unless
    // the shift amount is >= the size of the datatype, which is undefined.
    if (DemandedBits.isOne())
      return TLO.CombineTo(Op, TLO.DAG.getNode(ISD::SRL, dl, VT, Op0, Op1));

    if (std::optional<uint64_t> KnownSA =
            TLO.DAG.getValidShiftAmount(Op, DemandedElts, Depth + 1)) {
      unsigned ShAmt = *KnownSA;
      if (ShAmt == 0)
        return TLO.CombineTo(Op, Op0);

      // fold (sra (shl x, c1), c1) -> sext_inreg for some c1 and target
      // supports sext_inreg.
      if (Op0.getOpcode() == ISD::SHL) {
        if (std::optional<uint64_t> InnerSA =
                TLO.DAG.getValidShiftAmount(Op0, DemandedElts, Depth + 2)) {
          unsigned LowBits = BitWidth - ShAmt;
          EVT ExtVT = EVT::getIntegerVT(*TLO.DAG.getContext(), LowBits);
          if (VT.isVector())
            ExtVT = EVT::getVectorVT(*TLO.DAG.getContext(), ExtVT,
                                     VT.getVectorElementCount());

          if (*InnerSA == ShAmt) {
            if (!TLO.LegalOperations() ||
                getOperationAction(ISD::SIGN_EXTEND_INREG, ExtVT) == Legal)
              return TLO.CombineTo(
                  Op, TLO.DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, VT,
                                      Op0.getOperand(0),
                                      TLO.DAG.getValueType(ExtVT)));

            // Even if we can't convert to sext_inreg, we might be able to
            // remove this shift pair if the input is already sign extended.
            unsigned NumSignBits =
                TLO.DAG.ComputeNumSignBits(Op0.getOperand(0), DemandedElts);
            if (NumSignBits > ShAmt)
              return TLO.CombineTo(Op, Op0.getOperand(0));
          }
        }
      }

      APInt InDemandedMask = (DemandedBits << ShAmt);

      // If the shift is exact, then it does demand the low bits (and knows that
      // they are zero).
      if (Op->getFlags().hasExact())
        InDemandedMask.setLowBits(ShAmt);

      // If any of the demanded bits are produced by the sign extension, we also
      // demand the input sign bit.
      if (DemandedBits.countl_zero() < ShAmt)
        InDemandedMask.setSignBit();

      if (SimplifyDemandedBits(Op0, InDemandedMask, DemandedElts, Known, TLO,
                               Depth + 1))
        return true;
      Known.Zero.lshrInPlace(ShAmt);
      Known.One.lshrInPlace(ShAmt);

      // If the input sign bit is known to be zero, or if none of the top bits
      // are demanded, turn this into an unsigned shift right.
      if (Known.Zero[BitWidth - ShAmt - 1] ||
          DemandedBits.countl_zero() >= ShAmt) {
        SDNodeFlags Flags;
        Flags.setExact(Op->getFlags().hasExact());
        return TLO.CombineTo(
            Op, TLO.DAG.getNode(ISD::SRL, dl, VT, Op0, Op1, Flags));
      }

      int Log2 = DemandedBits.exactLogBase2();
      if (Log2 >= 0) {
        // The bit must come from the sign.
        SDValue NewSA = TLO.DAG.getConstant(BitWidth - 1 - Log2, dl, ShiftVT);
        return TLO.CombineTo(Op, TLO.DAG.getNode(ISD::SRL, dl, VT, Op0, NewSA));
      }

      if (Known.One[BitWidth - ShAmt - 1])
        // New bits are known one.
        Known.One.setHighBits(ShAmt);

      // Attempt to avoid multi-use ops if we don't need anything from them.
      if (!InDemandedMask.isAllOnes() || !DemandedElts.isAllOnes()) {
        SDValue DemandedOp0 = SimplifyMultipleUseDemandedBits(
            Op0, InDemandedMask, DemandedElts, TLO.DAG, Depth + 1);
        if (DemandedOp0) {
          SDValue NewOp = TLO.DAG.getNode(ISD::SRA, dl, VT, DemandedOp0, Op1);
          return TLO.CombineTo(Op, NewOp);
        }
      }
    }

    // Try to match AVG patterns (after shift simplification).
    if (SDValue AVG = combineShiftToAVG(Op, TLO, *this, DemandedBits,
                                        DemandedElts, Depth + 1))
      return TLO.CombineTo(Op, AVG);

    break;
  }
  case ISD::FSHL:
  case ISD::FSHR: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);
    SDValue Op2 = Op.getOperand(2);
    bool IsFSHL = (Op.getOpcode() == ISD::FSHL);

    if (ConstantSDNode *SA = isConstOrConstSplat(Op2, DemandedElts)) {
      unsigned Amt = SA->getAPIntValue().urem(BitWidth);

      // For fshl, 0-shift returns the 1st arg.
      // For fshr, 0-shift returns the 2nd arg.
      if (Amt == 0) {
        if (SimplifyDemandedBits(IsFSHL ? Op0 : Op1, DemandedBits, DemandedElts,
                                 Known, TLO, Depth + 1))
          return true;
        break;
      }

      // fshl: (Op0 << Amt) | (Op1 >> (BW - Amt))
      // fshr: (Op0 << (BW - Amt)) | (Op1 >> Amt)
      APInt Demanded0 = DemandedBits.lshr(IsFSHL ? Amt : (BitWidth - Amt));
      APInt Demanded1 = DemandedBits << (IsFSHL ? (BitWidth - Amt) : Amt);
      if (SimplifyDemandedBits(Op0, Demanded0, DemandedElts, Known2, TLO,
                               Depth + 1))
        return true;
      if (SimplifyDemandedBits(Op1, Demanded1, DemandedElts, Known, TLO,
                               Depth + 1))
        return true;

      Known2.One <<= (IsFSHL ? Amt : (BitWidth - Amt));
      Known2.Zero <<= (IsFSHL ? Amt : (BitWidth - Amt));
      Known.One.lshrInPlace(IsFSHL ? (BitWidth - Amt) : Amt);
      Known.Zero.lshrInPlace(IsFSHL ? (BitWidth - Amt) : Amt);
      Known = Known.unionWith(Known2);

      // Attempt to avoid multi-use ops if we don't need anything from them.
      if (!Demanded0.isAllOnes() || !Demanded1.isAllOnes() ||
          !DemandedElts.isAllOnes()) {
        SDValue DemandedOp0 = SimplifyMultipleUseDemandedBits(
            Op0, Demanded0, DemandedElts, TLO.DAG, Depth + 1);
        SDValue DemandedOp1 = SimplifyMultipleUseDemandedBits(
            Op1, Demanded1, DemandedElts, TLO.DAG, Depth + 1);
        if (DemandedOp0 || DemandedOp1) {
          DemandedOp0 = DemandedOp0 ? DemandedOp0 : Op0;
          DemandedOp1 = DemandedOp1 ? DemandedOp1 : Op1;
          SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), dl, VT, DemandedOp0,
                                          DemandedOp1, Op2);
          return TLO.CombineTo(Op, NewOp);
        }
      }
    }

    // For pow-2 bitwidths we only demand the bottom modulo amt bits.
    if (isPowerOf2_32(BitWidth)) {
      APInt DemandedAmtBits(Op2.getScalarValueSizeInBits(), BitWidth - 1);
      if (SimplifyDemandedBits(Op2, DemandedAmtBits, DemandedElts,
                               Known2, TLO, Depth + 1))
        return true;
    }
    break;
  }
  case ISD::ROTL:
  case ISD::ROTR: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);
    bool IsROTL = (Op.getOpcode() == ISD::ROTL);

    // If we're rotating an 0/-1 value, then it stays an 0/-1 value.
    if (BitWidth == TLO.DAG.ComputeNumSignBits(Op0, DemandedElts, Depth + 1))
      return TLO.CombineTo(Op, Op0);

    if (ConstantSDNode *SA = isConstOrConstSplat(Op1, DemandedElts)) {
      unsigned Amt = SA->getAPIntValue().urem(BitWidth);
      unsigned RevAmt = BitWidth - Amt;

      // rotl: (Op0 << Amt) | (Op0 >> (BW - Amt))
      // rotr: (Op0 << (BW - Amt)) | (Op0 >> Amt)
      APInt Demanded0 = DemandedBits.rotr(IsROTL ? Amt : RevAmt);
      if (SimplifyDemandedBits(Op0, Demanded0, DemandedElts, Known2, TLO,
                               Depth + 1))
        return true;

      // rot*(x, 0) --> x
      if (Amt == 0)
        return TLO.CombineTo(Op, Op0);

      // See if we don't demand either half of the rotated bits.
      if ((!TLO.LegalOperations() || isOperationLegal(ISD::SHL, VT)) &&
          DemandedBits.countr_zero() >= (IsROTL ? Amt : RevAmt)) {
        Op1 = TLO.DAG.getConstant(IsROTL ? Amt : RevAmt, dl, Op1.getValueType());
        return TLO.CombineTo(Op, TLO.DAG.getNode(ISD::SHL, dl, VT, Op0, Op1));
      }
      if ((!TLO.LegalOperations() || isOperationLegal(ISD::SRL, VT)) &&
          DemandedBits.countl_zero() >= (IsROTL ? RevAmt : Amt)) {
        Op1 = TLO.DAG.getConstant(IsROTL ? RevAmt : Amt, dl, Op1.getValueType());
        return TLO.CombineTo(Op, TLO.DAG.getNode(ISD::SRL, dl, VT, Op0, Op1));
      }
    }

    // For pow-2 bitwidths we only demand the bottom modulo amt bits.
    if (isPowerOf2_32(BitWidth)) {
      APInt DemandedAmtBits(Op1.getScalarValueSizeInBits(), BitWidth - 1);
      if (SimplifyDemandedBits(Op1, DemandedAmtBits, DemandedElts, Known2, TLO,
                               Depth + 1))
        return true;
    }
    break;
  }
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX: {
    unsigned Opc = Op.getOpcode();
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);

    // If we're only demanding signbits, then we can simplify to OR/AND node.
    unsigned BitOp =
        (Opc == ISD::SMIN || Opc == ISD::UMAX) ? ISD::OR : ISD::AND;
    unsigned NumSignBits =
        std::min(TLO.DAG.ComputeNumSignBits(Op0, DemandedElts, Depth + 1),
                 TLO.DAG.ComputeNumSignBits(Op1, DemandedElts, Depth + 1));
    unsigned NumDemandedUpperBits = BitWidth - DemandedBits.countr_zero();
    if (NumSignBits >= NumDemandedUpperBits)
      return TLO.CombineTo(Op, TLO.DAG.getNode(BitOp, SDLoc(Op), VT, Op0, Op1));

    // Check if one arg is always less/greater than (or equal) to the other arg.
    KnownBits Known0 = TLO.DAG.computeKnownBits(Op0, DemandedElts, Depth + 1);
    KnownBits Known1 = TLO.DAG.computeKnownBits(Op1, DemandedElts, Depth + 1);
    switch (Opc) {
    case ISD::SMIN:
      if (std::optional<bool> IsSLE = KnownBits::sle(Known0, Known1))
        return TLO.CombineTo(Op, *IsSLE ? Op0 : Op1);
      if (std::optional<bool> IsSLT = KnownBits::slt(Known0, Known1))
        return TLO.CombineTo(Op, *IsSLT ? Op0 : Op1);
      Known = KnownBits::smin(Known0, Known1);
      break;
    case ISD::SMAX:
      if (std::optional<bool> IsSGE = KnownBits::sge(Known0, Known1))
        return TLO.CombineTo(Op, *IsSGE ? Op0 : Op1);
      if (std::optional<bool> IsSGT = KnownBits::sgt(Known0, Known1))
        return TLO.CombineTo(Op, *IsSGT ? Op0 : Op1);
      Known = KnownBits::smax(Known0, Known1);
      break;
    case ISD::UMIN:
      if (std::optional<bool> IsULE = KnownBits::ule(Known0, Known1))
        return TLO.CombineTo(Op, *IsULE ? Op0 : Op1);
      if (std::optional<bool> IsULT = KnownBits::ult(Known0, Known1))
        return TLO.CombineTo(Op, *IsULT ? Op0 : Op1);
      Known = KnownBits::umin(Known0, Known1);
      break;
    case ISD::UMAX:
      if (std::optional<bool> IsUGE = KnownBits::uge(Known0, Known1))
        return TLO.CombineTo(Op, *IsUGE ? Op0 : Op1);
      if (std::optional<bool> IsUGT = KnownBits::ugt(Known0, Known1))
        return TLO.CombineTo(Op, *IsUGT ? Op0 : Op1);
      Known = KnownBits::umax(Known0, Known1);
      break;
    }
    break;
  }
  case ISD::BITREVERSE: {
    SDValue Src = Op.getOperand(0);
    APInt DemandedSrcBits = DemandedBits.reverseBits();
    if (SimplifyDemandedBits(Src, DemandedSrcBits, DemandedElts, Known2, TLO,
                             Depth + 1))
      return true;
    Known.One = Known2.One.reverseBits();
    Known.Zero = Known2.Zero.reverseBits();
    break;
  }
  case ISD::BSWAP: {
    SDValue Src = Op.getOperand(0);

    // If the only bits demanded come from one byte of the bswap result,
    // just shift the input byte into position to eliminate the bswap.
    unsigned NLZ = DemandedBits.countl_zero();
    unsigned NTZ = DemandedBits.countr_zero();

    // Round NTZ down to the next byte.  If we have 11 trailing zeros, then
    // we need all the bits down to bit 8.  Likewise, round NLZ.  If we
    // have 14 leading zeros, round to 8.
    NLZ = alignDown(NLZ, 8);
    NTZ = alignDown(NTZ, 8);
    // If we need exactly one byte, we can do this transformation.
    if (BitWidth - NLZ - NTZ == 8) {
      // Replace this with either a left or right shift to get the byte into
      // the right place.
      unsigned ShiftOpcode = NLZ > NTZ ? ISD::SRL : ISD::SHL;
      if (!TLO.LegalOperations() || isOperationLegal(ShiftOpcode, VT)) {
        unsigned ShiftAmount = NLZ > NTZ ? NLZ - NTZ : NTZ - NLZ;
        SDValue ShAmt = TLO.DAG.getShiftAmountConstant(ShiftAmount, VT, dl);
        SDValue NewOp = TLO.DAG.getNode(ShiftOpcode, dl, VT, Src, ShAmt);
        return TLO.CombineTo(Op, NewOp);
      }
    }

    APInt DemandedSrcBits = DemandedBits.byteSwap();
    if (SimplifyDemandedBits(Src, DemandedSrcBits, DemandedElts, Known2, TLO,
                             Depth + 1))
      return true;
    Known.One = Known2.One.byteSwap();
    Known.Zero = Known2.Zero.byteSwap();
    break;
  }
  case ISD::CTPOP: {
    // If only 1 bit is demanded, replace with PARITY as long as we're before
    // op legalization.
    // FIXME: Limit to scalars for now.
    if (DemandedBits.isOne() && !TLO.LegalOps && !VT.isVector())
      return TLO.CombineTo(Op, TLO.DAG.getNode(ISD::PARITY, dl, VT,
                                               Op.getOperand(0)));

    Known = TLO.DAG.computeKnownBits(Op, DemandedElts, Depth);
    break;
  }
  case ISD::SIGN_EXTEND_INREG: {
    SDValue Op0 = Op.getOperand(0);
    EVT ExVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
    unsigned ExVTBits = ExVT.getScalarSizeInBits();

    // If we only care about the highest bit, don't bother shifting right.
    if (DemandedBits.isSignMask()) {
      unsigned MinSignedBits =
          TLO.DAG.ComputeMaxSignificantBits(Op0, DemandedElts, Depth + 1);
      bool AlreadySignExtended = ExVTBits >= MinSignedBits;
      // However if the input is already sign extended we expect the sign
      // extension to be dropped altogether later and do not simplify.
      if (!AlreadySignExtended) {
        // Compute the correct shift amount type, which must be getShiftAmountTy
        // for scalar types after legalization.
        SDValue ShiftAmt =
            TLO.DAG.getShiftAmountConstant(BitWidth - ExVTBits, VT, dl);
        return TLO.CombineTo(Op,
                             TLO.DAG.getNode(ISD::SHL, dl, VT, Op0, ShiftAmt));
      }
    }

    // If none of the extended bits are demanded, eliminate the sextinreg.
    if (DemandedBits.getActiveBits() <= ExVTBits)
      return TLO.CombineTo(Op, Op0);

    APInt InputDemandedBits = DemandedBits.getLoBits(ExVTBits);

    // Since the sign extended bits are demanded, we know that the sign
    // bit is demanded.
    InputDemandedBits.setBit(ExVTBits - 1);

    if (SimplifyDemandedBits(Op0, InputDemandedBits, DemandedElts, Known, TLO,
                             Depth + 1))
      return true;

    // If the sign bit of the input is known set or clear, then we know the
    // top bits of the result.

    // If the input sign bit is known zero, convert this into a zero extension.
    if (Known.Zero[ExVTBits - 1])
      return TLO.CombineTo(Op, TLO.DAG.getZeroExtendInReg(Op0, dl, ExVT));

    APInt Mask = APInt::getLowBitsSet(BitWidth, ExVTBits);
    if (Known.One[ExVTBits - 1]) { // Input sign bit known set
      Known.One.setBitsFrom(ExVTBits);
      Known.Zero &= Mask;
    } else { // Input sign bit unknown
      Known.Zero &= Mask;
      Known.One &= Mask;
    }
    break;
  }
  case ISD::BUILD_PAIR: {
    EVT HalfVT = Op.getOperand(0).getValueType();
    unsigned HalfBitWidth = HalfVT.getScalarSizeInBits();

    APInt MaskLo = DemandedBits.getLoBits(HalfBitWidth).trunc(HalfBitWidth);
    APInt MaskHi = DemandedBits.getHiBits(HalfBitWidth).trunc(HalfBitWidth);

    KnownBits KnownLo, KnownHi;

    if (SimplifyDemandedBits(Op.getOperand(0), MaskLo, KnownLo, TLO, Depth + 1))
      return true;

    if (SimplifyDemandedBits(Op.getOperand(1), MaskHi, KnownHi, TLO, Depth + 1))
      return true;

    Known = KnownHi.concat(KnownLo);
    break;
  }
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    if (VT.isScalableVector())
      return false;
    [[fallthrough]];
  case ISD::ZERO_EXTEND: {
    SDValue Src = Op.getOperand(0);
    EVT SrcVT = Src.getValueType();
    unsigned InBits = SrcVT.getScalarSizeInBits();
    unsigned InElts = SrcVT.isFixedLengthVector() ? SrcVT.getVectorNumElements() : 1;
    bool IsVecInReg = Op.getOpcode() == ISD::ZERO_EXTEND_VECTOR_INREG;

    // If none of the top bits are demanded, convert this into an any_extend.
    if (DemandedBits.getActiveBits() <= InBits) {
      // If we only need the non-extended bits of the bottom element
      // then we can just bitcast to the result.
      if (IsLE && IsVecInReg && DemandedElts == 1 &&
          VT.getSizeInBits() == SrcVT.getSizeInBits())
        return TLO.CombineTo(Op, TLO.DAG.getBitcast(VT, Src));

      unsigned Opc =
          IsVecInReg ? ISD::ANY_EXTEND_VECTOR_INREG : ISD::ANY_EXTEND;
      if (!TLO.LegalOperations() || isOperationLegal(Opc, VT))
        return TLO.CombineTo(Op, TLO.DAG.getNode(Opc, dl, VT, Src));
    }

    SDNodeFlags Flags = Op->getFlags();
    APInt InDemandedBits = DemandedBits.trunc(InBits);
    APInt InDemandedElts = DemandedElts.zext(InElts);
    if (SimplifyDemandedBits(Src, InDemandedBits, InDemandedElts, Known, TLO,
                             Depth + 1)) {
      if (Flags.hasNonNeg()) {
        Flags.setNonNeg(false);
        Op->setFlags(Flags);
      }
      return true;
    }
    assert(Known.getBitWidth() == InBits && "Src width has changed?");
    Known = Known.zext(BitWidth);

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (SDValue NewSrc = SimplifyMultipleUseDemandedBits(
            Src, InDemandedBits, InDemandedElts, TLO.DAG, Depth + 1))
      return TLO.CombineTo(Op, TLO.DAG.getNode(Op.getOpcode(), dl, VT, NewSrc));
    break;
  }
  case ISD::SIGN_EXTEND_VECTOR_INREG:
    if (VT.isScalableVector())
      return false;
    [[fallthrough]];
  case ISD::SIGN_EXTEND: {
    SDValue Src = Op.getOperand(0);
    EVT SrcVT = Src.getValueType();
    unsigned InBits = SrcVT.getScalarSizeInBits();
    unsigned InElts = SrcVT.isFixedLengthVector() ? SrcVT.getVectorNumElements() : 1;
    bool IsVecInReg = Op.getOpcode() == ISD::SIGN_EXTEND_VECTOR_INREG;

    APInt InDemandedElts = DemandedElts.zext(InElts);
    APInt InDemandedBits = DemandedBits.trunc(InBits);

    // Since some of the sign extended bits are demanded, we know that the sign
    // bit is demanded.
    InDemandedBits.setBit(InBits - 1);

    // If none of the top bits are demanded, convert this into an any_extend.
    if (DemandedBits.getActiveBits() <= InBits) {
      // If we only need the non-extended bits of the bottom element
      // then we can just bitcast to the result.
      if (IsLE && IsVecInReg && DemandedElts == 1 &&
          VT.getSizeInBits() == SrcVT.getSizeInBits())
        return TLO.CombineTo(Op, TLO.DAG.getBitcast(VT, Src));

      // Don't lose an all signbits 0/-1 splat on targets with 0/-1 booleans.
      if (getBooleanContents(VT) != ZeroOrNegativeOneBooleanContent ||
          TLO.DAG.ComputeNumSignBits(Src, InDemandedElts, Depth + 1) !=
              InBits) {
        unsigned Opc =
            IsVecInReg ? ISD::ANY_EXTEND_VECTOR_INREG : ISD::ANY_EXTEND;
        if (!TLO.LegalOperations() || isOperationLegal(Opc, VT))
          return TLO.CombineTo(Op, TLO.DAG.getNode(Opc, dl, VT, Src));
      }
    }

    if (SimplifyDemandedBits(Src, InDemandedBits, InDemandedElts, Known, TLO,
                             Depth + 1))
      return true;
    assert(Known.getBitWidth() == InBits && "Src width has changed?");

    // If the sign bit is known one, the top bits match.
    Known = Known.sext(BitWidth);

    // If the sign bit is known zero, convert this to a zero extend.
    if (Known.isNonNegative()) {
      unsigned Opc =
          IsVecInReg ? ISD::ZERO_EXTEND_VECTOR_INREG : ISD::ZERO_EXTEND;
      if (!TLO.LegalOperations() || isOperationLegal(Opc, VT)) {
        SDNodeFlags Flags;
        if (!IsVecInReg)
          Flags.setNonNeg(true);
        return TLO.CombineTo(Op, TLO.DAG.getNode(Opc, dl, VT, Src, Flags));
      }
    }

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (SDValue NewSrc = SimplifyMultipleUseDemandedBits(
            Src, InDemandedBits, InDemandedElts, TLO.DAG, Depth + 1))
      return TLO.CombineTo(Op, TLO.DAG.getNode(Op.getOpcode(), dl, VT, NewSrc));
    break;
  }
  case ISD::ANY_EXTEND_VECTOR_INREG:
    if (VT.isScalableVector())
      return false;
    [[fallthrough]];
  case ISD::ANY_EXTEND: {
    SDValue Src = Op.getOperand(0);
    EVT SrcVT = Src.getValueType();
    unsigned InBits = SrcVT.getScalarSizeInBits();
    unsigned InElts = SrcVT.isFixedLengthVector() ? SrcVT.getVectorNumElements() : 1;
    bool IsVecInReg = Op.getOpcode() == ISD::ANY_EXTEND_VECTOR_INREG;

    // If we only need the bottom element then we can just bitcast.
    // TODO: Handle ANY_EXTEND?
    if (IsLE && IsVecInReg && DemandedElts == 1 &&
        VT.getSizeInBits() == SrcVT.getSizeInBits())
      return TLO.CombineTo(Op, TLO.DAG.getBitcast(VT, Src));

    APInt InDemandedBits = DemandedBits.trunc(InBits);
    APInt InDemandedElts = DemandedElts.zext(InElts);
    if (SimplifyDemandedBits(Src, InDemandedBits, InDemandedElts, Known, TLO,
                             Depth + 1))
      return true;
    assert(Known.getBitWidth() == InBits && "Src width has changed?");
    Known = Known.anyext(BitWidth);

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (SDValue NewSrc = SimplifyMultipleUseDemandedBits(
            Src, InDemandedBits, InDemandedElts, TLO.DAG, Depth + 1))
      return TLO.CombineTo(Op, TLO.DAG.getNode(Op.getOpcode(), dl, VT, NewSrc));
    break;
  }
  case ISD::TRUNCATE: {
    SDValue Src = Op.getOperand(0);

    // Simplify the input, using demanded bit information, and compute the known
    // zero/one bits live out.
    unsigned OperandBitWidth = Src.getScalarValueSizeInBits();
    APInt TruncMask = DemandedBits.zext(OperandBitWidth);
    if (SimplifyDemandedBits(Src, TruncMask, DemandedElts, Known, TLO,
                             Depth + 1))
      return true;
    Known = Known.trunc(BitWidth);

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (SDValue NewSrc = SimplifyMultipleUseDemandedBits(
            Src, TruncMask, DemandedElts, TLO.DAG, Depth + 1))
      return TLO.CombineTo(Op, TLO.DAG.getNode(ISD::TRUNCATE, dl, VT, NewSrc));

    // If the input is only used by this truncate, see if we can shrink it based
    // on the known demanded bits.
    switch (Src.getOpcode()) {
    default:
      break;
    case ISD::SRL:
      // Shrink SRL by a constant if none of the high bits shifted in are
      // demanded.
      if (TLO.LegalTypes() && !isTypeDesirableForOp(ISD::SRL, VT))
        // Do not turn (vt1 truncate (vt2 srl)) into (vt1 srl) if vt1 is
        // undesirable.
        break;

      if (Src.getNode()->hasOneUse()) {
        if (isTruncateFree(Src, VT) &&
            !isTruncateFree(Src.getValueType(), VT)) {
          // If truncate is only free at trunc(srl), do not turn it into
          // srl(trunc). The check is done by first check the truncate is free
          // at Src's opcode(srl), then check the truncate is not done by
          // referencing sub-register. In test, if both trunc(srl) and
          // srl(trunc)'s trunc are free, srl(trunc) performs better. If only
          // trunc(srl)'s trunc is free, trunc(srl) is better.
          break;
        }

        std::optional<uint64_t> ShAmtC =
            TLO.DAG.getValidShiftAmount(Src, DemandedElts, Depth + 2);
        if (!ShAmtC || *ShAmtC >= BitWidth)
          break;
        uint64_t ShVal = *ShAmtC;

        APInt HighBits =
            APInt::getHighBitsSet(OperandBitWidth, OperandBitWidth - BitWidth);
        HighBits.lshrInPlace(ShVal);
        HighBits = HighBits.trunc(BitWidth);
        if (!(HighBits & DemandedBits)) {
          // None of the shifted in bits are needed.  Add a truncate of the
          // shift input, then shift it.
          SDValue NewShAmt = TLO.DAG.getShiftAmountConstant(ShVal, VT, dl);
          SDValue NewTrunc =
              TLO.DAG.getNode(ISD::TRUNCATE, dl, VT, Src.getOperand(0));
          return TLO.CombineTo(
              Op, TLO.DAG.getNode(ISD::SRL, dl, VT, NewTrunc, NewShAmt));
        }
      }
      break;
    }

    break;
  }
  case ISD::AssertZext: {
    // AssertZext demands all of the high bits, plus any of the low bits
    // demanded by its users.
    EVT ZVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
    APInt InMask = APInt::getLowBitsSet(BitWidth, ZVT.getSizeInBits());
    if (SimplifyDemandedBits(Op.getOperand(0), ~InMask | DemandedBits, Known,
                             TLO, Depth + 1))
      return true;

    Known.Zero |= ~InMask;
    Known.One &= (~Known.Zero);
    break;
  }
  case ISD::EXTRACT_VECTOR_ELT: {
    SDValue Src = Op.getOperand(0);
    SDValue Idx = Op.getOperand(1);
    ElementCount SrcEltCnt = Src.getValueType().getVectorElementCount();
    unsigned EltBitWidth = Src.getScalarValueSizeInBits();

    if (SrcEltCnt.isScalable())
      return false;

    // Demand the bits from every vector element without a constant index.
    unsigned NumSrcElts = SrcEltCnt.getFixedValue();
    APInt DemandedSrcElts = APInt::getAllOnes(NumSrcElts);
    if (auto *CIdx = dyn_cast<ConstantSDNode>(Idx))
      if (CIdx->getAPIntValue().ult(NumSrcElts))
        DemandedSrcElts = APInt::getOneBitSet(NumSrcElts, CIdx->getZExtValue());

    // If BitWidth > EltBitWidth the value is anyext:ed. So we do not know
    // anything about the extended bits.
    APInt DemandedSrcBits = DemandedBits;
    if (BitWidth > EltBitWidth)
      DemandedSrcBits = DemandedSrcBits.trunc(EltBitWidth);

    if (SimplifyDemandedBits(Src, DemandedSrcBits, DemandedSrcElts, Known2, TLO,
                             Depth + 1))
      return true;

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (!DemandedSrcBits.isAllOnes() || !DemandedSrcElts.isAllOnes()) {
      if (SDValue DemandedSrc = SimplifyMultipleUseDemandedBits(
              Src, DemandedSrcBits, DemandedSrcElts, TLO.DAG, Depth + 1)) {
        SDValue NewOp =
            TLO.DAG.getNode(Op.getOpcode(), dl, VT, DemandedSrc, Idx);
        return TLO.CombineTo(Op, NewOp);
      }
    }

    Known = Known2;
    if (BitWidth > EltBitWidth)
      Known = Known.anyext(BitWidth);
    break;
  }
  case ISD::BITCAST: {
    if (VT.isScalableVector())
      return false;
    SDValue Src = Op.getOperand(0);
    EVT SrcVT = Src.getValueType();
    unsigned NumSrcEltBits = SrcVT.getScalarSizeInBits();

    // If this is an FP->Int bitcast and if the sign bit is the only
    // thing demanded, turn this into a FGETSIGN.
    if (!TLO.LegalOperations() && !VT.isVector() && !SrcVT.isVector() &&
        DemandedBits == APInt::getSignMask(Op.getValueSizeInBits()) &&
        SrcVT.isFloatingPoint()) {
      bool OpVTLegal = isOperationLegalOrCustom(ISD::FGETSIGN, VT);
      bool i32Legal = isOperationLegalOrCustom(ISD::FGETSIGN, MVT::i32);
      if ((OpVTLegal || i32Legal) && VT.isSimple() && SrcVT != MVT::f16 &&
          SrcVT != MVT::f128) {
        // Cannot eliminate/lower SHL for f128 yet.
        EVT Ty = OpVTLegal ? VT : MVT::i32;
        // Make a FGETSIGN + SHL to move the sign bit into the appropriate
        // place.  We expect the SHL to be eliminated by other optimizations.
        SDValue Sign = TLO.DAG.getNode(ISD::FGETSIGN, dl, Ty, Src);
        unsigned OpVTSizeInBits = Op.getValueSizeInBits();
        if (!OpVTLegal && OpVTSizeInBits > 32)
          Sign = TLO.DAG.getNode(ISD::ZERO_EXTEND, dl, VT, Sign);
        unsigned ShVal = Op.getValueSizeInBits() - 1;
        SDValue ShAmt = TLO.DAG.getConstant(ShVal, dl, VT);
        return TLO.CombineTo(Op,
                             TLO.DAG.getNode(ISD::SHL, dl, VT, Sign, ShAmt));
      }
    }

    // Bitcast from a vector using SimplifyDemanded Bits/VectorElts.
    // Demand the elt/bit if any of the original elts/bits are demanded.
    if (SrcVT.isVector() && (BitWidth % NumSrcEltBits) == 0) {
      unsigned Scale = BitWidth / NumSrcEltBits;
      unsigned NumSrcElts = SrcVT.getVectorNumElements();
      APInt DemandedSrcBits = APInt::getZero(NumSrcEltBits);
      APInt DemandedSrcElts = APInt::getZero(NumSrcElts);
      for (unsigned i = 0; i != Scale; ++i) {
        unsigned EltOffset = IsLE ? i : (Scale - 1 - i);
        unsigned BitOffset = EltOffset * NumSrcEltBits;
        APInt Sub = DemandedBits.extractBits(NumSrcEltBits, BitOffset);
        if (!Sub.isZero()) {
          DemandedSrcBits |= Sub;
          for (unsigned j = 0; j != NumElts; ++j)
            if (DemandedElts[j])
              DemandedSrcElts.setBit((j * Scale) + i);
        }
      }

      APInt KnownSrcUndef, KnownSrcZero;
      if (SimplifyDemandedVectorElts(Src, DemandedSrcElts, KnownSrcUndef,
                                     KnownSrcZero, TLO, Depth + 1))
        return true;

      KnownBits KnownSrcBits;
      if (SimplifyDemandedBits(Src, DemandedSrcBits, DemandedSrcElts,
                               KnownSrcBits, TLO, Depth + 1))
        return true;
    } else if (IsLE && (NumSrcEltBits % BitWidth) == 0) {
      // TODO - bigendian once we have test coverage.
      unsigned Scale = NumSrcEltBits / BitWidth;
      unsigned NumSrcElts = SrcVT.isVector() ? SrcVT.getVectorNumElements() : 1;
      APInt DemandedSrcBits = APInt::getZero(NumSrcEltBits);
      APInt DemandedSrcElts = APInt::getZero(NumSrcElts);
      for (unsigned i = 0; i != NumElts; ++i)
        if (DemandedElts[i]) {
          unsigned Offset = (i % Scale) * BitWidth;
          DemandedSrcBits.insertBits(DemandedBits, Offset);
          DemandedSrcElts.setBit(i / Scale);
        }

      if (SrcVT.isVector()) {
        APInt KnownSrcUndef, KnownSrcZero;
        if (SimplifyDemandedVectorElts(Src, DemandedSrcElts, KnownSrcUndef,
                                       KnownSrcZero, TLO, Depth + 1))
          return true;
      }

      KnownBits KnownSrcBits;
      if (SimplifyDemandedBits(Src, DemandedSrcBits, DemandedSrcElts,
                               KnownSrcBits, TLO, Depth + 1))
        return true;

      // Attempt to avoid multi-use ops if we don't need anything from them.
      if (!DemandedSrcBits.isAllOnes() || !DemandedSrcElts.isAllOnes()) {
        if (SDValue DemandedSrc = SimplifyMultipleUseDemandedBits(
                Src, DemandedSrcBits, DemandedSrcElts, TLO.DAG, Depth + 1)) {
          SDValue NewOp = TLO.DAG.getBitcast(VT, DemandedSrc);
          return TLO.CombineTo(Op, NewOp);
        }
      }
    }

    // If this is a bitcast, let computeKnownBits handle it.  Only do this on a
    // recursive call where Known may be useful to the caller.
    if (Depth > 0) {
      Known = TLO.DAG.computeKnownBits(Op, DemandedElts, Depth);
      return false;
    }
    break;
  }
  case ISD::MUL:
    if (DemandedBits.isPowerOf2()) {
      // The LSB of X*Y is set only if (X & 1) == 1 and (Y & 1) == 1.
      // If we demand exactly one bit N and we have "X * (C' << N)" where C' is
      // odd (has LSB set), then the left-shifted low bit of X is the answer.
      unsigned CTZ = DemandedBits.countr_zero();
      ConstantSDNode *C = isConstOrConstSplat(Op.getOperand(1), DemandedElts);
      if (C && C->getAPIntValue().countr_zero() == CTZ) {
        SDValue AmtC = TLO.DAG.getShiftAmountConstant(CTZ, VT, dl);
        SDValue Shl = TLO.DAG.getNode(ISD::SHL, dl, VT, Op.getOperand(0), AmtC);
        return TLO.CombineTo(Op, Shl);
      }
    }
    // For a squared value "X * X", the bottom 2 bits are 0 and X[0] because:
    // X * X is odd iff X is odd.
    // 'Quadratic Reciprocity': X * X -> 0 for bit[1]
    if (Op.getOperand(0) == Op.getOperand(1) && DemandedBits.ult(4)) {
      SDValue One = TLO.DAG.getConstant(1, dl, VT);
      SDValue And1 = TLO.DAG.getNode(ISD::AND, dl, VT, Op.getOperand(0), One);
      return TLO.CombineTo(Op, And1);
    }
    [[fallthrough]];
  case ISD::ADD:
  case ISD::SUB: {
    // Add, Sub, and Mul don't demand any bits in positions beyond that
    // of the highest bit demanded of them.
    SDValue Op0 = Op.getOperand(0), Op1 = Op.getOperand(1);
    SDNodeFlags Flags = Op.getNode()->getFlags();
    unsigned DemandedBitsLZ = DemandedBits.countl_zero();
    APInt LoMask = APInt::getLowBitsSet(BitWidth, BitWidth - DemandedBitsLZ);
    KnownBits KnownOp0, KnownOp1;
    auto GetDemandedBitsLHSMask = [&](APInt Demanded,
                                      const KnownBits &KnownRHS) {
      if (Op.getOpcode() == ISD::MUL)
        Demanded.clearHighBits(KnownRHS.countMinTrailingZeros());
      return Demanded;
    };
    if (SimplifyDemandedBits(Op1, LoMask, DemandedElts, KnownOp1, TLO,
                             Depth + 1) ||
        SimplifyDemandedBits(Op0, GetDemandedBitsLHSMask(LoMask, KnownOp1),
                             DemandedElts, KnownOp0, TLO, Depth + 1) ||
        // See if the operation should be performed at a smaller bit width.
        ShrinkDemandedOp(Op, BitWidth, DemandedBits, TLO)) {
      if (Flags.hasNoSignedWrap() || Flags.hasNoUnsignedWrap()) {
        // Disable the nsw and nuw flags. We can no longer guarantee that we
        // won't wrap after simplification.
        Flags.setNoSignedWrap(false);
        Flags.setNoUnsignedWrap(false);
        Op->setFlags(Flags);
      }
      return true;
    }

    // neg x with only low bit demanded is simply x.
    if (Op.getOpcode() == ISD::SUB && DemandedBits.isOne() &&
        isNullConstant(Op0))
      return TLO.CombineTo(Op, Op1);

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (!LoMask.isAllOnes() || !DemandedElts.isAllOnes()) {
      SDValue DemandedOp0 = SimplifyMultipleUseDemandedBits(
          Op0, LoMask, DemandedElts, TLO.DAG, Depth + 1);
      SDValue DemandedOp1 = SimplifyMultipleUseDemandedBits(
          Op1, LoMask, DemandedElts, TLO.DAG, Depth + 1);
      if (DemandedOp0 || DemandedOp1) {
        Flags.setNoSignedWrap(false);
        Flags.setNoUnsignedWrap(false);
        Op0 = DemandedOp0 ? DemandedOp0 : Op0;
        Op1 = DemandedOp1 ? DemandedOp1 : Op1;
        SDValue NewOp =
            TLO.DAG.getNode(Op.getOpcode(), dl, VT, Op0, Op1, Flags);
        return TLO.CombineTo(Op, NewOp);
      }
    }

    // If we have a constant operand, we may be able to turn it into -1 if we
    // do not demand the high bits. This can make the constant smaller to
    // encode, allow more general folding, or match specialized instruction
    // patterns (eg, 'blsr' on x86). Don't bother changing 1 to -1 because that
    // is probably not useful (and could be detrimental).
    ConstantSDNode *C = isConstOrConstSplat(Op1);
    APInt HighMask = APInt::getHighBitsSet(BitWidth, DemandedBitsLZ);
    if (C && !C->isAllOnes() && !C->isOne() &&
        (C->getAPIntValue() | HighMask).isAllOnes()) {
      SDValue Neg1 = TLO.DAG.getAllOnesConstant(dl, VT);
      // Disable the nsw and nuw flags. We can no longer guarantee that we
      // won't wrap after simplification.
      Flags.setNoSignedWrap(false);
      Flags.setNoUnsignedWrap(false);
      SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), dl, VT, Op0, Neg1, Flags);
      return TLO.CombineTo(Op, NewOp);
    }

    // Match a multiply with a disguised negated-power-of-2 and convert to a
    // an equivalent shift-left amount.
    // Example: (X * MulC) + Op1 --> Op1 - (X << log2(-MulC))
    auto getShiftLeftAmt = [&HighMask](SDValue Mul) -> unsigned {
      if (Mul.getOpcode() != ISD::MUL || !Mul.hasOneUse())
        return 0;

      // Don't touch opaque constants. Also, ignore zero and power-of-2
      // multiplies. Those will get folded later.
      ConstantSDNode *MulC = isConstOrConstSplat(Mul.getOperand(1));
      if (MulC && !MulC->isOpaque() && !MulC->isZero() &&
          !MulC->getAPIntValue().isPowerOf2()) {
        APInt UnmaskedC = MulC->getAPIntValue() | HighMask;
        if (UnmaskedC.isNegatedPowerOf2())
          return (-UnmaskedC).logBase2();
      }
      return 0;
    };

    auto foldMul = [&](ISD::NodeType NT, SDValue X, SDValue Y,
                       unsigned ShlAmt) {
      SDValue ShlAmtC = TLO.DAG.getShiftAmountConstant(ShlAmt, VT, dl);
      SDValue Shl = TLO.DAG.getNode(ISD::SHL, dl, VT, X, ShlAmtC);
      SDValue Res = TLO.DAG.getNode(NT, dl, VT, Y, Shl);
      return TLO.CombineTo(Op, Res);
    };

    if (isOperationLegalOrCustom(ISD::SHL, VT)) {
      if (Op.getOpcode() == ISD::ADD) {
        // (X * MulC) + Op1 --> Op1 - (X << log2(-MulC))
        if (unsigned ShAmt = getShiftLeftAmt(Op0))
          return foldMul(ISD::SUB, Op0.getOperand(0), Op1, ShAmt);
        // Op0 + (X * MulC) --> Op0 - (X << log2(-MulC))
        if (unsigned ShAmt = getShiftLeftAmt(Op1))
          return foldMul(ISD::SUB, Op1.getOperand(0), Op0, ShAmt);
      }
      if (Op.getOpcode() == ISD::SUB) {
        // Op0 - (X * MulC) --> Op0 + (X << log2(-MulC))
        if (unsigned ShAmt = getShiftLeftAmt(Op1))
          return foldMul(ISD::ADD, Op1.getOperand(0), Op0, ShAmt);
      }
    }

    if (Op.getOpcode() == ISD::MUL) {
      Known = KnownBits::mul(KnownOp0, KnownOp1);
    } else { // Op.getOpcode() is either ISD::ADD or ISD::SUB.
      Known = KnownBits::computeForAddSub(
          Op.getOpcode() == ISD::ADD, Flags.hasNoSignedWrap(),
          Flags.hasNoUnsignedWrap(), KnownOp0, KnownOp1);
    }
    break;
  }
  default:
    // We also ask the target about intrinsics (which could be specific to it).
    if (Op.getOpcode() >= ISD::BUILTIN_OP_END ||
        Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN) {
      // TODO: Probably okay to remove after audit; here to reduce change size
      // in initial enablement patch for scalable vectors
      if (Op.getValueType().isScalableVector())
        break;
      if (SimplifyDemandedBitsForTargetNode(Op, DemandedBits, DemandedElts,
                                            Known, TLO, Depth))
        return true;
      break;
    }

    // Just use computeKnownBits to compute output bits.
    Known = TLO.DAG.computeKnownBits(Op, DemandedElts, Depth);
    break;
  }

  // If we know the value of all of the demanded bits, return this as a
  // constant.
  if (!isTargetCanonicalConstantNode(Op) &&
      DemandedBits.isSubsetOf(Known.Zero | Known.One)) {
    // Avoid folding to a constant if any OpaqueConstant is involved.
    const SDNode *N = Op.getNode();
    for (SDNode *Op :
         llvm::make_range(SDNodeIterator::begin(N), SDNodeIterator::end(N))) {
      if (auto *C = dyn_cast<ConstantSDNode>(Op))
        if (C->isOpaque())
          return false;
    }
    if (VT.isInteger())
      return TLO.CombineTo(Op, TLO.DAG.getConstant(Known.One, dl, VT));
    if (VT.isFloatingPoint())
      return TLO.CombineTo(
          Op,
          TLO.DAG.getConstantFP(
              APFloat(TLO.DAG.EVTToAPFloatSemantics(VT), Known.One), dl, VT));
  }

  // A multi use 'all demanded elts' simplify failed to find any knownbits.
  // Try again just for the original demanded elts.
  // Ensure we do this AFTER constant folding above.
  if (HasMultiUse && Known.isUnknown() && !OriginalDemandedElts.isAllOnes())
    Known = TLO.DAG.computeKnownBits(Op, OriginalDemandedElts, Depth);

  return false;
}

bool TargetLowering::SimplifyDemandedVectorElts(SDValue Op,
                                                const APInt &DemandedElts,
                                                DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  TargetLoweringOpt TLO(DAG, !DCI.isBeforeLegalize(),
                        !DCI.isBeforeLegalizeOps());

  APInt KnownUndef, KnownZero;
  bool Simplified =
      SimplifyDemandedVectorElts(Op, DemandedElts, KnownUndef, KnownZero, TLO);
  if (Simplified) {
    DCI.AddToWorklist(Op.getNode());
    DCI.CommitTargetLoweringOpt(TLO);
  }

  return Simplified;
}

/// Given a vector binary operation and known undefined elements for each input
/// operand, compute whether each element of the output is undefined.
static APInt getKnownUndefForVectorBinop(SDValue BO, SelectionDAG &DAG,
                                         const APInt &UndefOp0,
                                         const APInt &UndefOp1) {
  EVT VT = BO.getValueType();
  assert(DAG.getTargetLoweringInfo().isBinOp(BO.getOpcode()) && VT.isVector() &&
         "Vector binop only");

  EVT EltVT = VT.getVectorElementType();
  unsigned NumElts = VT.isFixedLengthVector() ? VT.getVectorNumElements() : 1;
  assert(UndefOp0.getBitWidth() == NumElts &&
         UndefOp1.getBitWidth() == NumElts && "Bad type for undef analysis");

  auto getUndefOrConstantElt = [&](SDValue V, unsigned Index,
                                   const APInt &UndefVals) {
    if (UndefVals[Index])
      return DAG.getUNDEF(EltVT);

    if (auto *BV = dyn_cast<BuildVectorSDNode>(V)) {
      // Try hard to make sure that the getNode() call is not creating temporary
      // nodes. Ignore opaque integers because they do not constant fold.
      SDValue Elt = BV->getOperand(Index);
      auto *C = dyn_cast<ConstantSDNode>(Elt);
      if (isa<ConstantFPSDNode>(Elt) || Elt.isUndef() || (C && !C->isOpaque()))
        return Elt;
    }

    return SDValue();
  };

  APInt KnownUndef = APInt::getZero(NumElts);
  for (unsigned i = 0; i != NumElts; ++i) {
    // If both inputs for this element are either constant or undef and match
    // the element type, compute the constant/undef result for this element of
    // the vector.
    // TODO: Ideally we would use FoldConstantArithmetic() here, but that does
    // not handle FP constants. The code within getNode() should be refactored
    // to avoid the danger of creating a bogus temporary node here.
    SDValue C0 = getUndefOrConstantElt(BO.getOperand(0), i, UndefOp0);
    SDValue C1 = getUndefOrConstantElt(BO.getOperand(1), i, UndefOp1);
    if (C0 && C1 && C0.getValueType() == EltVT && C1.getValueType() == EltVT)
      if (DAG.getNode(BO.getOpcode(), SDLoc(BO), EltVT, C0, C1).isUndef())
        KnownUndef.setBit(i);
  }
  return KnownUndef;
}

bool TargetLowering::SimplifyDemandedVectorElts(
    SDValue Op, const APInt &OriginalDemandedElts, APInt &KnownUndef,
    APInt &KnownZero, TargetLoweringOpt &TLO, unsigned Depth,
    bool AssumeSingleUse) const {
  EVT VT = Op.getValueType();
  unsigned Opcode = Op.getOpcode();
  APInt DemandedElts = OriginalDemandedElts;
  unsigned NumElts = DemandedElts.getBitWidth();
  assert(VT.isVector() && "Expected vector op");

  KnownUndef = KnownZero = APInt::getZero(NumElts);

  const TargetLowering &TLI = TLO.DAG.getTargetLoweringInfo();
  if (!TLI.shouldSimplifyDemandedVectorElts(Op, TLO))
    return false;

  // TODO: For now we assume we know nothing about scalable vectors.
  if (VT.isScalableVector())
    return false;

  assert(VT.getVectorNumElements() == NumElts &&
         "Mask size mismatches value type element count!");

  // Undef operand.
  if (Op.isUndef()) {
    KnownUndef.setAllBits();
    return false;
  }

  // If Op has other users, assume that all elements are needed.
  if (!AssumeSingleUse && !Op.getNode()->hasOneUse())
    DemandedElts.setAllBits();

  // Not demanding any elements from Op.
  if (DemandedElts == 0) {
    KnownUndef.setAllBits();
    return TLO.CombineTo(Op, TLO.DAG.getUNDEF(VT));
  }

  // Limit search depth.
  if (Depth >= SelectionDAG::MaxRecursionDepth)
    return false;

  SDLoc DL(Op);
  unsigned EltSizeInBits = VT.getScalarSizeInBits();
  bool IsLE = TLO.DAG.getDataLayout().isLittleEndian();

  // Helper for demanding the specified elements and all the bits of both binary
  // operands.
  auto SimplifyDemandedVectorEltsBinOp = [&](SDValue Op0, SDValue Op1) {
    SDValue NewOp0 = SimplifyMultipleUseDemandedVectorElts(Op0, DemandedElts,
                                                           TLO.DAG, Depth + 1);
    SDValue NewOp1 = SimplifyMultipleUseDemandedVectorElts(Op1, DemandedElts,
                                                           TLO.DAG, Depth + 1);
    if (NewOp0 || NewOp1) {
      SDValue NewOp =
          TLO.DAG.getNode(Opcode, SDLoc(Op), VT, NewOp0 ? NewOp0 : Op0,
                          NewOp1 ? NewOp1 : Op1, Op->getFlags());
      return TLO.CombineTo(Op, NewOp);
    }
    return false;
  };

  switch (Opcode) {
  case ISD::SCALAR_TO_VECTOR: {
    if (!DemandedElts[0]) {
      KnownUndef.setAllBits();
      return TLO.CombineTo(Op, TLO.DAG.getUNDEF(VT));
    }
    SDValue ScalarSrc = Op.getOperand(0);
    if (ScalarSrc.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
      SDValue Src = ScalarSrc.getOperand(0);
      SDValue Idx = ScalarSrc.getOperand(1);
      EVT SrcVT = Src.getValueType();

      ElementCount SrcEltCnt = SrcVT.getVectorElementCount();

      if (SrcEltCnt.isScalable())
        return false;

      unsigned NumSrcElts = SrcEltCnt.getFixedValue();
      if (isNullConstant(Idx)) {
        APInt SrcDemandedElts = APInt::getOneBitSet(NumSrcElts, 0);
        APInt SrcUndef = KnownUndef.zextOrTrunc(NumSrcElts);
        APInt SrcZero = KnownZero.zextOrTrunc(NumSrcElts);
        if (SimplifyDemandedVectorElts(Src, SrcDemandedElts, SrcUndef, SrcZero,
                                       TLO, Depth + 1))
          return true;
      }
    }
    KnownUndef.setHighBits(NumElts - 1);
    break;
  }
  case ISD::BITCAST: {
    SDValue Src = Op.getOperand(0);
    EVT SrcVT = Src.getValueType();

    // We only handle vectors here.
    // TODO - investigate calling SimplifyDemandedBits/ComputeKnownBits?
    if (!SrcVT.isVector())
      break;

    // Fast handling of 'identity' bitcasts.
    unsigned NumSrcElts = SrcVT.getVectorNumElements();
    if (NumSrcElts == NumElts)
      return SimplifyDemandedVectorElts(Src, DemandedElts, KnownUndef,
                                        KnownZero, TLO, Depth + 1);

    APInt SrcDemandedElts, SrcZero, SrcUndef;

    // Bitcast from 'large element' src vector to 'small element' vector, we
    // must demand a source element if any DemandedElt maps to it.
    if ((NumElts % NumSrcElts) == 0) {
      unsigned Scale = NumElts / NumSrcElts;
      SrcDemandedElts = APIntOps::ScaleBitMask(DemandedElts, NumSrcElts);
      if (SimplifyDemandedVectorElts(Src, SrcDemandedElts, SrcUndef, SrcZero,
                                     TLO, Depth + 1))
        return true;

      // Try calling SimplifyDemandedBits, converting demanded elts to the bits
      // of the large element.
      // TODO - bigendian once we have test coverage.
      if (IsLE) {
        unsigned SrcEltSizeInBits = SrcVT.getScalarSizeInBits();
        APInt SrcDemandedBits = APInt::getZero(SrcEltSizeInBits);
        for (unsigned i = 0; i != NumElts; ++i)
          if (DemandedElts[i]) {
            unsigned Ofs = (i % Scale) * EltSizeInBits;
            SrcDemandedBits.setBits(Ofs, Ofs + EltSizeInBits);
          }

        KnownBits Known;
        if (SimplifyDemandedBits(Src, SrcDemandedBits, SrcDemandedElts, Known,
                                 TLO, Depth + 1))
          return true;

        // The bitcast has split each wide element into a number of
        // narrow subelements. We have just computed the Known bits
        // for wide elements. See if element splitting results in
        // some subelements being zero. Only for demanded elements!
        for (unsigned SubElt = 0; SubElt != Scale; ++SubElt) {
          if (!Known.Zero.extractBits(EltSizeInBits, SubElt * EltSizeInBits)
                   .isAllOnes())
            continue;
          for (unsigned SrcElt = 0; SrcElt != NumSrcElts; ++SrcElt) {
            unsigned Elt = Scale * SrcElt + SubElt;
            if (DemandedElts[Elt])
              KnownZero.setBit(Elt);
          }
        }
      }

      // If the src element is zero/undef then all the output elements will be -
      // only demanded elements are guaranteed to be correct.
      for (unsigned i = 0; i != NumSrcElts; ++i) {
        if (SrcDemandedElts[i]) {
          if (SrcZero[i])
            KnownZero.setBits(i * Scale, (i + 1) * Scale);
          if (SrcUndef[i])
            KnownUndef.setBits(i * Scale, (i + 1) * Scale);
        }
      }
    }

    // Bitcast from 'small element' src vector to 'large element' vector, we
    // demand all smaller source elements covered by the larger demanded element
    // of this vector.
    if ((NumSrcElts % NumElts) == 0) {
      unsigned Scale = NumSrcElts / NumElts;
      SrcDemandedElts = APIntOps::ScaleBitMask(DemandedElts, NumSrcElts);
      if (SimplifyDemandedVectorElts(Src, SrcDemandedElts, SrcUndef, SrcZero,
                                     TLO, Depth + 1))
        return true;

      // If all the src elements covering an output element are zero/undef, then
      // the output element will be as well, assuming it was demanded.
      for (unsigned i = 0; i != NumElts; ++i) {
        if (DemandedElts[i]) {
          if (SrcZero.extractBits(Scale, i * Scale).isAllOnes())
            KnownZero.setBit(i);
          if (SrcUndef.extractBits(Scale, i * Scale).isAllOnes())
            KnownUndef.setBit(i);
        }
      }
    }
    break;
  }
  case ISD::FREEZE: {
    SDValue N0 = Op.getOperand(0);
    if (TLO.DAG.isGuaranteedNotToBeUndefOrPoison(N0, DemandedElts,
                                                 /*PoisonOnly=*/false))
      return TLO.CombineTo(Op, N0);

    // TODO: Replace this with the general fold from DAGCombiner::visitFREEZE
    // freeze(op(x, ...)) -> op(freeze(x), ...).
    if (N0.getOpcode() == ISD::SCALAR_TO_VECTOR && DemandedElts == 1)
      return TLO.CombineTo(
          Op, TLO.DAG.getNode(ISD::SCALAR_TO_VECTOR, DL, VT,
                              TLO.DAG.getFreeze(N0.getOperand(0))));
    break;
  }
  case ISD::BUILD_VECTOR: {
    // Check all elements and simplify any unused elements with UNDEF.
    if (!DemandedElts.isAllOnes()) {
      // Don't simplify BROADCASTS.
      if (llvm::any_of(Op->op_values(),
                       [&](SDValue Elt) { return Op.getOperand(0) != Elt; })) {
        SmallVector<SDValue, 32> Ops(Op->op_begin(), Op->op_end());
        bool Updated = false;
        for (unsigned i = 0; i != NumElts; ++i) {
          if (!DemandedElts[i] && !Ops[i].isUndef()) {
            Ops[i] = TLO.DAG.getUNDEF(Ops[0].getValueType());
            KnownUndef.setBit(i);
            Updated = true;
          }
        }
        if (Updated)
          return TLO.CombineTo(Op, TLO.DAG.getBuildVector(VT, DL, Ops));
      }
    }
    for (unsigned i = 0; i != NumElts; ++i) {
      SDValue SrcOp = Op.getOperand(i);
      if (SrcOp.isUndef()) {
        KnownUndef.setBit(i);
      } else if (EltSizeInBits == SrcOp.getScalarValueSizeInBits() &&
                 (isNullConstant(SrcOp) || isNullFPConstant(SrcOp))) {
        KnownZero.setBit(i);
      }
    }
    break;
  }
  case ISD::CONCAT_VECTORS: {
    EVT SubVT = Op.getOperand(0).getValueType();
    unsigned NumSubVecs = Op.getNumOperands();
    unsigned NumSubElts = SubVT.getVectorNumElements();
    for (unsigned i = 0; i != NumSubVecs; ++i) {
      SDValue SubOp = Op.getOperand(i);
      APInt SubElts = DemandedElts.extractBits(NumSubElts, i * NumSubElts);
      APInt SubUndef, SubZero;
      if (SimplifyDemandedVectorElts(SubOp, SubElts, SubUndef, SubZero, TLO,
                                     Depth + 1))
        return true;
      KnownUndef.insertBits(SubUndef, i * NumSubElts);
      KnownZero.insertBits(SubZero, i * NumSubElts);
    }

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (!DemandedElts.isAllOnes()) {
      bool FoundNewSub = false;
      SmallVector<SDValue, 2> DemandedSubOps;
      for (unsigned i = 0; i != NumSubVecs; ++i) {
        SDValue SubOp = Op.getOperand(i);
        APInt SubElts = DemandedElts.extractBits(NumSubElts, i * NumSubElts);
        SDValue NewSubOp = SimplifyMultipleUseDemandedVectorElts(
            SubOp, SubElts, TLO.DAG, Depth + 1);
        DemandedSubOps.push_back(NewSubOp ? NewSubOp : SubOp);
        FoundNewSub = NewSubOp ? true : FoundNewSub;
      }
      if (FoundNewSub) {
        SDValue NewOp =
            TLO.DAG.getNode(Op.getOpcode(), SDLoc(Op), VT, DemandedSubOps);
        return TLO.CombineTo(Op, NewOp);
      }
    }
    break;
  }
  case ISD::INSERT_SUBVECTOR: {
    // Demand any elements from the subvector and the remainder from the src its
    // inserted into.
    SDValue Src = Op.getOperand(0);
    SDValue Sub = Op.getOperand(1);
    uint64_t Idx = Op.getConstantOperandVal(2);
    unsigned NumSubElts = Sub.getValueType().getVectorNumElements();
    APInt DemandedSubElts = DemandedElts.extractBits(NumSubElts, Idx);
    APInt DemandedSrcElts = DemandedElts;
    DemandedSrcElts.insertBits(APInt::getZero(NumSubElts), Idx);

    APInt SubUndef, SubZero;
    if (SimplifyDemandedVectorElts(Sub, DemandedSubElts, SubUndef, SubZero, TLO,
                                   Depth + 1))
      return true;

    // If none of the src operand elements are demanded, replace it with undef.
    if (!DemandedSrcElts && !Src.isUndef())
      return TLO.CombineTo(Op, TLO.DAG.getNode(ISD::INSERT_SUBVECTOR, DL, VT,
                                               TLO.DAG.getUNDEF(VT), Sub,
                                               Op.getOperand(2)));

    if (SimplifyDemandedVectorElts(Src, DemandedSrcElts, KnownUndef, KnownZero,
                                   TLO, Depth + 1))
      return true;
    KnownUndef.insertBits(SubUndef, Idx);
    KnownZero.insertBits(SubZero, Idx);

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (!DemandedSrcElts.isAllOnes() || !DemandedSubElts.isAllOnes()) {
      SDValue NewSrc = SimplifyMultipleUseDemandedVectorElts(
          Src, DemandedSrcElts, TLO.DAG, Depth + 1);
      SDValue NewSub = SimplifyMultipleUseDemandedVectorElts(
          Sub, DemandedSubElts, TLO.DAG, Depth + 1);
      if (NewSrc || NewSub) {
        NewSrc = NewSrc ? NewSrc : Src;
        NewSub = NewSub ? NewSub : Sub;
        SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), SDLoc(Op), VT, NewSrc,
                                        NewSub, Op.getOperand(2));
        return TLO.CombineTo(Op, NewOp);
      }
    }
    break;
  }
  case ISD::EXTRACT_SUBVECTOR: {
    // Offset the demanded elts by the subvector index.
    SDValue Src = Op.getOperand(0);
    if (Src.getValueType().isScalableVector())
      break;
    uint64_t Idx = Op.getConstantOperandVal(1);
    unsigned NumSrcElts = Src.getValueType().getVectorNumElements();
    APInt DemandedSrcElts = DemandedElts.zext(NumSrcElts).shl(Idx);

    APInt SrcUndef, SrcZero;
    if (SimplifyDemandedVectorElts(Src, DemandedSrcElts, SrcUndef, SrcZero, TLO,
                                   Depth + 1))
      return true;
    KnownUndef = SrcUndef.extractBits(NumElts, Idx);
    KnownZero = SrcZero.extractBits(NumElts, Idx);

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (!DemandedElts.isAllOnes()) {
      SDValue NewSrc = SimplifyMultipleUseDemandedVectorElts(
          Src, DemandedSrcElts, TLO.DAG, Depth + 1);
      if (NewSrc) {
        SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), SDLoc(Op), VT, NewSrc,
                                        Op.getOperand(1));
        return TLO.CombineTo(Op, NewOp);
      }
    }
    break;
  }
  case ISD::INSERT_VECTOR_ELT: {
    SDValue Vec = Op.getOperand(0);
    SDValue Scl = Op.getOperand(1);
    auto *CIdx = dyn_cast<ConstantSDNode>(Op.getOperand(2));

    // For a legal, constant insertion index, if we don't need this insertion
    // then strip it, else remove it from the demanded elts.
    if (CIdx && CIdx->getAPIntValue().ult(NumElts)) {
      unsigned Idx = CIdx->getZExtValue();
      if (!DemandedElts[Idx])
        return TLO.CombineTo(Op, Vec);

      APInt DemandedVecElts(DemandedElts);
      DemandedVecElts.clearBit(Idx);
      if (SimplifyDemandedVectorElts(Vec, DemandedVecElts, KnownUndef,
                                     KnownZero, TLO, Depth + 1))
        return true;

      KnownUndef.setBitVal(Idx, Scl.isUndef());

      KnownZero.setBitVal(Idx, isNullConstant(Scl) || isNullFPConstant(Scl));
      break;
    }

    APInt VecUndef, VecZero;
    if (SimplifyDemandedVectorElts(Vec, DemandedElts, VecUndef, VecZero, TLO,
                                   Depth + 1))
      return true;
    // Without knowing the insertion index we can't set KnownUndef/KnownZero.
    break;
  }
  case ISD::VSELECT: {
    SDValue Sel = Op.getOperand(0);
    SDValue LHS = Op.getOperand(1);
    SDValue RHS = Op.getOperand(2);

    // Try to transform the select condition based on the current demanded
    // elements.
    APInt UndefSel, ZeroSel;
    if (SimplifyDemandedVectorElts(Sel, DemandedElts, UndefSel, ZeroSel, TLO,
                                   Depth + 1))
      return true;

    // See if we can simplify either vselect operand.
    APInt DemandedLHS(DemandedElts);
    APInt DemandedRHS(DemandedElts);
    APInt UndefLHS, ZeroLHS;
    APInt UndefRHS, ZeroRHS;
    if (SimplifyDemandedVectorElts(LHS, DemandedLHS, UndefLHS, ZeroLHS, TLO,
                                   Depth + 1))
      return true;
    if (SimplifyDemandedVectorElts(RHS, DemandedRHS, UndefRHS, ZeroRHS, TLO,
                                   Depth + 1))
      return true;

    KnownUndef = UndefLHS & UndefRHS;
    KnownZero = ZeroLHS & ZeroRHS;

    // If we know that the selected element is always zero, we don't need the
    // select value element.
    APInt DemandedSel = DemandedElts & ~KnownZero;
    if (DemandedSel != DemandedElts)
      if (SimplifyDemandedVectorElts(Sel, DemandedSel, UndefSel, ZeroSel, TLO,
                                     Depth + 1))
        return true;

    break;
  }
  case ISD::VECTOR_SHUFFLE: {
    SDValue LHS = Op.getOperand(0);
    SDValue RHS = Op.getOperand(1);
    ArrayRef<int> ShuffleMask = cast<ShuffleVectorSDNode>(Op)->getMask();

    // Collect demanded elements from shuffle operands..
    APInt DemandedLHS(NumElts, 0);
    APInt DemandedRHS(NumElts, 0);
    for (unsigned i = 0; i != NumElts; ++i) {
      int M = ShuffleMask[i];
      if (M < 0 || !DemandedElts[i])
        continue;
      assert(0 <= M && M < (int)(2 * NumElts) && "Shuffle index out of range");
      if (M < (int)NumElts)
        DemandedLHS.setBit(M);
      else
        DemandedRHS.setBit(M - NumElts);
    }

    // See if we can simplify either shuffle operand.
    APInt UndefLHS, ZeroLHS;
    APInt UndefRHS, ZeroRHS;
    if (SimplifyDemandedVectorElts(LHS, DemandedLHS, UndefLHS, ZeroLHS, TLO,
                                   Depth + 1))
      return true;
    if (SimplifyDemandedVectorElts(RHS, DemandedRHS, UndefRHS, ZeroRHS, TLO,
                                   Depth + 1))
      return true;

    // Simplify mask using undef elements from LHS/RHS.
    bool Updated = false;
    bool IdentityLHS = true, IdentityRHS = true;
    SmallVector<int, 32> NewMask(ShuffleMask);
    for (unsigned i = 0; i != NumElts; ++i) {
      int &M = NewMask[i];
      if (M < 0)
        continue;
      if (!DemandedElts[i] || (M < (int)NumElts && UndefLHS[M]) ||
          (M >= (int)NumElts && UndefRHS[M - NumElts])) {
        Updated = true;
        M = -1;
      }
      IdentityLHS &= (M < 0) || (M == (int)i);
      IdentityRHS &= (M < 0) || ((M - NumElts) == i);
    }

    // Update legal shuffle masks based on demanded elements if it won't reduce
    // to Identity which can cause premature removal of the shuffle mask.
    if (Updated && !IdentityLHS && !IdentityRHS && !TLO.LegalOps) {
      SDValue LegalShuffle =
          buildLegalVectorShuffle(VT, DL, LHS, RHS, NewMask, TLO.DAG);
      if (LegalShuffle)
        return TLO.CombineTo(Op, LegalShuffle);
    }

    // Propagate undef/zero elements from LHS/RHS.
    for (unsigned i = 0; i != NumElts; ++i) {
      int M = ShuffleMask[i];
      if (M < 0) {
        KnownUndef.setBit(i);
      } else if (M < (int)NumElts) {
        if (UndefLHS[M])
          KnownUndef.setBit(i);
        if (ZeroLHS[M])
          KnownZero.setBit(i);
      } else {
        if (UndefRHS[M - NumElts])
          KnownUndef.setBit(i);
        if (ZeroRHS[M - NumElts])
          KnownZero.setBit(i);
      }
    }
    break;
  }
  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG: {
    APInt SrcUndef, SrcZero;
    SDValue Src = Op.getOperand(0);
    unsigned NumSrcElts = Src.getValueType().getVectorNumElements();
    APInt DemandedSrcElts = DemandedElts.zext(NumSrcElts);
    if (SimplifyDemandedVectorElts(Src, DemandedSrcElts, SrcUndef, SrcZero, TLO,
                                   Depth + 1))
      return true;
    KnownZero = SrcZero.zextOrTrunc(NumElts);
    KnownUndef = SrcUndef.zextOrTrunc(NumElts);

    if (IsLE && Op.getOpcode() == ISD::ANY_EXTEND_VECTOR_INREG &&
        Op.getValueSizeInBits() == Src.getValueSizeInBits() &&
        DemandedSrcElts == 1) {
      // aext - if we just need the bottom element then we can bitcast.
      return TLO.CombineTo(Op, TLO.DAG.getBitcast(VT, Src));
    }

    if (Op.getOpcode() == ISD::ZERO_EXTEND_VECTOR_INREG) {
      // zext(undef) upper bits are guaranteed to be zero.
      if (DemandedElts.isSubsetOf(KnownUndef))
        return TLO.CombineTo(Op, TLO.DAG.getConstant(0, SDLoc(Op), VT));
      KnownUndef.clearAllBits();

      // zext - if we just need the bottom element then we can mask:
      // zext(and(x,c)) -> and(x,c') iff the zext is the only user of the and.
      if (IsLE && DemandedSrcElts == 1 && Src.getOpcode() == ISD::AND &&
          Op->isOnlyUserOf(Src.getNode()) &&
          Op.getValueSizeInBits() == Src.getValueSizeInBits()) {
        SDLoc DL(Op);
        EVT SrcVT = Src.getValueType();
        EVT SrcSVT = SrcVT.getScalarType();
        SmallVector<SDValue> MaskElts;
        MaskElts.push_back(TLO.DAG.getAllOnesConstant(DL, SrcSVT));
        MaskElts.append(NumSrcElts - 1, TLO.DAG.getConstant(0, DL, SrcSVT));
        SDValue Mask = TLO.DAG.getBuildVector(SrcVT, DL, MaskElts);
        if (SDValue Fold = TLO.DAG.FoldConstantArithmetic(
                ISD::AND, DL, SrcVT, {Src.getOperand(1), Mask})) {
          Fold = TLO.DAG.getNode(ISD::AND, DL, SrcVT, Src.getOperand(0), Fold);
          return TLO.CombineTo(Op, TLO.DAG.getBitcast(VT, Fold));
        }
      }
    }
    break;
  }

  // TODO: There are more binop opcodes that could be handled here - MIN,
  // MAX, saturated math, etc.
  case ISD::ADD: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);
    if (Op0 == Op1 && Op->isOnlyUserOf(Op0.getNode())) {
      APInt UndefLHS, ZeroLHS;
      if (SimplifyDemandedVectorElts(Op0, DemandedElts, UndefLHS, ZeroLHS, TLO,
                                     Depth + 1, /*AssumeSingleUse*/ true))
        return true;
    }
    [[fallthrough]];
  }
  case ISD::AVGCEILS:
  case ISD::AVGCEILU:
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU:
  case ISD::OR:
  case ISD::XOR:
  case ISD::SUB:
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FDIV:
  case ISD::FREM: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);

    APInt UndefRHS, ZeroRHS;
    if (SimplifyDemandedVectorElts(Op1, DemandedElts, UndefRHS, ZeroRHS, TLO,
                                   Depth + 1))
      return true;
    APInt UndefLHS, ZeroLHS;
    if (SimplifyDemandedVectorElts(Op0, DemandedElts, UndefLHS, ZeroLHS, TLO,
                                   Depth + 1))
      return true;

    KnownZero = ZeroLHS & ZeroRHS;
    KnownUndef = getKnownUndefForVectorBinop(Op, TLO.DAG, UndefLHS, UndefRHS);

    // Attempt to avoid multi-use ops if we don't need anything from them.
    // TODO - use KnownUndef to relax the demandedelts?
    if (!DemandedElts.isAllOnes())
      if (SimplifyDemandedVectorEltsBinOp(Op0, Op1))
        return true;
    break;
  }
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
  case ISD::ROTL:
  case ISD::ROTR: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);

    APInt UndefRHS, ZeroRHS;
    if (SimplifyDemandedVectorElts(Op1, DemandedElts, UndefRHS, ZeroRHS, TLO,
                                   Depth + 1))
      return true;
    APInt UndefLHS, ZeroLHS;
    if (SimplifyDemandedVectorElts(Op0, DemandedElts, UndefLHS, ZeroLHS, TLO,
                                   Depth + 1))
      return true;

    KnownZero = ZeroLHS;
    KnownUndef = UndefLHS & UndefRHS; // TODO: use getKnownUndefForVectorBinop?

    // Attempt to avoid multi-use ops if we don't need anything from them.
    // TODO - use KnownUndef to relax the demandedelts?
    if (!DemandedElts.isAllOnes())
      if (SimplifyDemandedVectorEltsBinOp(Op0, Op1))
        return true;
    break;
  }
  case ISD::MUL:
  case ISD::MULHU:
  case ISD::MULHS:
  case ISD::AND: {
    SDValue Op0 = Op.getOperand(0);
    SDValue Op1 = Op.getOperand(1);

    APInt SrcUndef, SrcZero;
    if (SimplifyDemandedVectorElts(Op1, DemandedElts, SrcUndef, SrcZero, TLO,
                                   Depth + 1))
      return true;
    // If we know that a demanded element was zero in Op1 we don't need to
    // demand it in Op0 - its guaranteed to be zero.
    APInt DemandedElts0 = DemandedElts & ~SrcZero;
    if (SimplifyDemandedVectorElts(Op0, DemandedElts0, KnownUndef, KnownZero,
                                   TLO, Depth + 1))
      return true;

    KnownUndef &= DemandedElts0;
    KnownZero &= DemandedElts0;

    // If every element pair has a zero/undef then just fold to zero.
    // fold (and x, undef) -> 0  /  (and x, 0) -> 0
    // fold (mul x, undef) -> 0  /  (mul x, 0) -> 0
    if (DemandedElts.isSubsetOf(SrcZero | KnownZero | SrcUndef | KnownUndef))
      return TLO.CombineTo(Op, TLO.DAG.getConstant(0, SDLoc(Op), VT));

    // If either side has a zero element, then the result element is zero, even
    // if the other is an UNDEF.
    // TODO: Extend getKnownUndefForVectorBinop to also deal with known zeros
    // and then handle 'and' nodes with the rest of the binop opcodes.
    KnownZero |= SrcZero;
    KnownUndef &= SrcUndef;
    KnownUndef &= ~KnownZero;

    // Attempt to avoid multi-use ops if we don't need anything from them.
    if (!DemandedElts.isAllOnes())
      if (SimplifyDemandedVectorEltsBinOp(Op0, Op1))
        return true;
    break;
  }
  case ISD::TRUNCATE:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
    if (SimplifyDemandedVectorElts(Op.getOperand(0), DemandedElts, KnownUndef,
                                   KnownZero, TLO, Depth + 1))
      return true;

    if (Op.getOpcode() == ISD::ZERO_EXTEND) {
      // zext(undef) upper bits are guaranteed to be zero.
      if (DemandedElts.isSubsetOf(KnownUndef))
        return TLO.CombineTo(Op, TLO.DAG.getConstant(0, SDLoc(Op), VT));
      KnownUndef.clearAllBits();
    }
    break;
  default: {
    if (Op.getOpcode() >= ISD::BUILTIN_OP_END) {
      if (SimplifyDemandedVectorEltsForTargetNode(Op, DemandedElts, KnownUndef,
                                                  KnownZero, TLO, Depth))
        return true;
    } else {
      KnownBits Known;
      APInt DemandedBits = APInt::getAllOnes(EltSizeInBits);
      if (SimplifyDemandedBits(Op, DemandedBits, OriginalDemandedElts, Known,
                               TLO, Depth, AssumeSingleUse))
        return true;
    }
    break;
  }
  }
  assert((KnownUndef & KnownZero) == 0 && "Elements flagged as undef AND zero");

  // Constant fold all undef cases.
  // TODO: Handle zero cases as well.
  if (DemandedElts.isSubsetOf(KnownUndef))
    return TLO.CombineTo(Op, TLO.DAG.getUNDEF(VT));

  return false;
}

/// Determine which of the bits specified in Mask are known to be either zero or
/// one and return them in the Known.
void TargetLowering::computeKnownBitsForTargetNode(const SDValue Op,
                                                   KnownBits &Known,
                                                   const APInt &DemandedElts,
                                                   const SelectionDAG &DAG,
                                                   unsigned Depth) const {
  assert((Op.getOpcode() >= ISD::BUILTIN_OP_END ||
          Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_VOID) &&
         "Should use MaskedValueIsZero if you don't know whether Op"
         " is a target node!");
  Known.resetAll();
}

void TargetLowering::computeKnownBitsForTargetInstr(
    GISelKnownBits &Analysis, Register R, KnownBits &Known,
    const APInt &DemandedElts, const MachineRegisterInfo &MRI,
    unsigned Depth) const {
  Known.resetAll();
}

void TargetLowering::computeKnownBitsForFrameIndex(
  const int FrameIdx, KnownBits &Known, const MachineFunction &MF) const {
  // The low bits are known zero if the pointer is aligned.
  Known.Zero.setLowBits(Log2(MF.getFrameInfo().getObjectAlign(FrameIdx)));
}

Align TargetLowering::computeKnownAlignForTargetInstr(
  GISelKnownBits &Analysis, Register R, const MachineRegisterInfo &MRI,
  unsigned Depth) const {
  return Align(1);
}

/// This method can be implemented by targets that want to expose additional
/// information about sign bits to the DAG Combiner.
unsigned TargetLowering::ComputeNumSignBitsForTargetNode(SDValue Op,
                                                         const APInt &,
                                                         const SelectionDAG &,
                                                         unsigned Depth) const {
  assert((Op.getOpcode() >= ISD::BUILTIN_OP_END ||
          Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_VOID) &&
         "Should use ComputeNumSignBits if you don't know whether Op"
         " is a target node!");
  return 1;
}

unsigned TargetLowering::computeNumSignBitsForTargetInstr(
  GISelKnownBits &Analysis, Register R, const APInt &DemandedElts,
  const MachineRegisterInfo &MRI, unsigned Depth) const {
  return 1;
}

bool TargetLowering::SimplifyDemandedVectorEltsForTargetNode(
    SDValue Op, const APInt &DemandedElts, APInt &KnownUndef, APInt &KnownZero,
    TargetLoweringOpt &TLO, unsigned Depth) const {
  assert((Op.getOpcode() >= ISD::BUILTIN_OP_END ||
          Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_VOID) &&
         "Should use SimplifyDemandedVectorElts if you don't know whether Op"
         " is a target node!");
  return false;
}

bool TargetLowering::SimplifyDemandedBitsForTargetNode(
    SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
    KnownBits &Known, TargetLoweringOpt &TLO, unsigned Depth) const {
  assert((Op.getOpcode() >= ISD::BUILTIN_OP_END ||
          Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_VOID) &&
         "Should use SimplifyDemandedBits if you don't know whether Op"
         " is a target node!");
  computeKnownBitsForTargetNode(Op, Known, DemandedElts, TLO.DAG, Depth);
  return false;
}

SDValue TargetLowering::SimplifyMultipleUseDemandedBitsForTargetNode(
    SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
    SelectionDAG &DAG, unsigned Depth) const {
  assert(
      (Op.getOpcode() >= ISD::BUILTIN_OP_END ||
       Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
       Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
       Op.getOpcode() == ISD::INTRINSIC_VOID) &&
      "Should use SimplifyMultipleUseDemandedBits if you don't know whether Op"
      " is a target node!");
  return SDValue();
}

SDValue
TargetLowering::buildLegalVectorShuffle(EVT VT, const SDLoc &DL, SDValue N0,
                                        SDValue N1, MutableArrayRef<int> Mask,
                                        SelectionDAG &DAG) const {
  bool LegalMask = isShuffleMaskLegal(Mask, VT);
  if (!LegalMask) {
    std::swap(N0, N1);
    ShuffleVectorSDNode::commuteMask(Mask);
    LegalMask = isShuffleMaskLegal(Mask, VT);
  }

  if (!LegalMask)
    return SDValue();

  return DAG.getVectorShuffle(VT, DL, N0, N1, Mask);
}

const Constant *TargetLowering::getTargetConstantFromLoad(LoadSDNode*) const {
  return nullptr;
}

bool TargetLowering::isGuaranteedNotToBeUndefOrPoisonForTargetNode(
    SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
    bool PoisonOnly, unsigned Depth) const {
  assert(
      (Op.getOpcode() >= ISD::BUILTIN_OP_END ||
       Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
       Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
       Op.getOpcode() == ISD::INTRINSIC_VOID) &&
      "Should use isGuaranteedNotToBeUndefOrPoison if you don't know whether Op"
      " is a target node!");

  // If Op can't create undef/poison and none of its operands are undef/poison
  // then Op is never undef/poison.
  return !canCreateUndefOrPoisonForTargetNode(Op, DemandedElts, DAG, PoisonOnly,
                                              /*ConsiderFlags*/ true, Depth) &&
         all_of(Op->ops(), [&](SDValue V) {
           return DAG.isGuaranteedNotToBeUndefOrPoison(V, PoisonOnly,
                                                       Depth + 1);
         });
}

bool TargetLowering::canCreateUndefOrPoisonForTargetNode(
    SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
    bool PoisonOnly, bool ConsiderFlags, unsigned Depth) const {
  assert((Op.getOpcode() >= ISD::BUILTIN_OP_END ||
          Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_VOID) &&
         "Should use canCreateUndefOrPoison if you don't know whether Op"
         " is a target node!");
  // Be conservative and return true.
  return true;
}

bool TargetLowering::isKnownNeverNaNForTargetNode(SDValue Op,
                                                  const SelectionDAG &DAG,
                                                  bool SNaN,
                                                  unsigned Depth) const {
  assert((Op.getOpcode() >= ISD::BUILTIN_OP_END ||
          Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_VOID) &&
         "Should use isKnownNeverNaN if you don't know whether Op"
         " is a target node!");
  return false;
}

bool TargetLowering::isSplatValueForTargetNode(SDValue Op,
                                               const APInt &DemandedElts,
                                               APInt &UndefElts,
                                               const SelectionDAG &DAG,
                                               unsigned Depth) const {
  assert((Op.getOpcode() >= ISD::BUILTIN_OP_END ||
          Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
          Op.getOpcode() == ISD::INTRINSIC_VOID) &&
         "Should use isSplatValue if you don't know whether Op"
         " is a target node!");
  return false;
}

// FIXME: Ideally, this would use ISD::isConstantSplatVector(), but that must
// work with truncating build vectors and vectors with elements of less than
// 8 bits.
bool TargetLowering::isConstTrueVal(SDValue N) const {
  if (!N)
    return false;

  unsigned EltWidth;
  APInt CVal;
  if (ConstantSDNode *CN = isConstOrConstSplat(N, /*AllowUndefs=*/false,
                                               /*AllowTruncation=*/true)) {
    CVal = CN->getAPIntValue();
    EltWidth = N.getValueType().getScalarSizeInBits();
  } else
    return false;

  // If this is a truncating splat, truncate the splat value.
  // Otherwise, we may fail to match the expected values below.
  if (EltWidth < CVal.getBitWidth())
    CVal = CVal.trunc(EltWidth);

  switch (getBooleanContents(N.getValueType())) {
  case UndefinedBooleanContent:
    return CVal[0];
  case ZeroOrOneBooleanContent:
    return CVal.isOne();
  case ZeroOrNegativeOneBooleanContent:
    return CVal.isAllOnes();
  }

  llvm_unreachable("Invalid boolean contents");
}

bool TargetLowering::isConstFalseVal(SDValue N) const {
  if (!N)
    return false;

  const ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N);
  if (!CN) {
    const BuildVectorSDNode *BV = dyn_cast<BuildVectorSDNode>(N);
    if (!BV)
      return false;

    // Only interested in constant splats, we don't care about undef
    // elements in identifying boolean constants and getConstantSplatNode
    // returns NULL if all ops are undef;
    CN = BV->getConstantSplatNode();
    if (!CN)
      return false;
  }

  if (getBooleanContents(N->getValueType(0)) == UndefinedBooleanContent)
    return !CN->getAPIntValue()[0];

  return CN->isZero();
}

bool TargetLowering::isExtendedTrueVal(const ConstantSDNode *N, EVT VT,
                                       bool SExt) const {
  if (VT == MVT::i1)
    return N->isOne();

  TargetLowering::BooleanContent Cnt = getBooleanContents(VT);
  switch (Cnt) {
  case TargetLowering::ZeroOrOneBooleanContent:
    // An extended value of 1 is always true, unless its original type is i1,
    // in which case it will be sign extended to -1.
    return (N->isOne() && !SExt) || (SExt && (N->getValueType(0) != MVT::i1));
  case TargetLowering::UndefinedBooleanContent:
  case TargetLowering::ZeroOrNegativeOneBooleanContent:
    return N->isAllOnes() && SExt;
  }
  llvm_unreachable("Unexpected enumeration.");
}

/// This helper function of SimplifySetCC tries to optimize the comparison when
/// either operand of the SetCC node is a bitwise-and instruction.
SDValue TargetLowering::foldSetCCWithAnd(EVT VT, SDValue N0, SDValue N1,
                                         ISD::CondCode Cond, const SDLoc &DL,
                                         DAGCombinerInfo &DCI) const {
  if (N1.getOpcode() == ISD::AND && N0.getOpcode() != ISD::AND)
    std::swap(N0, N1);

  SelectionDAG &DAG = DCI.DAG;
  EVT OpVT = N0.getValueType();
  if (N0.getOpcode() != ISD::AND || !OpVT.isInteger() ||
      (Cond != ISD::SETEQ && Cond != ISD::SETNE))
    return SDValue();

  // (X & Y) != 0 --> zextOrTrunc(X & Y)
  // iff everything but LSB is known zero:
  if (Cond == ISD::SETNE && isNullConstant(N1) &&
      (getBooleanContents(OpVT) == TargetLowering::UndefinedBooleanContent ||
       getBooleanContents(OpVT) == TargetLowering::ZeroOrOneBooleanContent)) {
    unsigned NumEltBits = OpVT.getScalarSizeInBits();
    APInt UpperBits = APInt::getHighBitsSet(NumEltBits, NumEltBits - 1);
    if (DAG.MaskedValueIsZero(N0, UpperBits))
      return DAG.getBoolExtOrTrunc(N0, DL, VT, OpVT);
  }

  // Try to eliminate a power-of-2 mask constant by converting to a signbit
  // test in a narrow type that we can truncate to with no cost. Examples:
  // (i32 X & 32768) == 0 --> (trunc X to i16) >= 0
  // (i32 X & 32768) != 0 --> (trunc X to i16) < 0
  // TODO: This conservatively checks for type legality on the source and
  //       destination types. That may inhibit optimizations, but it also
  //       allows setcc->shift transforms that may be more beneficial.
  auto *AndC = dyn_cast<ConstantSDNode>(N0.getOperand(1));
  if (AndC && isNullConstant(N1) && AndC->getAPIntValue().isPowerOf2() &&
      isTypeLegal(OpVT) && N0.hasOneUse()) {
    EVT NarrowVT = EVT::getIntegerVT(*DAG.getContext(),
                                     AndC->getAPIntValue().getActiveBits());
    if (isTruncateFree(OpVT, NarrowVT) && isTypeLegal(NarrowVT)) {
      SDValue Trunc = DAG.getZExtOrTrunc(N0.getOperand(0), DL, NarrowVT);
      SDValue Zero = DAG.getConstant(0, DL, NarrowVT);
      return DAG.getSetCC(DL, VT, Trunc, Zero,
                          Cond == ISD::SETEQ ? ISD::SETGE : ISD::SETLT);
    }
  }

  // Match these patterns in any of their permutations:
  // (X & Y) == Y
  // (X & Y) != Y
  SDValue X, Y;
  if (N0.getOperand(0) == N1) {
    X = N0.getOperand(1);
    Y = N0.getOperand(0);
  } else if (N0.getOperand(1) == N1) {
    X = N0.getOperand(0);
    Y = N0.getOperand(1);
  } else {
    return SDValue();
  }

  // TODO: We should invert (X & Y) eq/ne 0 -> (X & Y) ne/eq Y if
  // `isXAndYEqZeroPreferableToXAndYEqY` is false. This is a bit difficult as
  // its liable to create and infinite loop.
  SDValue Zero = DAG.getConstant(0, DL, OpVT);
  if (isXAndYEqZeroPreferableToXAndYEqY(Cond, OpVT) &&
      DAG.isKnownToBeAPowerOfTwo(Y)) {
    // Simplify X & Y == Y to X & Y != 0 if Y has exactly one bit set.
    // Note that where Y is variable and is known to have at most one bit set
    // (for example, if it is Z & 1) we cannot do this; the expressions are not
    // equivalent when Y == 0.
    assert(OpVT.isInteger());
    Cond = ISD::getSetCCInverse(Cond, OpVT);
    if (DCI.isBeforeLegalizeOps() ||
        isCondCodeLegal(Cond, N0.getSimpleValueType()))
      return DAG.getSetCC(DL, VT, N0, Zero, Cond);
  } else if (N0.hasOneUse() && hasAndNotCompare(Y)) {
    // If the target supports an 'and-not' or 'and-complement' logic operation,
    // try to use that to make a comparison operation more efficient.
    // But don't do this transform if the mask is a single bit because there are
    // more efficient ways to deal with that case (for example, 'bt' on x86 or
    // 'rlwinm' on PPC).

    // Bail out if the compare operand that we want to turn into a zero is
    // already a zero (otherwise, infinite loop).
    if (isNullConstant(Y))
      return SDValue();

    // Transform this into: ~X & Y == 0.
    SDValue NotX = DAG.getNOT(SDLoc(X), X, OpVT);
    SDValue NewAnd = DAG.getNode(ISD::AND, SDLoc(N0), OpVT, NotX, Y);
    return DAG.getSetCC(DL, VT, NewAnd, Zero, Cond);
  }

  return SDValue();
}

/// There are multiple IR patterns that could be checking whether certain
/// truncation of a signed number would be lossy or not. The pattern which is
/// best at IR level, may not lower optimally. Thus, we want to unfold it.
/// We are looking for the following pattern: (KeptBits is a constant)
///   (add %x, (1 << (KeptBits-1))) srccond (1 << KeptBits)
/// KeptBits won't be bitwidth(x), that will be constant-folded to true/false.
/// KeptBits also can't be 1, that would have been folded to  %x dstcond 0
/// We will unfold it into the natural trunc+sext pattern:
///   ((%x << C) a>> C) dstcond %x
/// Where  C = bitwidth(x) - KeptBits  and  C u< bitwidth(x)
SDValue TargetLowering::optimizeSetCCOfSignedTruncationCheck(
    EVT SCCVT, SDValue N0, SDValue N1, ISD::CondCode Cond, DAGCombinerInfo &DCI,
    const SDLoc &DL) const {
  // We must be comparing with a constant.
  ConstantSDNode *C1;
  if (!(C1 = dyn_cast<ConstantSDNode>(N1)))
    return SDValue();

  // N0 should be:  add %x, (1 << (KeptBits-1))
  if (N0->getOpcode() != ISD::ADD)
    return SDValue();

  // And we must be 'add'ing a constant.
  ConstantSDNode *C01;
  if (!(C01 = dyn_cast<ConstantSDNode>(N0->getOperand(1))))
    return SDValue();

  SDValue X = N0->getOperand(0);
  EVT XVT = X.getValueType();

  // Validate constants ...

  APInt I1 = C1->getAPIntValue();

  ISD::CondCode NewCond;
  if (Cond == ISD::CondCode::SETULT) {
    NewCond = ISD::CondCode::SETEQ;
  } else if (Cond == ISD::CondCode::SETULE) {
    NewCond = ISD::CondCode::SETEQ;
    // But need to 'canonicalize' the constant.
    I1 += 1;
  } else if (Cond == ISD::CondCode::SETUGT) {
    NewCond = ISD::CondCode::SETNE;
    // But need to 'canonicalize' the constant.
    I1 += 1;
  } else if (Cond == ISD::CondCode::SETUGE) {
    NewCond = ISD::CondCode::SETNE;
  } else
    return SDValue();

  APInt I01 = C01->getAPIntValue();

  auto checkConstants = [&I1, &I01]() -> bool {
    // Both of them must be power-of-two, and the constant from setcc is bigger.
    return I1.ugt(I01) && I1.isPowerOf2() && I01.isPowerOf2();
  };

  if (checkConstants()) {
    // Great, e.g. got  icmp ult i16 (add i16 %x, 128), 256
  } else {
    // What if we invert constants? (and the target predicate)
    I1.negate();
    I01.negate();
    assert(XVT.isInteger());
    NewCond = getSetCCInverse(NewCond, XVT);
    if (!checkConstants())
      return SDValue();
    // Great, e.g. got  icmp uge i16 (add i16 %x, -128), -256
  }

  // They are power-of-two, so which bit is set?
  const unsigned KeptBits = I1.logBase2();
  const unsigned KeptBitsMinusOne = I01.logBase2();

  // Magic!
  if (KeptBits != (KeptBitsMinusOne + 1))
    return SDValue();
  assert(KeptBits > 0 && KeptBits < XVT.getSizeInBits() && "unreachable");

  // We don't want to do this in every single case.
  SelectionDAG &DAG = DCI.DAG;
  if (!DAG.getTargetLoweringInfo().shouldTransformSignedTruncationCheck(
          XVT, KeptBits))
    return SDValue();

  // Unfold into:  sext_inreg(%x) cond %x
  // Where 'cond' will be either 'eq' or 'ne'.
  SDValue SExtInReg = DAG.getNode(
      ISD::SIGN_EXTEND_INREG, DL, XVT, X,
      DAG.getValueType(EVT::getIntegerVT(*DAG.getContext(), KeptBits)));
  return DAG.getSetCC(DL, SCCVT, SExtInReg, X, NewCond);
}

// (X & (C l>>/<< Y)) ==/!= 0  -->  ((X <</l>> Y) & C) ==/!= 0
SDValue TargetLowering::optimizeSetCCByHoistingAndByConstFromLogicalShift(
    EVT SCCVT, SDValue N0, SDValue N1C, ISD::CondCode Cond,
    DAGCombinerInfo &DCI, const SDLoc &DL) const {
  assert(isConstOrConstSplat(N1C) && isConstOrConstSplat(N1C)->isZero() &&
         "Should be a comparison with 0.");
  assert((Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
         "Valid only for [in]equality comparisons.");

  unsigned NewShiftOpcode;
  SDValue X, C, Y;

  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  // Look for '(C l>>/<< Y)'.
  auto Match = [&NewShiftOpcode, &X, &C, &Y, &TLI, &DAG](SDValue V) {
    // The shift should be one-use.
    if (!V.hasOneUse())
      return false;
    unsigned OldShiftOpcode = V.getOpcode();
    switch (OldShiftOpcode) {
    case ISD::SHL:
      NewShiftOpcode = ISD::SRL;
      break;
    case ISD::SRL:
      NewShiftOpcode = ISD::SHL;
      break;
    default:
      return false; // must be a logical shift.
    }
    // We should be shifting a constant.
    // FIXME: best to use isConstantOrConstantVector().
    C = V.getOperand(0);
    ConstantSDNode *CC =
        isConstOrConstSplat(C, /*AllowUndefs=*/true, /*AllowTruncation=*/true);
    if (!CC)
      return false;
    Y = V.getOperand(1);

    ConstantSDNode *XC =
        isConstOrConstSplat(X, /*AllowUndefs=*/true, /*AllowTruncation=*/true);
    return TLI.shouldProduceAndByConstByHoistingConstFromShiftsLHSOfAnd(
        X, XC, CC, Y, OldShiftOpcode, NewShiftOpcode, DAG);
  };

  // LHS of comparison should be an one-use 'and'.
  if (N0.getOpcode() != ISD::AND || !N0.hasOneUse())
    return SDValue();

  X = N0.getOperand(0);
  SDValue Mask = N0.getOperand(1);

  // 'and' is commutative!
  if (!Match(Mask)) {
    std::swap(X, Mask);
    if (!Match(Mask))
      return SDValue();
  }

  EVT VT = X.getValueType();

  // Produce:
  // ((X 'OppositeShiftOpcode' Y) & C) Cond 0
  SDValue T0 = DAG.getNode(NewShiftOpcode, DL, VT, X, Y);
  SDValue T1 = DAG.getNode(ISD::AND, DL, VT, T0, C);
  SDValue T2 = DAG.getSetCC(DL, SCCVT, T1, N1C, Cond);
  return T2;
}

/// Try to fold an equality comparison with a {add/sub/xor} binary operation as
/// the 1st operand (N0). Callers are expected to swap the N0/N1 parameters to
/// handle the commuted versions of these patterns.
SDValue TargetLowering::foldSetCCWithBinOp(EVT VT, SDValue N0, SDValue N1,
                                           ISD::CondCode Cond, const SDLoc &DL,
                                           DAGCombinerInfo &DCI) const {
  unsigned BOpcode = N0.getOpcode();
  assert((BOpcode == ISD::ADD || BOpcode == ISD::SUB || BOpcode == ISD::XOR) &&
         "Unexpected binop");
  assert((Cond == ISD::SETEQ || Cond == ISD::SETNE) && "Unexpected condcode");

  // (X + Y) == X --> Y == 0
  // (X - Y) == X --> Y == 0
  // (X ^ Y) == X --> Y == 0
  SelectionDAG &DAG = DCI.DAG;
  EVT OpVT = N0.getValueType();
  SDValue X = N0.getOperand(0);
  SDValue Y = N0.getOperand(1);
  if (X == N1)
    return DAG.getSetCC(DL, VT, Y, DAG.getConstant(0, DL, OpVT), Cond);

  if (Y != N1)
    return SDValue();

  // (X + Y) == Y --> X == 0
  // (X ^ Y) == Y --> X == 0
  if (BOpcode == ISD::ADD || BOpcode == ISD::XOR)
    return DAG.getSetCC(DL, VT, X, DAG.getConstant(0, DL, OpVT), Cond);

  // The shift would not be valid if the operands are boolean (i1).
  if (!N0.hasOneUse() || OpVT.getScalarSizeInBits() == 1)
    return SDValue();

  // (X - Y) == Y --> X == Y << 1
  SDValue One = DAG.getShiftAmountConstant(1, OpVT, DL);
  SDValue YShl1 = DAG.getNode(ISD::SHL, DL, N1.getValueType(), Y, One);
  if (!DCI.isCalledByLegalizer())
    DCI.AddToWorklist(YShl1.getNode());
  return DAG.getSetCC(DL, VT, X, YShl1, Cond);
}

static SDValue simplifySetCCWithCTPOP(const TargetLowering &TLI, EVT VT,
                                      SDValue N0, const APInt &C1,
                                      ISD::CondCode Cond, const SDLoc &dl,
                                      SelectionDAG &DAG) {
  // Look through truncs that don't change the value of a ctpop.
  // FIXME: Add vector support? Need to be careful with setcc result type below.
  SDValue CTPOP = N0;
  if (N0.getOpcode() == ISD::TRUNCATE && N0.hasOneUse() && !VT.isVector() &&
      N0.getScalarValueSizeInBits() > Log2_32(N0.getOperand(0).getScalarValueSizeInBits()))
    CTPOP = N0.getOperand(0);

  if (CTPOP.getOpcode() != ISD::CTPOP || !CTPOP.hasOneUse())
    return SDValue();

  EVT CTVT = CTPOP.getValueType();
  SDValue CTOp = CTPOP.getOperand(0);

  // Expand a power-of-2-or-zero comparison based on ctpop:
  // (ctpop x) u< 2 -> (x & x-1) == 0
  // (ctpop x) u> 1 -> (x & x-1) != 0
  if (Cond == ISD::SETULT || Cond == ISD::SETUGT) {
    // Keep the CTPOP if it is a cheap vector op.
    if (CTVT.isVector() && TLI.isCtpopFast(CTVT))
      return SDValue();

    unsigned CostLimit = TLI.getCustomCtpopCost(CTVT, Cond);
    if (C1.ugt(CostLimit + (Cond == ISD::SETULT)))
      return SDValue();
    if (C1 == 0 && (Cond == ISD::SETULT))
      return SDValue(); // This is handled elsewhere.

    unsigned Passes = C1.getLimitedValue() - (Cond == ISD::SETULT);

    SDValue NegOne = DAG.getAllOnesConstant(dl, CTVT);
    SDValue Result = CTOp;
    for (unsigned i = 0; i < Passes; i++) {
      SDValue Add = DAG.getNode(ISD::ADD, dl, CTVT, Result, NegOne);
      Result = DAG.getNode(ISD::AND, dl, CTVT, Result, Add);
    }
    ISD::CondCode CC = Cond == ISD::SETULT ? ISD::SETEQ : ISD::SETNE;
    return DAG.getSetCC(dl, VT, Result, DAG.getConstant(0, dl, CTVT), CC);
  }

  // Expand a power-of-2 comparison based on ctpop
  if ((Cond == ISD::SETEQ || Cond == ISD::SETNE) && C1 == 1) {
    // Keep the CTPOP if it is cheap.
    if (TLI.isCtpopFast(CTVT))
      return SDValue();

    SDValue Zero = DAG.getConstant(0, dl, CTVT);
    SDValue NegOne = DAG.getAllOnesConstant(dl, CTVT);
    assert(CTVT.isInteger());
    SDValue Add = DAG.getNode(ISD::ADD, dl, CTVT, CTOp, NegOne);

    // Its not uncommon for known-never-zero X to exist in (ctpop X) eq/ne 1, so
    // check before emitting a potentially unnecessary op.
    if (DAG.isKnownNeverZero(CTOp)) {
      // (ctpop x) == 1 --> (x & x-1) == 0
      // (ctpop x) != 1 --> (x & x-1) != 0
      SDValue And = DAG.getNode(ISD::AND, dl, CTVT, CTOp, Add);
      SDValue RHS = DAG.getSetCC(dl, VT, And, Zero, Cond);
      return RHS;
    }

    // (ctpop x) == 1 --> (x ^ x-1) >  x-1
    // (ctpop x) != 1 --> (x ^ x-1) <= x-1
    SDValue Xor = DAG.getNode(ISD::XOR, dl, CTVT, CTOp, Add);
    ISD::CondCode CmpCond = Cond == ISD::SETEQ ? ISD::SETUGT : ISD::SETULE;
    return DAG.getSetCC(dl, VT, Xor, Add, CmpCond);
  }

  return SDValue();
}

static SDValue foldSetCCWithRotate(EVT VT, SDValue N0, SDValue N1,
                                   ISD::CondCode Cond, const SDLoc &dl,
                                   SelectionDAG &DAG) {
  if (Cond != ISD::SETEQ && Cond != ISD::SETNE)
    return SDValue();

  auto *C1 = isConstOrConstSplat(N1, /* AllowUndefs */ true);
  if (!C1 || !(C1->isZero() || C1->isAllOnes()))
    return SDValue();

  auto getRotateSource = [](SDValue X) {
    if (X.getOpcode() == ISD::ROTL || X.getOpcode() == ISD::ROTR)
      return X.getOperand(0);
    return SDValue();
  };

  // Peek through a rotated value compared against 0 or -1:
  // (rot X, Y) == 0/-1 --> X == 0/-1
  // (rot X, Y) != 0/-1 --> X != 0/-1
  if (SDValue R = getRotateSource(N0))
    return DAG.getSetCC(dl, VT, R, N1, Cond);

  // Peek through an 'or' of a rotated value compared against 0:
  // or (rot X, Y), Z ==/!= 0 --> (or X, Z) ==/!= 0
  // or Z, (rot X, Y) ==/!= 0 --> (or X, Z) ==/!= 0
  //
  // TODO: Add the 'and' with -1 sibling.
  // TODO: Recurse through a series of 'or' ops to find the rotate.
  EVT OpVT = N0.getValueType();
  if (N0.hasOneUse() && N0.getOpcode() == ISD::OR && C1->isZero()) {
    if (SDValue R = getRotateSource(N0.getOperand(0))) {
      SDValue NewOr = DAG.getNode(ISD::OR, dl, OpVT, R, N0.getOperand(1));
      return DAG.getSetCC(dl, VT, NewOr, N1, Cond);
    }
    if (SDValue R = getRotateSource(N0.getOperand(1))) {
      SDValue NewOr = DAG.getNode(ISD::OR, dl, OpVT, R, N0.getOperand(0));
      return DAG.getSetCC(dl, VT, NewOr, N1, Cond);
    }
  }

  return SDValue();
}

static SDValue foldSetCCWithFunnelShift(EVT VT, SDValue N0, SDValue N1,
                                        ISD::CondCode Cond, const SDLoc &dl,
                                        SelectionDAG &DAG) {
  // If we are testing for all-bits-clear, we might be able to do that with
  // less shifting since bit-order does not matter.
  if (Cond != ISD::SETEQ && Cond != ISD::SETNE)
    return SDValue();

  auto *C1 = isConstOrConstSplat(N1, /* AllowUndefs */ true);
  if (!C1 || !C1->isZero())
    return SDValue();

  if (!N0.hasOneUse() ||
      (N0.getOpcode() != ISD::FSHL && N0.getOpcode() != ISD::FSHR))
    return SDValue();

  unsigned BitWidth = N0.getScalarValueSizeInBits();
  auto *ShAmtC = isConstOrConstSplat(N0.getOperand(2));
  if (!ShAmtC || ShAmtC->getAPIntValue().uge(BitWidth))
    return SDValue();

  // Canonicalize fshr as fshl to reduce pattern-matching.
  unsigned ShAmt = ShAmtC->getZExtValue();
  if (N0.getOpcode() == ISD::FSHR)
    ShAmt = BitWidth - ShAmt;

  // Match an 'or' with a specific operand 'Other' in either commuted variant.
  SDValue X, Y;
  auto matchOr = [&X, &Y](SDValue Or, SDValue Other) {
    if (Or.getOpcode() != ISD::OR || !Or.hasOneUse())
      return false;
    if (Or.getOperand(0) == Other) {
      X = Or.getOperand(0);
      Y = Or.getOperand(1);
      return true;
    }
    if (Or.getOperand(1) == Other) {
      X = Or.getOperand(1);
      Y = Or.getOperand(0);
      return true;
    }
    return false;
  };

  EVT OpVT = N0.getValueType();
  EVT ShAmtVT = N0.getOperand(2).getValueType();
  SDValue F0 = N0.getOperand(0);
  SDValue F1 = N0.getOperand(1);
  if (matchOr(F0, F1)) {
    // fshl (or X, Y), X, C ==/!= 0 --> or (shl Y, C), X ==/!= 0
    SDValue NewShAmt = DAG.getConstant(ShAmt, dl, ShAmtVT);
    SDValue Shift = DAG.getNode(ISD::SHL, dl, OpVT, Y, NewShAmt);
    SDValue NewOr = DAG.getNode(ISD::OR, dl, OpVT, Shift, X);
    return DAG.getSetCC(dl, VT, NewOr, N1, Cond);
  }
  if (matchOr(F1, F0)) {
    // fshl X, (or X, Y), C ==/!= 0 --> or (srl Y, BW-C), X ==/!= 0
    SDValue NewShAmt = DAG.getConstant(BitWidth - ShAmt, dl, ShAmtVT);
    SDValue Shift = DAG.getNode(ISD::SRL, dl, OpVT, Y, NewShAmt);
    SDValue NewOr = DAG.getNode(ISD::OR, dl, OpVT, Shift, X);
    return DAG.getSetCC(dl, VT, NewOr, N1, Cond);
  }

  return SDValue();
}

/// Try to simplify a setcc built with the specified operands and cc. If it is
/// unable to simplify it, return a null SDValue.
SDValue TargetLowering::SimplifySetCC(EVT VT, SDValue N0, SDValue N1,
                                      ISD::CondCode Cond, bool foldBooleans,
                                      DAGCombinerInfo &DCI,
                                      const SDLoc &dl) const {
  SelectionDAG &DAG = DCI.DAG;
  const DataLayout &Layout = DAG.getDataLayout();
  EVT OpVT = N0.getValueType();
  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();

  // Constant fold or commute setcc.
  if (SDValue Fold = DAG.FoldSetCC(VT, N0, N1, Cond, dl))
    return Fold;

  bool N0ConstOrSplat =
      isConstOrConstSplat(N0, /*AllowUndefs*/ false, /*AllowTruncate*/ true);
  bool N1ConstOrSplat =
      isConstOrConstSplat(N1, /*AllowUndefs*/ false, /*AllowTruncate*/ true);

  // Canonicalize toward having the constant on the RHS.
  // TODO: Handle non-splat vector constants. All undef causes trouble.
  // FIXME: We can't yet fold constant scalable vector splats, so avoid an
  // infinite loop here when we encounter one.
  ISD::CondCode SwappedCC = ISD::getSetCCSwappedOperands(Cond);
  if (N0ConstOrSplat && !N1ConstOrSplat &&
      (DCI.isBeforeLegalizeOps() ||
       isCondCodeLegal(SwappedCC, N0.getSimpleValueType())))
    return DAG.getSetCC(dl, VT, N1, N0, SwappedCC);

  // If we have a subtract with the same 2 non-constant operands as this setcc
  // -- but in reverse order -- then try to commute the operands of this setcc
  // to match. A matching pair of setcc (cmp) and sub may be combined into 1
  // instruction on some targets.
  if (!N0ConstOrSplat && !N1ConstOrSplat &&
      (DCI.isBeforeLegalizeOps() ||
       isCondCodeLegal(SwappedCC, N0.getSimpleValueType())) &&
      DAG.doesNodeExist(ISD::SUB, DAG.getVTList(OpVT), {N1, N0}) &&
      !DAG.doesNodeExist(ISD::SUB, DAG.getVTList(OpVT), {N0, N1}))
    return DAG.getSetCC(dl, VT, N1, N0, SwappedCC);

  if (SDValue V = foldSetCCWithRotate(VT, N0, N1, Cond, dl, DAG))
    return V;

  if (SDValue V = foldSetCCWithFunnelShift(VT, N0, N1, Cond, dl, DAG))
    return V;

  if (auto *N1C = isConstOrConstSplat(N1)) {
    const APInt &C1 = N1C->getAPIntValue();

    // Optimize some CTPOP cases.
    if (SDValue V = simplifySetCCWithCTPOP(*this, VT, N0, C1, Cond, dl, DAG))
      return V;

    // For equality to 0 of a no-wrap multiply, decompose and test each op:
    // X * Y == 0 --> (X == 0) || (Y == 0)
    // X * Y != 0 --> (X != 0) && (Y != 0)
    // TODO: This bails out if minsize is set, but if the target doesn't have a
    //       single instruction multiply for this type, it would likely be
    //       smaller to decompose.
    if (C1.isZero() && (Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
        N0.getOpcode() == ISD::MUL && N0.hasOneUse() &&
        (N0->getFlags().hasNoUnsignedWrap() ||
         N0->getFlags().hasNoSignedWrap()) &&
        !Attr.hasFnAttr(Attribute::MinSize)) {
      SDValue IsXZero = DAG.getSetCC(dl, VT, N0.getOperand(0), N1, Cond);
      SDValue IsYZero = DAG.getSetCC(dl, VT, N0.getOperand(1), N1, Cond);
      unsigned LogicOp = Cond == ISD::SETEQ ? ISD::OR : ISD::AND;
      return DAG.getNode(LogicOp, dl, VT, IsXZero, IsYZero);
    }

    // If the LHS is '(srl (ctlz x), 5)', the RHS is 0/1, and this is an
    // equality comparison, then we're just comparing whether X itself is
    // zero.
    if (N0.getOpcode() == ISD::SRL && (C1.isZero() || C1.isOne()) &&
        N0.getOperand(0).getOpcode() == ISD::CTLZ &&
        llvm::has_single_bit<uint32_t>(N0.getScalarValueSizeInBits())) {
      if (ConstantSDNode *ShAmt = isConstOrConstSplat(N0.getOperand(1))) {
        if ((Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
            ShAmt->getAPIntValue() == Log2_32(N0.getScalarValueSizeInBits())) {
          if ((C1 == 0) == (Cond == ISD::SETEQ)) {
            // (srl (ctlz x), 5) == 0  -> X != 0
            // (srl (ctlz x), 5) != 1  -> X != 0
            Cond = ISD::SETNE;
          } else {
            // (srl (ctlz x), 5) != 0  -> X == 0
            // (srl (ctlz x), 5) == 1  -> X == 0
            Cond = ISD::SETEQ;
          }
          SDValue Zero = DAG.getConstant(0, dl, N0.getValueType());
          return DAG.getSetCC(dl, VT, N0.getOperand(0).getOperand(0), Zero,
                              Cond);
        }
      }
    }
  }

  // FIXME: Support vectors.
  if (auto *N1C = dyn_cast<ConstantSDNode>(N1.getNode())) {
    const APInt &C1 = N1C->getAPIntValue();

    // (zext x) == C --> x == (trunc C)
    // (sext x) == C --> x == (trunc C)
    if ((Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
        DCI.isBeforeLegalize() && N0->hasOneUse()) {
      unsigned MinBits = N0.getValueSizeInBits();
      SDValue PreExt;
      bool Signed = false;
      if (N0->getOpcode() == ISD::ZERO_EXTEND) {
        // ZExt
        MinBits = N0->getOperand(0).getValueSizeInBits();
        PreExt = N0->getOperand(0);
      } else if (N0->getOpcode() == ISD::AND) {
        // DAGCombine turns costly ZExts into ANDs
        if (auto *C = dyn_cast<ConstantSDNode>(N0->getOperand(1)))
          if ((C->getAPIntValue()+1).isPowerOf2()) {
            MinBits = C->getAPIntValue().countr_one();
            PreExt = N0->getOperand(0);
          }
      } else if (N0->getOpcode() == ISD::SIGN_EXTEND) {
        // SExt
        MinBits = N0->getOperand(0).getValueSizeInBits();
        PreExt = N0->getOperand(0);
        Signed = true;
      } else if (auto *LN0 = dyn_cast<LoadSDNode>(N0)) {
        // ZEXTLOAD / SEXTLOAD
        if (LN0->getExtensionType() == ISD::ZEXTLOAD) {
          MinBits = LN0->getMemoryVT().getSizeInBits();
          PreExt = N0;
        } else if (LN0->getExtensionType() == ISD::SEXTLOAD) {
          Signed = true;
          MinBits = LN0->getMemoryVT().getSizeInBits();
          PreExt = N0;
        }
      }

      // Figure out how many bits we need to preserve this constant.
      unsigned ReqdBits = Signed ? C1.getSignificantBits() : C1.getActiveBits();

      // Make sure we're not losing bits from the constant.
      if (MinBits > 0 &&
          MinBits < C1.getBitWidth() &&
          MinBits >= ReqdBits) {
        EVT MinVT = EVT::getIntegerVT(*DAG.getContext(), MinBits);
        if (isTypeDesirableForOp(ISD::SETCC, MinVT)) {
          // Will get folded away.
          SDValue Trunc = DAG.getNode(ISD::TRUNCATE, dl, MinVT, PreExt);
          if (MinBits == 1 && C1 == 1)
            // Invert the condition.
            return DAG.getSetCC(dl, VT, Trunc, DAG.getConstant(0, dl, MVT::i1),
                                Cond == ISD::SETEQ ? ISD::SETNE : ISD::SETEQ);
          SDValue C = DAG.getConstant(C1.trunc(MinBits), dl, MinVT);
          return DAG.getSetCC(dl, VT, Trunc, C, Cond);
        }

        // If truncating the setcc operands is not desirable, we can still
        // simplify the expression in some cases:
        // setcc ([sz]ext (setcc x, y, cc)), 0, setne) -> setcc (x, y, cc)
        // setcc ([sz]ext (setcc x, y, cc)), 0, seteq) -> setcc (x, y, inv(cc))
        // setcc (zext (setcc x, y, cc)), 1, setne) -> setcc (x, y, inv(cc))
        // setcc (zext (setcc x, y, cc)), 1, seteq) -> setcc (x, y, cc)
        // setcc (sext (setcc x, y, cc)), -1, setne) -> setcc (x, y, inv(cc))
        // setcc (sext (setcc x, y, cc)), -1, seteq) -> setcc (x, y, cc)
        SDValue TopSetCC = N0->getOperand(0);
        unsigned N0Opc = N0->getOpcode();
        bool SExt = (N0Opc == ISD::SIGN_EXTEND);
        if (TopSetCC.getValueType() == MVT::i1 && VT == MVT::i1 &&
            TopSetCC.getOpcode() == ISD::SETCC &&
            (N0Opc == ISD::ZERO_EXTEND || N0Opc == ISD::SIGN_EXTEND) &&
            (isConstFalseVal(N1) ||
             isExtendedTrueVal(N1C, N0->getValueType(0), SExt))) {

          bool Inverse = (N1C->isZero() && Cond == ISD::SETEQ) ||
                         (!N1C->isZero() && Cond == ISD::SETNE);

          if (!Inverse)
            return TopSetCC;

          ISD::CondCode InvCond = ISD::getSetCCInverse(
              cast<CondCodeSDNode>(TopSetCC.getOperand(2))->get(),
              TopSetCC.getOperand(0).getValueType());
          return DAG.getSetCC(dl, VT, TopSetCC.getOperand(0),
                                      TopSetCC.getOperand(1),
                                      InvCond);
        }
      }
    }

    // If the LHS is '(and load, const)', the RHS is 0, the test is for
    // equality or unsigned, and all 1 bits of the const are in the same
    // partial word, see if we can shorten the load.
    if (DCI.isBeforeLegalize() &&
        !ISD::isSignedIntSetCC(Cond) &&
        N0.getOpcode() == ISD::AND && C1 == 0 &&
        N0.getNode()->hasOneUse() &&
        isa<LoadSDNode>(N0.getOperand(0)) &&
        N0.getOperand(0).getNode()->hasOneUse() &&
        isa<ConstantSDNode>(N0.getOperand(1))) {
      auto *Lod = cast<LoadSDNode>(N0.getOperand(0));
      APInt bestMask;
      unsigned bestWidth = 0, bestOffset = 0;
      if (Lod->isSimple() && Lod->isUnindexed() &&
          (Lod->getMemoryVT().isByteSized() ||
           isPaddedAtMostSignificantBitsWhenStored(Lod->getMemoryVT()))) {
        unsigned memWidth = Lod->getMemoryVT().getStoreSizeInBits();
        unsigned origWidth = N0.getValueSizeInBits();
        unsigned maskWidth = origWidth;
        // We can narrow (e.g.) 16-bit extending loads on 32-bit target to
        // 8 bits, but have to be careful...
        if (Lod->getExtensionType() != ISD::NON_EXTLOAD)
          origWidth = Lod->getMemoryVT().getSizeInBits();
        const APInt &Mask = N0.getConstantOperandAPInt(1);
        // Only consider power-of-2 widths (and at least one byte) as candiates
        // for the narrowed load.
        for (unsigned width = 8; width < origWidth; width *= 2) {
          EVT newVT = EVT::getIntegerVT(*DAG.getContext(), width);
          if (!shouldReduceLoadWidth(Lod, ISD::NON_EXTLOAD, newVT))
            continue;
          APInt newMask = APInt::getLowBitsSet(maskWidth, width);
          // Avoid accessing any padding here for now (we could use memWidth
          // instead of origWidth here otherwise).
          unsigned maxOffset = origWidth - width;
          for (unsigned offset = 0; offset <= maxOffset; offset += 8) {
            if (Mask.isSubsetOf(newMask)) {
              unsigned ptrOffset =
                  Layout.isLittleEndian() ? offset : memWidth - width - offset;
              unsigned IsFast = 0;
              Align NewAlign = commonAlignment(Lod->getAlign(), ptrOffset / 8);
              if (allowsMemoryAccess(
                      *DAG.getContext(), Layout, newVT, Lod->getAddressSpace(),
                      NewAlign, Lod->getMemOperand()->getFlags(), &IsFast) &&
                  IsFast) {
                bestOffset = ptrOffset / 8;
                bestMask = Mask.lshr(offset);
                bestWidth = width;
                break;
              }
            }
            newMask <<= 8;
          }
          if (bestWidth)
            break;
        }
      }
      if (bestWidth) {
        EVT newVT = EVT::getIntegerVT(*DAG.getContext(), bestWidth);
        SDValue Ptr = Lod->getBasePtr();
        if (bestOffset != 0)
          Ptr = DAG.getObjectPtrOffset(dl, Ptr, TypeSize::getFixed(bestOffset));
        SDValue NewLoad =
            DAG.getLoad(newVT, dl, Lod->getChain(), Ptr,
                        Lod->getPointerInfo().getWithOffset(bestOffset),
                        Lod->getOriginalAlign());
        SDValue And =
            DAG.getNode(ISD::AND, dl, newVT, NewLoad,
                        DAG.getConstant(bestMask.trunc(bestWidth), dl, newVT));
        return DAG.getSetCC(dl, VT, And, DAG.getConstant(0LL, dl, newVT), Cond);
      }
    }

    // If the LHS is a ZERO_EXTEND, perform the comparison on the input.
    if (N0.getOpcode() == ISD::ZERO_EXTEND) {
      unsigned InSize = N0.getOperand(0).getValueSizeInBits();

      // If the comparison constant has bits in the upper part, the
      // zero-extended value could never match.
      if (C1.intersects(APInt::getHighBitsSet(C1.getBitWidth(),
                                              C1.getBitWidth() - InSize))) {
        switch (Cond) {
        case ISD::SETUGT:
        case ISD::SETUGE:
        case ISD::SETEQ:
          return DAG.getConstant(0, dl, VT);
        case ISD::SETULT:
        case ISD::SETULE:
        case ISD::SETNE:
          return DAG.getConstant(1, dl, VT);
        case ISD::SETGT:
        case ISD::SETGE:
          // True if the sign bit of C1 is set.
          return DAG.getConstant(C1.isNegative(), dl, VT);
        case ISD::SETLT:
        case ISD::SETLE:
          // True if the sign bit of C1 isn't set.
          return DAG.getConstant(C1.isNonNegative(), dl, VT);
        default:
          break;
        }
      }

      // Otherwise, we can perform the comparison with the low bits.
      switch (Cond) {
      case ISD::SETEQ:
      case ISD::SETNE:
      case ISD::SETUGT:
      case ISD::SETUGE:
      case ISD::SETULT:
      case ISD::SETULE: {
        EVT newVT = N0.getOperand(0).getValueType();
        if (DCI.isBeforeLegalizeOps() ||
            (isOperationLegal(ISD::SETCC, newVT) &&
             isCondCodeLegal(Cond, newVT.getSimpleVT()))) {
          EVT NewSetCCVT = getSetCCResultType(Layout, *DAG.getContext(), newVT);
          SDValue NewConst = DAG.getConstant(C1.trunc(InSize), dl, newVT);

          SDValue NewSetCC = DAG.getSetCC(dl, NewSetCCVT, N0.getOperand(0),
                                          NewConst, Cond);
          return DAG.getBoolExtOrTrunc(NewSetCC, dl, VT, N0.getValueType());
        }
        break;
      }
      default:
        break; // todo, be more careful with signed comparisons
      }
    } else if (N0.getOpcode() == ISD::SIGN_EXTEND_INREG &&
               (Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
               !isSExtCheaperThanZExt(cast<VTSDNode>(N0.getOperand(1))->getVT(),
                                      OpVT)) {
      EVT ExtSrcTy = cast<VTSDNode>(N0.getOperand(1))->getVT();
      unsigned ExtSrcTyBits = ExtSrcTy.getSizeInBits();
      EVT ExtDstTy = N0.getValueType();
      unsigned ExtDstTyBits = ExtDstTy.getSizeInBits();

      // If the constant doesn't fit into the number of bits for the source of
      // the sign extension, it is impossible for both sides to be equal.
      if (C1.getSignificantBits() > ExtSrcTyBits)
        return DAG.getBoolConstant(Cond == ISD::SETNE, dl, VT, OpVT);

      assert(ExtDstTy == N0.getOperand(0).getValueType() &&
             ExtDstTy != ExtSrcTy && "Unexpected types!");
      APInt Imm = APInt::getLowBitsSet(ExtDstTyBits, ExtSrcTyBits);
      SDValue ZextOp = DAG.getNode(ISD::AND, dl, ExtDstTy, N0.getOperand(0),
                                   DAG.getConstant(Imm, dl, ExtDstTy));
      if (!DCI.isCalledByLegalizer())
        DCI.AddToWorklist(ZextOp.getNode());
      // Otherwise, make this a use of a zext.
      return DAG.getSetCC(dl, VT, ZextOp,
                          DAG.getConstant(C1 & Imm, dl, ExtDstTy), Cond);
    } else if ((N1C->isZero() || N1C->isOne()) &&
               (Cond == ISD::SETEQ || Cond == ISD::SETNE)) {
      // SETCC (X), [0|1], [EQ|NE]  -> X if X is known 0/1. i1 types are
      // excluded as they are handled below whilst checking for foldBooleans.
      if ((N0.getOpcode() == ISD::SETCC || VT.getScalarType() != MVT::i1) &&
          isTypeLegal(VT) && VT.bitsLE(N0.getValueType()) &&
          (N0.getValueType() == MVT::i1 ||
           getBooleanContents(N0.getValueType()) == ZeroOrOneBooleanContent) &&
          DAG.MaskedValueIsZero(
              N0, APInt::getBitsSetFrom(N0.getValueSizeInBits(), 1))) {
        bool TrueWhenTrue = (Cond == ISD::SETEQ) ^ (!N1C->isOne());
        if (TrueWhenTrue)
          return DAG.getNode(ISD::TRUNCATE, dl, VT, N0);
        // Invert the condition.
        if (N0.getOpcode() == ISD::SETCC) {
          ISD::CondCode CC = cast<CondCodeSDNode>(N0.getOperand(2))->get();
          CC = ISD::getSetCCInverse(CC, N0.getOperand(0).getValueType());
          if (DCI.isBeforeLegalizeOps() ||
              isCondCodeLegal(CC, N0.getOperand(0).getSimpleValueType()))
            return DAG.getSetCC(dl, VT, N0.getOperand(0), N0.getOperand(1), CC);
        }
      }

      if ((N0.getOpcode() == ISD::XOR ||
           (N0.getOpcode() == ISD::AND &&
            N0.getOperand(0).getOpcode() == ISD::XOR &&
            N0.getOperand(1) == N0.getOperand(0).getOperand(1))) &&
          isOneConstant(N0.getOperand(1))) {
        // If this is (X^1) == 0/1, swap the RHS and eliminate the xor.  We
        // can only do this if the top bits are known zero.
        unsigned BitWidth = N0.getValueSizeInBits();
        if (DAG.MaskedValueIsZero(N0,
                                  APInt::getHighBitsSet(BitWidth,
                                                        BitWidth-1))) {
          // Okay, get the un-inverted input value.
          SDValue Val;
          if (N0.getOpcode() == ISD::XOR) {
            Val = N0.getOperand(0);
          } else {
            assert(N0.getOpcode() == ISD::AND &&
                    N0.getOperand(0).getOpcode() == ISD::XOR);
            // ((X^1)&1)^1 -> X & 1
            Val = DAG.getNode(ISD::AND, dl, N0.getValueType(),
                              N0.getOperand(0).getOperand(0),
                              N0.getOperand(1));
          }

          return DAG.getSetCC(dl, VT, Val, N1,
                              Cond == ISD::SETEQ ? ISD::SETNE : ISD::SETEQ);
        }
      } else if (N1C->isOne()) {
        SDValue Op0 = N0;
        if (Op0.getOpcode() == ISD::TRUNCATE)
          Op0 = Op0.getOperand(0);

        if ((Op0.getOpcode() == ISD::XOR) &&
            Op0.getOperand(0).getOpcode() == ISD::SETCC &&
            Op0.getOperand(1).getOpcode() == ISD::SETCC) {
          SDValue XorLHS = Op0.getOperand(0);
          SDValue XorRHS = Op0.getOperand(1);
          // Ensure that the input setccs return an i1 type or 0/1 value.
          if (Op0.getValueType() == MVT::i1 ||
              (getBooleanContents(XorLHS.getOperand(0).getValueType()) ==
                      ZeroOrOneBooleanContent &&
               getBooleanContents(XorRHS.getOperand(0).getValueType()) ==
                        ZeroOrOneBooleanContent)) {
            // (xor (setcc), (setcc)) == / != 1 -> (setcc) != / == (setcc)
            Cond = (Cond == ISD::SETEQ) ? ISD::SETNE : ISD::SETEQ;
            return DAG.getSetCC(dl, VT, XorLHS, XorRHS, Cond);
          }
        }
        if (Op0.getOpcode() == ISD::AND && isOneConstant(Op0.getOperand(1))) {
          // If this is (X&1) == / != 1, normalize it to (X&1) != / == 0.
          if (Op0.getValueType().bitsGT(VT))
            Op0 = DAG.getNode(ISD::AND, dl, VT,
                          DAG.getNode(ISD::TRUNCATE, dl, VT, Op0.getOperand(0)),
                          DAG.getConstant(1, dl, VT));
          else if (Op0.getValueType().bitsLT(VT))
            Op0 = DAG.getNode(ISD::AND, dl, VT,
                        DAG.getNode(ISD::ANY_EXTEND, dl, VT, Op0.getOperand(0)),
                        DAG.getConstant(1, dl, VT));

          return DAG.getSetCC(dl, VT, Op0,
                              DAG.getConstant(0, dl, Op0.getValueType()),
                              Cond == ISD::SETEQ ? ISD::SETNE : ISD::SETEQ);
        }
        if (Op0.getOpcode() == ISD::AssertZext &&
            cast<VTSDNode>(Op0.getOperand(1))->getVT() == MVT::i1)
          return DAG.getSetCC(dl, VT, Op0,
                              DAG.getConstant(0, dl, Op0.getValueType()),
                              Cond == ISD::SETEQ ? ISD::SETNE : ISD::SETEQ);
      }
    }

    // Given:
    //   icmp eq/ne (urem %x, %y), 0
    // Iff %x has 0 or 1 bits set, and %y has at least 2 bits set, omit 'urem':
    //   icmp eq/ne %x, 0
    if (N0.getOpcode() == ISD::UREM && N1C->isZero() &&
        (Cond == ISD::SETEQ || Cond == ISD::SETNE)) {
      KnownBits XKnown = DAG.computeKnownBits(N0.getOperand(0));
      KnownBits YKnown = DAG.computeKnownBits(N0.getOperand(1));
      if (XKnown.countMaxPopulation() == 1 && YKnown.countMinPopulation() >= 2)
        return DAG.getSetCC(dl, VT, N0.getOperand(0), N1, Cond);
    }

    // Fold set_cc seteq (ashr X, BW-1), -1 -> set_cc setlt X, 0
    //  and set_cc setne (ashr X, BW-1), -1 -> set_cc setge X, 0
    if ((Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
        N0.getOpcode() == ISD::SRA && isa<ConstantSDNode>(N0.getOperand(1)) &&
        N0.getConstantOperandAPInt(1) == OpVT.getScalarSizeInBits() - 1 &&
        N1C && N1C->isAllOnes()) {
      return DAG.getSetCC(dl, VT, N0.getOperand(0),
                          DAG.getConstant(0, dl, OpVT),
                          Cond == ISD::SETEQ ? ISD::SETLT : ISD::SETGE);
    }

    if (SDValue V =
            optimizeSetCCOfSignedTruncationCheck(VT, N0, N1, Cond, DCI, dl))
      return V;
  }

  // These simplifications apply to splat vectors as well.
  // TODO: Handle more splat vector cases.
  if (auto *N1C = isConstOrConstSplat(N1)) {
    const APInt &C1 = N1C->getAPIntValue();

    APInt MinVal, MaxVal;
    unsigned OperandBitSize = N1C->getValueType(0).getScalarSizeInBits();
    if (ISD::isSignedIntSetCC(Cond)) {
      MinVal = APInt::getSignedMinValue(OperandBitSize);
      MaxVal = APInt::getSignedMaxValue(OperandBitSize);
    } else {
      MinVal = APInt::getMinValue(OperandBitSize);
      MaxVal = APInt::getMaxValue(OperandBitSize);
    }

    // Canonicalize GE/LE comparisons to use GT/LT comparisons.
    if (Cond == ISD::SETGE || Cond == ISD::SETUGE) {
      // X >= MIN --> true
      if (C1 == MinVal)
        return DAG.getBoolConstant(true, dl, VT, OpVT);

      if (!VT.isVector()) { // TODO: Support this for vectors.
        // X >= C0 --> X > (C0 - 1)
        APInt C = C1 - 1;
        ISD::CondCode NewCC = (Cond == ISD::SETGE) ? ISD::SETGT : ISD::SETUGT;
        if ((DCI.isBeforeLegalizeOps() ||
             isCondCodeLegal(NewCC, VT.getSimpleVT())) &&
            (!N1C->isOpaque() || (C.getBitWidth() <= 64 &&
                                  isLegalICmpImmediate(C.getSExtValue())))) {
          return DAG.getSetCC(dl, VT, N0,
                              DAG.getConstant(C, dl, N1.getValueType()),
                              NewCC);
        }
      }
    }

    if (Cond == ISD::SETLE || Cond == ISD::SETULE) {
      // X <= MAX --> true
      if (C1 == MaxVal)
        return DAG.getBoolConstant(true, dl, VT, OpVT);

      // X <= C0 --> X < (C0 + 1)
      if (!VT.isVector()) { // TODO: Support this for vectors.
        APInt C = C1 + 1;
        ISD::CondCode NewCC = (Cond == ISD::SETLE) ? ISD::SETLT : ISD::SETULT;
        if ((DCI.isBeforeLegalizeOps() ||
             isCondCodeLegal(NewCC, VT.getSimpleVT())) &&
            (!N1C->isOpaque() || (C.getBitWidth() <= 64 &&
                                  isLegalICmpImmediate(C.getSExtValue())))) {
          return DAG.getSetCC(dl, VT, N0,
                              DAG.getConstant(C, dl, N1.getValueType()),
                              NewCC);
        }
      }
    }

    if (Cond == ISD::SETLT || Cond == ISD::SETULT) {
      if (C1 == MinVal)
        return DAG.getBoolConstant(false, dl, VT, OpVT); // X < MIN --> false

      // TODO: Support this for vectors after legalize ops.
      if (!VT.isVector() || DCI.isBeforeLegalizeOps()) {
        // Canonicalize setlt X, Max --> setne X, Max
        if (C1 == MaxVal)
          return DAG.getSetCC(dl, VT, N0, N1, ISD::SETNE);

        // If we have setult X, 1, turn it into seteq X, 0
        if (C1 == MinVal+1)
          return DAG.getSetCC(dl, VT, N0,
                              DAG.getConstant(MinVal, dl, N0.getValueType()),
                              ISD::SETEQ);
      }
    }

    if (Cond == ISD::SETGT || Cond == ISD::SETUGT) {
      if (C1 == MaxVal)
        return DAG.getBoolConstant(false, dl, VT, OpVT); // X > MAX --> false

      // TODO: Support this for vectors after legalize ops.
      if (!VT.isVector() || DCI.isBeforeLegalizeOps()) {
        // Canonicalize setgt X, Min --> setne X, Min
        if (C1 == MinVal)
          return DAG.getSetCC(dl, VT, N0, N1, ISD::SETNE);

        // If we have setugt X, Max-1, turn it into seteq X, Max
        if (C1 == MaxVal-1)
          return DAG.getSetCC(dl, VT, N0,
                              DAG.getConstant(MaxVal, dl, N0.getValueType()),
                              ISD::SETEQ);
      }
    }

    if (Cond == ISD::SETEQ || Cond == ISD::SETNE) {
      // (X & (C l>>/<< Y)) ==/!= 0  -->  ((X <</l>> Y) & C) ==/!= 0
      if (C1.isZero())
        if (SDValue CC = optimizeSetCCByHoistingAndByConstFromLogicalShift(
                VT, N0, N1, Cond, DCI, dl))
          return CC;

      // For all/any comparisons, replace or(x,shl(y,bw/2)) with and/or(x,y).
      // For example, when high 32-bits of i64 X are known clear:
      // all bits clear: (X | (Y<<32)) ==  0 --> (X | Y) ==  0
      // all bits set:   (X | (Y<<32)) == -1 --> (X & Y) == -1
      bool CmpZero = N1C->isZero();
      bool CmpNegOne = N1C->isAllOnes();
      if ((CmpZero || CmpNegOne) && N0.hasOneUse()) {
        // Match or(lo,shl(hi,bw/2)) pattern.
        auto IsConcat = [&](SDValue V, SDValue &Lo, SDValue &Hi) {
          unsigned EltBits = V.getScalarValueSizeInBits();
          if (V.getOpcode() != ISD::OR || (EltBits % 2) != 0)
            return false;
          SDValue LHS = V.getOperand(0);
          SDValue RHS = V.getOperand(1);
          APInt HiBits = APInt::getHighBitsSet(EltBits, EltBits / 2);
          // Unshifted element must have zero upperbits.
          if (RHS.getOpcode() == ISD::SHL &&
              isa<ConstantSDNode>(RHS.getOperand(1)) &&
              RHS.getConstantOperandAPInt(1) == (EltBits / 2) &&
              DAG.MaskedValueIsZero(LHS, HiBits)) {
            Lo = LHS;
            Hi = RHS.getOperand(0);
            return true;
          }
          if (LHS.getOpcode() == ISD::SHL &&
              isa<ConstantSDNode>(LHS.getOperand(1)) &&
              LHS.getConstantOperandAPInt(1) == (EltBits / 2) &&
              DAG.MaskedValueIsZero(RHS, HiBits)) {
            Lo = RHS;
            Hi = LHS.getOperand(0);
            return true;
          }
          return false;
        };

        auto MergeConcat = [&](SDValue Lo, SDValue Hi) {
          unsigned EltBits = N0.getScalarValueSizeInBits();
          unsigned HalfBits = EltBits / 2;
          APInt HiBits = APInt::getHighBitsSet(EltBits, HalfBits);
          SDValue LoBits = DAG.getConstant(~HiBits, dl, OpVT);
          SDValue HiMask = DAG.getNode(ISD::AND, dl, OpVT, Hi, LoBits);
          SDValue NewN0 =
              DAG.getNode(CmpZero ? ISD::OR : ISD::AND, dl, OpVT, Lo, HiMask);
          SDValue NewN1 = CmpZero ? DAG.getConstant(0, dl, OpVT) : LoBits;
          return DAG.getSetCC(dl, VT, NewN0, NewN1, Cond);
        };

        SDValue Lo, Hi;
        if (IsConcat(N0, Lo, Hi))
          return MergeConcat(Lo, Hi);

        if (N0.getOpcode() == ISD::AND || N0.getOpcode() == ISD::OR) {
          SDValue Lo0, Lo1, Hi0, Hi1;
          if (IsConcat(N0.getOperand(0), Lo0, Hi0) &&
              IsConcat(N0.getOperand(1), Lo1, Hi1)) {
            return MergeConcat(DAG.getNode(N0.getOpcode(), dl, OpVT, Lo0, Lo1),
                               DAG.getNode(N0.getOpcode(), dl, OpVT, Hi0, Hi1));
          }
        }
      }
    }

    // If we have "setcc X, C0", check to see if we can shrink the immediate
    // by changing cc.
    // TODO: Support this for vectors after legalize ops.
    if (!VT.isVector() || DCI.isBeforeLegalizeOps()) {
      // SETUGT X, SINTMAX  -> SETLT X, 0
      // SETUGE X, SINTMIN -> SETLT X, 0
      if ((Cond == ISD::SETUGT && C1.isMaxSignedValue()) ||
          (Cond == ISD::SETUGE && C1.isMinSignedValue()))
        return DAG.getSetCC(dl, VT, N0,
                            DAG.getConstant(0, dl, N1.getValueType()),
                            ISD::SETLT);

      // SETULT X, SINTMIN  -> SETGT X, -1
      // SETULE X, SINTMAX  -> SETGT X, -1
      if ((Cond == ISD::SETULT && C1.isMinSignedValue()) ||
          (Cond == ISD::SETULE && C1.isMaxSignedValue()))
        return DAG.getSetCC(dl, VT, N0,
                            DAG.getAllOnesConstant(dl, N1.getValueType()),
                            ISD::SETGT);
    }
  }

  // Back to non-vector simplifications.
  // TODO: Can we do these for vector splats?
  if (auto *N1C = dyn_cast<ConstantSDNode>(N1.getNode())) {
    const TargetLowering &TLI = DAG.getTargetLoweringInfo();
    const APInt &C1 = N1C->getAPIntValue();
    EVT ShValTy = N0.getValueType();

    // Fold bit comparisons when we can. This will result in an
    // incorrect value when boolean false is negative one, unless
    // the bitsize is 1 in which case the false value is the same
    // in practice regardless of the representation.
    if ((VT.getSizeInBits() == 1 ||
         getBooleanContents(N0.getValueType()) == ZeroOrOneBooleanContent) &&
        (Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
        (VT == ShValTy || (isTypeLegal(VT) && VT.bitsLE(ShValTy))) &&
        N0.getOpcode() == ISD::AND) {
      if (auto *AndRHS = dyn_cast<ConstantSDNode>(N0.getOperand(1))) {
        if (Cond == ISD::SETNE && C1 == 0) {// (X & 8) != 0  -->  (X & 8) >> 3
          // Perform the xform if the AND RHS is a single bit.
          unsigned ShCt = AndRHS->getAPIntValue().logBase2();
          if (AndRHS->getAPIntValue().isPowerOf2() &&
              !TLI.shouldAvoidTransformToShift(ShValTy, ShCt)) {
            return DAG.getNode(
                ISD::TRUNCATE, dl, VT,
                DAG.getNode(ISD::SRL, dl, ShValTy, N0,
                            DAG.getShiftAmountConstant(ShCt, ShValTy, dl)));
          }
        } else if (Cond == ISD::SETEQ && C1 == AndRHS->getAPIntValue()) {
          // (X & 8) == 8  -->  (X & 8) >> 3
          // Perform the xform if C1 is a single bit.
          unsigned ShCt = C1.logBase2();
          if (C1.isPowerOf2() &&
              !TLI.shouldAvoidTransformToShift(ShValTy, ShCt)) {
            return DAG.getNode(
                ISD::TRUNCATE, dl, VT,
                DAG.getNode(ISD::SRL, dl, ShValTy, N0,
                            DAG.getShiftAmountConstant(ShCt, ShValTy, dl)));
          }
        }
      }
    }

    if (C1.getSignificantBits() <= 64 &&
        !isLegalICmpImmediate(C1.getSExtValue())) {
      // (X & -256) == 256 -> (X >> 8) == 1
      if ((Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
          N0.getOpcode() == ISD::AND && N0.hasOneUse()) {
        if (auto *AndRHS = dyn_cast<ConstantSDNode>(N0.getOperand(1))) {
          const APInt &AndRHSC = AndRHS->getAPIntValue();
          if (AndRHSC.isNegatedPowerOf2() && (AndRHSC & C1) == C1) {
            unsigned ShiftBits = AndRHSC.countr_zero();
            if (!TLI.shouldAvoidTransformToShift(ShValTy, ShiftBits)) {
              SDValue Shift = DAG.getNode(
                  ISD::SRL, dl, ShValTy, N0.getOperand(0),
                  DAG.getShiftAmountConstant(ShiftBits, ShValTy, dl));
              SDValue CmpRHS = DAG.getConstant(C1.lshr(ShiftBits), dl, ShValTy);
              return DAG.getSetCC(dl, VT, Shift, CmpRHS, Cond);
            }
          }
        }
      } else if (Cond == ISD::SETULT || Cond == ISD::SETUGE ||
                 Cond == ISD::SETULE || Cond == ISD::SETUGT) {
        bool AdjOne = (Cond == ISD::SETULE || Cond == ISD::SETUGT);
        // X <  0x100000000 -> (X >> 32) <  1
        // X >= 0x100000000 -> (X >> 32) >= 1
        // X <= 0x0ffffffff -> (X >> 32) <  1
        // X >  0x0ffffffff -> (X >> 32) >= 1
        unsigned ShiftBits;
        APInt NewC = C1;
        ISD::CondCode NewCond = Cond;
        if (AdjOne) {
          ShiftBits = C1.countr_one();
          NewC = NewC + 1;
          NewCond = (Cond == ISD::SETULE) ? ISD::SETULT : ISD::SETUGE;
        } else {
          ShiftBits = C1.countr_zero();
        }
        NewC.lshrInPlace(ShiftBits);
        if (ShiftBits && NewC.getSignificantBits() <= 64 &&
            isLegalICmpImmediate(NewC.getSExtValue()) &&
            !TLI.shouldAvoidTransformToShift(ShValTy, ShiftBits)) {
          SDValue Shift =
              DAG.getNode(ISD::SRL, dl, ShValTy, N0,
                          DAG.getShiftAmountConstant(ShiftBits, ShValTy, dl));
          SDValue CmpRHS = DAG.getConstant(NewC, dl, ShValTy);
          return DAG.getSetCC(dl, VT, Shift, CmpRHS, NewCond);
        }
      }
    }
  }

  if (!isa<ConstantFPSDNode>(N0) && isa<ConstantFPSDNode>(N1)) {
    auto *CFP = cast<ConstantFPSDNode>(N1);
    assert(!CFP->getValueAPF().isNaN() && "Unexpected NaN value");

    // Otherwise, we know the RHS is not a NaN.  Simplify the node to drop the
    // constant if knowing that the operand is non-nan is enough.  We prefer to
    // have SETO(x,x) instead of SETO(x, 0.0) because this avoids having to
    // materialize 0.0.
    if (Cond == ISD::SETO || Cond == ISD::SETUO)
      return DAG.getSetCC(dl, VT, N0, N0, Cond);

    // setcc (fneg x), C -> setcc swap(pred) x, -C
    if (N0.getOpcode() == ISD::FNEG) {
      ISD::CondCode SwapCond = ISD::getSetCCSwappedOperands(Cond);
      if (DCI.isBeforeLegalizeOps() ||
          isCondCodeLegal(SwapCond, N0.getSimpleValueType())) {
        SDValue NegN1 = DAG.getNode(ISD::FNEG, dl, N0.getValueType(), N1);
        return DAG.getSetCC(dl, VT, N0.getOperand(0), NegN1, SwapCond);
      }
    }

    // setueq/setoeq X, (fabs Inf) -> is_fpclass X, fcInf
    if (isOperationLegalOrCustom(ISD::IS_FPCLASS, N0.getValueType()) &&
        !isFPImmLegal(CFP->getValueAPF(), CFP->getValueType(0))) {
      bool IsFabs = N0.getOpcode() == ISD::FABS;
      SDValue Op = IsFabs ? N0.getOperand(0) : N0;
      if ((Cond == ISD::SETOEQ || Cond == ISD::SETUEQ) && CFP->isInfinity()) {
        FPClassTest Flag = CFP->isNegative() ? (IsFabs ? fcNone : fcNegInf)
                                             : (IsFabs ? fcInf : fcPosInf);
        if (Cond == ISD::SETUEQ)
          Flag |= fcNan;
        return DAG.getNode(ISD::IS_FPCLASS, dl, VT, Op,
                           DAG.getTargetConstant(Flag, dl, MVT::i32));
      }
    }

    // If the condition is not legal, see if we can find an equivalent one
    // which is legal.
    if (!isCondCodeLegal(Cond, N0.getSimpleValueType())) {
      // If the comparison was an awkward floating-point == or != and one of
      // the comparison operands is infinity or negative infinity, convert the
      // condition to a less-awkward <= or >=.
      if (CFP->getValueAPF().isInfinity()) {
        bool IsNegInf = CFP->getValueAPF().isNegative();
        ISD::CondCode NewCond = ISD::SETCC_INVALID;
        switch (Cond) {
        case ISD::SETOEQ: NewCond = IsNegInf ? ISD::SETOLE : ISD::SETOGE; break;
        case ISD::SETUEQ: NewCond = IsNegInf ? ISD::SETULE : ISD::SETUGE; break;
        case ISD::SETUNE: NewCond = IsNegInf ? ISD::SETUGT : ISD::SETULT; break;
        case ISD::SETONE: NewCond = IsNegInf ? ISD::SETOGT : ISD::SETOLT; break;
        default: break;
        }
        if (NewCond != ISD::SETCC_INVALID &&
            isCondCodeLegal(NewCond, N0.getSimpleValueType()))
          return DAG.getSetCC(dl, VT, N0, N1, NewCond);
      }
    }
  }

  if (N0 == N1) {
    // The sext(setcc()) => setcc() optimization relies on the appropriate
    // constant being emitted.
    assert(!N0.getValueType().isInteger() &&
           "Integer types should be handled by FoldSetCC");

    bool EqTrue = ISD::isTrueWhenEqual(Cond);
    unsigned UOF = ISD::getUnorderedFlavor(Cond);
    if (UOF == 2) // FP operators that are undefined on NaNs.
      return DAG.getBoolConstant(EqTrue, dl, VT, OpVT);
    if (UOF == unsigned(EqTrue))
      return DAG.getBoolConstant(EqTrue, dl, VT, OpVT);
    // Otherwise, we can't fold it.  However, we can simplify it to SETUO/SETO
    // if it is not already.
    ISD::CondCode NewCond = UOF == 0 ? ISD::SETO : ISD::SETUO;
    if (NewCond != Cond &&
        (DCI.isBeforeLegalizeOps() ||
                            isCondCodeLegal(NewCond, N0.getSimpleValueType())))
      return DAG.getSetCC(dl, VT, N0, N1, NewCond);
  }

  // ~X > ~Y --> Y > X
  // ~X < ~Y --> Y < X
  // ~X < C --> X > ~C
  // ~X > C --> X < ~C
  if ((isSignedIntSetCC(Cond) || isUnsignedIntSetCC(Cond)) &&
      N0.getValueType().isInteger()) {
    if (isBitwiseNot(N0)) {
      if (isBitwiseNot(N1))
        return DAG.getSetCC(dl, VT, N1.getOperand(0), N0.getOperand(0), Cond);

      if (DAG.isConstantIntBuildVectorOrConstantInt(N1) &&
          !DAG.isConstantIntBuildVectorOrConstantInt(N0.getOperand(0))) {
        SDValue Not = DAG.getNOT(dl, N1, OpVT);
        return DAG.getSetCC(dl, VT, Not, N0.getOperand(0), Cond);
      }
    }
  }

  if ((Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
      N0.getValueType().isInteger()) {
    if (N0.getOpcode() == ISD::ADD || N0.getOpcode() == ISD::SUB ||
        N0.getOpcode() == ISD::XOR) {
      // Simplify (X+Y) == (X+Z) -->  Y == Z
      if (N0.getOpcode() == N1.getOpcode()) {
        if (N0.getOperand(0) == N1.getOperand(0))
          return DAG.getSetCC(dl, VT, N0.getOperand(1), N1.getOperand(1), Cond);
        if (N0.getOperand(1) == N1.getOperand(1))
          return DAG.getSetCC(dl, VT, N0.getOperand(0), N1.getOperand(0), Cond);
        if (isCommutativeBinOp(N0.getOpcode())) {
          // If X op Y == Y op X, try other combinations.
          if (N0.getOperand(0) == N1.getOperand(1))
            return DAG.getSetCC(dl, VT, N0.getOperand(1), N1.getOperand(0),
                                Cond);
          if (N0.getOperand(1) == N1.getOperand(0))
            return DAG.getSetCC(dl, VT, N0.getOperand(0), N1.getOperand(1),
                                Cond);
        }
      }

      // If RHS is a legal immediate value for a compare instruction, we need
      // to be careful about increasing register pressure needlessly.
      bool LegalRHSImm = false;

      if (auto *RHSC = dyn_cast<ConstantSDNode>(N1)) {
        if (auto *LHSR = dyn_cast<ConstantSDNode>(N0.getOperand(1))) {
          // Turn (X+C1) == C2 --> X == C2-C1
          if (N0.getOpcode() == ISD::ADD && N0.getNode()->hasOneUse())
            return DAG.getSetCC(
                dl, VT, N0.getOperand(0),
                DAG.getConstant(RHSC->getAPIntValue() - LHSR->getAPIntValue(),
                                dl, N0.getValueType()),
                Cond);

          // Turn (X^C1) == C2 --> X == C1^C2
          if (N0.getOpcode() == ISD::XOR && N0.getNode()->hasOneUse())
            return DAG.getSetCC(
                dl, VT, N0.getOperand(0),
                DAG.getConstant(LHSR->getAPIntValue() ^ RHSC->getAPIntValue(),
                                dl, N0.getValueType()),
                Cond);
        }

        // Turn (C1-X) == C2 --> X == C1-C2
        if (auto *SUBC = dyn_cast<ConstantSDNode>(N0.getOperand(0)))
          if (N0.getOpcode() == ISD::SUB && N0.getNode()->hasOneUse())
            return DAG.getSetCC(
                dl, VT, N0.getOperand(1),
                DAG.getConstant(SUBC->getAPIntValue() - RHSC->getAPIntValue(),
                                dl, N0.getValueType()),
                Cond);

        // Could RHSC fold directly into a compare?
        if (RHSC->getValueType(0).getSizeInBits() <= 64)
          LegalRHSImm = isLegalICmpImmediate(RHSC->getSExtValue());
      }

      // (X+Y) == X --> Y == 0 and similar folds.
      // Don't do this if X is an immediate that can fold into a cmp
      // instruction and X+Y has other uses. It could be an induction variable
      // chain, and the transform would increase register pressure.
      if (!LegalRHSImm || N0.hasOneUse())
        if (SDValue V = foldSetCCWithBinOp(VT, N0, N1, Cond, dl, DCI))
          return V;
    }

    if (N1.getOpcode() == ISD::ADD || N1.getOpcode() == ISD::SUB ||
        N1.getOpcode() == ISD::XOR)
      if (SDValue V = foldSetCCWithBinOp(VT, N1, N0, Cond, dl, DCI))
        return V;

    if (SDValue V = foldSetCCWithAnd(VT, N0, N1, Cond, dl, DCI))
      return V;
  }

  // Fold remainder of division by a constant.
  if ((N0.getOpcode() == ISD::UREM || N0.getOpcode() == ISD::SREM) &&
      N0.hasOneUse() && (Cond == ISD::SETEQ || Cond == ISD::SETNE)) {
    // When division is cheap or optimizing for minimum size,
    // fall through to DIVREM creation by skipping this fold.
    if (!isIntDivCheap(VT, Attr) && !Attr.hasFnAttr(Attribute::MinSize)) {
      if (N0.getOpcode() == ISD::UREM) {
        if (SDValue Folded = buildUREMEqFold(VT, N0, N1, Cond, DCI, dl))
          return Folded;
      } else if (N0.getOpcode() == ISD::SREM) {
        if (SDValue Folded = buildSREMEqFold(VT, N0, N1, Cond, DCI, dl))
          return Folded;
      }
    }
  }

  // Fold away ALL boolean setcc's.
  if (N0.getValueType().getScalarType() == MVT::i1 && foldBooleans) {
    SDValue Temp;
    switch (Cond) {
    default: llvm_unreachable("Unknown integer setcc!");
    case ISD::SETEQ:  // X == Y  -> ~(X^Y)
      Temp = DAG.getNode(ISD::XOR, dl, OpVT, N0, N1);
      N0 = DAG.getNOT(dl, Temp, OpVT);
      if (!DCI.isCalledByLegalizer())
        DCI.AddToWorklist(Temp.getNode());
      break;
    case ISD::SETNE:  // X != Y   -->  (X^Y)
      N0 = DAG.getNode(ISD::XOR, dl, OpVT, N0, N1);
      break;
    case ISD::SETGT:  // X >s Y   -->  X == 0 & Y == 1  -->  ~X & Y
    case ISD::SETULT: // X <u Y   -->  X == 0 & Y == 1  -->  ~X & Y
      Temp = DAG.getNOT(dl, N0, OpVT);
      N0 = DAG.getNode(ISD::AND, dl, OpVT, N1, Temp);
      if (!DCI.isCalledByLegalizer())
        DCI.AddToWorklist(Temp.getNode());
      break;
    case ISD::SETLT:  // X <s Y   --> X == 1 & Y == 0  -->  ~Y & X
    case ISD::SETUGT: // X >u Y   --> X == 1 & Y == 0  -->  ~Y & X
      Temp = DAG.getNOT(dl, N1, OpVT);
      N0 = DAG.getNode(ISD::AND, dl, OpVT, N0, Temp);
      if (!DCI.isCalledByLegalizer())
        DCI.AddToWorklist(Temp.getNode());
      break;
    case ISD::SETULE: // X <=u Y  --> X == 0 | Y == 1  -->  ~X | Y
    case ISD::SETGE:  // X >=s Y  --> X == 0 | Y == 1  -->  ~X | Y
      Temp = DAG.getNOT(dl, N0, OpVT);
      N0 = DAG.getNode(ISD::OR, dl, OpVT, N1, Temp);
      if (!DCI.isCalledByLegalizer())
        DCI.AddToWorklist(Temp.getNode());
      break;
    case ISD::SETUGE: // X >=u Y  --> X == 1 | Y == 0  -->  ~Y | X
    case ISD::SETLE:  // X <=s Y  --> X == 1 | Y == 0  -->  ~Y | X
      Temp = DAG.getNOT(dl, N1, OpVT);
      N0 = DAG.getNode(ISD::OR, dl, OpVT, N0, Temp);
      break;
    }
    if (VT.getScalarType() != MVT::i1) {
      if (!DCI.isCalledByLegalizer())
        DCI.AddToWorklist(N0.getNode());
      // FIXME: If running after legalize, we probably can't do this.
      ISD::NodeType ExtendCode = getExtendForContent(getBooleanContents(OpVT));
      N0 = DAG.getNode(ExtendCode, dl, VT, N0);
    }
    return N0;
  }

  // Could not fold it.
  return SDValue();
}

/// Returns true (and the GlobalValue and the offset) if the node is a
/// GlobalAddress + offset.
bool TargetLowering::isGAPlusOffset(SDNode *WN, const GlobalValue *&GA,
                                    int64_t &Offset) const {

  SDNode *N = unwrapAddress(SDValue(WN, 0)).getNode();

  if (auto *GASD = dyn_cast<GlobalAddressSDNode>(N)) {
    GA = GASD->getGlobal();
    Offset += GASD->getOffset();
    return true;
  }

  if (N->getOpcode() == ISD::ADD) {
    SDValue N1 = N->getOperand(0);
    SDValue N2 = N->getOperand(1);
    if (isGAPlusOffset(N1.getNode(), GA, Offset)) {
      if (auto *V = dyn_cast<ConstantSDNode>(N2)) {
        Offset += V->getSExtValue();
        return true;
      }
    } else if (isGAPlusOffset(N2.getNode(), GA, Offset)) {
      if (auto *V = dyn_cast<ConstantSDNode>(N1)) {
        Offset += V->getSExtValue();
        return true;
      }
    }
  }

  return false;
}

SDValue TargetLowering::PerformDAGCombine(SDNode *N,
                                          DAGCombinerInfo &DCI) const {
  // Default implementation: no optimization.
  return SDValue();
}

//===----------------------------------------------------------------------===//
//  Inline Assembler Implementation Methods
//===----------------------------------------------------------------------===//

TargetLowering::ConstraintType
TargetLowering::getConstraintType(StringRef Constraint) const {
  unsigned S = Constraint.size();

  if (S == 1) {
    switch (Constraint[0]) {
    default: break;
    case 'r':
      return C_RegisterClass;
    case 'm': // memory
    case 'o': // offsetable
    case 'V': // not offsetable
      return C_Memory;
    case 'p': // Address.
      return C_Address;
    case 'n': // Simple Integer
    case 'E': // Floating Point Constant
    case 'F': // Floating Point Constant
      return C_Immediate;
    case 'i': // Simple Integer or Relocatable Constant
    case 's': // Relocatable Constant
    case 'X': // Allow ANY value.
    case 'I': // Target registers.
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case '<':
    case '>':
      return C_Other;
    }
  }

  if (S > 1 && Constraint[0] == '{' && Constraint[S - 1] == '}') {
    if (S == 8 && Constraint.substr(1, 6) == "memory") // "{memory}"
      return C_Memory;
    return C_Register;
  }
  return C_Unknown;
}

/// Try to replace an X constraint, which matches anything, with another that
/// has more specific requirements based on the type of the corresponding
/// operand.
const char *TargetLowering::LowerXConstraint(EVT ConstraintVT) const {
  if (ConstraintVT.isInteger())
    return "r";
  if (ConstraintVT.isFloatingPoint())
    return "f"; // works for many targets
  return nullptr;
}

SDValue TargetLowering::LowerAsmOutputForConstraint(
    SDValue &Chain, SDValue &Glue, const SDLoc &DL,
    const AsmOperandInfo &OpInfo, SelectionDAG &DAG) const {
  return SDValue();
}

/// Lower the specified operand into the Ops vector.
/// If it is invalid, don't add anything to Ops.
void TargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                  StringRef Constraint,
                                                  std::vector<SDValue> &Ops,
                                                  SelectionDAG &DAG) const {

  if (Constraint.size() > 1)
    return;

  char ConstraintLetter = Constraint[0];
  switch (ConstraintLetter) {
  default: break;
  case 'X':    // Allows any operand
  case 'i':    // Simple Integer or Relocatable Constant
  case 'n':    // Simple Integer
  case 's': {  // Relocatable Constant

    ConstantSDNode *C;
    uint64_t Offset = 0;

    // Match (GA) or (C) or (GA+C) or (GA-C) or ((GA+C)+C) or (((GA+C)+C)+C),
    // etc., since getelementpointer is variadic. We can't use
    // SelectionDAG::FoldSymbolOffset because it expects the GA to be accessible
    // while in this case the GA may be furthest from the root node which is
    // likely an ISD::ADD.
    while (true) {
      if ((C = dyn_cast<ConstantSDNode>(Op)) && ConstraintLetter != 's') {
        // gcc prints these as sign extended.  Sign extend value to 64 bits
        // now; without this it would get ZExt'd later in
        // ScheduleDAGSDNodes::EmitNode, which is very generic.
        bool IsBool = C->getConstantIntValue()->getBitWidth() == 1;
        BooleanContent BCont = getBooleanContents(MVT::i64);
        ISD::NodeType ExtOpc =
            IsBool ? getExtendForContent(BCont) : ISD::SIGN_EXTEND;
        int64_t ExtVal =
            ExtOpc == ISD::ZERO_EXTEND ? C->getZExtValue() : C->getSExtValue();
        Ops.push_back(
            DAG.getTargetConstant(Offset + ExtVal, SDLoc(C), MVT::i64));
        return;
      }
      if (ConstraintLetter != 'n') {
        if (const auto *GA = dyn_cast<GlobalAddressSDNode>(Op)) {
          Ops.push_back(DAG.getTargetGlobalAddress(GA->getGlobal(), SDLoc(Op),
                                                   GA->getValueType(0),
                                                   Offset + GA->getOffset()));
          return;
        }
        if (const auto *BA = dyn_cast<BlockAddressSDNode>(Op)) {
          Ops.push_back(DAG.getTargetBlockAddress(
              BA->getBlockAddress(), BA->getValueType(0),
              Offset + BA->getOffset(), BA->getTargetFlags()));
          return;
        }
        if (isa<BasicBlockSDNode>(Op)) {
          Ops.push_back(Op);
          return;
        }
      }
      const unsigned OpCode = Op.getOpcode();
      if (OpCode == ISD::ADD || OpCode == ISD::SUB) {
        if ((C = dyn_cast<ConstantSDNode>(Op.getOperand(0))))
          Op = Op.getOperand(1);
        // Subtraction is not commutative.
        else if (OpCode == ISD::ADD &&
                 (C = dyn_cast<ConstantSDNode>(Op.getOperand(1))))
          Op = Op.getOperand(0);
        else
          return;
        Offset += (OpCode == ISD::ADD ? 1 : -1) * C->getSExtValue();
        continue;
      }
      return;
    }
    break;
  }
  }
}

void TargetLowering::CollectTargetIntrinsicOperands(
    const CallInst &I, SmallVectorImpl<SDValue> &Ops, SelectionDAG &DAG) const {
}

std::pair<unsigned, const TargetRegisterClass *>
TargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *RI,
                                             StringRef Constraint,
                                             MVT VT) const {
  if (!Constraint.starts_with("{"))
    return std::make_pair(0u, static_cast<TargetRegisterClass *>(nullptr));
  assert(*(Constraint.end() - 1) == '}' && "Not a brace enclosed constraint?");

  // Remove the braces from around the name.
  StringRef RegName(Constraint.data() + 1, Constraint.size() - 2);

  std::pair<unsigned, const TargetRegisterClass *> R =
      std::make_pair(0u, static_cast<const TargetRegisterClass *>(nullptr));

  // Figure out which register class contains this reg.
  for (const TargetRegisterClass *RC : RI->regclasses()) {
    // If none of the value types for this register class are valid, we
    // can't use it.  For example, 64-bit reg classes on 32-bit targets.
    if (!isLegalRC(*RI, *RC))
      continue;

    for (const MCPhysReg &PR : *RC) {
      if (RegName.equals_insensitive(RI->getRegAsmName(PR))) {
        std::pair<unsigned, const TargetRegisterClass *> S =
            std::make_pair(PR, RC);

        // If this register class has the requested value type, return it,
        // otherwise keep searching and return the first class found
        // if no other is found which explicitly has the requested type.
        if (RI->isTypeLegalForClass(*RC, VT))
          return S;
        if (!R.second)
          R = S;
      }
    }
  }

  return R;
}

//===----------------------------------------------------------------------===//
// Constraint Selection.

/// Return true of this is an input operand that is a matching constraint like
/// "4".
bool TargetLowering::AsmOperandInfo::isMatchingInputConstraint() const {
  assert(!ConstraintCode.empty() && "No known constraint!");
  return isdigit(static_cast<unsigned char>(ConstraintCode[0]));
}

/// If this is an input matching constraint, this method returns the output
/// operand it matches.
unsigned TargetLowering::AsmOperandInfo::getMatchedOperand() const {
  assert(!ConstraintCode.empty() && "No known constraint!");
  return atoi(ConstraintCode.c_str());
}

/// Split up the constraint string from the inline assembly value into the
/// specific constraints and their prefixes, and also tie in the associated
/// operand values.
/// If this returns an empty vector, and if the constraint string itself
/// isn't empty, there was an error parsing.
TargetLowering::AsmOperandInfoVector
TargetLowering::ParseConstraints(const DataLayout &DL,
                                 const TargetRegisterInfo *TRI,
                                 const CallBase &Call) const {
  /// Information about all of the constraints.
  AsmOperandInfoVector ConstraintOperands;
  const InlineAsm *IA = cast<InlineAsm>(Call.getCalledOperand());
  unsigned maCount = 0; // Largest number of multiple alternative constraints.

  // Do a prepass over the constraints, canonicalizing them, and building up the
  // ConstraintOperands list.
  unsigned ArgNo = 0; // ArgNo - The argument of the CallInst.
  unsigned ResNo = 0; // ResNo - The result number of the next output.
  unsigned LabelNo = 0; // LabelNo - CallBr indirect dest number.

  for (InlineAsm::ConstraintInfo &CI : IA->ParseConstraints()) {
    ConstraintOperands.emplace_back(std::move(CI));
    AsmOperandInfo &OpInfo = ConstraintOperands.back();

    // Update multiple alternative constraint count.
    if (OpInfo.multipleAlternatives.size() > maCount)
      maCount = OpInfo.multipleAlternatives.size();

    OpInfo.ConstraintVT = MVT::Other;

    // Compute the value type for each operand.
    switch (OpInfo.Type) {
    case InlineAsm::isOutput:
      // Indirect outputs just consume an argument.
      if (OpInfo.isIndirect) {
        OpInfo.CallOperandVal = Call.getArgOperand(ArgNo);
        break;
      }

      // The return value of the call is this value.  As such, there is no
      // corresponding argument.
      assert(!Call.getType()->isVoidTy() && "Bad inline asm!");
      if (auto *STy = dyn_cast<StructType>(Call.getType())) {
        OpInfo.ConstraintVT =
            getSimpleValueType(DL, STy->getElementType(ResNo));
      } else {
        assert(ResNo == 0 && "Asm only has one result!");
        OpInfo.ConstraintVT =
            getAsmOperandValueType(DL, Call.getType()).getSimpleVT();
      }
      ++ResNo;
      break;
    case InlineAsm::isInput:
      OpInfo.CallOperandVal = Call.getArgOperand(ArgNo);
      break;
    case InlineAsm::isLabel:
      OpInfo.CallOperandVal = cast<CallBrInst>(&Call)->getIndirectDest(LabelNo);
      ++LabelNo;
      continue;
    case InlineAsm::isClobber:
      // Nothing to do.
      break;
    }

    if (OpInfo.CallOperandVal) {
      llvm::Type *OpTy = OpInfo.CallOperandVal->getType();
      if (OpInfo.isIndirect) {
        OpTy = Call.getParamElementType(ArgNo);
        assert(OpTy && "Indirect operand must have elementtype attribute");
      }

      // Look for vector wrapped in a struct. e.g. { <16 x i8> }.
      if (StructType *STy = dyn_cast<StructType>(OpTy))
        if (STy->getNumElements() == 1)
          OpTy = STy->getElementType(0);

      // If OpTy is not a single value, it may be a struct/union that we
      // can tile with integers.
      if (!OpTy->isSingleValueType() && OpTy->isSized()) {
        unsigned BitSize = DL.getTypeSizeInBits(OpTy);
        switch (BitSize) {
        default: break;
        case 1:
        case 8:
        case 16:
        case 32:
        case 64:
        case 128:
          OpTy = IntegerType::get(OpTy->getContext(), BitSize);
          break;
        }
      }

      EVT VT = getAsmOperandValueType(DL, OpTy, true);
      OpInfo.ConstraintVT = VT.isSimple() ? VT.getSimpleVT() : MVT::Other;
      ArgNo++;
    }
  }

  // If we have multiple alternative constraints, select the best alternative.
  if (!ConstraintOperands.empty()) {
    if (maCount) {
      unsigned bestMAIndex = 0;
      int bestWeight = -1;
      // weight:  -1 = invalid match, and 0 = so-so match to 5 = good match.
      int weight = -1;
      unsigned maIndex;
      // Compute the sums of the weights for each alternative, keeping track
      // of the best (highest weight) one so far.
      for (maIndex = 0; maIndex < maCount; ++maIndex) {
        int weightSum = 0;
        for (unsigned cIndex = 0, eIndex = ConstraintOperands.size();
             cIndex != eIndex; ++cIndex) {
          AsmOperandInfo &OpInfo = ConstraintOperands[cIndex];
          if (OpInfo.Type == InlineAsm::isClobber)
            continue;

          // If this is an output operand with a matching input operand,
          // look up the matching input. If their types mismatch, e.g. one
          // is an integer, the other is floating point, or their sizes are
          // different, flag it as an maCantMatch.
          if (OpInfo.hasMatchingInput()) {
            AsmOperandInfo &Input = ConstraintOperands[OpInfo.MatchingInput];
            if (OpInfo.ConstraintVT != Input.ConstraintVT) {
              if ((OpInfo.ConstraintVT.isInteger() !=
                   Input.ConstraintVT.isInteger()) ||
                  (OpInfo.ConstraintVT.getSizeInBits() !=
                   Input.ConstraintVT.getSizeInBits())) {
                weightSum = -1; // Can't match.
                break;
              }
            }
          }
          weight = getMultipleConstraintMatchWeight(OpInfo, maIndex);
          if (weight == -1) {
            weightSum = -1;
            break;
          }
          weightSum += weight;
        }
        // Update best.
        if (weightSum > bestWeight) {
          bestWeight = weightSum;
          bestMAIndex = maIndex;
        }
      }

      // Now select chosen alternative in each constraint.
      for (AsmOperandInfo &cInfo : ConstraintOperands)
        if (cInfo.Type != InlineAsm::isClobber)
          cInfo.selectAlternative(bestMAIndex);
    }
  }

  // Check and hook up tied operands, choose constraint code to use.
  for (unsigned cIndex = 0, eIndex = ConstraintOperands.size();
       cIndex != eIndex; ++cIndex) {
    AsmOperandInfo &OpInfo = ConstraintOperands[cIndex];

    // If this is an output operand with a matching input operand, look up the
    // matching input. If their types mismatch, e.g. one is an integer, the
    // other is floating point, or their sizes are different, flag it as an
    // error.
    if (OpInfo.hasMatchingInput()) {
      AsmOperandInfo &Input = ConstraintOperands[OpInfo.MatchingInput];

      if (OpInfo.ConstraintVT != Input.ConstraintVT) {
        std::pair<unsigned, const TargetRegisterClass *> MatchRC =
            getRegForInlineAsmConstraint(TRI, OpInfo.ConstraintCode,
                                         OpInfo.ConstraintVT);
        std::pair<unsigned, const TargetRegisterClass *> InputRC =
            getRegForInlineAsmConstraint(TRI, Input.ConstraintCode,
                                         Input.ConstraintVT);
        if ((OpInfo.ConstraintVT.isInteger() !=
             Input.ConstraintVT.isInteger()) ||
            (MatchRC.second != InputRC.second)) {
          report_fatal_error("Unsupported asm: input constraint"
                             " with a matching output constraint of"
                             " incompatible type!");
        }
      }
    }
  }

  return ConstraintOperands;
}

/// Return a number indicating our preference for chosing a type of constraint
/// over another, for the purpose of sorting them. Immediates are almost always
/// preferrable (when they can be emitted). A higher return value means a
/// stronger preference for one constraint type relative to another.
/// FIXME: We should prefer registers over memory but doing so may lead to
/// unrecoverable register exhaustion later.
/// https://github.com/llvm/llvm-project/issues/20571
static unsigned getConstraintPiority(TargetLowering::ConstraintType CT) {
  switch (CT) {
  case TargetLowering::C_Immediate:
  case TargetLowering::C_Other:
    return 4;
  case TargetLowering::C_Memory:
  case TargetLowering::C_Address:
    return 3;
  case TargetLowering::C_RegisterClass:
    return 2;
  case TargetLowering::C_Register:
    return 1;
  case TargetLowering::C_Unknown:
    return 0;
  }
  llvm_unreachable("Invalid constraint type");
}

/// Examine constraint type and operand type and determine a weight value.
/// This object must already have been set up with the operand type
/// and the current alternative constraint selected.
TargetLowering::ConstraintWeight
  TargetLowering::getMultipleConstraintMatchWeight(
    AsmOperandInfo &info, int maIndex) const {
  InlineAsm::ConstraintCodeVector *rCodes;
  if (maIndex >= (int)info.multipleAlternatives.size())
    rCodes = &info.Codes;
  else
    rCodes = &info.multipleAlternatives[maIndex].Codes;
  ConstraintWeight BestWeight = CW_Invalid;

  // Loop over the options, keeping track of the most general one.
  for (const std::string &rCode : *rCodes) {
    ConstraintWeight weight =
        getSingleConstraintMatchWeight(info, rCode.c_str());
    if (weight > BestWeight)
      BestWeight = weight;
  }

  return BestWeight;
}

/// Examine constraint type and operand type and determine a weight value.
/// This object must already have been set up with the operand type
/// and the current alternative constraint selected.
TargetLowering::ConstraintWeight
  TargetLowering::getSingleConstraintMatchWeight(
    AsmOperandInfo &info, const char *constraint) const {
  ConstraintWeight weight = CW_Invalid;
  Value *CallOperandVal = info.CallOperandVal;
    // If we don't have a value, we can't do a match,
    // but allow it at the lowest weight.
  if (!CallOperandVal)
    return CW_Default;
  // Look at the constraint type.
  switch (*constraint) {
    case 'i': // immediate integer.
    case 'n': // immediate integer with a known value.
      if (isa<ConstantInt>(CallOperandVal))
        weight = CW_Constant;
      break;
    case 's': // non-explicit intregal immediate.
      if (isa<GlobalValue>(CallOperandVal))
        weight = CW_Constant;
      break;
    case 'E': // immediate float if host format.
    case 'F': // immediate float.
      if (isa<ConstantFP>(CallOperandVal))
        weight = CW_Constant;
      break;
    case '<': // memory operand with autodecrement.
    case '>': // memory operand with autoincrement.
    case 'm': // memory operand.
    case 'o': // offsettable memory operand
    case 'V': // non-offsettable memory operand
      weight = CW_Memory;
      break;
    case 'r': // general register.
    case 'g': // general register, memory operand or immediate integer.
              // note: Clang converts "g" to "imr".
      if (CallOperandVal->getType()->isIntegerTy())
        weight = CW_Register;
      break;
    case 'X': // any operand.
  default:
    weight = CW_Default;
    break;
  }
  return weight;
}

/// If there are multiple different constraints that we could pick for this
/// operand (e.g. "imr") try to pick the 'best' one.
/// This is somewhat tricky: constraints (TargetLowering::ConstraintType) fall
/// into seven classes:
///    Register      -> one specific register
///    RegisterClass -> a group of regs
///    Memory        -> memory
///    Address       -> a symbolic memory reference
///    Immediate     -> immediate values
///    Other         -> magic values (such as "Flag Output Operands")
///    Unknown       -> something we don't recognize yet and can't handle
/// Ideally, we would pick the most specific constraint possible: if we have
/// something that fits into a register, we would pick it.  The problem here
/// is that if we have something that could either be in a register or in
/// memory that use of the register could cause selection of *other*
/// operands to fail: they might only succeed if we pick memory.  Because of
/// this the heuristic we use is:
///
///  1) If there is an 'other' constraint, and if the operand is valid for
///     that constraint, use it.  This makes us take advantage of 'i'
///     constraints when available.
///  2) Otherwise, pick the most general constraint present.  This prefers
///     'm' over 'r', for example.
///
TargetLowering::ConstraintGroup TargetLowering::getConstraintPreferences(
    TargetLowering::AsmOperandInfo &OpInfo) const {
  ConstraintGroup Ret;

  Ret.reserve(OpInfo.Codes.size());
  for (StringRef Code : OpInfo.Codes) {
    TargetLowering::ConstraintType CType = getConstraintType(Code);

    // Indirect 'other' or 'immediate' constraints are not allowed.
    if (OpInfo.isIndirect && !(CType == TargetLowering::C_Memory ||
                               CType == TargetLowering::C_Register ||
                               CType == TargetLowering::C_RegisterClass))
      continue;

    // Things with matching constraints can only be registers, per gcc
    // documentation.  This mainly affects "g" constraints.
    if (CType == TargetLowering::C_Memory && OpInfo.hasMatchingInput())
      continue;

    Ret.emplace_back(Code, CType);
  }

  std::stable_sort(
      Ret.begin(), Ret.end(), [](ConstraintPair a, ConstraintPair b) {
        return getConstraintPiority(a.second) > getConstraintPiority(b.second);
      });

  return Ret;
}

/// If we have an immediate, see if we can lower it. Return true if we can,
/// false otherwise.
static bool lowerImmediateIfPossible(TargetLowering::ConstraintPair &P,
                                     SDValue Op, SelectionDAG *DAG,
                                     const TargetLowering &TLI) {

  assert((P.second == TargetLowering::C_Other ||
          P.second == TargetLowering::C_Immediate) &&
         "need immediate or other");

  if (!Op.getNode())
    return false;

  std::vector<SDValue> ResultOps;
  TLI.LowerAsmOperandForConstraint(Op, P.first, ResultOps, *DAG);
  return !ResultOps.empty();
}

/// Determines the constraint code and constraint type to use for the specific
/// AsmOperandInfo, setting OpInfo.ConstraintCode and OpInfo.ConstraintType.
void TargetLowering::ComputeConstraintToUse(AsmOperandInfo &OpInfo,
                                            SDValue Op,
                                            SelectionDAG *DAG) const {
  assert(!OpInfo.Codes.empty() && "Must have at least one constraint");

  // Single-letter constraints ('r') are very common.
  if (OpInfo.Codes.size() == 1) {
    OpInfo.ConstraintCode = OpInfo.Codes[0];
    OpInfo.ConstraintType = getConstraintType(OpInfo.ConstraintCode);
  } else {
    ConstraintGroup G = getConstraintPreferences(OpInfo);
    if (G.empty())
      return;

    unsigned BestIdx = 0;
    for (const unsigned E = G.size();
         BestIdx < E && (G[BestIdx].second == TargetLowering::C_Other ||
                         G[BestIdx].second == TargetLowering::C_Immediate);
         ++BestIdx) {
      if (lowerImmediateIfPossible(G[BestIdx], Op, DAG, *this))
        break;
      // If we're out of constraints, just pick the first one.
      if (BestIdx + 1 == E) {
        BestIdx = 0;
        break;
      }
    }

    OpInfo.ConstraintCode = G[BestIdx].first;
    OpInfo.ConstraintType = G[BestIdx].second;
  }

  // 'X' matches anything.
  if (OpInfo.ConstraintCode == "X" && OpInfo.CallOperandVal) {
    // Constants are handled elsewhere.  For Functions, the type here is the
    // type of the result, which is not what we want to look at; leave them
    // alone.
    Value *v = OpInfo.CallOperandVal;
    if (isa<ConstantInt>(v) || isa<Function>(v)) {
      return;
    }

    if (isa<BasicBlock>(v) || isa<BlockAddress>(v)) {
      OpInfo.ConstraintCode = "i";
      return;
    }

    // Otherwise, try to resolve it to something we know about by looking at
    // the actual operand type.
    if (const char *Repl = LowerXConstraint(OpInfo.ConstraintVT)) {
      OpInfo.ConstraintCode = Repl;
      OpInfo.ConstraintType = getConstraintType(OpInfo.ConstraintCode);
    }
  }
}

/// Given an exact SDIV by a constant, create a multiplication
/// with the multiplicative inverse of the constant.
/// Ref: "Hacker's Delight" by Henry Warren, 2nd Edition, p. 242
static SDValue BuildExactSDIV(const TargetLowering &TLI, SDNode *N,
                              const SDLoc &dl, SelectionDAG &DAG,
                              SmallVectorImpl<SDNode *> &Created) {
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  EVT SVT = VT.getScalarType();
  EVT ShVT = TLI.getShiftAmountTy(VT, DAG.getDataLayout());
  EVT ShSVT = ShVT.getScalarType();

  bool UseSRA = false;
  SmallVector<SDValue, 16> Shifts, Factors;

  auto BuildSDIVPattern = [&](ConstantSDNode *C) {
    if (C->isZero())
      return false;
    APInt Divisor = C->getAPIntValue();
    unsigned Shift = Divisor.countr_zero();
    if (Shift) {
      Divisor.ashrInPlace(Shift);
      UseSRA = true;
    }
    APInt Factor = Divisor.multiplicativeInverse();
    Shifts.push_back(DAG.getConstant(Shift, dl, ShSVT));
    Factors.push_back(DAG.getConstant(Factor, dl, SVT));
    return true;
  };

  // Collect all magic values from the build vector.
  if (!ISD::matchUnaryPredicate(Op1, BuildSDIVPattern))
    return SDValue();

  SDValue Shift, Factor;
  if (Op1.getOpcode() == ISD::BUILD_VECTOR) {
    Shift = DAG.getBuildVector(ShVT, dl, Shifts);
    Factor = DAG.getBuildVector(VT, dl, Factors);
  } else if (Op1.getOpcode() == ISD::SPLAT_VECTOR) {
    assert(Shifts.size() == 1 && Factors.size() == 1 &&
           "Expected matchUnaryPredicate to return one element for scalable "
           "vectors");
    Shift = DAG.getSplatVector(ShVT, dl, Shifts[0]);
    Factor = DAG.getSplatVector(VT, dl, Factors[0]);
  } else {
    assert(isa<ConstantSDNode>(Op1) && "Expected a constant");
    Shift = Shifts[0];
    Factor = Factors[0];
  }

  SDValue Res = Op0;
  if (UseSRA) {
    SDNodeFlags Flags;
    Flags.setExact(true);
    Res = DAG.getNode(ISD::SRA, dl, VT, Res, Shift, Flags);
    Created.push_back(Res.getNode());
  }

  return DAG.getNode(ISD::MUL, dl, VT, Res, Factor);
}

/// Given an exact UDIV by a constant, create a multiplication
/// with the multiplicative inverse of the constant.
/// Ref: "Hacker's Delight" by Henry Warren, 2nd Edition, p. 242
static SDValue BuildExactUDIV(const TargetLowering &TLI, SDNode *N,
                              const SDLoc &dl, SelectionDAG &DAG,
                              SmallVectorImpl<SDNode *> &Created) {
  EVT VT = N->getValueType(0);
  EVT SVT = VT.getScalarType();
  EVT ShVT = TLI.getShiftAmountTy(VT, DAG.getDataLayout());
  EVT ShSVT = ShVT.getScalarType();

  bool UseSRL = false;
  SmallVector<SDValue, 16> Shifts, Factors;

  auto BuildUDIVPattern = [&](ConstantSDNode *C) {
    if (C->isZero())
      return false;
    APInt Divisor = C->getAPIntValue();
    unsigned Shift = Divisor.countr_zero();
    if (Shift) {
      Divisor.lshrInPlace(Shift);
      UseSRL = true;
    }
    // Calculate the multiplicative inverse modulo BW.
    APInt Factor = Divisor.multiplicativeInverse();
    Shifts.push_back(DAG.getConstant(Shift, dl, ShSVT));
    Factors.push_back(DAG.getConstant(Factor, dl, SVT));
    return true;
  };

  SDValue Op1 = N->getOperand(1);

  // Collect all magic values from the build vector.
  if (!ISD::matchUnaryPredicate(Op1, BuildUDIVPattern))
    return SDValue();

  SDValue Shift, Factor;
  if (Op1.getOpcode() == ISD::BUILD_VECTOR) {
    Shift = DAG.getBuildVector(ShVT, dl, Shifts);
    Factor = DAG.getBuildVector(VT, dl, Factors);
  } else if (Op1.getOpcode() == ISD::SPLAT_VECTOR) {
    assert(Shifts.size() == 1 && Factors.size() == 1 &&
           "Expected matchUnaryPredicate to return one element for scalable "
           "vectors");
    Shift = DAG.getSplatVector(ShVT, dl, Shifts[0]);
    Factor = DAG.getSplatVector(VT, dl, Factors[0]);
  } else {
    assert(isa<ConstantSDNode>(Op1) && "Expected a constant");
    Shift = Shifts[0];
    Factor = Factors[0];
  }

  SDValue Res = N->getOperand(0);
  if (UseSRL) {
    SDNodeFlags Flags;
    Flags.setExact(true);
    Res = DAG.getNode(ISD::SRL, dl, VT, Res, Shift, Flags);
    Created.push_back(Res.getNode());
  }

  return DAG.getNode(ISD::MUL, dl, VT, Res, Factor);
}

SDValue TargetLowering::BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                              SelectionDAG &DAG,
                              SmallVectorImpl<SDNode *> &Created) const {
  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (TLI.isIntDivCheap(N->getValueType(0), Attr))
    return SDValue(N, 0); // Lower SDIV as SDIV
  return SDValue();
}

SDValue
TargetLowering::BuildSREMPow2(SDNode *N, const APInt &Divisor,
                              SelectionDAG &DAG,
                              SmallVectorImpl<SDNode *> &Created) const {
  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (TLI.isIntDivCheap(N->getValueType(0), Attr))
    return SDValue(N, 0); // Lower SREM as SREM
  return SDValue();
}

/// Build sdiv by power-of-2 with conditional move instructions
/// Ref: "Hacker's Delight" by Henry Warren 10-1
/// If conditional move/branch is preferred, we lower sdiv x, +/-2**k into:
///   bgez x, label
///   add x, x, 2**k-1
/// label:
///   sra res, x, k
///   neg res, res (when the divisor is negative)
SDValue TargetLowering::buildSDIVPow2WithCMov(
    SDNode *N, const APInt &Divisor, SelectionDAG &DAG,
    SmallVectorImpl<SDNode *> &Created) const {
  unsigned Lg2 = Divisor.countr_zero();
  EVT VT = N->getValueType(0);

  SDLoc DL(N);
  SDValue N0 = N->getOperand(0);
  SDValue Zero = DAG.getConstant(0, DL, VT);
  APInt Lg2Mask = APInt::getLowBitsSet(VT.getSizeInBits(), Lg2);
  SDValue Pow2MinusOne = DAG.getConstant(Lg2Mask, DL, VT);

  // If N0 is negative, we need to add (Pow2 - 1) to it before shifting right.
  EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue Cmp = DAG.getSetCC(DL, CCVT, N0, Zero, ISD::SETLT);
  SDValue Add = DAG.getNode(ISD::ADD, DL, VT, N0, Pow2MinusOne);
  SDValue CMov = DAG.getNode(ISD::SELECT, DL, VT, Cmp, Add, N0);

  Created.push_back(Cmp.getNode());
  Created.push_back(Add.getNode());
  Created.push_back(CMov.getNode());

  // Divide by pow2.
  SDValue SRA =
      DAG.getNode(ISD::SRA, DL, VT, CMov, DAG.getConstant(Lg2, DL, VT));

  // If we're dividing by a positive value, we're done.  Otherwise, we must
  // negate the result.
  if (Divisor.isNonNegative())
    return SRA;

  Created.push_back(SRA.getNode());
  return DAG.getNode(ISD::SUB, DL, VT, Zero, SRA);
}

/// Given an ISD::SDIV node expressing a divide by constant,
/// return a DAG expression to select that will generate the same value by
/// multiplying by a magic number.
/// Ref: "Hacker's Delight" or "The PowerPC Compiler Writer's Guide".
SDValue TargetLowering::BuildSDIV(SDNode *N, SelectionDAG &DAG,
                                  bool IsAfterLegalization,
                                  SmallVectorImpl<SDNode *> &Created) const {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  EVT SVT = VT.getScalarType();
  EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
  EVT ShSVT = ShVT.getScalarType();
  unsigned EltBits = VT.getScalarSizeInBits();
  EVT MulVT;

  // Check to see if we can do this.
  // FIXME: We should be more aggressive here.
  if (!isTypeLegal(VT)) {
    // Limit this to simple scalars for now.
    if (VT.isVector() || !VT.isSimple())
      return SDValue();

    // If this type will be promoted to a large enough type with a legal
    // multiply operation, we can go ahead and do this transform.
    if (getTypeAction(VT.getSimpleVT()) != TypePromoteInteger)
      return SDValue();

    MulVT = getTypeToTransformTo(*DAG.getContext(), VT);
    if (MulVT.getSizeInBits() < (2 * EltBits) ||
        !isOperationLegal(ISD::MUL, MulVT))
      return SDValue();
  }

  // If the sdiv has an 'exact' bit we can use a simpler lowering.
  if (N->getFlags().hasExact())
    return BuildExactSDIV(*this, N, dl, DAG, Created);

  SmallVector<SDValue, 16> MagicFactors, Factors, Shifts, ShiftMasks;

  auto BuildSDIVPattern = [&](ConstantSDNode *C) {
    if (C->isZero())
      return false;

    const APInt &Divisor = C->getAPIntValue();
    SignedDivisionByConstantInfo magics = SignedDivisionByConstantInfo::get(Divisor);
    int NumeratorFactor = 0;
    int ShiftMask = -1;

    if (Divisor.isOne() || Divisor.isAllOnes()) {
      // If d is +1/-1, we just multiply the numerator by +1/-1.
      NumeratorFactor = Divisor.getSExtValue();
      magics.Magic = 0;
      magics.ShiftAmount = 0;
      ShiftMask = 0;
    } else if (Divisor.isStrictlyPositive() && magics.Magic.isNegative()) {
      // If d > 0 and m < 0, add the numerator.
      NumeratorFactor = 1;
    } else if (Divisor.isNegative() && magics.Magic.isStrictlyPositive()) {
      // If d < 0 and m > 0, subtract the numerator.
      NumeratorFactor = -1;
    }

    MagicFactors.push_back(DAG.getConstant(magics.Magic, dl, SVT));
    Factors.push_back(DAG.getConstant(NumeratorFactor, dl, SVT));
    Shifts.push_back(DAG.getConstant(magics.ShiftAmount, dl, ShSVT));
    ShiftMasks.push_back(DAG.getConstant(ShiftMask, dl, SVT));
    return true;
  };

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // Collect the shifts / magic values from each element.
  if (!ISD::matchUnaryPredicate(N1, BuildSDIVPattern))
    return SDValue();

  SDValue MagicFactor, Factor, Shift, ShiftMask;
  if (N1.getOpcode() == ISD::BUILD_VECTOR) {
    MagicFactor = DAG.getBuildVector(VT, dl, MagicFactors);
    Factor = DAG.getBuildVector(VT, dl, Factors);
    Shift = DAG.getBuildVector(ShVT, dl, Shifts);
    ShiftMask = DAG.getBuildVector(VT, dl, ShiftMasks);
  } else if (N1.getOpcode() == ISD::SPLAT_VECTOR) {
    assert(MagicFactors.size() == 1 && Factors.size() == 1 &&
           Shifts.size() == 1 && ShiftMasks.size() == 1 &&
           "Expected matchUnaryPredicate to return one element for scalable "
           "vectors");
    MagicFactor = DAG.getSplatVector(VT, dl, MagicFactors[0]);
    Factor = DAG.getSplatVector(VT, dl, Factors[0]);
    Shift = DAG.getSplatVector(ShVT, dl, Shifts[0]);
    ShiftMask = DAG.getSplatVector(VT, dl, ShiftMasks[0]);
  } else {
    assert(isa<ConstantSDNode>(N1) && "Expected a constant");
    MagicFactor = MagicFactors[0];
    Factor = Factors[0];
    Shift = Shifts[0];
    ShiftMask = ShiftMasks[0];
  }

  // Multiply the numerator (operand 0) by the magic value.
  // FIXME: We should support doing a MUL in a wider type.
  auto GetMULHS = [&](SDValue X, SDValue Y) {
    // If the type isn't legal, use a wider mul of the type calculated
    // earlier.
    if (!isTypeLegal(VT)) {
      X = DAG.getNode(ISD::SIGN_EXTEND, dl, MulVT, X);
      Y = DAG.getNode(ISD::SIGN_EXTEND, dl, MulVT, Y);
      Y = DAG.getNode(ISD::MUL, dl, MulVT, X, Y);
      Y = DAG.getNode(ISD::SRL, dl, MulVT, Y,
                      DAG.getShiftAmountConstant(EltBits, MulVT, dl));
      return DAG.getNode(ISD::TRUNCATE, dl, VT, Y);
    }

    if (isOperationLegalOrCustom(ISD::MULHS, VT, IsAfterLegalization))
      return DAG.getNode(ISD::MULHS, dl, VT, X, Y);
    if (isOperationLegalOrCustom(ISD::SMUL_LOHI, VT, IsAfterLegalization)) {
      SDValue LoHi =
          DAG.getNode(ISD::SMUL_LOHI, dl, DAG.getVTList(VT, VT), X, Y);
      return SDValue(LoHi.getNode(), 1);
    }
    // If type twice as wide legal, widen and use a mul plus a shift.
    unsigned Size = VT.getScalarSizeInBits();
    EVT WideVT = EVT::getIntegerVT(*DAG.getContext(), Size * 2);
    if (VT.isVector())
      WideVT = EVT::getVectorVT(*DAG.getContext(), WideVT,
                                VT.getVectorElementCount());
    if (isOperationLegalOrCustom(ISD::MUL, WideVT)) {
      X = DAG.getNode(ISD::SIGN_EXTEND, dl, WideVT, X);
      Y = DAG.getNode(ISD::SIGN_EXTEND, dl, WideVT, Y);
      Y = DAG.getNode(ISD::MUL, dl, WideVT, X, Y);
      Y = DAG.getNode(ISD::SRL, dl, WideVT, Y,
                      DAG.getShiftAmountConstant(EltBits, WideVT, dl));
      return DAG.getNode(ISD::TRUNCATE, dl, VT, Y);
    }
    return SDValue();
  };

  SDValue Q = GetMULHS(N0, MagicFactor);
  if (!Q)
    return SDValue();

  Created.push_back(Q.getNode());

  // (Optionally) Add/subtract the numerator using Factor.
  Factor = DAG.getNode(ISD::MUL, dl, VT, N0, Factor);
  Created.push_back(Factor.getNode());
  Q = DAG.getNode(ISD::ADD, dl, VT, Q, Factor);
  Created.push_back(Q.getNode());

  // Shift right algebraic by shift value.
  Q = DAG.getNode(ISD::SRA, dl, VT, Q, Shift);
  Created.push_back(Q.getNode());

  // Extract the sign bit, mask it and add it to the quotient.
  SDValue SignShift = DAG.getConstant(EltBits - 1, dl, ShVT);
  SDValue T = DAG.getNode(ISD::SRL, dl, VT, Q, SignShift);
  Created.push_back(T.getNode());
  T = DAG.getNode(ISD::AND, dl, VT, T, ShiftMask);
  Created.push_back(T.getNode());
  return DAG.getNode(ISD::ADD, dl, VT, Q, T);
}

/// Given an ISD::UDIV node expressing a divide by constant,
/// return a DAG expression to select that will generate the same value by
/// multiplying by a magic number.
/// Ref: "Hacker's Delight" or "The PowerPC Compiler Writer's Guide".
SDValue TargetLowering::BuildUDIV(SDNode *N, SelectionDAG &DAG,
                                  bool IsAfterLegalization,
                                  SmallVectorImpl<SDNode *> &Created) const {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  EVT SVT = VT.getScalarType();
  EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
  EVT ShSVT = ShVT.getScalarType();
  unsigned EltBits = VT.getScalarSizeInBits();
  EVT MulVT;

  // Check to see if we can do this.
  // FIXME: We should be more aggressive here.
  if (!isTypeLegal(VT)) {
    // Limit this to simple scalars for now.
    if (VT.isVector() || !VT.isSimple())
      return SDValue();

    // If this type will be promoted to a large enough type with a legal
    // multiply operation, we can go ahead and do this transform.
    if (getTypeAction(VT.getSimpleVT()) != TypePromoteInteger)
      return SDValue();

    MulVT = getTypeToTransformTo(*DAG.getContext(), VT);
    if (MulVT.getSizeInBits() < (2 * EltBits) ||
        !isOperationLegal(ISD::MUL, MulVT))
      return SDValue();
  }

  // If the udiv has an 'exact' bit we can use a simpler lowering.
  if (N->getFlags().hasExact())
    return BuildExactUDIV(*this, N, dl, DAG, Created);

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // Try to use leading zeros of the dividend to reduce the multiplier and
  // avoid expensive fixups.
  unsigned KnownLeadingZeros = DAG.computeKnownBits(N0).countMinLeadingZeros();

  bool UseNPQ = false, UsePreShift = false, UsePostShift = false;
  SmallVector<SDValue, 16> PreShifts, PostShifts, MagicFactors, NPQFactors;

  auto BuildUDIVPattern = [&](ConstantSDNode *C) {
    if (C->isZero())
      return false;
    const APInt& Divisor = C->getAPIntValue();

    SDValue PreShift, MagicFactor, NPQFactor, PostShift;

    // Magic algorithm doesn't work for division by 1. We need to emit a select
    // at the end.
    if (Divisor.isOne()) {
      PreShift = PostShift = DAG.getUNDEF(ShSVT);
      MagicFactor = NPQFactor = DAG.getUNDEF(SVT);
    } else {
      UnsignedDivisionByConstantInfo magics =
          UnsignedDivisionByConstantInfo::get(
              Divisor, std::min(KnownLeadingZeros, Divisor.countl_zero()));

      MagicFactor = DAG.getConstant(magics.Magic, dl, SVT);

      assert(magics.PreShift < Divisor.getBitWidth() &&
             "We shouldn't generate an undefined shift!");
      assert(magics.PostShift < Divisor.getBitWidth() &&
             "We shouldn't generate an undefined shift!");
      assert((!magics.IsAdd || magics.PreShift == 0) &&
             "Unexpected pre-shift");
      PreShift = DAG.getConstant(magics.PreShift, dl, ShSVT);
      PostShift = DAG.getConstant(magics.PostShift, dl, ShSVT);
      NPQFactor = DAG.getConstant(
          magics.IsAdd ? APInt::getOneBitSet(EltBits, EltBits - 1)
                       : APInt::getZero(EltBits),
          dl, SVT);
      UseNPQ |= magics.IsAdd;
      UsePreShift |= magics.PreShift != 0;
      UsePostShift |= magics.PostShift != 0;
    }

    PreShifts.push_back(PreShift);
    MagicFactors.push_back(MagicFactor);
    NPQFactors.push_back(NPQFactor);
    PostShifts.push_back(PostShift);
    return true;
  };

  // Collect the shifts/magic values from each element.
  if (!ISD::matchUnaryPredicate(N1, BuildUDIVPattern))
    return SDValue();

  SDValue PreShift, PostShift, MagicFactor, NPQFactor;
  if (N1.getOpcode() == ISD::BUILD_VECTOR) {
    PreShift = DAG.getBuildVector(ShVT, dl, PreShifts);
    MagicFactor = DAG.getBuildVector(VT, dl, MagicFactors);
    NPQFactor = DAG.getBuildVector(VT, dl, NPQFactors);
    PostShift = DAG.getBuildVector(ShVT, dl, PostShifts);
  } else if (N1.getOpcode() == ISD::SPLAT_VECTOR) {
    assert(PreShifts.size() == 1 && MagicFactors.size() == 1 &&
           NPQFactors.size() == 1 && PostShifts.size() == 1 &&
           "Expected matchUnaryPredicate to return one for scalable vectors");
    PreShift = DAG.getSplatVector(ShVT, dl, PreShifts[0]);
    MagicFactor = DAG.getSplatVector(VT, dl, MagicFactors[0]);
    NPQFactor = DAG.getSplatVector(VT, dl, NPQFactors[0]);
    PostShift = DAG.getSplatVector(ShVT, dl, PostShifts[0]);
  } else {
    assert(isa<ConstantSDNode>(N1) && "Expected a constant");
    PreShift = PreShifts[0];
    MagicFactor = MagicFactors[0];
    PostShift = PostShifts[0];
  }

  SDValue Q = N0;
  if (UsePreShift) {
    Q = DAG.getNode(ISD::SRL, dl, VT, Q, PreShift);
    Created.push_back(Q.getNode());
  }

  // FIXME: We should support doing a MUL in a wider type.
  auto GetMULHU = [&](SDValue X, SDValue Y) {
    // If the type isn't legal, use a wider mul of the type calculated
    // earlier.
    if (!isTypeLegal(VT)) {
      X = DAG.getNode(ISD::ZERO_EXTEND, dl, MulVT, X);
      Y = DAG.getNode(ISD::ZERO_EXTEND, dl, MulVT, Y);
      Y = DAG.getNode(ISD::MUL, dl, MulVT, X, Y);
      Y = DAG.getNode(ISD::SRL, dl, MulVT, Y,
                      DAG.getShiftAmountConstant(EltBits, MulVT, dl));
      return DAG.getNode(ISD::TRUNCATE, dl, VT, Y);
    }

    if (isOperationLegalOrCustom(ISD::MULHU, VT, IsAfterLegalization))
      return DAG.getNode(ISD::MULHU, dl, VT, X, Y);
    if (isOperationLegalOrCustom(ISD::UMUL_LOHI, VT, IsAfterLegalization)) {
      SDValue LoHi =
          DAG.getNode(ISD::UMUL_LOHI, dl, DAG.getVTList(VT, VT), X, Y);
      return SDValue(LoHi.getNode(), 1);
    }
    // If type twice as wide legal, widen and use a mul plus a shift.
    unsigned Size = VT.getScalarSizeInBits();
    EVT WideVT = EVT::getIntegerVT(*DAG.getContext(), Size * 2);
    if (VT.isVector())
      WideVT = EVT::getVectorVT(*DAG.getContext(), WideVT,
                                VT.getVectorElementCount());
    if (isOperationLegalOrCustom(ISD::MUL, WideVT)) {
      X = DAG.getNode(ISD::ZERO_EXTEND, dl, WideVT, X);
      Y = DAG.getNode(ISD::ZERO_EXTEND, dl, WideVT, Y);
      Y = DAG.getNode(ISD::MUL, dl, WideVT, X, Y);
      Y = DAG.getNode(ISD::SRL, dl, WideVT, Y,
                      DAG.getShiftAmountConstant(EltBits, WideVT, dl));
      return DAG.getNode(ISD::TRUNCATE, dl, VT, Y);
    }
    return SDValue(); // No mulhu or equivalent
  };

  // Multiply the numerator (operand 0) by the magic value.
  Q = GetMULHU(Q, MagicFactor);
  if (!Q)
    return SDValue();

  Created.push_back(Q.getNode());

  if (UseNPQ) {
    SDValue NPQ = DAG.getNode(ISD::SUB, dl, VT, N0, Q);
    Created.push_back(NPQ.getNode());

    // For vectors we might have a mix of non-NPQ/NPQ paths, so use
    // MULHU to act as a SRL-by-1 for NPQ, else multiply by zero.
    if (VT.isVector())
      NPQ = GetMULHU(NPQ, NPQFactor);
    else
      NPQ = DAG.getNode(ISD::SRL, dl, VT, NPQ, DAG.getConstant(1, dl, ShVT));

    Created.push_back(NPQ.getNode());

    Q = DAG.getNode(ISD::ADD, dl, VT, NPQ, Q);
    Created.push_back(Q.getNode());
  }

  if (UsePostShift) {
    Q = DAG.getNode(ISD::SRL, dl, VT, Q, PostShift);
    Created.push_back(Q.getNode());
  }

  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  SDValue One = DAG.getConstant(1, dl, VT);
  SDValue IsOne = DAG.getSetCC(dl, SetCCVT, N1, One, ISD::SETEQ);
  return DAG.getSelect(dl, VT, IsOne, N0, Q);
}

/// If all values in Values that *don't* match the predicate are same 'splat'
/// value, then replace all values with that splat value.
/// Else, if AlternativeReplacement was provided, then replace all values that
/// do match predicate with AlternativeReplacement value.
static void
turnVectorIntoSplatVector(MutableArrayRef<SDValue> Values,
                          std::function<bool(SDValue)> Predicate,
                          SDValue AlternativeReplacement = SDValue()) {
  SDValue Replacement;
  // Is there a value for which the Predicate does *NOT* match? What is it?
  auto SplatValue = llvm::find_if_not(Values, Predicate);
  if (SplatValue != Values.end()) {
    // Does Values consist only of SplatValue's and values matching Predicate?
    if (llvm::all_of(Values, [Predicate, SplatValue](SDValue Value) {
          return Value == *SplatValue || Predicate(Value);
        })) // Then we shall replace values matching predicate with SplatValue.
      Replacement = *SplatValue;
  }
  if (!Replacement) {
    // Oops, we did not find the "baseline" splat value.
    if (!AlternativeReplacement)
      return; // Nothing to do.
    // Let's replace with provided value then.
    Replacement = AlternativeReplacement;
  }
  std::replace_if(Values.begin(), Values.end(), Predicate, Replacement);
}

/// Given an ISD::UREM used only by an ISD::SETEQ or ISD::SETNE
/// where the divisor is constant and the comparison target is zero,
/// return a DAG expression that will generate the same comparison result
/// using only multiplications, additions and shifts/rotations.
/// Ref: "Hacker's Delight" 10-17.
SDValue TargetLowering::buildUREMEqFold(EVT SETCCVT, SDValue REMNode,
                                        SDValue CompTargetNode,
                                        ISD::CondCode Cond,
                                        DAGCombinerInfo &DCI,
                                        const SDLoc &DL) const {
  SmallVector<SDNode *, 5> Built;
  if (SDValue Folded = prepareUREMEqFold(SETCCVT, REMNode, CompTargetNode, Cond,
                                         DCI, DL, Built)) {
    for (SDNode *N : Built)
      DCI.AddToWorklist(N);
    return Folded;
  }

  return SDValue();
}

SDValue
TargetLowering::prepareUREMEqFold(EVT SETCCVT, SDValue REMNode,
                                  SDValue CompTargetNode, ISD::CondCode Cond,
                                  DAGCombinerInfo &DCI, const SDLoc &DL,
                                  SmallVectorImpl<SDNode *> &Created) const {
  // fold (seteq/ne (urem N, D), 0) -> (setule/ugt (rotr (mul N, P), K), Q)
  // - D must be constant, with D = D0 * 2^K where D0 is odd
  // - P is the multiplicative inverse of D0 modulo 2^W
  // - Q = floor(((2^W) - 1) / D)
  // where W is the width of the common type of N and D.
  assert((Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
         "Only applicable for (in)equality comparisons.");

  SelectionDAG &DAG = DCI.DAG;

  EVT VT = REMNode.getValueType();
  EVT SVT = VT.getScalarType();
  EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
  EVT ShSVT = ShVT.getScalarType();

  // If MUL is unavailable, we cannot proceed in any case.
  if (!DCI.isBeforeLegalizeOps() && !isOperationLegalOrCustom(ISD::MUL, VT))
    return SDValue();

  bool ComparingWithAllZeros = true;
  bool AllComparisonsWithNonZerosAreTautological = true;
  bool HadTautologicalLanes = false;
  bool AllLanesAreTautological = true;
  bool HadEvenDivisor = false;
  bool AllDivisorsArePowerOfTwo = true;
  bool HadTautologicalInvertedLanes = false;
  SmallVector<SDValue, 16> PAmts, KAmts, QAmts, IAmts;

  auto BuildUREMPattern = [&](ConstantSDNode *CDiv, ConstantSDNode *CCmp) {
    // Division by 0 is UB. Leave it to be constant-folded elsewhere.
    if (CDiv->isZero())
      return false;

    const APInt &D = CDiv->getAPIntValue();
    const APInt &Cmp = CCmp->getAPIntValue();

    ComparingWithAllZeros &= Cmp.isZero();

    // x u% C1` is *always* less than C1. So given `x u% C1 == C2`,
    // if C2 is not less than C1, the comparison is always false.
    // But we will only be able to produce the comparison that will give the
    // opposive tautological answer. So this lane would need to be fixed up.
    bool TautologicalInvertedLane = D.ule(Cmp);
    HadTautologicalInvertedLanes |= TautologicalInvertedLane;

    // If all lanes are tautological (either all divisors are ones, or divisor
    // is not greater than the constant we are comparing with),
    // we will prefer to avoid the fold.
    bool TautologicalLane = D.isOne() || TautologicalInvertedLane;
    HadTautologicalLanes |= TautologicalLane;
    AllLanesAreTautological &= TautologicalLane;

    // If we are comparing with non-zero, we need'll need  to subtract said
    // comparison value from the LHS. But there is no point in doing that if
    // every lane where we are comparing with non-zero is tautological..
    if (!Cmp.isZero())
      AllComparisonsWithNonZerosAreTautological &= TautologicalLane;

    // Decompose D into D0 * 2^K
    unsigned K = D.countr_zero();
    assert((!D.isOne() || (K == 0)) && "For divisor '1' we won't rotate.");
    APInt D0 = D.lshr(K);

    // D is even if it has trailing zeros.
    HadEvenDivisor |= (K != 0);
    // D is a power-of-two if D0 is one.
    // If all divisors are power-of-two, we will prefer to avoid the fold.
    AllDivisorsArePowerOfTwo &= D0.isOne();

    // P = inv(D0, 2^W)
    // 2^W requires W + 1 bits, so we have to extend and then truncate.
    unsigned W = D.getBitWidth();
    APInt P = D0.multiplicativeInverse();
    assert((D0 * P).isOne() && "Multiplicative inverse basic check failed.");

    // Q = floor((2^W - 1) u/ D)
    // R = ((2^W - 1) u% D)
    APInt Q, R;
    APInt::udivrem(APInt::getAllOnes(W), D, Q, R);

    // If we are comparing with zero, then that comparison constant is okay,
    // else it may need to be one less than that.
    if (Cmp.ugt(R))
      Q -= 1;

    assert(APInt::getAllOnes(ShSVT.getSizeInBits()).ugt(K) &&
           "We are expecting that K is always less than all-ones for ShSVT");

    // If the lane is tautological the result can be constant-folded.
    if (TautologicalLane) {
      // Set P and K amount to a bogus values so we can try to splat them.
      P = 0;
      K = -1;
      // And ensure that comparison constant is tautological,
      // it will always compare true/false.
      Q = -1;
    }

    PAmts.push_back(DAG.getConstant(P, DL, SVT));
    KAmts.push_back(
        DAG.getConstant(APInt(ShSVT.getSizeInBits(), K), DL, ShSVT));
    QAmts.push_back(DAG.getConstant(Q, DL, SVT));
    return true;
  };

  SDValue N = REMNode.getOperand(0);
  SDValue D = REMNode.getOperand(1);

  // Collect the values from each element.
  if (!ISD::matchBinaryPredicate(D, CompTargetNode, BuildUREMPattern))
    return SDValue();

  // If all lanes are tautological, the result can be constant-folded.
  if (AllLanesAreTautological)
    return SDValue();

  // If this is a urem by a powers-of-two, avoid the fold since it can be
  // best implemented as a bit test.
  if (AllDivisorsArePowerOfTwo)
    return SDValue();

  SDValue PVal, KVal, QVal;
  if (D.getOpcode() == ISD::BUILD_VECTOR) {
    if (HadTautologicalLanes) {
      // Try to turn PAmts into a splat, since we don't care about the values
      // that are currently '0'. If we can't, just keep '0'`s.
      turnVectorIntoSplatVector(PAmts, isNullConstant);
      // Try to turn KAmts into a splat, since we don't care about the values
      // that are currently '-1'. If we can't, change them to '0'`s.
      turnVectorIntoSplatVector(KAmts, isAllOnesConstant,
                                DAG.getConstant(0, DL, ShSVT));
    }

    PVal = DAG.getBuildVector(VT, DL, PAmts);
    KVal = DAG.getBuildVector(ShVT, DL, KAmts);
    QVal = DAG.getBuildVector(VT, DL, QAmts);
  } else if (D.getOpcode() == ISD::SPLAT_VECTOR) {
    assert(PAmts.size() == 1 && KAmts.size() == 1 && QAmts.size() == 1 &&
           "Expected matchBinaryPredicate to return one element for "
           "SPLAT_VECTORs");
    PVal = DAG.getSplatVector(VT, DL, PAmts[0]);
    KVal = DAG.getSplatVector(ShVT, DL, KAmts[0]);
    QVal = DAG.getSplatVector(VT, DL, QAmts[0]);
  } else {
    PVal = PAmts[0];
    KVal = KAmts[0];
    QVal = QAmts[0];
  }

  if (!ComparingWithAllZeros && !AllComparisonsWithNonZerosAreTautological) {
    if (!DCI.isBeforeLegalizeOps() && !isOperationLegalOrCustom(ISD::SUB, VT))
      return SDValue(); // FIXME: Could/should use `ISD::ADD`?
    assert(CompTargetNode.getValueType() == N.getValueType() &&
           "Expecting that the types on LHS and RHS of comparisons match.");
    N = DAG.getNode(ISD::SUB, DL, VT, N, CompTargetNode);
  }

  // (mul N, P)
  SDValue Op0 = DAG.getNode(ISD::MUL, DL, VT, N, PVal);
  Created.push_back(Op0.getNode());

  // Rotate right only if any divisor was even. We avoid rotates for all-odd
  // divisors as a performance improvement, since rotating by 0 is a no-op.
  if (HadEvenDivisor) {
    // We need ROTR to do this.
    if (!DCI.isBeforeLegalizeOps() && !isOperationLegalOrCustom(ISD::ROTR, VT))
      return SDValue();
    // UREM: (rotr (mul N, P), K)
    Op0 = DAG.getNode(ISD::ROTR, DL, VT, Op0, KVal);
    Created.push_back(Op0.getNode());
  }

  // UREM: (setule/setugt (rotr (mul N, P), K), Q)
  SDValue NewCC =
      DAG.getSetCC(DL, SETCCVT, Op0, QVal,
                   ((Cond == ISD::SETEQ) ? ISD::SETULE : ISD::SETUGT));
  if (!HadTautologicalInvertedLanes)
    return NewCC;

  // If any lanes previously compared always-false, the NewCC will give
  // always-true result for them, so we need to fixup those lanes.
  // Or the other way around for inequality predicate.
  assert(VT.isVector() && "Can/should only get here for vectors.");
  Created.push_back(NewCC.getNode());

  // x u% C1` is *always* less than C1. So given `x u% C1 == C2`,
  // if C2 is not less than C1, the comparison is always false.
  // But we have produced the comparison that will give the
  // opposive tautological answer. So these lanes would need to be fixed up.
  SDValue TautologicalInvertedChannels =
      DAG.getSetCC(DL, SETCCVT, D, CompTargetNode, ISD::SETULE);
  Created.push_back(TautologicalInvertedChannels.getNode());

  // NOTE: we avoid letting illegal types through even if we're before legalize
  // ops – legalization has a hard time producing good code for this.
  if (isOperationLegalOrCustom(ISD::VSELECT, SETCCVT)) {
    // If we have a vector select, let's replace the comparison results in the
    // affected lanes with the correct tautological result.
    SDValue Replacement = DAG.getBoolConstant(Cond == ISD::SETEQ ? false : true,
                                              DL, SETCCVT, SETCCVT);
    return DAG.getNode(ISD::VSELECT, DL, SETCCVT, TautologicalInvertedChannels,
                       Replacement, NewCC);
  }

  // Else, we can just invert the comparison result in the appropriate lanes.
  //
  // NOTE: see the note above VSELECT above.
  if (isOperationLegalOrCustom(ISD::XOR, SETCCVT))
    return DAG.getNode(ISD::XOR, DL, SETCCVT, NewCC,
                       TautologicalInvertedChannels);

  return SDValue(); // Don't know how to lower.
}

/// Given an ISD::SREM used only by an ISD::SETEQ or ISD::SETNE
/// where the divisor is constant and the comparison target is zero,
/// return a DAG expression that will generate the same comparison result
/// using only multiplications, additions and shifts/rotations.
/// Ref: "Hacker's Delight" 10-17.
SDValue TargetLowering::buildSREMEqFold(EVT SETCCVT, SDValue REMNode,
                                        SDValue CompTargetNode,
                                        ISD::CondCode Cond,
                                        DAGCombinerInfo &DCI,
                                        const SDLoc &DL) const {
  SmallVector<SDNode *, 7> Built;
  if (SDValue Folded = prepareSREMEqFold(SETCCVT, REMNode, CompTargetNode, Cond,
                                         DCI, DL, Built)) {
    assert(Built.size() <= 7 && "Max size prediction failed.");
    for (SDNode *N : Built)
      DCI.AddToWorklist(N);
    return Folded;
  }

  return SDValue();
}

SDValue
TargetLowering::prepareSREMEqFold(EVT SETCCVT, SDValue REMNode,
                                  SDValue CompTargetNode, ISD::CondCode Cond,
                                  DAGCombinerInfo &DCI, const SDLoc &DL,
                                  SmallVectorImpl<SDNode *> &Created) const {
  // Derived from Hacker's Delight, 2nd Edition, by Hank Warren. Section 10-17.
  // Fold:
  //   (seteq/ne (srem N, D), 0)
  // To:
  //   (setule/ugt (rotr (add (mul N, P), A), K), Q)
  //
  // - D must be constant, with D = D0 * 2^K where D0 is odd
  // - P is the multiplicative inverse of D0 modulo 2^W
  // - A = bitwiseand(floor((2^(W - 1) - 1) / D0), (-(2^k)))
  // - Q = floor((2 * A) / (2^K))
  // where W is the width of the common type of N and D.
  //
  // When D is a power of two (and thus D0 is 1), the normal
  // formula for A and Q don't apply, because the derivation
  // depends on D not dividing 2^(W-1), and thus theorem ZRS
  // does not apply. This specifically fails when N = INT_MIN.
  //
  // Instead, for power-of-two D, we use:
  // - A = 2^(W-1)
  // |-> Order-preserving map from [-2^(W-1), 2^(W-1) - 1] to [0,2^W - 1])
  // - Q = 2^(W-K) - 1
  // |-> Test that the top K bits are zero after rotation
  assert((Cond == ISD::SETEQ || Cond == ISD::SETNE) &&
         "Only applicable for (in)equality comparisons.");

  SelectionDAG &DAG = DCI.DAG;

  EVT VT = REMNode.getValueType();
  EVT SVT = VT.getScalarType();
  EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
  EVT ShSVT = ShVT.getScalarType();

  // If we are after ops legalization, and MUL is unavailable, we can not
  // proceed.
  if (!DCI.isBeforeLegalizeOps() && !isOperationLegalOrCustom(ISD::MUL, VT))
    return SDValue();

  // TODO: Could support comparing with non-zero too.
  ConstantSDNode *CompTarget = isConstOrConstSplat(CompTargetNode);
  if (!CompTarget || !CompTarget->isZero())
    return SDValue();

  bool HadIntMinDivisor = false;
  bool HadOneDivisor = false;
  bool AllDivisorsAreOnes = true;
  bool HadEvenDivisor = false;
  bool NeedToApplyOffset = false;
  bool AllDivisorsArePowerOfTwo = true;
  SmallVector<SDValue, 16> PAmts, AAmts, KAmts, QAmts;

  auto BuildSREMPattern = [&](ConstantSDNode *C) {
    // Division by 0 is UB. Leave it to be constant-folded elsewhere.
    if (C->isZero())
      return false;

    // FIXME: we don't fold `rem %X, -C` to `rem %X, C` in DAGCombine.

    // WARNING: this fold is only valid for positive divisors!
    APInt D = C->getAPIntValue();
    if (D.isNegative())
      D.negate(); //  `rem %X, -C` is equivalent to `rem %X, C`

    HadIntMinDivisor |= D.isMinSignedValue();

    // If all divisors are ones, we will prefer to avoid the fold.
    HadOneDivisor |= D.isOne();
    AllDivisorsAreOnes &= D.isOne();

    // Decompose D into D0 * 2^K
    unsigned K = D.countr_zero();
    assert((!D.isOne() || (K == 0)) && "For divisor '1' we won't rotate.");
    APInt D0 = D.lshr(K);

    if (!D.isMinSignedValue()) {
      // D is even if it has trailing zeros; unless it's INT_MIN, in which case
      // we don't care about this lane in this fold, we'll special-handle it.
      HadEvenDivisor |= (K != 0);
    }

    // D is a power-of-two if D0 is one. This includes INT_MIN.
    // If all divisors are power-of-two, we will prefer to avoid the fold.
    AllDivisorsArePowerOfTwo &= D0.isOne();

    // P = inv(D0, 2^W)
    // 2^W requires W + 1 bits, so we have to extend and then truncate.
    unsigned W = D.getBitWidth();
    APInt P = D0.multiplicativeInverse();
    assert((D0 * P).isOne() && "Multiplicative inverse basic check failed.");

    // A = floor((2^(W - 1) - 1) / D0) & -2^K
    APInt A = APInt::getSignedMaxValue(W).udiv(D0);
    A.clearLowBits(K);

    if (!D.isMinSignedValue()) {
      // If divisor INT_MIN, then we don't care about this lane in this fold,
      // we'll special-handle it.
      NeedToApplyOffset |= A != 0;
    }

    // Q = floor((2 * A) / (2^K))
    APInt Q = (2 * A).udiv(APInt::getOneBitSet(W, K));

    assert(APInt::getAllOnes(SVT.getSizeInBits()).ugt(A) &&
           "We are expecting that A is always less than all-ones for SVT");
    assert(APInt::getAllOnes(ShSVT.getSizeInBits()).ugt(K) &&
           "We are expecting that K is always less than all-ones for ShSVT");

    // If D was a power of two, apply the alternate constant derivation.
    if (D0.isOne()) {
      // A = 2^(W-1)
      A = APInt::getSignedMinValue(W);
      // - Q = 2^(W-K) - 1
      Q = APInt::getAllOnes(W - K).zext(W);
    }

    // If the divisor is 1 the result can be constant-folded. Likewise, we
    // don't care about INT_MIN lanes, those can be set to undef if appropriate.
    if (D.isOne()) {
      // Set P, A and K to a bogus values so we can try to splat them.
      P = 0;
      A = -1;
      K = -1;

      // x ?% 1 == 0  <-->  true  <-->  x u<= -1
      Q = -1;
    }

    PAmts.push_back(DAG.getConstant(P, DL, SVT));
    AAmts.push_back(DAG.getConstant(A, DL, SVT));
    KAmts.push_back(
        DAG.getConstant(APInt(ShSVT.getSizeInBits(), K), DL, ShSVT));
    QAmts.push_back(DAG.getConstant(Q, DL, SVT));
    return true;
  };

  SDValue N = REMNode.getOperand(0);
  SDValue D = REMNode.getOperand(1);

  // Collect the values from each element.
  if (!ISD::matchUnaryPredicate(D, BuildSREMPattern))
    return SDValue();

  // If this is a srem by a one, avoid the fold since it can be constant-folded.
  if (AllDivisorsAreOnes)
    return SDValue();

  // If this is a srem by a powers-of-two (including INT_MIN), avoid the fold
  // since it can be best implemented as a bit test.
  if (AllDivisorsArePowerOfTwo)
    return SDValue();

  SDValue PVal, AVal, KVal, QVal;
  if (D.getOpcode() == ISD::BUILD_VECTOR) {
    if (HadOneDivisor) {
      // Try to turn PAmts into a splat, since we don't care about the values
      // that are currently '0'. If we can't, just keep '0'`s.
      turnVectorIntoSplatVector(PAmts, isNullConstant);
      // Try to turn AAmts into a splat, since we don't care about the
      // values that are currently '-1'. If we can't, change them to '0'`s.
      turnVectorIntoSplatVector(AAmts, isAllOnesConstant,
                                DAG.getConstant(0, DL, SVT));
      // Try to turn KAmts into a splat, since we don't care about the values
      // that are currently '-1'. If we can't, change them to '0'`s.
      turnVectorIntoSplatVector(KAmts, isAllOnesConstant,
                                DAG.getConstant(0, DL, ShSVT));
    }

    PVal = DAG.getBuildVector(VT, DL, PAmts);
    AVal = DAG.getBuildVector(VT, DL, AAmts);
    KVal = DAG.getBuildVector(ShVT, DL, KAmts);
    QVal = DAG.getBuildVector(VT, DL, QAmts);
  } else if (D.getOpcode() == ISD::SPLAT_VECTOR) {
    assert(PAmts.size() == 1 && AAmts.size() == 1 && KAmts.size() == 1 &&
           QAmts.size() == 1 &&
           "Expected matchUnaryPredicate to return one element for scalable "
           "vectors");
    PVal = DAG.getSplatVector(VT, DL, PAmts[0]);
    AVal = DAG.getSplatVector(VT, DL, AAmts[0]);
    KVal = DAG.getSplatVector(ShVT, DL, KAmts[0]);
    QVal = DAG.getSplatVector(VT, DL, QAmts[0]);
  } else {
    assert(isa<ConstantSDNode>(D) && "Expected a constant");
    PVal = PAmts[0];
    AVal = AAmts[0];
    KVal = KAmts[0];
    QVal = QAmts[0];
  }

  // (mul N, P)
  SDValue Op0 = DAG.getNode(ISD::MUL, DL, VT, N, PVal);
  Created.push_back(Op0.getNode());

  if (NeedToApplyOffset) {
    // We need ADD to do this.
    if (!DCI.isBeforeLegalizeOps() && !isOperationLegalOrCustom(ISD::ADD, VT))
      return SDValue();

    // (add (mul N, P), A)
    Op0 = DAG.getNode(ISD::ADD, DL, VT, Op0, AVal);
    Created.push_back(Op0.getNode());
  }

  // Rotate right only if any divisor was even. We avoid rotates for all-odd
  // divisors as a performance improvement, since rotating by 0 is a no-op.
  if (HadEvenDivisor) {
    // We need ROTR to do this.
    if (!DCI.isBeforeLegalizeOps() && !isOperationLegalOrCustom(ISD::ROTR, VT))
      return SDValue();
    // SREM: (rotr (add (mul N, P), A), K)
    Op0 = DAG.getNode(ISD::ROTR, DL, VT, Op0, KVal);
    Created.push_back(Op0.getNode());
  }

  // SREM: (setule/setugt (rotr (add (mul N, P), A), K), Q)
  SDValue Fold =
      DAG.getSetCC(DL, SETCCVT, Op0, QVal,
                   ((Cond == ISD::SETEQ) ? ISD::SETULE : ISD::SETUGT));

  // If we didn't have lanes with INT_MIN divisor, then we're done.
  if (!HadIntMinDivisor)
    return Fold;

  // That fold is only valid for positive divisors. Which effectively means,
  // it is invalid for INT_MIN divisors. So if we have such a lane,
  // we must fix-up results for said lanes.
  assert(VT.isVector() && "Can/should only get here for vectors.");

  // NOTE: we avoid letting illegal types through even if we're before legalize
  // ops – legalization has a hard time producing good code for the code that
  // follows.
  if (!isOperationLegalOrCustom(ISD::SETCC, SETCCVT) ||
      !isOperationLegalOrCustom(ISD::AND, VT) ||
      !isCondCodeLegalOrCustom(Cond, VT.getSimpleVT()) ||
      !isOperationLegalOrCustom(ISD::VSELECT, SETCCVT))
    return SDValue();

  Created.push_back(Fold.getNode());

  SDValue IntMin = DAG.getConstant(
      APInt::getSignedMinValue(SVT.getScalarSizeInBits()), DL, VT);
  SDValue IntMax = DAG.getConstant(
      APInt::getSignedMaxValue(SVT.getScalarSizeInBits()), DL, VT);
  SDValue Zero =
      DAG.getConstant(APInt::getZero(SVT.getScalarSizeInBits()), DL, VT);

  // Which lanes had INT_MIN divisors? Divisor is constant, so const-folded.
  SDValue DivisorIsIntMin = DAG.getSetCC(DL, SETCCVT, D, IntMin, ISD::SETEQ);
  Created.push_back(DivisorIsIntMin.getNode());

  // (N s% INT_MIN) ==/!= 0  <-->  (N & INT_MAX) ==/!= 0
  SDValue Masked = DAG.getNode(ISD::AND, DL, VT, N, IntMax);
  Created.push_back(Masked.getNode());
  SDValue MaskedIsZero = DAG.getSetCC(DL, SETCCVT, Masked, Zero, Cond);
  Created.push_back(MaskedIsZero.getNode());

  // To produce final result we need to blend 2 vectors: 'SetCC' and
  // 'MaskedIsZero'. If the divisor for channel was *NOT* INT_MIN, we pick
  // from 'Fold', else pick from 'MaskedIsZero'. Since 'DivisorIsIntMin' is
  // constant-folded, select can get lowered to a shuffle with constant mask.
  SDValue Blended = DAG.getNode(ISD::VSELECT, DL, SETCCVT, DivisorIsIntMin,
                                MaskedIsZero, Fold);

  return Blended;
}

bool TargetLowering::
verifyReturnAddressArgumentIsConstant(SDValue Op, SelectionDAG &DAG) const {
  if (!isa<ConstantSDNode>(Op.getOperand(0))) {
    DAG.getContext()->emitError("argument to '__builtin_return_address' must "
                                "be a constant integer");
    return true;
  }

  return false;
}

SDValue TargetLowering::getSqrtInputTest(SDValue Op, SelectionDAG &DAG,
                                         const DenormalMode &Mode) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue FPZero = DAG.getConstantFP(0.0, DL, VT);

  // This is specifically a check for the handling of denormal inputs, not the
  // result.
  if (Mode.Input == DenormalMode::PreserveSign ||
      Mode.Input == DenormalMode::PositiveZero) {
    // Test = X == 0.0
    return DAG.getSetCC(DL, CCVT, Op, FPZero, ISD::SETEQ);
  }

  // Testing it with denormal inputs to avoid wrong estimate.
  //
  // Test = fabs(X) < SmallestNormal
  const fltSemantics &FltSem = DAG.EVTToAPFloatSemantics(VT);
  APFloat SmallestNorm = APFloat::getSmallestNormalized(FltSem);
  SDValue NormC = DAG.getConstantFP(SmallestNorm, DL, VT);
  SDValue Fabs = DAG.getNode(ISD::FABS, DL, VT, Op);
  return DAG.getSetCC(DL, CCVT, Fabs, NormC, ISD::SETLT);
}

SDValue TargetLowering::getNegatedExpression(SDValue Op, SelectionDAG &DAG,
                                             bool LegalOps, bool OptForSize,
                                             NegatibleCost &Cost,
                                             unsigned Depth) const {
  // fneg is removable even if it has multiple uses.
  if (Op.getOpcode() == ISD::FNEG || Op.getOpcode() == ISD::VP_FNEG) {
    Cost = NegatibleCost::Cheaper;
    return Op.getOperand(0);
  }

  // Don't recurse exponentially.
  if (Depth > SelectionDAG::MaxRecursionDepth)
    return SDValue();

  // Pre-increment recursion depth for use in recursive calls.
  ++Depth;
  const SDNodeFlags Flags = Op->getFlags();
  const TargetOptions &Options = DAG.getTarget().Options;
  EVT VT = Op.getValueType();
  unsigned Opcode = Op.getOpcode();

  // Don't allow anything with multiple uses unless we know it is free.
  if (!Op.hasOneUse() && Opcode != ISD::ConstantFP) {
    bool IsFreeExtend = Opcode == ISD::FP_EXTEND &&
                        isFPExtFree(VT, Op.getOperand(0).getValueType());
    if (!IsFreeExtend)
      return SDValue();
  }

  auto RemoveDeadNode = [&](SDValue N) {
    if (N && N.getNode()->use_empty())
      DAG.RemoveDeadNode(N.getNode());
  };

  SDLoc DL(Op);

  // Because getNegatedExpression can delete nodes we need a handle to keep
  // temporary nodes alive in case the recursion manages to create an identical
  // node.
  std::list<HandleSDNode> Handles;

  switch (Opcode) {
  case ISD::ConstantFP: {
    // Don't invert constant FP values after legalization unless the target says
    // the negated constant is legal.
    bool IsOpLegal =
        isOperationLegal(ISD::ConstantFP, VT) ||
        isFPImmLegal(neg(cast<ConstantFPSDNode>(Op)->getValueAPF()), VT,
                     OptForSize);

    if (LegalOps && !IsOpLegal)
      break;

    APFloat V = cast<ConstantFPSDNode>(Op)->getValueAPF();
    V.changeSign();
    SDValue CFP = DAG.getConstantFP(V, DL, VT);

    // If we already have the use of the negated floating constant, it is free
    // to negate it even it has multiple uses.
    if (!Op.hasOneUse() && CFP.use_empty())
      break;
    Cost = NegatibleCost::Neutral;
    return CFP;
  }
  case ISD::BUILD_VECTOR: {
    // Only permit BUILD_VECTOR of constants.
    if (llvm::any_of(Op->op_values(), [&](SDValue N) {
          return !N.isUndef() && !isa<ConstantFPSDNode>(N);
        }))
      break;

    bool IsOpLegal =
        (isOperationLegal(ISD::ConstantFP, VT) &&
         isOperationLegal(ISD::BUILD_VECTOR, VT)) ||
        llvm::all_of(Op->op_values(), [&](SDValue N) {
          return N.isUndef() ||
                 isFPImmLegal(neg(cast<ConstantFPSDNode>(N)->getValueAPF()), VT,
                              OptForSize);
        });

    if (LegalOps && !IsOpLegal)
      break;

    SmallVector<SDValue, 4> Ops;
    for (SDValue C : Op->op_values()) {
      if (C.isUndef()) {
        Ops.push_back(C);
        continue;
      }
      APFloat V = cast<ConstantFPSDNode>(C)->getValueAPF();
      V.changeSign();
      Ops.push_back(DAG.getConstantFP(V, DL, C.getValueType()));
    }
    Cost = NegatibleCost::Neutral;
    return DAG.getBuildVector(VT, DL, Ops);
  }
  case ISD::FADD: {
    if (!Options.NoSignedZerosFPMath && !Flags.hasNoSignedZeros())
      break;

    // After operation legalization, it might not be legal to create new FSUBs.
    if (LegalOps && !isOperationLegalOrCustom(ISD::FSUB, VT))
      break;
    SDValue X = Op.getOperand(0), Y = Op.getOperand(1);

    // fold (fneg (fadd X, Y)) -> (fsub (fneg X), Y)
    NegatibleCost CostX = NegatibleCost::Expensive;
    SDValue NegX =
        getNegatedExpression(X, DAG, LegalOps, OptForSize, CostX, Depth);
    // Prevent this node from being deleted by the next call.
    if (NegX)
      Handles.emplace_back(NegX);

    // fold (fneg (fadd X, Y)) -> (fsub (fneg Y), X)
    NegatibleCost CostY = NegatibleCost::Expensive;
    SDValue NegY =
        getNegatedExpression(Y, DAG, LegalOps, OptForSize, CostY, Depth);

    // We're done with the handles.
    Handles.clear();

    // Negate the X if its cost is less or equal than Y.
    if (NegX && (CostX <= CostY)) {
      Cost = CostX;
      SDValue N = DAG.getNode(ISD::FSUB, DL, VT, NegX, Y, Flags);
      if (NegY != N)
        RemoveDeadNode(NegY);
      return N;
    }

    // Negate the Y if it is not expensive.
    if (NegY) {
      Cost = CostY;
      SDValue N = DAG.getNode(ISD::FSUB, DL, VT, NegY, X, Flags);
      if (NegX != N)
        RemoveDeadNode(NegX);
      return N;
    }
    break;
  }
  case ISD::FSUB: {
    // We can't turn -(A-B) into B-A when we honor signed zeros.
    if (!Options.NoSignedZerosFPMath && !Flags.hasNoSignedZeros())
      break;

    SDValue X = Op.getOperand(0), Y = Op.getOperand(1);
    // fold (fneg (fsub 0, Y)) -> Y
    if (ConstantFPSDNode *C = isConstOrConstSplatFP(X, /*AllowUndefs*/ true))
      if (C->isZero()) {
        Cost = NegatibleCost::Cheaper;
        return Y;
      }

    // fold (fneg (fsub X, Y)) -> (fsub Y, X)
    Cost = NegatibleCost::Neutral;
    return DAG.getNode(ISD::FSUB, DL, VT, Y, X, Flags);
  }
  case ISD::FMUL:
  case ISD::FDIV: {
    SDValue X = Op.getOperand(0), Y = Op.getOperand(1);

    // fold (fneg (fmul X, Y)) -> (fmul (fneg X), Y)
    NegatibleCost CostX = NegatibleCost::Expensive;
    SDValue NegX =
        getNegatedExpression(X, DAG, LegalOps, OptForSize, CostX, Depth);
    // Prevent this node from being deleted by the next call.
    if (NegX)
      Handles.emplace_back(NegX);

    // fold (fneg (fmul X, Y)) -> (fmul X, (fneg Y))
    NegatibleCost CostY = NegatibleCost::Expensive;
    SDValue NegY =
        getNegatedExpression(Y, DAG, LegalOps, OptForSize, CostY, Depth);

    // We're done with the handles.
    Handles.clear();

    // Negate the X if its cost is less or equal than Y.
    if (NegX && (CostX <= CostY)) {
      Cost = CostX;
      SDValue N = DAG.getNode(Opcode, DL, VT, NegX, Y, Flags);
      if (NegY != N)
        RemoveDeadNode(NegY);
      return N;
    }

    // Ignore X * 2.0 because that is expected to be canonicalized to X + X.
    if (auto *C = isConstOrConstSplatFP(Op.getOperand(1)))
      if (C->isExactlyValue(2.0) && Op.getOpcode() == ISD::FMUL)
        break;

    // Negate the Y if it is not expensive.
    if (NegY) {
      Cost = CostY;
      SDValue N = DAG.getNode(Opcode, DL, VT, X, NegY, Flags);
      if (NegX != N)
        RemoveDeadNode(NegX);
      return N;
    }
    break;
  }
  case ISD::FMA:
  case ISD::FMAD: {
    if (!Options.NoSignedZerosFPMath && !Flags.hasNoSignedZeros())
      break;

    SDValue X = Op.getOperand(0), Y = Op.getOperand(1), Z = Op.getOperand(2);
    NegatibleCost CostZ = NegatibleCost::Expensive;
    SDValue NegZ =
        getNegatedExpression(Z, DAG, LegalOps, OptForSize, CostZ, Depth);
    // Give up if fail to negate the Z.
    if (!NegZ)
      break;

    // Prevent this node from being deleted by the next two calls.
    Handles.emplace_back(NegZ);

    // fold (fneg (fma X, Y, Z)) -> (fma (fneg X), Y, (fneg Z))
    NegatibleCost CostX = NegatibleCost::Expensive;
    SDValue NegX =
        getNegatedExpression(X, DAG, LegalOps, OptForSize, CostX, Depth);
    // Prevent this node from being deleted by the next call.
    if (NegX)
      Handles.emplace_back(NegX);

    // fold (fneg (fma X, Y, Z)) -> (fma X, (fneg Y), (fneg Z))
    NegatibleCost CostY = NegatibleCost::Expensive;
    SDValue NegY =
        getNegatedExpression(Y, DAG, LegalOps, OptForSize, CostY, Depth);

    // We're done with the handles.
    Handles.clear();

    // Negate the X if its cost is less or equal than Y.
    if (NegX && (CostX <= CostY)) {
      Cost = std::min(CostX, CostZ);
      SDValue N = DAG.getNode(Opcode, DL, VT, NegX, Y, NegZ, Flags);
      if (NegY != N)
        RemoveDeadNode(NegY);
      return N;
    }

    // Negate the Y if it is not expensive.
    if (NegY) {
      Cost = std::min(CostY, CostZ);
      SDValue N = DAG.getNode(Opcode, DL, VT, X, NegY, NegZ, Flags);
      if (NegX != N)
        RemoveDeadNode(NegX);
      return N;
    }
    break;
  }

  case ISD::FP_EXTEND:
  case ISD::FSIN:
    if (SDValue NegV = getNegatedExpression(Op.getOperand(0), DAG, LegalOps,
                                            OptForSize, Cost, Depth))
      return DAG.getNode(Opcode, DL, VT, NegV);
    break;
  case ISD::FP_ROUND:
    if (SDValue NegV = getNegatedExpression(Op.getOperand(0), DAG, LegalOps,
                                            OptForSize, Cost, Depth))
      return DAG.getNode(ISD::FP_ROUND, DL, VT, NegV, Op.getOperand(1));
    break;
  case ISD::SELECT:
  case ISD::VSELECT: {
    // fold (fneg (select C, LHS, RHS)) -> (select C, (fneg LHS), (fneg RHS))
    // iff at least one cost is cheaper and the other is neutral/cheaper
    SDValue LHS = Op.getOperand(1);
    NegatibleCost CostLHS = NegatibleCost::Expensive;
    SDValue NegLHS =
        getNegatedExpression(LHS, DAG, LegalOps, OptForSize, CostLHS, Depth);
    if (!NegLHS || CostLHS > NegatibleCost::Neutral) {
      RemoveDeadNode(NegLHS);
      break;
    }

    // Prevent this node from being deleted by the next call.
    Handles.emplace_back(NegLHS);

    SDValue RHS = Op.getOperand(2);
    NegatibleCost CostRHS = NegatibleCost::Expensive;
    SDValue NegRHS =
        getNegatedExpression(RHS, DAG, LegalOps, OptForSize, CostRHS, Depth);

    // We're done with the handles.
    Handles.clear();

    if (!NegRHS || CostRHS > NegatibleCost::Neutral ||
        (CostLHS != NegatibleCost::Cheaper &&
         CostRHS != NegatibleCost::Cheaper)) {
      RemoveDeadNode(NegLHS);
      RemoveDeadNode(NegRHS);
      break;
    }

    Cost = std::min(CostLHS, CostRHS);
    return DAG.getSelect(DL, VT, Op.getOperand(0), NegLHS, NegRHS);
  }
  }

  return SDValue();
}

//===----------------------------------------------------------------------===//
// Legalization Utilities
//===----------------------------------------------------------------------===//

bool TargetLowering::expandMUL_LOHI(unsigned Opcode, EVT VT, const SDLoc &dl,
                                    SDValue LHS, SDValue RHS,
                                    SmallVectorImpl<SDValue> &Result,
                                    EVT HiLoVT, SelectionDAG &DAG,
                                    MulExpansionKind Kind, SDValue LL,
                                    SDValue LH, SDValue RL, SDValue RH) const {
  assert(Opcode == ISD::MUL || Opcode == ISD::UMUL_LOHI ||
         Opcode == ISD::SMUL_LOHI);

  bool HasMULHS = (Kind == MulExpansionKind::Always) ||
                  isOperationLegalOrCustom(ISD::MULHS, HiLoVT);
  bool HasMULHU = (Kind == MulExpansionKind::Always) ||
                  isOperationLegalOrCustom(ISD::MULHU, HiLoVT);
  bool HasSMUL_LOHI = (Kind == MulExpansionKind::Always) ||
                      isOperationLegalOrCustom(ISD::SMUL_LOHI, HiLoVT);
  bool HasUMUL_LOHI = (Kind == MulExpansionKind::Always) ||
                      isOperationLegalOrCustom(ISD::UMUL_LOHI, HiLoVT);

  if (!HasMULHU && !HasMULHS && !HasUMUL_LOHI && !HasSMUL_LOHI)
    return false;

  unsigned OuterBitSize = VT.getScalarSizeInBits();
  unsigned InnerBitSize = HiLoVT.getScalarSizeInBits();

  // LL, LH, RL, and RH must be either all NULL or all set to a value.
  assert((LL.getNode() && LH.getNode() && RL.getNode() && RH.getNode()) ||
         (!LL.getNode() && !LH.getNode() && !RL.getNode() && !RH.getNode()));

  SDVTList VTs = DAG.getVTList(HiLoVT, HiLoVT);
  auto MakeMUL_LOHI = [&](SDValue L, SDValue R, SDValue &Lo, SDValue &Hi,
                          bool Signed) -> bool {
    if ((Signed && HasSMUL_LOHI) || (!Signed && HasUMUL_LOHI)) {
      Lo = DAG.getNode(Signed ? ISD::SMUL_LOHI : ISD::UMUL_LOHI, dl, VTs, L, R);
      Hi = SDValue(Lo.getNode(), 1);
      return true;
    }
    if ((Signed && HasMULHS) || (!Signed && HasMULHU)) {
      Lo = DAG.getNode(ISD::MUL, dl, HiLoVT, L, R);
      Hi = DAG.getNode(Signed ? ISD::MULHS : ISD::MULHU, dl, HiLoVT, L, R);
      return true;
    }
    return false;
  };

  SDValue Lo, Hi;

  if (!LL.getNode() && !RL.getNode() &&
      isOperationLegalOrCustom(ISD::TRUNCATE, HiLoVT)) {
    LL = DAG.getNode(ISD::TRUNCATE, dl, HiLoVT, LHS);
    RL = DAG.getNode(ISD::TRUNCATE, dl, HiLoVT, RHS);
  }

  if (!LL.getNode())
    return false;

  APInt HighMask = APInt::getHighBitsSet(OuterBitSize, InnerBitSize);
  if (DAG.MaskedValueIsZero(LHS, HighMask) &&
      DAG.MaskedValueIsZero(RHS, HighMask)) {
    // The inputs are both zero-extended.
    if (MakeMUL_LOHI(LL, RL, Lo, Hi, false)) {
      Result.push_back(Lo);
      Result.push_back(Hi);
      if (Opcode != ISD::MUL) {
        SDValue Zero = DAG.getConstant(0, dl, HiLoVT);
        Result.push_back(Zero);
        Result.push_back(Zero);
      }
      return true;
    }
  }

  if (!VT.isVector() && Opcode == ISD::MUL &&
      DAG.ComputeMaxSignificantBits(LHS) <= InnerBitSize &&
      DAG.ComputeMaxSignificantBits(RHS) <= InnerBitSize) {
    // The input values are both sign-extended.
    // TODO non-MUL case?
    if (MakeMUL_LOHI(LL, RL, Lo, Hi, true)) {
      Result.push_back(Lo);
      Result.push_back(Hi);
      return true;
    }
  }

  unsigned ShiftAmount = OuterBitSize - InnerBitSize;
  SDValue Shift = DAG.getShiftAmountConstant(ShiftAmount, VT, dl);

  if (!LH.getNode() && !RH.getNode() &&
      isOperationLegalOrCustom(ISD::SRL, VT) &&
      isOperationLegalOrCustom(ISD::TRUNCATE, HiLoVT)) {
    LH = DAG.getNode(ISD::SRL, dl, VT, LHS, Shift);
    LH = DAG.getNode(ISD::TRUNCATE, dl, HiLoVT, LH);
    RH = DAG.getNode(ISD::SRL, dl, VT, RHS, Shift);
    RH = DAG.getNode(ISD::TRUNCATE, dl, HiLoVT, RH);
  }

  if (!LH.getNode())
    return false;

  if (!MakeMUL_LOHI(LL, RL, Lo, Hi, false))
    return false;

  Result.push_back(Lo);

  if (Opcode == ISD::MUL) {
    RH = DAG.getNode(ISD::MUL, dl, HiLoVT, LL, RH);
    LH = DAG.getNode(ISD::MUL, dl, HiLoVT, LH, RL);
    Hi = DAG.getNode(ISD::ADD, dl, HiLoVT, Hi, RH);
    Hi = DAG.getNode(ISD::ADD, dl, HiLoVT, Hi, LH);
    Result.push_back(Hi);
    return true;
  }

  // Compute the full width result.
  auto Merge = [&](SDValue Lo, SDValue Hi) -> SDValue {
    Lo = DAG.getNode(ISD::ZERO_EXTEND, dl, VT, Lo);
    Hi = DAG.getNode(ISD::ZERO_EXTEND, dl, VT, Hi);
    Hi = DAG.getNode(ISD::SHL, dl, VT, Hi, Shift);
    return DAG.getNode(ISD::OR, dl, VT, Lo, Hi);
  };

  SDValue Next = DAG.getNode(ISD::ZERO_EXTEND, dl, VT, Hi);
  if (!MakeMUL_LOHI(LL, RH, Lo, Hi, false))
    return false;

  // This is effectively the add part of a multiply-add of half-sized operands,
  // so it cannot overflow.
  Next = DAG.getNode(ISD::ADD, dl, VT, Next, Merge(Lo, Hi));

  if (!MakeMUL_LOHI(LH, RL, Lo, Hi, false))
    return false;

  SDValue Zero = DAG.getConstant(0, dl, HiLoVT);
  EVT BoolType = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  bool UseGlue = (isOperationLegalOrCustom(ISD::ADDC, VT) &&
                  isOperationLegalOrCustom(ISD::ADDE, VT));
  if (UseGlue)
    Next = DAG.getNode(ISD::ADDC, dl, DAG.getVTList(VT, MVT::Glue), Next,
                       Merge(Lo, Hi));
  else
    Next = DAG.getNode(ISD::UADDO_CARRY, dl, DAG.getVTList(VT, BoolType), Next,
                       Merge(Lo, Hi), DAG.getConstant(0, dl, BoolType));

  SDValue Carry = Next.getValue(1);
  Result.push_back(DAG.getNode(ISD::TRUNCATE, dl, HiLoVT, Next));
  Next = DAG.getNode(ISD::SRL, dl, VT, Next, Shift);

  if (!MakeMUL_LOHI(LH, RH, Lo, Hi, Opcode == ISD::SMUL_LOHI))
    return false;

  if (UseGlue)
    Hi = DAG.getNode(ISD::ADDE, dl, DAG.getVTList(HiLoVT, MVT::Glue), Hi, Zero,
                     Carry);
  else
    Hi = DAG.getNode(ISD::UADDO_CARRY, dl, DAG.getVTList(HiLoVT, BoolType), Hi,
                     Zero, Carry);

  Next = DAG.getNode(ISD::ADD, dl, VT, Next, Merge(Lo, Hi));

  if (Opcode == ISD::SMUL_LOHI) {
    SDValue NextSub = DAG.getNode(ISD::SUB, dl, VT, Next,
                                  DAG.getNode(ISD::ZERO_EXTEND, dl, VT, RL));
    Next = DAG.getSelectCC(dl, LH, Zero, NextSub, Next, ISD::SETLT);

    NextSub = DAG.getNode(ISD::SUB, dl, VT, Next,
                          DAG.getNode(ISD::ZERO_EXTEND, dl, VT, LL));
    Next = DAG.getSelectCC(dl, RH, Zero, NextSub, Next, ISD::SETLT);
  }

  Result.push_back(DAG.getNode(ISD::TRUNCATE, dl, HiLoVT, Next));
  Next = DAG.getNode(ISD::SRL, dl, VT, Next, Shift);
  Result.push_back(DAG.getNode(ISD::TRUNCATE, dl, HiLoVT, Next));
  return true;
}

bool TargetLowering::expandMUL(SDNode *N, SDValue &Lo, SDValue &Hi, EVT HiLoVT,
                               SelectionDAG &DAG, MulExpansionKind Kind,
                               SDValue LL, SDValue LH, SDValue RL,
                               SDValue RH) const {
  SmallVector<SDValue, 2> Result;
  bool Ok = expandMUL_LOHI(N->getOpcode(), N->getValueType(0), SDLoc(N),
                           N->getOperand(0), N->getOperand(1), Result, HiLoVT,
                           DAG, Kind, LL, LH, RL, RH);
  if (Ok) {
    assert(Result.size() == 2);
    Lo = Result[0];
    Hi = Result[1];
  }
  return Ok;
}

// Optimize unsigned division or remainder by constants for types twice as large
// as a legal VT.
//
// If (1 << (BitWidth / 2)) % Constant == 1, then the remainder
// can be computed
// as:
//   Sum += __builtin_uadd_overflow(Lo, High, &Sum);
//   Remainder = Sum % Constant
// This is based on "Remainder by Summing Digits" from Hacker's Delight.
//
// For division, we can compute the remainder using the algorithm described
// above, subtract it from the dividend to get an exact multiple of Constant.
// Then multiply that exact multiply by the multiplicative inverse modulo
// (1 << (BitWidth / 2)) to get the quotient.

// If Constant is even, we can shift right the dividend and the divisor by the
// number of trailing zeros in Constant before applying the remainder algorithm.
// If we're after the quotient, we can subtract this value from the shifted
// dividend and multiply by the multiplicative inverse of the shifted divisor.
// If we want the remainder, we shift the value left by the number of trailing
// zeros and add the bits that were shifted out of the dividend.
bool TargetLowering::expandDIVREMByConstant(SDNode *N,
                                            SmallVectorImpl<SDValue> &Result,
                                            EVT HiLoVT, SelectionDAG &DAG,
                                            SDValue LL, SDValue LH) const {
  unsigned Opcode = N->getOpcode();
  EVT VT = N->getValueType(0);

  // TODO: Support signed division/remainder.
  if (Opcode == ISD::SREM || Opcode == ISD::SDIV || Opcode == ISD::SDIVREM)
    return false;
  assert(
      (Opcode == ISD::UREM || Opcode == ISD::UDIV || Opcode == ISD::UDIVREM) &&
      "Unexpected opcode");

  auto *CN = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!CN)
    return false;

  APInt Divisor = CN->getAPIntValue();
  unsigned BitWidth = Divisor.getBitWidth();
  unsigned HBitWidth = BitWidth / 2;
  assert(VT.getScalarSizeInBits() == BitWidth &&
         HiLoVT.getScalarSizeInBits() == HBitWidth && "Unexpected VTs");

  // Divisor needs to less than (1 << HBitWidth).
  APInt HalfMaxPlus1 = APInt::getOneBitSet(BitWidth, HBitWidth);
  if (Divisor.uge(HalfMaxPlus1))
    return false;

  // We depend on the UREM by constant optimization in DAGCombiner that requires
  // high multiply.
  if (!isOperationLegalOrCustom(ISD::MULHU, HiLoVT) &&
      !isOperationLegalOrCustom(ISD::UMUL_LOHI, HiLoVT))
    return false;

  // Don't expand if optimizing for size.
  if (DAG.shouldOptForSize())
    return false;

  // Early out for 0 or 1 divisors.
  if (Divisor.ule(1))
    return false;

  // If the divisor is even, shift it until it becomes odd.
  unsigned TrailingZeros = 0;
  if (!Divisor[0]) {
    TrailingZeros = Divisor.countr_zero();
    Divisor.lshrInPlace(TrailingZeros);
  }

  SDLoc dl(N);
  SDValue Sum;
  SDValue PartialRem;

  // If (1 << HBitWidth) % divisor == 1, we can add the two halves together and
  // then add in the carry.
  // TODO: If we can't split it in half, we might be able to split into 3 or
  // more pieces using a smaller bit width.
  if (HalfMaxPlus1.urem(Divisor).isOne()) {
    assert(!LL == !LH && "Expected both input halves or no input halves!");
    if (!LL)
      std::tie(LL, LH) = DAG.SplitScalar(N->getOperand(0), dl, HiLoVT, HiLoVT);

    // Shift the input by the number of TrailingZeros in the divisor. The
    // shifted out bits will be added to the remainder later.
    if (TrailingZeros) {
      // Save the shifted off bits if we need the remainder.
      if (Opcode != ISD::UDIV) {
        APInt Mask = APInt::getLowBitsSet(HBitWidth, TrailingZeros);
        PartialRem = DAG.getNode(ISD::AND, dl, HiLoVT, LL,
                                 DAG.getConstant(Mask, dl, HiLoVT));
      }

      LL = DAG.getNode(
          ISD::OR, dl, HiLoVT,
          DAG.getNode(ISD::SRL, dl, HiLoVT, LL,
                      DAG.getShiftAmountConstant(TrailingZeros, HiLoVT, dl)),
          DAG.getNode(ISD::SHL, dl, HiLoVT, LH,
                      DAG.getShiftAmountConstant(HBitWidth - TrailingZeros,
                                                 HiLoVT, dl)));
      LH = DAG.getNode(ISD::SRL, dl, HiLoVT, LH,
                       DAG.getShiftAmountConstant(TrailingZeros, HiLoVT, dl));
    }

    // Use uaddo_carry if we can, otherwise use a compare to detect overflow.
    EVT SetCCType =
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), HiLoVT);
    if (isOperationLegalOrCustom(ISD::UADDO_CARRY, HiLoVT)) {
      SDVTList VTList = DAG.getVTList(HiLoVT, SetCCType);
      Sum = DAG.getNode(ISD::UADDO, dl, VTList, LL, LH);
      Sum = DAG.getNode(ISD::UADDO_CARRY, dl, VTList, Sum,
                        DAG.getConstant(0, dl, HiLoVT), Sum.getValue(1));
    } else {
      Sum = DAG.getNode(ISD::ADD, dl, HiLoVT, LL, LH);
      SDValue Carry = DAG.getSetCC(dl, SetCCType, Sum, LL, ISD::SETULT);
      // If the boolean for the target is 0 or 1, we can add the setcc result
      // directly.
      if (getBooleanContents(HiLoVT) ==
          TargetLoweringBase::ZeroOrOneBooleanContent)
        Carry = DAG.getZExtOrTrunc(Carry, dl, HiLoVT);
      else
        Carry = DAG.getSelect(dl, HiLoVT, Carry, DAG.getConstant(1, dl, HiLoVT),
                              DAG.getConstant(0, dl, HiLoVT));
      Sum = DAG.getNode(ISD::ADD, dl, HiLoVT, Sum, Carry);
    }
  }

  // If we didn't find a sum, we can't do the expansion.
  if (!Sum)
    return false;

  // Perform a HiLoVT urem on the Sum using truncated divisor.
  SDValue RemL =
      DAG.getNode(ISD::UREM, dl, HiLoVT, Sum,
                  DAG.getConstant(Divisor.trunc(HBitWidth), dl, HiLoVT));
  SDValue RemH = DAG.getConstant(0, dl, HiLoVT);

  if (Opcode != ISD::UREM) {
    // Subtract the remainder from the shifted dividend.
    SDValue Dividend = DAG.getNode(ISD::BUILD_PAIR, dl, VT, LL, LH);
    SDValue Rem = DAG.getNode(ISD::BUILD_PAIR, dl, VT, RemL, RemH);

    Dividend = DAG.getNode(ISD::SUB, dl, VT, Dividend, Rem);

    // Multiply by the multiplicative inverse of the divisor modulo
    // (1 << BitWidth).
    APInt MulFactor = Divisor.multiplicativeInverse();

    SDValue Quotient = DAG.getNode(ISD::MUL, dl, VT, Dividend,
                                   DAG.getConstant(MulFactor, dl, VT));

    // Split the quotient into low and high parts.
    SDValue QuotL, QuotH;
    std::tie(QuotL, QuotH) = DAG.SplitScalar(Quotient, dl, HiLoVT, HiLoVT);
    Result.push_back(QuotL);
    Result.push_back(QuotH);
  }

  if (Opcode != ISD::UDIV) {
    // If we shifted the input, shift the remainder left and add the bits we
    // shifted off the input.
    if (TrailingZeros) {
      APInt Mask = APInt::getLowBitsSet(HBitWidth, TrailingZeros);
      RemL = DAG.getNode(ISD::SHL, dl, HiLoVT, RemL,
                         DAG.getShiftAmountConstant(TrailingZeros, HiLoVT, dl));
      RemL = DAG.getNode(ISD::ADD, dl, HiLoVT, RemL, PartialRem);
    }
    Result.push_back(RemL);
    Result.push_back(DAG.getConstant(0, dl, HiLoVT));
  }

  return true;
}

// Check that (every element of) Z is undef or not an exact multiple of BW.
static bool isNonZeroModBitWidthOrUndef(SDValue Z, unsigned BW) {
  return ISD::matchUnaryPredicate(
      Z,
      [=](ConstantSDNode *C) { return !C || C->getAPIntValue().urem(BW) != 0; },
      true);
}

static SDValue expandVPFunnelShift(SDNode *Node, SelectionDAG &DAG) {
  EVT VT = Node->getValueType(0);
  SDValue ShX, ShY;
  SDValue ShAmt, InvShAmt;
  SDValue X = Node->getOperand(0);
  SDValue Y = Node->getOperand(1);
  SDValue Z = Node->getOperand(2);
  SDValue Mask = Node->getOperand(3);
  SDValue VL = Node->getOperand(4);

  unsigned BW = VT.getScalarSizeInBits();
  bool IsFSHL = Node->getOpcode() == ISD::VP_FSHL;
  SDLoc DL(SDValue(Node, 0));

  EVT ShVT = Z.getValueType();
  if (isNonZeroModBitWidthOrUndef(Z, BW)) {
    // fshl: X << C | Y >> (BW - C)
    // fshr: X << (BW - C) | Y >> C
    // where C = Z % BW is not zero
    SDValue BitWidthC = DAG.getConstant(BW, DL, ShVT);
    ShAmt = DAG.getNode(ISD::VP_UREM, DL, ShVT, Z, BitWidthC, Mask, VL);
    InvShAmt = DAG.getNode(ISD::VP_SUB, DL, ShVT, BitWidthC, ShAmt, Mask, VL);
    ShX = DAG.getNode(ISD::VP_SHL, DL, VT, X, IsFSHL ? ShAmt : InvShAmt, Mask,
                      VL);
    ShY = DAG.getNode(ISD::VP_SRL, DL, VT, Y, IsFSHL ? InvShAmt : ShAmt, Mask,
                      VL);
  } else {
    // fshl: X << (Z % BW) | Y >> 1 >> (BW - 1 - (Z % BW))
    // fshr: X << 1 << (BW - 1 - (Z % BW)) | Y >> (Z % BW)
    SDValue BitMask = DAG.getConstant(BW - 1, DL, ShVT);
    if (isPowerOf2_32(BW)) {
      // Z % BW -> Z & (BW - 1)
      ShAmt = DAG.getNode(ISD::VP_AND, DL, ShVT, Z, BitMask, Mask, VL);
      // (BW - 1) - (Z % BW) -> ~Z & (BW - 1)
      SDValue NotZ = DAG.getNode(ISD::VP_XOR, DL, ShVT, Z,
                                 DAG.getAllOnesConstant(DL, ShVT), Mask, VL);
      InvShAmt = DAG.getNode(ISD::VP_AND, DL, ShVT, NotZ, BitMask, Mask, VL);
    } else {
      SDValue BitWidthC = DAG.getConstant(BW, DL, ShVT);
      ShAmt = DAG.getNode(ISD::VP_UREM, DL, ShVT, Z, BitWidthC, Mask, VL);
      InvShAmt = DAG.getNode(ISD::VP_SUB, DL, ShVT, BitMask, ShAmt, Mask, VL);
    }

    SDValue One = DAG.getConstant(1, DL, ShVT);
    if (IsFSHL) {
      ShX = DAG.getNode(ISD::VP_SHL, DL, VT, X, ShAmt, Mask, VL);
      SDValue ShY1 = DAG.getNode(ISD::VP_SRL, DL, VT, Y, One, Mask, VL);
      ShY = DAG.getNode(ISD::VP_SRL, DL, VT, ShY1, InvShAmt, Mask, VL);
    } else {
      SDValue ShX1 = DAG.getNode(ISD::VP_SHL, DL, VT, X, One, Mask, VL);
      ShX = DAG.getNode(ISD::VP_SHL, DL, VT, ShX1, InvShAmt, Mask, VL);
      ShY = DAG.getNode(ISD::VP_SRL, DL, VT, Y, ShAmt, Mask, VL);
    }
  }
  return DAG.getNode(ISD::VP_OR, DL, VT, ShX, ShY, Mask, VL);
}

SDValue TargetLowering::expandFunnelShift(SDNode *Node,
                                          SelectionDAG &DAG) const {
  if (Node->isVPOpcode())
    return expandVPFunnelShift(Node, DAG);

  EVT VT = Node->getValueType(0);

  if (VT.isVector() && (!isOperationLegalOrCustom(ISD::SHL, VT) ||
                        !isOperationLegalOrCustom(ISD::SRL, VT) ||
                        !isOperationLegalOrCustom(ISD::SUB, VT) ||
                        !isOperationLegalOrCustomOrPromote(ISD::OR, VT)))
    return SDValue();

  SDValue X = Node->getOperand(0);
  SDValue Y = Node->getOperand(1);
  SDValue Z = Node->getOperand(2);

  unsigned BW = VT.getScalarSizeInBits();
  bool IsFSHL = Node->getOpcode() == ISD::FSHL;
  SDLoc DL(SDValue(Node, 0));

  EVT ShVT = Z.getValueType();

  // If a funnel shift in the other direction is more supported, use it.
  unsigned RevOpcode = IsFSHL ? ISD::FSHR : ISD::FSHL;
  if (!isOperationLegalOrCustom(Node->getOpcode(), VT) &&
      isOperationLegalOrCustom(RevOpcode, VT) && isPowerOf2_32(BW)) {
    if (isNonZeroModBitWidthOrUndef(Z, BW)) {
      // fshl X, Y, Z -> fshr X, Y, -Z
      // fshr X, Y, Z -> fshl X, Y, -Z
      SDValue Zero = DAG.getConstant(0, DL, ShVT);
      Z = DAG.getNode(ISD::SUB, DL, VT, Zero, Z);
    } else {
      // fshl X, Y, Z -> fshr (srl X, 1), (fshr X, Y, 1), ~Z
      // fshr X, Y, Z -> fshl (fshl X, Y, 1), (shl Y, 1), ~Z
      SDValue One = DAG.getConstant(1, DL, ShVT);
      if (IsFSHL) {
        Y = DAG.getNode(RevOpcode, DL, VT, X, Y, One);
        X = DAG.getNode(ISD::SRL, DL, VT, X, One);
      } else {
        X = DAG.getNode(RevOpcode, DL, VT, X, Y, One);
        Y = DAG.getNode(ISD::SHL, DL, VT, Y, One);
      }
      Z = DAG.getNOT(DL, Z, ShVT);
    }
    return DAG.getNode(RevOpcode, DL, VT, X, Y, Z);
  }

  SDValue ShX, ShY;
  SDValue ShAmt, InvShAmt;
  if (isNonZeroModBitWidthOrUndef(Z, BW)) {
    // fshl: X << C | Y >> (BW - C)
    // fshr: X << (BW - C) | Y >> C
    // where C = Z % BW is not zero
    SDValue BitWidthC = DAG.getConstant(BW, DL, ShVT);
    ShAmt = DAG.getNode(ISD::UREM, DL, ShVT, Z, BitWidthC);
    InvShAmt = DAG.getNode(ISD::SUB, DL, ShVT, BitWidthC, ShAmt);
    ShX = DAG.getNode(ISD::SHL, DL, VT, X, IsFSHL ? ShAmt : InvShAmt);
    ShY = DAG.getNode(ISD::SRL, DL, VT, Y, IsFSHL ? InvShAmt : ShAmt);
  } else {
    // fshl: X << (Z % BW) | Y >> 1 >> (BW - 1 - (Z % BW))
    // fshr: X << 1 << (BW - 1 - (Z % BW)) | Y >> (Z % BW)
    SDValue Mask = DAG.getConstant(BW - 1, DL, ShVT);
    if (isPowerOf2_32(BW)) {
      // Z % BW -> Z & (BW - 1)
      ShAmt = DAG.getNode(ISD::AND, DL, ShVT, Z, Mask);
      // (BW - 1) - (Z % BW) -> ~Z & (BW - 1)
      InvShAmt = DAG.getNode(ISD::AND, DL, ShVT, DAG.getNOT(DL, Z, ShVT), Mask);
    } else {
      SDValue BitWidthC = DAG.getConstant(BW, DL, ShVT);
      ShAmt = DAG.getNode(ISD::UREM, DL, ShVT, Z, BitWidthC);
      InvShAmt = DAG.getNode(ISD::SUB, DL, ShVT, Mask, ShAmt);
    }

    SDValue One = DAG.getConstant(1, DL, ShVT);
    if (IsFSHL) {
      ShX = DAG.getNode(ISD::SHL, DL, VT, X, ShAmt);
      SDValue ShY1 = DAG.getNode(ISD::SRL, DL, VT, Y, One);
      ShY = DAG.getNode(ISD::SRL, DL, VT, ShY1, InvShAmt);
    } else {
      SDValue ShX1 = DAG.getNode(ISD::SHL, DL, VT, X, One);
      ShX = DAG.getNode(ISD::SHL, DL, VT, ShX1, InvShAmt);
      ShY = DAG.getNode(ISD::SRL, DL, VT, Y, ShAmt);
    }
  }
  return DAG.getNode(ISD::OR, DL, VT, ShX, ShY);
}

// TODO: Merge with expandFunnelShift.
SDValue TargetLowering::expandROT(SDNode *Node, bool AllowVectorOps,
                                  SelectionDAG &DAG) const {
  EVT VT = Node->getValueType(0);
  unsigned EltSizeInBits = VT.getScalarSizeInBits();
  bool IsLeft = Node->getOpcode() == ISD::ROTL;
  SDValue Op0 = Node->getOperand(0);
  SDValue Op1 = Node->getOperand(1);
  SDLoc DL(SDValue(Node, 0));

  EVT ShVT = Op1.getValueType();
  SDValue Zero = DAG.getConstant(0, DL, ShVT);

  // If a rotate in the other direction is more supported, use it.
  unsigned RevRot = IsLeft ? ISD::ROTR : ISD::ROTL;
  if (!isOperationLegalOrCustom(Node->getOpcode(), VT) &&
      isOperationLegalOrCustom(RevRot, VT) && isPowerOf2_32(EltSizeInBits)) {
    SDValue Sub = DAG.getNode(ISD::SUB, DL, ShVT, Zero, Op1);
    return DAG.getNode(RevRot, DL, VT, Op0, Sub);
  }

  if (!AllowVectorOps && VT.isVector() &&
      (!isOperationLegalOrCustom(ISD::SHL, VT) ||
       !isOperationLegalOrCustom(ISD::SRL, VT) ||
       !isOperationLegalOrCustom(ISD::SUB, VT) ||
       !isOperationLegalOrCustomOrPromote(ISD::OR, VT) ||
       !isOperationLegalOrCustomOrPromote(ISD::AND, VT)))
    return SDValue();

  unsigned ShOpc = IsLeft ? ISD::SHL : ISD::SRL;
  unsigned HsOpc = IsLeft ? ISD::SRL : ISD::SHL;
  SDValue BitWidthMinusOneC = DAG.getConstant(EltSizeInBits - 1, DL, ShVT);
  SDValue ShVal;
  SDValue HsVal;
  if (isPowerOf2_32(EltSizeInBits)) {
    // (rotl x, c) -> x << (c & (w - 1)) | x >> (-c & (w - 1))
    // (rotr x, c) -> x >> (c & (w - 1)) | x << (-c & (w - 1))
    SDValue NegOp1 = DAG.getNode(ISD::SUB, DL, ShVT, Zero, Op1);
    SDValue ShAmt = DAG.getNode(ISD::AND, DL, ShVT, Op1, BitWidthMinusOneC);
    ShVal = DAG.getNode(ShOpc, DL, VT, Op0, ShAmt);
    SDValue HsAmt = DAG.getNode(ISD::AND, DL, ShVT, NegOp1, BitWidthMinusOneC);
    HsVal = DAG.getNode(HsOpc, DL, VT, Op0, HsAmt);
  } else {
    // (rotl x, c) -> x << (c % w) | x >> 1 >> (w - 1 - (c % w))
    // (rotr x, c) -> x >> (c % w) | x << 1 << (w - 1 - (c % w))
    SDValue BitWidthC = DAG.getConstant(EltSizeInBits, DL, ShVT);
    SDValue ShAmt = DAG.getNode(ISD::UREM, DL, ShVT, Op1, BitWidthC);
    ShVal = DAG.getNode(ShOpc, DL, VT, Op0, ShAmt);
    SDValue HsAmt = DAG.getNode(ISD::SUB, DL, ShVT, BitWidthMinusOneC, ShAmt);
    SDValue One = DAG.getConstant(1, DL, ShVT);
    HsVal =
        DAG.getNode(HsOpc, DL, VT, DAG.getNode(HsOpc, DL, VT, Op0, One), HsAmt);
  }
  return DAG.getNode(ISD::OR, DL, VT, ShVal, HsVal);
}

void TargetLowering::expandShiftParts(SDNode *Node, SDValue &Lo, SDValue &Hi,
                                      SelectionDAG &DAG) const {
  assert(Node->getNumOperands() == 3 && "Not a double-shift!");
  EVT VT = Node->getValueType(0);
  unsigned VTBits = VT.getScalarSizeInBits();
  assert(isPowerOf2_32(VTBits) && "Power-of-two integer type expected");

  bool IsSHL = Node->getOpcode() == ISD::SHL_PARTS;
  bool IsSRA = Node->getOpcode() == ISD::SRA_PARTS;
  SDValue ShOpLo = Node->getOperand(0);
  SDValue ShOpHi = Node->getOperand(1);
  SDValue ShAmt = Node->getOperand(2);
  EVT ShAmtVT = ShAmt.getValueType();
  EVT ShAmtCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), ShAmtVT);
  SDLoc dl(Node);

  // ISD::FSHL and ISD::FSHR have defined overflow behavior but ISD::SHL and
  // ISD::SRA/L nodes haven't. Insert an AND to be safe, it's usually optimized
  // away during isel.
  SDValue SafeShAmt = DAG.getNode(ISD::AND, dl, ShAmtVT, ShAmt,
                                  DAG.getConstant(VTBits - 1, dl, ShAmtVT));
  SDValue Tmp1 = IsSRA ? DAG.getNode(ISD::SRA, dl, VT, ShOpHi,
                                     DAG.getConstant(VTBits - 1, dl, ShAmtVT))
                       : DAG.getConstant(0, dl, VT);

  SDValue Tmp2, Tmp3;
  if (IsSHL) {
    Tmp2 = DAG.getNode(ISD::FSHL, dl, VT, ShOpHi, ShOpLo, ShAmt);
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, SafeShAmt);
  } else {
    Tmp2 = DAG.getNode(ISD::FSHR, dl, VT, ShOpHi, ShOpLo, ShAmt);
    Tmp3 = DAG.getNode(IsSRA ? ISD::SRA : ISD::SRL, dl, VT, ShOpHi, SafeShAmt);
  }

  // If the shift amount is larger or equal than the width of a part we don't
  // use the result from the FSHL/FSHR. Insert a test and select the appropriate
  // values for large shift amounts.
  SDValue AndNode = DAG.getNode(ISD::AND, dl, ShAmtVT, ShAmt,
                                DAG.getConstant(VTBits, dl, ShAmtVT));
  SDValue Cond = DAG.getSetCC(dl, ShAmtCCVT, AndNode,
                              DAG.getConstant(0, dl, ShAmtVT), ISD::SETNE);

  if (IsSHL) {
    Hi = DAG.getNode(ISD::SELECT, dl, VT, Cond, Tmp3, Tmp2);
    Lo = DAG.getNode(ISD::SELECT, dl, VT, Cond, Tmp1, Tmp3);
  } else {
    Lo = DAG.getNode(ISD::SELECT, dl, VT, Cond, Tmp3, Tmp2);
    Hi = DAG.getNode(ISD::SELECT, dl, VT, Cond, Tmp1, Tmp3);
  }
}

bool TargetLowering::expandFP_TO_SINT(SDNode *Node, SDValue &Result,
                                      SelectionDAG &DAG) const {
  unsigned OpNo = Node->isStrictFPOpcode() ? 1 : 0;
  SDValue Src = Node->getOperand(OpNo);
  EVT SrcVT = Src.getValueType();
  EVT DstVT = Node->getValueType(0);
  SDLoc dl(SDValue(Node, 0));

  // FIXME: Only f32 to i64 conversions are supported.
  if (SrcVT != MVT::f32 || DstVT != MVT::i64)
    return false;

  if (Node->isStrictFPOpcode())
    // When a NaN is converted to an integer a trap is allowed. We can't
    // use this expansion here because it would eliminate that trap. Other
    // traps are also allowed and cannot be eliminated. See
    // IEEE 754-2008 sec 5.8.
    return false;

  // Expand f32 -> i64 conversion
  // This algorithm comes from compiler-rt's implementation of fixsfdi:
  // https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/builtins/fixsfdi.c
  unsigned SrcEltBits = SrcVT.getScalarSizeInBits();
  EVT IntVT = SrcVT.changeTypeToInteger();
  EVT IntShVT = getShiftAmountTy(IntVT, DAG.getDataLayout());

  SDValue ExponentMask = DAG.getConstant(0x7F800000, dl, IntVT);
  SDValue ExponentLoBit = DAG.getConstant(23, dl, IntVT);
  SDValue Bias = DAG.getConstant(127, dl, IntVT);
  SDValue SignMask = DAG.getConstant(APInt::getSignMask(SrcEltBits), dl, IntVT);
  SDValue SignLowBit = DAG.getConstant(SrcEltBits - 1, dl, IntVT);
  SDValue MantissaMask = DAG.getConstant(0x007FFFFF, dl, IntVT);

  SDValue Bits = DAG.getNode(ISD::BITCAST, dl, IntVT, Src);

  SDValue ExponentBits = DAG.getNode(
      ISD::SRL, dl, IntVT, DAG.getNode(ISD::AND, dl, IntVT, Bits, ExponentMask),
      DAG.getZExtOrTrunc(ExponentLoBit, dl, IntShVT));
  SDValue Exponent = DAG.getNode(ISD::SUB, dl, IntVT, ExponentBits, Bias);

  SDValue Sign = DAG.getNode(ISD::SRA, dl, IntVT,
                             DAG.getNode(ISD::AND, dl, IntVT, Bits, SignMask),
                             DAG.getZExtOrTrunc(SignLowBit, dl, IntShVT));
  Sign = DAG.getSExtOrTrunc(Sign, dl, DstVT);

  SDValue R = DAG.getNode(ISD::OR, dl, IntVT,
                          DAG.getNode(ISD::AND, dl, IntVT, Bits, MantissaMask),
                          DAG.getConstant(0x00800000, dl, IntVT));

  R = DAG.getZExtOrTrunc(R, dl, DstVT);

  R = DAG.getSelectCC(
      dl, Exponent, ExponentLoBit,
      DAG.getNode(ISD::SHL, dl, DstVT, R,
                  DAG.getZExtOrTrunc(
                      DAG.getNode(ISD::SUB, dl, IntVT, Exponent, ExponentLoBit),
                      dl, IntShVT)),
      DAG.getNode(ISD::SRL, dl, DstVT, R,
                  DAG.getZExtOrTrunc(
                      DAG.getNode(ISD::SUB, dl, IntVT, ExponentLoBit, Exponent),
                      dl, IntShVT)),
      ISD::SETGT);

  SDValue Ret = DAG.getNode(ISD::SUB, dl, DstVT,
                            DAG.getNode(ISD::XOR, dl, DstVT, R, Sign), Sign);

  Result = DAG.getSelectCC(dl, Exponent, DAG.getConstant(0, dl, IntVT),
                           DAG.getConstant(0, dl, DstVT), Ret, ISD::SETLT);
  return true;
}

bool TargetLowering::expandFP_TO_UINT(SDNode *Node, SDValue &Result,
                                      SDValue &Chain,
                                      SelectionDAG &DAG) const {
  SDLoc dl(SDValue(Node, 0));
  unsigned OpNo = Node->isStrictFPOpcode() ? 1 : 0;
  SDValue Src = Node->getOperand(OpNo);

  EVT SrcVT = Src.getValueType();
  EVT DstVT = Node->getValueType(0);
  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), SrcVT);
  EVT DstSetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), DstVT);

  // Only expand vector types if we have the appropriate vector bit operations.
  unsigned SIntOpcode = Node->isStrictFPOpcode() ? ISD::STRICT_FP_TO_SINT :
                                                   ISD::FP_TO_SINT;
  if (DstVT.isVector() && (!isOperationLegalOrCustom(SIntOpcode, DstVT) ||
                           !isOperationLegalOrCustomOrPromote(ISD::XOR, SrcVT)))
    return false;

  // If the maximum float value is smaller then the signed integer range,
  // the destination signmask can't be represented by the float, so we can
  // just use FP_TO_SINT directly.
  const fltSemantics &APFSem = DAG.EVTToAPFloatSemantics(SrcVT);
  APFloat APF(APFSem, APInt::getZero(SrcVT.getScalarSizeInBits()));
  APInt SignMask = APInt::getSignMask(DstVT.getScalarSizeInBits());
  if (APFloat::opOverflow &
      APF.convertFromAPInt(SignMask, false, APFloat::rmNearestTiesToEven)) {
    if (Node->isStrictFPOpcode()) {
      Result = DAG.getNode(ISD::STRICT_FP_TO_SINT, dl, { DstVT, MVT::Other },
                           { Node->getOperand(0), Src });
      Chain = Result.getValue(1);
    } else
      Result = DAG.getNode(ISD::FP_TO_SINT, dl, DstVT, Src);
    return true;
  }

  // Don't expand it if there isn't cheap fsub instruction.
  if (!isOperationLegalOrCustom(
          Node->isStrictFPOpcode() ? ISD::STRICT_FSUB : ISD::FSUB, SrcVT))
    return false;

  SDValue Cst = DAG.getConstantFP(APF, dl, SrcVT);
  SDValue Sel;

  if (Node->isStrictFPOpcode()) {
    Sel = DAG.getSetCC(dl, SetCCVT, Src, Cst, ISD::SETLT,
                       Node->getOperand(0), /*IsSignaling*/ true);
    Chain = Sel.getValue(1);
  } else {
    Sel = DAG.getSetCC(dl, SetCCVT, Src, Cst, ISD::SETLT);
  }

  bool Strict = Node->isStrictFPOpcode() ||
                shouldUseStrictFP_TO_INT(SrcVT, DstVT, /*IsSigned*/ false);

  if (Strict) {
    // Expand based on maximum range of FP_TO_SINT, if the value exceeds the
    // signmask then offset (the result of which should be fully representable).
    // Sel = Src < 0x8000000000000000
    // FltOfs = select Sel, 0, 0x8000000000000000
    // IntOfs = select Sel, 0, 0x8000000000000000
    // Result = fp_to_sint(Src - FltOfs) ^ IntOfs

    // TODO: Should any fast-math-flags be set for the FSUB?
    SDValue FltOfs = DAG.getSelect(dl, SrcVT, Sel,
                                   DAG.getConstantFP(0.0, dl, SrcVT), Cst);
    Sel = DAG.getBoolExtOrTrunc(Sel, dl, DstSetCCVT, DstVT);
    SDValue IntOfs = DAG.getSelect(dl, DstVT, Sel,
                                   DAG.getConstant(0, dl, DstVT),
                                   DAG.getConstant(SignMask, dl, DstVT));
    SDValue SInt;
    if (Node->isStrictFPOpcode()) {
      SDValue Val = DAG.getNode(ISD::STRICT_FSUB, dl, { SrcVT, MVT::Other },
                                { Chain, Src, FltOfs });
      SInt = DAG.getNode(ISD::STRICT_FP_TO_SINT, dl, { DstVT, MVT::Other },
                         { Val.getValue(1), Val });
      Chain = SInt.getValue(1);
    } else {
      SDValue Val = DAG.getNode(ISD::FSUB, dl, SrcVT, Src, FltOfs);
      SInt = DAG.getNode(ISD::FP_TO_SINT, dl, DstVT, Val);
    }
    Result = DAG.getNode(ISD::XOR, dl, DstVT, SInt, IntOfs);
  } else {
    // Expand based on maximum range of FP_TO_SINT:
    // True = fp_to_sint(Src)
    // False = 0x8000000000000000 + fp_to_sint(Src - 0x8000000000000000)
    // Result = select (Src < 0x8000000000000000), True, False

    SDValue True = DAG.getNode(ISD::FP_TO_SINT, dl, DstVT, Src);
    // TODO: Should any fast-math-flags be set for the FSUB?
    SDValue False = DAG.getNode(ISD::FP_TO_SINT, dl, DstVT,
                                DAG.getNode(ISD::FSUB, dl, SrcVT, Src, Cst));
    False = DAG.getNode(ISD::XOR, dl, DstVT, False,
                        DAG.getConstant(SignMask, dl, DstVT));
    Sel = DAG.getBoolExtOrTrunc(Sel, dl, DstSetCCVT, DstVT);
    Result = DAG.getSelect(dl, DstVT, Sel, True, False);
  }
  return true;
}

bool TargetLowering::expandUINT_TO_FP(SDNode *Node, SDValue &Result,
                                      SDValue &Chain,
                                      SelectionDAG &DAG) const {
  // This transform is not correct for converting 0 when rounding mode is set
  // to round toward negative infinity which will produce -0.0. So disable under
  // strictfp.
  if (Node->isStrictFPOpcode())
    return false;

  SDValue Src = Node->getOperand(0);
  EVT SrcVT = Src.getValueType();
  EVT DstVT = Node->getValueType(0);

  if (SrcVT.getScalarType() != MVT::i64 || DstVT.getScalarType() != MVT::f64)
    return false;

  // Only expand vector types if we have the appropriate vector bit operations.
  if (SrcVT.isVector() && (!isOperationLegalOrCustom(ISD::SRL, SrcVT) ||
                           !isOperationLegalOrCustom(ISD::FADD, DstVT) ||
                           !isOperationLegalOrCustom(ISD::FSUB, DstVT) ||
                           !isOperationLegalOrCustomOrPromote(ISD::OR, SrcVT) ||
                           !isOperationLegalOrCustomOrPromote(ISD::AND, SrcVT)))
    return false;

  SDLoc dl(SDValue(Node, 0));
  EVT ShiftVT = getShiftAmountTy(SrcVT, DAG.getDataLayout());

  // Implementation of unsigned i64 to f64 following the algorithm in
  // __floatundidf in compiler_rt.  This implementation performs rounding
  // correctly in all rounding modes with the exception of converting 0
  // when rounding toward negative infinity. In that case the fsub will produce
  // -0.0. This will be added to +0.0 and produce -0.0 which is incorrect.
  SDValue TwoP52 = DAG.getConstant(UINT64_C(0x4330000000000000), dl, SrcVT);
  SDValue TwoP84PlusTwoP52 = DAG.getConstantFP(
      llvm::bit_cast<double>(UINT64_C(0x4530000000100000)), dl, DstVT);
  SDValue TwoP84 = DAG.getConstant(UINT64_C(0x4530000000000000), dl, SrcVT);
  SDValue LoMask = DAG.getConstant(UINT64_C(0x00000000FFFFFFFF), dl, SrcVT);
  SDValue HiShift = DAG.getConstant(32, dl, ShiftVT);

  SDValue Lo = DAG.getNode(ISD::AND, dl, SrcVT, Src, LoMask);
  SDValue Hi = DAG.getNode(ISD::SRL, dl, SrcVT, Src, HiShift);
  SDValue LoOr = DAG.getNode(ISD::OR, dl, SrcVT, Lo, TwoP52);
  SDValue HiOr = DAG.getNode(ISD::OR, dl, SrcVT, Hi, TwoP84);
  SDValue LoFlt = DAG.getBitcast(DstVT, LoOr);
  SDValue HiFlt = DAG.getBitcast(DstVT, HiOr);
  SDValue HiSub =
      DAG.getNode(ISD::FSUB, dl, DstVT, HiFlt, TwoP84PlusTwoP52);
  Result = DAG.getNode(ISD::FADD, dl, DstVT, LoFlt, HiSub);
  return true;
}

SDValue
TargetLowering::createSelectForFMINNUM_FMAXNUM(SDNode *Node,
                                               SelectionDAG &DAG) const {
  unsigned Opcode = Node->getOpcode();
  assert((Opcode == ISD::FMINNUM || Opcode == ISD::FMAXNUM ||
          Opcode == ISD::STRICT_FMINNUM || Opcode == ISD::STRICT_FMAXNUM) &&
         "Wrong opcode");

  if (Node->getFlags().hasNoNaNs()) {
    ISD::CondCode Pred = Opcode == ISD::FMINNUM ? ISD::SETLT : ISD::SETGT;
    SDValue Op1 = Node->getOperand(0);
    SDValue Op2 = Node->getOperand(1);
    SDValue SelCC = DAG.getSelectCC(SDLoc(Node), Op1, Op2, Op1, Op2, Pred);
    // Copy FMF flags, but always set the no-signed-zeros flag
    // as this is implied by the FMINNUM/FMAXNUM semantics.
    SDNodeFlags Flags = Node->getFlags();
    Flags.setNoSignedZeros(true);
    SelCC->setFlags(Flags);
    return SelCC;
  }

  return SDValue();
}

SDValue TargetLowering::expandFMINNUM_FMAXNUM(SDNode *Node,
                                              SelectionDAG &DAG) const {
  SDLoc dl(Node);
  unsigned NewOp = Node->getOpcode() == ISD::FMINNUM ?
    ISD::FMINNUM_IEEE : ISD::FMAXNUM_IEEE;
  EVT VT = Node->getValueType(0);

  if (VT.isScalableVector())
    report_fatal_error(
        "Expanding fminnum/fmaxnum for scalable vectors is undefined.");

  if (isOperationLegalOrCustom(NewOp, VT)) {
    SDValue Quiet0 = Node->getOperand(0);
    SDValue Quiet1 = Node->getOperand(1);

    if (!Node->getFlags().hasNoNaNs()) {
      // Insert canonicalizes if it's possible we need to quiet to get correct
      // sNaN behavior.
      if (!DAG.isKnownNeverSNaN(Quiet0)) {
        Quiet0 = DAG.getNode(ISD::FCANONICALIZE, dl, VT, Quiet0,
                             Node->getFlags());
      }
      if (!DAG.isKnownNeverSNaN(Quiet1)) {
        Quiet1 = DAG.getNode(ISD::FCANONICALIZE, dl, VT, Quiet1,
                             Node->getFlags());
      }
    }

    return DAG.getNode(NewOp, dl, VT, Quiet0, Quiet1, Node->getFlags());
  }

  // If the target has FMINIMUM/FMAXIMUM but not FMINNUM/FMAXNUM use that
  // instead if there are no NaNs and there can't be an incompatible zero
  // compare: at least one operand isn't +/-0, or there are no signed-zeros.
  if ((Node->getFlags().hasNoNaNs() ||
       (DAG.isKnownNeverNaN(Node->getOperand(0)) &&
        DAG.isKnownNeverNaN(Node->getOperand(1)))) &&
      (Node->getFlags().hasNoSignedZeros() ||
       DAG.isKnownNeverZeroFloat(Node->getOperand(0)) ||
       DAG.isKnownNeverZeroFloat(Node->getOperand(1)))) {
    unsigned IEEE2018Op =
        Node->getOpcode() == ISD::FMINNUM ? ISD::FMINIMUM : ISD::FMAXIMUM;
    if (isOperationLegalOrCustom(IEEE2018Op, VT))
      return DAG.getNode(IEEE2018Op, dl, VT, Node->getOperand(0),
                         Node->getOperand(1), Node->getFlags());
  }

  if (SDValue SelCC = createSelectForFMINNUM_FMAXNUM(Node, DAG))
    return SelCC;

  return SDValue();
}

SDValue TargetLowering::expandFMINIMUM_FMAXIMUM(SDNode *N,
                                                SelectionDAG &DAG) const {
  SDLoc DL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  unsigned Opc = N->getOpcode();
  EVT VT = N->getValueType(0);
  EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  bool IsMax = Opc == ISD::FMAXIMUM;
  SDNodeFlags Flags = N->getFlags();

  // First, implement comparison not propagating NaN. If no native fmin or fmax
  // available, use plain select with setcc instead.
  SDValue MinMax;
  unsigned CompOpcIeee = IsMax ? ISD::FMAXNUM_IEEE : ISD::FMINNUM_IEEE;
  unsigned CompOpc = IsMax ? ISD::FMAXNUM : ISD::FMINNUM;

  // FIXME: We should probably define fminnum/fmaxnum variants with correct
  // signed zero behavior.
  bool MinMaxMustRespectOrderedZero = false;

  if (isOperationLegalOrCustom(CompOpcIeee, VT)) {
    MinMax = DAG.getNode(CompOpcIeee, DL, VT, LHS, RHS, Flags);
    MinMaxMustRespectOrderedZero = true;
  } else if (isOperationLegalOrCustom(CompOpc, VT)) {
    MinMax = DAG.getNode(CompOpc, DL, VT, LHS, RHS, Flags);
  } else {
    if (VT.isVector() && !isOperationLegalOrCustom(ISD::VSELECT, VT))
      return DAG.UnrollVectorOp(N);

    // NaN (if exists) will be propagated later, so orderness doesn't matter.
    SDValue Compare =
        DAG.getSetCC(DL, CCVT, LHS, RHS, IsMax ? ISD::SETGT : ISD::SETLT);
    MinMax = DAG.getSelect(DL, VT, Compare, LHS, RHS, Flags);
  }

  // Propagate any NaN of both operands
  if (!N->getFlags().hasNoNaNs() &&
      (!DAG.isKnownNeverNaN(RHS) || !DAG.isKnownNeverNaN(LHS))) {
    ConstantFP *FPNaN = ConstantFP::get(
        *DAG.getContext(), APFloat::getNaN(DAG.EVTToAPFloatSemantics(VT)));
    MinMax = DAG.getSelect(DL, VT, DAG.getSetCC(DL, CCVT, LHS, RHS, ISD::SETUO),
                           DAG.getConstantFP(*FPNaN, DL, VT), MinMax, Flags);
  }

  // fminimum/fmaximum requires -0.0 less than +0.0
  if (!MinMaxMustRespectOrderedZero && !N->getFlags().hasNoSignedZeros() &&
      !DAG.isKnownNeverZeroFloat(RHS) && !DAG.isKnownNeverZeroFloat(LHS)) {
    SDValue IsZero = DAG.getSetCC(DL, CCVT, MinMax,
                                  DAG.getConstantFP(0.0, DL, VT), ISD::SETEQ);
    SDValue TestZero =
        DAG.getTargetConstant(IsMax ? fcPosZero : fcNegZero, DL, MVT::i32);
    SDValue LCmp = DAG.getSelect(
        DL, VT, DAG.getNode(ISD::IS_FPCLASS, DL, CCVT, LHS, TestZero), LHS,
        MinMax, Flags);
    SDValue RCmp = DAG.getSelect(
        DL, VT, DAG.getNode(ISD::IS_FPCLASS, DL, CCVT, RHS, TestZero), RHS,
        LCmp, Flags);
    MinMax = DAG.getSelect(DL, VT, IsZero, RCmp, MinMax, Flags);
  }

  return MinMax;
}

/// Returns a true value if if this FPClassTest can be performed with an ordered
/// fcmp to 0, and a false value if it's an unordered fcmp to 0. Returns
/// std::nullopt if it cannot be performed as a compare with 0.
static std::optional<bool> isFCmpEqualZero(FPClassTest Test,
                                           const fltSemantics &Semantics,
                                           const MachineFunction &MF) {
  FPClassTest OrderedMask = Test & ~fcNan;
  FPClassTest NanTest = Test & fcNan;
  bool IsOrdered = NanTest == fcNone;
  bool IsUnordered = NanTest == fcNan;

  // Skip cases that are testing for only a qnan or snan.
  if (!IsOrdered && !IsUnordered)
    return std::nullopt;

  if (OrderedMask == fcZero &&
      MF.getDenormalMode(Semantics).Input == DenormalMode::IEEE)
    return IsOrdered;
  if (OrderedMask == (fcZero | fcSubnormal) &&
      MF.getDenormalMode(Semantics).inputsAreZero())
    return IsOrdered;
  return std::nullopt;
}

SDValue TargetLowering::expandIS_FPCLASS(EVT ResultVT, SDValue Op,
                                         FPClassTest Test, SDNodeFlags Flags,
                                         const SDLoc &DL,
                                         SelectionDAG &DAG) const {
  EVT OperandVT = Op.getValueType();
  assert(OperandVT.isFloatingPoint());

  // Degenerated cases.
  if (Test == fcNone)
    return DAG.getBoolConstant(false, DL, ResultVT, OperandVT);
  if ((Test & fcAllFlags) == fcAllFlags)
    return DAG.getBoolConstant(true, DL, ResultVT, OperandVT);

  // PPC double double is a pair of doubles, of which the higher part determines
  // the value class.
  if (OperandVT == MVT::ppcf128) {
    Op = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::f64, Op,
                     DAG.getConstant(1, DL, MVT::i32));
    OperandVT = MVT::f64;
  }

  // Some checks may be represented as inversion of simpler check, for example
  // "inf|normal|subnormal|zero" => !"nan".
  bool IsInverted = false;
  if (FPClassTest InvertedCheck = invertFPClassTestIfSimpler(Test)) {
    IsInverted = true;
    Test = InvertedCheck;
  }

  // Floating-point type properties.
  EVT ScalarFloatVT = OperandVT.getScalarType();
  const Type *FloatTy = ScalarFloatVT.getTypeForEVT(*DAG.getContext());
  const llvm::fltSemantics &Semantics = FloatTy->getFltSemantics();
  bool IsF80 = (ScalarFloatVT == MVT::f80);

  // Some checks can be implemented using float comparisons, if floating point
  // exceptions are ignored.
  if (Flags.hasNoFPExcept() &&
      isOperationLegalOrCustom(ISD::SETCC, OperandVT.getScalarType())) {
    ISD::CondCode OrderedCmpOpcode = IsInverted ? ISD::SETUNE : ISD::SETOEQ;
    ISD::CondCode UnorderedCmpOpcode = IsInverted ? ISD::SETONE : ISD::SETUEQ;

    if (std::optional<bool> IsCmp0 =
            isFCmpEqualZero(Test, Semantics, DAG.getMachineFunction());
        IsCmp0 && (isCondCodeLegalOrCustom(
                      *IsCmp0 ? OrderedCmpOpcode : UnorderedCmpOpcode,
                      OperandVT.getScalarType().getSimpleVT()))) {

      // If denormals could be implicitly treated as 0, this is not equivalent
      // to a compare with 0 since it will also be true for denormals.
      return DAG.getSetCC(DL, ResultVT, Op,
                          DAG.getConstantFP(0.0, DL, OperandVT),
                          *IsCmp0 ? OrderedCmpOpcode : UnorderedCmpOpcode);
    }

    if (Test == fcNan &&
        isCondCodeLegalOrCustom(IsInverted ? ISD::SETO : ISD::SETUO,
                                OperandVT.getScalarType().getSimpleVT())) {
      return DAG.getSetCC(DL, ResultVT, Op, Op,
                          IsInverted ? ISD::SETO : ISD::SETUO);
    }

    if (Test == fcInf &&
        isCondCodeLegalOrCustom(IsInverted ? ISD::SETUNE : ISD::SETOEQ,
                                OperandVT.getScalarType().getSimpleVT()) &&
        isOperationLegalOrCustom(ISD::FABS, OperandVT.getScalarType())) {
      // isinf(x) --> fabs(x) == inf
      SDValue Abs = DAG.getNode(ISD::FABS, DL, OperandVT, Op);
      SDValue Inf =
          DAG.getConstantFP(APFloat::getInf(Semantics), DL, OperandVT);
      return DAG.getSetCC(DL, ResultVT, Abs, Inf,
                          IsInverted ? ISD::SETUNE : ISD::SETOEQ);
    }
  }

  // In the general case use integer operations.
  unsigned BitSize = OperandVT.getScalarSizeInBits();
  EVT IntVT = EVT::getIntegerVT(*DAG.getContext(), BitSize);
  if (OperandVT.isVector())
    IntVT = EVT::getVectorVT(*DAG.getContext(), IntVT,
                             OperandVT.getVectorElementCount());
  SDValue OpAsInt = DAG.getBitcast(IntVT, Op);

  // Various masks.
  APInt SignBit = APInt::getSignMask(BitSize);
  APInt ValueMask = APInt::getSignedMaxValue(BitSize);     // All bits but sign.
  APInt Inf = APFloat::getInf(Semantics).bitcastToAPInt(); // Exp and int bit.
  const unsigned ExplicitIntBitInF80 = 63;
  APInt ExpMask = Inf;
  if (IsF80)
    ExpMask.clearBit(ExplicitIntBitInF80);
  APInt AllOneMantissa = APFloat::getLargest(Semantics).bitcastToAPInt() & ~Inf;
  APInt QNaNBitMask =
      APInt::getOneBitSet(BitSize, AllOneMantissa.getActiveBits() - 1);
  APInt InvertionMask = APInt::getAllOnes(ResultVT.getScalarSizeInBits());

  SDValue ValueMaskV = DAG.getConstant(ValueMask, DL, IntVT);
  SDValue SignBitV = DAG.getConstant(SignBit, DL, IntVT);
  SDValue ExpMaskV = DAG.getConstant(ExpMask, DL, IntVT);
  SDValue ZeroV = DAG.getConstant(0, DL, IntVT);
  SDValue InfV = DAG.getConstant(Inf, DL, IntVT);
  SDValue ResultInvertionMask = DAG.getConstant(InvertionMask, DL, ResultVT);

  SDValue Res;
  const auto appendResult = [&](SDValue PartialRes) {
    if (PartialRes) {
      if (Res)
        Res = DAG.getNode(ISD::OR, DL, ResultVT, Res, PartialRes);
      else
        Res = PartialRes;
    }
  };

  SDValue IntBitIsSetV; // Explicit integer bit in f80 mantissa is set.
  const auto getIntBitIsSet = [&]() -> SDValue {
    if (!IntBitIsSetV) {
      APInt IntBitMask(BitSize, 0);
      IntBitMask.setBit(ExplicitIntBitInF80);
      SDValue IntBitMaskV = DAG.getConstant(IntBitMask, DL, IntVT);
      SDValue IntBitV = DAG.getNode(ISD::AND, DL, IntVT, OpAsInt, IntBitMaskV);
      IntBitIsSetV = DAG.getSetCC(DL, ResultVT, IntBitV, ZeroV, ISD::SETNE);
    }
    return IntBitIsSetV;
  };

  // Split the value into sign bit and absolute value.
  SDValue AbsV = DAG.getNode(ISD::AND, DL, IntVT, OpAsInt, ValueMaskV);
  SDValue SignV = DAG.getSetCC(DL, ResultVT, OpAsInt,
                               DAG.getConstant(0.0, DL, IntVT), ISD::SETLT);

  // Tests that involve more than one class should be processed first.
  SDValue PartialRes;

  if (IsF80)
    ; // Detect finite numbers of f80 by checking individual classes because
      // they have different settings of the explicit integer bit.
  else if ((Test & fcFinite) == fcFinite) {
    // finite(V) ==> abs(V) < exp_mask
    PartialRes = DAG.getSetCC(DL, ResultVT, AbsV, ExpMaskV, ISD::SETLT);
    Test &= ~fcFinite;
  } else if ((Test & fcFinite) == fcPosFinite) {
    // finite(V) && V > 0 ==> V < exp_mask
    PartialRes = DAG.getSetCC(DL, ResultVT, OpAsInt, ExpMaskV, ISD::SETULT);
    Test &= ~fcPosFinite;
  } else if ((Test & fcFinite) == fcNegFinite) {
    // finite(V) && V < 0 ==> abs(V) < exp_mask && signbit == 1
    PartialRes = DAG.getSetCC(DL, ResultVT, AbsV, ExpMaskV, ISD::SETLT);
    PartialRes = DAG.getNode(ISD::AND, DL, ResultVT, PartialRes, SignV);
    Test &= ~fcNegFinite;
  }
  appendResult(PartialRes);

  if (FPClassTest PartialCheck = Test & (fcZero | fcSubnormal)) {
    // fcZero | fcSubnormal => test all exponent bits are 0
    // TODO: Handle sign bit specific cases
    if (PartialCheck == (fcZero | fcSubnormal)) {
      SDValue ExpBits = DAG.getNode(ISD::AND, DL, IntVT, OpAsInt, ExpMaskV);
      SDValue ExpIsZero =
          DAG.getSetCC(DL, ResultVT, ExpBits, ZeroV, ISD::SETEQ);
      appendResult(ExpIsZero);
      Test &= ~PartialCheck & fcAllFlags;
    }
  }

  // Check for individual classes.

  if (unsigned PartialCheck = Test & fcZero) {
    if (PartialCheck == fcPosZero)
      PartialRes = DAG.getSetCC(DL, ResultVT, OpAsInt, ZeroV, ISD::SETEQ);
    else if (PartialCheck == fcZero)
      PartialRes = DAG.getSetCC(DL, ResultVT, AbsV, ZeroV, ISD::SETEQ);
    else // ISD::fcNegZero
      PartialRes = DAG.getSetCC(DL, ResultVT, OpAsInt, SignBitV, ISD::SETEQ);
    appendResult(PartialRes);
  }

  if (unsigned PartialCheck = Test & fcSubnormal) {
    // issubnormal(V) ==> unsigned(abs(V) - 1) < (all mantissa bits set)
    // issubnormal(V) && V>0 ==> unsigned(V - 1) < (all mantissa bits set)
    SDValue V = (PartialCheck == fcPosSubnormal) ? OpAsInt : AbsV;
    SDValue MantissaV = DAG.getConstant(AllOneMantissa, DL, IntVT);
    SDValue VMinusOneV =
        DAG.getNode(ISD::SUB, DL, IntVT, V, DAG.getConstant(1, DL, IntVT));
    PartialRes = DAG.getSetCC(DL, ResultVT, VMinusOneV, MantissaV, ISD::SETULT);
    if (PartialCheck == fcNegSubnormal)
      PartialRes = DAG.getNode(ISD::AND, DL, ResultVT, PartialRes, SignV);
    appendResult(PartialRes);
  }

  if (unsigned PartialCheck = Test & fcInf) {
    if (PartialCheck == fcPosInf)
      PartialRes = DAG.getSetCC(DL, ResultVT, OpAsInt, InfV, ISD::SETEQ);
    else if (PartialCheck == fcInf)
      PartialRes = DAG.getSetCC(DL, ResultVT, AbsV, InfV, ISD::SETEQ);
    else { // ISD::fcNegInf
      APInt NegInf = APFloat::getInf(Semantics, true).bitcastToAPInt();
      SDValue NegInfV = DAG.getConstant(NegInf, DL, IntVT);
      PartialRes = DAG.getSetCC(DL, ResultVT, OpAsInt, NegInfV, ISD::SETEQ);
    }
    appendResult(PartialRes);
  }

  if (unsigned PartialCheck = Test & fcNan) {
    APInt InfWithQnanBit = Inf | QNaNBitMask;
    SDValue InfWithQnanBitV = DAG.getConstant(InfWithQnanBit, DL, IntVT);
    if (PartialCheck == fcNan) {
      // isnan(V) ==> abs(V) > int(inf)
      PartialRes = DAG.getSetCC(DL, ResultVT, AbsV, InfV, ISD::SETGT);
      if (IsF80) {
        // Recognize unsupported values as NaNs for compatibility with glibc.
        // In them (exp(V)==0) == int_bit.
        SDValue ExpBits = DAG.getNode(ISD::AND, DL, IntVT, AbsV, ExpMaskV);
        SDValue ExpIsZero =
            DAG.getSetCC(DL, ResultVT, ExpBits, ZeroV, ISD::SETEQ);
        SDValue IsPseudo =
            DAG.getSetCC(DL, ResultVT, getIntBitIsSet(), ExpIsZero, ISD::SETEQ);
        PartialRes = DAG.getNode(ISD::OR, DL, ResultVT, PartialRes, IsPseudo);
      }
    } else if (PartialCheck == fcQNan) {
      // isquiet(V) ==> abs(V) >= (unsigned(Inf) | quiet_bit)
      PartialRes =
          DAG.getSetCC(DL, ResultVT, AbsV, InfWithQnanBitV, ISD::SETGE);
    } else { // ISD::fcSNan
      // issignaling(V) ==> abs(V) > unsigned(Inf) &&
      //                    abs(V) < (unsigned(Inf) | quiet_bit)
      SDValue IsNan = DAG.getSetCC(DL, ResultVT, AbsV, InfV, ISD::SETGT);
      SDValue IsNotQnan =
          DAG.getSetCC(DL, ResultVT, AbsV, InfWithQnanBitV, ISD::SETLT);
      PartialRes = DAG.getNode(ISD::AND, DL, ResultVT, IsNan, IsNotQnan);
    }
    appendResult(PartialRes);
  }

  if (unsigned PartialCheck = Test & fcNormal) {
    // isnormal(V) ==> (0 < exp < max_exp) ==> (unsigned(exp-1) < (max_exp-1))
    APInt ExpLSB = ExpMask & ~(ExpMask.shl(1));
    SDValue ExpLSBV = DAG.getConstant(ExpLSB, DL, IntVT);
    SDValue ExpMinus1 = DAG.getNode(ISD::SUB, DL, IntVT, AbsV, ExpLSBV);
    APInt ExpLimit = ExpMask - ExpLSB;
    SDValue ExpLimitV = DAG.getConstant(ExpLimit, DL, IntVT);
    PartialRes = DAG.getSetCC(DL, ResultVT, ExpMinus1, ExpLimitV, ISD::SETULT);
    if (PartialCheck == fcNegNormal)
      PartialRes = DAG.getNode(ISD::AND, DL, ResultVT, PartialRes, SignV);
    else if (PartialCheck == fcPosNormal) {
      SDValue PosSignV =
          DAG.getNode(ISD::XOR, DL, ResultVT, SignV, ResultInvertionMask);
      PartialRes = DAG.getNode(ISD::AND, DL, ResultVT, PartialRes, PosSignV);
    }
    if (IsF80)
      PartialRes =
          DAG.getNode(ISD::AND, DL, ResultVT, PartialRes, getIntBitIsSet());
    appendResult(PartialRes);
  }

  if (!Res)
    return DAG.getConstant(IsInverted, DL, ResultVT);
  if (IsInverted)
    Res = DAG.getNode(ISD::XOR, DL, ResultVT, Res, ResultInvertionMask);
  return Res;
}

// Only expand vector types if we have the appropriate vector bit operations.
static bool canExpandVectorCTPOP(const TargetLowering &TLI, EVT VT) {
  assert(VT.isVector() && "Expected vector type");
  unsigned Len = VT.getScalarSizeInBits();
  return TLI.isOperationLegalOrCustom(ISD::ADD, VT) &&
         TLI.isOperationLegalOrCustom(ISD::SUB, VT) &&
         TLI.isOperationLegalOrCustom(ISD::SRL, VT) &&
         (Len == 8 || TLI.isOperationLegalOrCustom(ISD::MUL, VT)) &&
         TLI.isOperationLegalOrCustomOrPromote(ISD::AND, VT);
}

SDValue TargetLowering::expandCTPOP(SDNode *Node, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
  SDValue Op = Node->getOperand(0);
  unsigned Len = VT.getScalarSizeInBits();
  assert(VT.isInteger() && "CTPOP not implemented for this type.");

  // TODO: Add support for irregular type lengths.
  if (!(Len <= 128 && Len % 8 == 0))
    return SDValue();

  // Only expand vector types if we have the appropriate vector bit operations.
  if (VT.isVector() && !canExpandVectorCTPOP(*this, VT))
    return SDValue();

  // This is the "best" algorithm from
  // http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
  SDValue Mask55 =
      DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x55)), dl, VT);
  SDValue Mask33 =
      DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x33)), dl, VT);
  SDValue Mask0F =
      DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x0F)), dl, VT);

  // v = v - ((v >> 1) & 0x55555555...)
  Op = DAG.getNode(ISD::SUB, dl, VT, Op,
                   DAG.getNode(ISD::AND, dl, VT,
                               DAG.getNode(ISD::SRL, dl, VT, Op,
                                           DAG.getConstant(1, dl, ShVT)),
                               Mask55));
  // v = (v & 0x33333333...) + ((v >> 2) & 0x33333333...)
  Op = DAG.getNode(ISD::ADD, dl, VT, DAG.getNode(ISD::AND, dl, VT, Op, Mask33),
                   DAG.getNode(ISD::AND, dl, VT,
                               DAG.getNode(ISD::SRL, dl, VT, Op,
                                           DAG.getConstant(2, dl, ShVT)),
                               Mask33));
  // v = (v + (v >> 4)) & 0x0F0F0F0F...
  Op = DAG.getNode(ISD::AND, dl, VT,
                   DAG.getNode(ISD::ADD, dl, VT, Op,
                               DAG.getNode(ISD::SRL, dl, VT, Op,
                                           DAG.getConstant(4, dl, ShVT))),
                   Mask0F);

  if (Len <= 8)
    return Op;

  // Avoid the multiply if we only have 2 bytes to add.
  // TODO: Only doing this for scalars because vectors weren't as obviously
  // improved.
  if (Len == 16 && !VT.isVector()) {
    // v = (v + (v >> 8)) & 0x00FF;
    return DAG.getNode(ISD::AND, dl, VT,
                     DAG.getNode(ISD::ADD, dl, VT, Op,
                                 DAG.getNode(ISD::SRL, dl, VT, Op,
                                             DAG.getConstant(8, dl, ShVT))),
                     DAG.getConstant(0xFF, dl, VT));
  }

  // v = (v * 0x01010101...) >> (Len - 8)
  SDValue V;
  if (isOperationLegalOrCustomOrPromote(
          ISD::MUL, getTypeToTransformTo(*DAG.getContext(), VT))) {
    SDValue Mask01 =
        DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x01)), dl, VT);
    V = DAG.getNode(ISD::MUL, dl, VT, Op, Mask01);
  } else {
    V = Op;
    for (unsigned Shift = 8; Shift < Len; Shift *= 2) {
      SDValue ShiftC = DAG.getShiftAmountConstant(Shift, VT, dl);
      V = DAG.getNode(ISD::ADD, dl, VT, V,
                      DAG.getNode(ISD::SHL, dl, VT, V, ShiftC));
    }
  }
  return DAG.getNode(ISD::SRL, dl, VT, V, DAG.getConstant(Len - 8, dl, ShVT));
}

SDValue TargetLowering::expandVPCTPOP(SDNode *Node, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
  SDValue Op = Node->getOperand(0);
  SDValue Mask = Node->getOperand(1);
  SDValue VL = Node->getOperand(2);
  unsigned Len = VT.getScalarSizeInBits();
  assert(VT.isInteger() && "VP_CTPOP not implemented for this type.");

  // TODO: Add support for irregular type lengths.
  if (!(Len <= 128 && Len % 8 == 0))
    return SDValue();

  // This is same algorithm of expandCTPOP from
  // http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
  SDValue Mask55 =
      DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x55)), dl, VT);
  SDValue Mask33 =
      DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x33)), dl, VT);
  SDValue Mask0F =
      DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x0F)), dl, VT);

  SDValue Tmp1, Tmp2, Tmp3, Tmp4, Tmp5;

  // v = v - ((v >> 1) & 0x55555555...)
  Tmp1 = DAG.getNode(ISD::VP_AND, dl, VT,
                     DAG.getNode(ISD::VP_SRL, dl, VT, Op,
                                 DAG.getConstant(1, dl, ShVT), Mask, VL),
                     Mask55, Mask, VL);
  Op = DAG.getNode(ISD::VP_SUB, dl, VT, Op, Tmp1, Mask, VL);

  // v = (v & 0x33333333...) + ((v >> 2) & 0x33333333...)
  Tmp2 = DAG.getNode(ISD::VP_AND, dl, VT, Op, Mask33, Mask, VL);
  Tmp3 = DAG.getNode(ISD::VP_AND, dl, VT,
                     DAG.getNode(ISD::VP_SRL, dl, VT, Op,
                                 DAG.getConstant(2, dl, ShVT), Mask, VL),
                     Mask33, Mask, VL);
  Op = DAG.getNode(ISD::VP_ADD, dl, VT, Tmp2, Tmp3, Mask, VL);

  // v = (v + (v >> 4)) & 0x0F0F0F0F...
  Tmp4 = DAG.getNode(ISD::VP_SRL, dl, VT, Op, DAG.getConstant(4, dl, ShVT),
                     Mask, VL),
  Tmp5 = DAG.getNode(ISD::VP_ADD, dl, VT, Op, Tmp4, Mask, VL);
  Op = DAG.getNode(ISD::VP_AND, dl, VT, Tmp5, Mask0F, Mask, VL);

  if (Len <= 8)
    return Op;

  // v = (v * 0x01010101...) >> (Len - 8)
  SDValue V;
  if (isOperationLegalOrCustomOrPromote(
          ISD::VP_MUL, getTypeToTransformTo(*DAG.getContext(), VT))) {
    SDValue Mask01 =
        DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x01)), dl, VT);
    V = DAG.getNode(ISD::VP_MUL, dl, VT, Op, Mask01, Mask, VL);
  } else {
    V = Op;
    for (unsigned Shift = 8; Shift < Len; Shift *= 2) {
      SDValue ShiftC = DAG.getShiftAmountConstant(Shift, VT, dl);
      V = DAG.getNode(ISD::VP_ADD, dl, VT, V,
                      DAG.getNode(ISD::VP_SHL, dl, VT, V, ShiftC, Mask, VL),
                      Mask, VL);
    }
  }
  return DAG.getNode(ISD::VP_SRL, dl, VT, V, DAG.getConstant(Len - 8, dl, ShVT),
                     Mask, VL);
}

SDValue TargetLowering::expandCTLZ(SDNode *Node, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
  SDValue Op = Node->getOperand(0);
  unsigned NumBitsPerElt = VT.getScalarSizeInBits();

  // If the non-ZERO_UNDEF version is supported we can use that instead.
  if (Node->getOpcode() == ISD::CTLZ_ZERO_UNDEF &&
      isOperationLegalOrCustom(ISD::CTLZ, VT))
    return DAG.getNode(ISD::CTLZ, dl, VT, Op);

  // If the ZERO_UNDEF version is supported use that and handle the zero case.
  if (isOperationLegalOrCustom(ISD::CTLZ_ZERO_UNDEF, VT)) {
    EVT SetCCVT =
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    SDValue CTLZ = DAG.getNode(ISD::CTLZ_ZERO_UNDEF, dl, VT, Op);
    SDValue Zero = DAG.getConstant(0, dl, VT);
    SDValue SrcIsZero = DAG.getSetCC(dl, SetCCVT, Op, Zero, ISD::SETEQ);
    return DAG.getSelect(dl, VT, SrcIsZero,
                         DAG.getConstant(NumBitsPerElt, dl, VT), CTLZ);
  }

  // Only expand vector types if we have the appropriate vector bit operations.
  // This includes the operations needed to expand CTPOP if it isn't supported.
  if (VT.isVector() && (!isPowerOf2_32(NumBitsPerElt) ||
                        (!isOperationLegalOrCustom(ISD::CTPOP, VT) &&
                         !canExpandVectorCTPOP(*this, VT)) ||
                        !isOperationLegalOrCustom(ISD::SRL, VT) ||
                        !isOperationLegalOrCustomOrPromote(ISD::OR, VT)))
    return SDValue();

  // for now, we do this:
  // x = x | (x >> 1);
  // x = x | (x >> 2);
  // ...
  // x = x | (x >>16);
  // x = x | (x >>32); // for 64-bit input
  // return popcount(~x);
  //
  // Ref: "Hacker's Delight" by Henry Warren
  for (unsigned i = 0; (1U << i) < NumBitsPerElt; ++i) {
    SDValue Tmp = DAG.getConstant(1ULL << i, dl, ShVT);
    Op = DAG.getNode(ISD::OR, dl, VT, Op,
                     DAG.getNode(ISD::SRL, dl, VT, Op, Tmp));
  }
  Op = DAG.getNOT(dl, Op, VT);
  return DAG.getNode(ISD::CTPOP, dl, VT, Op);
}

SDValue TargetLowering::expandVPCTLZ(SDNode *Node, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
  SDValue Op = Node->getOperand(0);
  SDValue Mask = Node->getOperand(1);
  SDValue VL = Node->getOperand(2);
  unsigned NumBitsPerElt = VT.getScalarSizeInBits();

  // do this:
  // x = x | (x >> 1);
  // x = x | (x >> 2);
  // ...
  // x = x | (x >>16);
  // x = x | (x >>32); // for 64-bit input
  // return popcount(~x);
  for (unsigned i = 0; (1U << i) < NumBitsPerElt; ++i) {
    SDValue Tmp = DAG.getConstant(1ULL << i, dl, ShVT);
    Op = DAG.getNode(ISD::VP_OR, dl, VT, Op,
                     DAG.getNode(ISD::VP_SRL, dl, VT, Op, Tmp, Mask, VL), Mask,
                     VL);
  }
  Op = DAG.getNode(ISD::VP_XOR, dl, VT, Op, DAG.getConstant(-1, dl, VT), Mask,
                   VL);
  return DAG.getNode(ISD::VP_CTPOP, dl, VT, Op, Mask, VL);
}

SDValue TargetLowering::CTTZTableLookup(SDNode *Node, SelectionDAG &DAG,
                                        const SDLoc &DL, EVT VT, SDValue Op,
                                        unsigned BitWidth) const {
  if (BitWidth != 32 && BitWidth != 64)
    return SDValue();
  APInt DeBruijn = BitWidth == 32 ? APInt(32, 0x077CB531U)
                                  : APInt(64, 0x0218A392CD3D5DBFULL);
  const DataLayout &TD = DAG.getDataLayout();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction());
  unsigned ShiftAmt = BitWidth - Log2_32(BitWidth);
  SDValue Neg = DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), Op);
  SDValue Lookup = DAG.getNode(
      ISD::SRL, DL, VT,
      DAG.getNode(ISD::MUL, DL, VT, DAG.getNode(ISD::AND, DL, VT, Op, Neg),
                  DAG.getConstant(DeBruijn, DL, VT)),
      DAG.getConstant(ShiftAmt, DL, VT));
  Lookup = DAG.getSExtOrTrunc(Lookup, DL, getPointerTy(TD));

  SmallVector<uint8_t> Table(BitWidth, 0);
  for (unsigned i = 0; i < BitWidth; i++) {
    APInt Shl = DeBruijn.shl(i);
    APInt Lshr = Shl.lshr(ShiftAmt);
    Table[Lshr.getZExtValue()] = i;
  }

  // Create a ConstantArray in Constant Pool
  auto *CA = ConstantDataArray::get(*DAG.getContext(), Table);
  SDValue CPIdx = DAG.getConstantPool(CA, getPointerTy(TD),
                                      TD.getPrefTypeAlign(CA->getType()));
  SDValue ExtLoad = DAG.getExtLoad(ISD::ZEXTLOAD, DL, VT, DAG.getEntryNode(),
                                   DAG.getMemBasePlusOffset(CPIdx, Lookup, DL),
                                   PtrInfo, MVT::i8);
  if (Node->getOpcode() == ISD::CTTZ_ZERO_UNDEF)
    return ExtLoad;

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue SrcIsZero = DAG.getSetCC(DL, SetCCVT, Op, Zero, ISD::SETEQ);
  return DAG.getSelect(DL, VT, SrcIsZero,
                       DAG.getConstant(BitWidth, DL, VT), ExtLoad);
}

SDValue TargetLowering::expandCTTZ(SDNode *Node, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  SDValue Op = Node->getOperand(0);
  unsigned NumBitsPerElt = VT.getScalarSizeInBits();

  // If the non-ZERO_UNDEF version is supported we can use that instead.
  if (Node->getOpcode() == ISD::CTTZ_ZERO_UNDEF &&
      isOperationLegalOrCustom(ISD::CTTZ, VT))
    return DAG.getNode(ISD::CTTZ, dl, VT, Op);

  // If the ZERO_UNDEF version is supported use that and handle the zero case.
  if (isOperationLegalOrCustom(ISD::CTTZ_ZERO_UNDEF, VT)) {
    EVT SetCCVT =
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    SDValue CTTZ = DAG.getNode(ISD::CTTZ_ZERO_UNDEF, dl, VT, Op);
    SDValue Zero = DAG.getConstant(0, dl, VT);
    SDValue SrcIsZero = DAG.getSetCC(dl, SetCCVT, Op, Zero, ISD::SETEQ);
    return DAG.getSelect(dl, VT, SrcIsZero,
                         DAG.getConstant(NumBitsPerElt, dl, VT), CTTZ);
  }

  // Only expand vector types if we have the appropriate vector bit operations.
  // This includes the operations needed to expand CTPOP if it isn't supported.
  if (VT.isVector() && (!isPowerOf2_32(NumBitsPerElt) ||
                        (!isOperationLegalOrCustom(ISD::CTPOP, VT) &&
                         !isOperationLegalOrCustom(ISD::CTLZ, VT) &&
                         !canExpandVectorCTPOP(*this, VT)) ||
                        !isOperationLegalOrCustom(ISD::SUB, VT) ||
                        !isOperationLegalOrCustomOrPromote(ISD::AND, VT) ||
                        !isOperationLegalOrCustomOrPromote(ISD::XOR, VT)))
    return SDValue();

  // Emit Table Lookup if ISD::CTLZ and ISD::CTPOP are not legal.
  if (!VT.isVector() && isOperationExpand(ISD::CTPOP, VT) &&
      !isOperationLegal(ISD::CTLZ, VT))
    if (SDValue V = CTTZTableLookup(Node, DAG, dl, VT, Op, NumBitsPerElt))
      return V;

  // for now, we use: { return popcount(~x & (x - 1)); }
  // unless the target has ctlz but not ctpop, in which case we use:
  // { return 32 - nlz(~x & (x-1)); }
  // Ref: "Hacker's Delight" by Henry Warren
  SDValue Tmp = DAG.getNode(
      ISD::AND, dl, VT, DAG.getNOT(dl, Op, VT),
      DAG.getNode(ISD::SUB, dl, VT, Op, DAG.getConstant(1, dl, VT)));

  // If ISD::CTLZ is legal and CTPOP isn't, then do that instead.
  if (isOperationLegal(ISD::CTLZ, VT) && !isOperationLegal(ISD::CTPOP, VT)) {
    return DAG.getNode(ISD::SUB, dl, VT, DAG.getConstant(NumBitsPerElt, dl, VT),
                       DAG.getNode(ISD::CTLZ, dl, VT, Tmp));
  }

  return DAG.getNode(ISD::CTPOP, dl, VT, Tmp);
}

SDValue TargetLowering::expandVPCTTZ(SDNode *Node, SelectionDAG &DAG) const {
  SDValue Op = Node->getOperand(0);
  SDValue Mask = Node->getOperand(1);
  SDValue VL = Node->getOperand(2);
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);

  // Same as the vector part of expandCTTZ, use: popcount(~x & (x - 1))
  SDValue Not = DAG.getNode(ISD::VP_XOR, dl, VT, Op,
                            DAG.getConstant(-1, dl, VT), Mask, VL);
  SDValue MinusOne = DAG.getNode(ISD::VP_SUB, dl, VT, Op,
                                 DAG.getConstant(1, dl, VT), Mask, VL);
  SDValue Tmp = DAG.getNode(ISD::VP_AND, dl, VT, Not, MinusOne, Mask, VL);
  return DAG.getNode(ISD::VP_CTPOP, dl, VT, Tmp, Mask, VL);
}

SDValue TargetLowering::expandVPCTTZElements(SDNode *N,
                                             SelectionDAG &DAG) const {
  // %cond = to_bool_vec %source
  // %splat = splat /*val=*/VL
  // %tz = step_vector
  // %v = vp.select %cond, /*true=*/tz, /*false=*/%splat
  // %r = vp.reduce.umin %v
  SDLoc DL(N);
  SDValue Source = N->getOperand(0);
  SDValue Mask = N->getOperand(1);
  SDValue EVL = N->getOperand(2);
  EVT SrcVT = Source.getValueType();
  EVT ResVT = N->getValueType(0);
  EVT ResVecVT =
      EVT::getVectorVT(*DAG.getContext(), ResVT, SrcVT.getVectorElementCount());

  // Convert to boolean vector.
  if (SrcVT.getScalarType() != MVT::i1) {
    SDValue AllZero = DAG.getConstant(0, DL, SrcVT);
    SrcVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1,
                             SrcVT.getVectorElementCount());
    Source = DAG.getNode(ISD::VP_SETCC, DL, SrcVT, Source, AllZero,
                         DAG.getCondCode(ISD::SETNE), Mask, EVL);
  }

  SDValue ExtEVL = DAG.getZExtOrTrunc(EVL, DL, ResVT);
  SDValue Splat = DAG.getSplat(ResVecVT, DL, ExtEVL);
  SDValue StepVec = DAG.getStepVector(DL, ResVecVT);
  SDValue Select =
      DAG.getNode(ISD::VP_SELECT, DL, ResVecVT, Source, StepVec, Splat, EVL);
  return DAG.getNode(ISD::VP_REDUCE_UMIN, DL, ResVT, ExtEVL, Select, Mask, EVL);
}

SDValue TargetLowering::expandABS(SDNode *N, SelectionDAG &DAG,
                                  bool IsNegative) const {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Op = N->getOperand(0);

  // abs(x) -> smax(x,sub(0,x))
  if (!IsNegative && isOperationLegal(ISD::SUB, VT) &&
      isOperationLegal(ISD::SMAX, VT)) {
    SDValue Zero = DAG.getConstant(0, dl, VT);
    Op = DAG.getFreeze(Op);
    return DAG.getNode(ISD::SMAX, dl, VT, Op,
                       DAG.getNode(ISD::SUB, dl, VT, Zero, Op));
  }

  // abs(x) -> umin(x,sub(0,x))
  if (!IsNegative && isOperationLegal(ISD::SUB, VT) &&
      isOperationLegal(ISD::UMIN, VT)) {
    SDValue Zero = DAG.getConstant(0, dl, VT);
    Op = DAG.getFreeze(Op);
    return DAG.getNode(ISD::UMIN, dl, VT, Op,
                       DAG.getNode(ISD::SUB, dl, VT, Zero, Op));
  }

  // 0 - abs(x) -> smin(x, sub(0,x))
  if (IsNegative && isOperationLegal(ISD::SUB, VT) &&
      isOperationLegal(ISD::SMIN, VT)) {
    SDValue Zero = DAG.getConstant(0, dl, VT);
    Op = DAG.getFreeze(Op);
    return DAG.getNode(ISD::SMIN, dl, VT, Op,
                       DAG.getNode(ISD::SUB, dl, VT, Zero, Op));
  }

  // Only expand vector types if we have the appropriate vector operations.
  if (VT.isVector() &&
      (!isOperationLegalOrCustom(ISD::SRA, VT) ||
       (!IsNegative && !isOperationLegalOrCustom(ISD::ADD, VT)) ||
       (IsNegative && !isOperationLegalOrCustom(ISD::SUB, VT)) ||
       !isOperationLegalOrCustomOrPromote(ISD::XOR, VT)))
    return SDValue();

  Op = DAG.getFreeze(Op);
  SDValue Shift = DAG.getNode(
      ISD::SRA, dl, VT, Op,
      DAG.getShiftAmountConstant(VT.getScalarSizeInBits() - 1, VT, dl));
  SDValue Xor = DAG.getNode(ISD::XOR, dl, VT, Op, Shift);

  // abs(x) -> Y = sra (X, size(X)-1); sub (xor (X, Y), Y)
  if (!IsNegative)
    return DAG.getNode(ISD::SUB, dl, VT, Xor, Shift);

  // 0 - abs(x) -> Y = sra (X, size(X)-1); sub (Y, xor (X, Y))
  return DAG.getNode(ISD::SUB, dl, VT, Shift, Xor);
}

SDValue TargetLowering::expandABD(SDNode *N, SelectionDAG &DAG) const {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue LHS = DAG.getFreeze(N->getOperand(0));
  SDValue RHS = DAG.getFreeze(N->getOperand(1));
  bool IsSigned = N->getOpcode() == ISD::ABDS;

  // abds(lhs, rhs) -> sub(smax(lhs,rhs), smin(lhs,rhs))
  // abdu(lhs, rhs) -> sub(umax(lhs,rhs), umin(lhs,rhs))
  unsigned MaxOpc = IsSigned ? ISD::SMAX : ISD::UMAX;
  unsigned MinOpc = IsSigned ? ISD::SMIN : ISD::UMIN;
  if (isOperationLegal(MaxOpc, VT) && isOperationLegal(MinOpc, VT)) {
    SDValue Max = DAG.getNode(MaxOpc, dl, VT, LHS, RHS);
    SDValue Min = DAG.getNode(MinOpc, dl, VT, LHS, RHS);
    return DAG.getNode(ISD::SUB, dl, VT, Max, Min);
  }

  // abdu(lhs, rhs) -> or(usubsat(lhs,rhs), usubsat(rhs,lhs))
  if (!IsSigned && isOperationLegal(ISD::USUBSAT, VT))
    return DAG.getNode(ISD::OR, dl, VT,
                       DAG.getNode(ISD::USUBSAT, dl, VT, LHS, RHS),
                       DAG.getNode(ISD::USUBSAT, dl, VT, RHS, LHS));

  EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  ISD::CondCode CC = IsSigned ? ISD::CondCode::SETGT : ISD::CondCode::SETUGT;
  SDValue Cmp = DAG.getSetCC(dl, CCVT, LHS, RHS, CC);

  // Branchless expansion iff cmp result is allbits:
  // abds(lhs, rhs) -> sub(sgt(lhs, rhs), xor(sgt(lhs, rhs), sub(lhs, rhs)))
  // abdu(lhs, rhs) -> sub(ugt(lhs, rhs), xor(ugt(lhs, rhs), sub(lhs, rhs)))
  if (CCVT == VT && getBooleanContents(VT) == ZeroOrNegativeOneBooleanContent) {
    SDValue Diff = DAG.getNode(ISD::SUB, dl, VT, LHS, RHS);
    SDValue Xor = DAG.getNode(ISD::XOR, dl, VT, Diff, Cmp);
    return DAG.getNode(ISD::SUB, dl, VT, Cmp, Xor);
  }

  // abds(lhs, rhs) -> select(sgt(lhs,rhs), sub(lhs,rhs), sub(rhs,lhs))
  // abdu(lhs, rhs) -> select(ugt(lhs,rhs), sub(lhs,rhs), sub(rhs,lhs))
  return DAG.getSelect(dl, VT, Cmp, DAG.getNode(ISD::SUB, dl, VT, LHS, RHS),
                       DAG.getNode(ISD::SUB, dl, VT, RHS, LHS));
}

SDValue TargetLowering::expandAVG(SDNode *N, SelectionDAG &DAG) const {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  unsigned Opc = N->getOpcode();
  bool IsFloor = Opc == ISD::AVGFLOORS || Opc == ISD::AVGFLOORU;
  bool IsSigned = Opc == ISD::AVGCEILS || Opc == ISD::AVGFLOORS;
  unsigned SumOpc = IsFloor ? ISD::ADD : ISD::SUB;
  unsigned SignOpc = IsFloor ? ISD::AND : ISD::OR;
  unsigned ShiftOpc = IsSigned ? ISD::SRA : ISD::SRL;
  unsigned ExtOpc = IsSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
  assert((Opc == ISD::AVGFLOORS || Opc == ISD::AVGCEILS ||
          Opc == ISD::AVGFLOORU || Opc == ISD::AVGCEILU) &&
         "Unknown AVG node");

  // If the operands are already extended, we can add+shift.
  bool IsExt =
      (IsSigned && DAG.ComputeNumSignBits(LHS) >= 2 &&
       DAG.ComputeNumSignBits(RHS) >= 2) ||
      (!IsSigned && DAG.computeKnownBits(LHS).countMinLeadingZeros() >= 1 &&
       DAG.computeKnownBits(RHS).countMinLeadingZeros() >= 1);
  if (IsExt) {
    SDValue Sum = DAG.getNode(ISD::ADD, dl, VT, LHS, RHS);
    if (!IsFloor)
      Sum = DAG.getNode(ISD::ADD, dl, VT, Sum, DAG.getConstant(1, dl, VT));
    return DAG.getNode(ShiftOpc, dl, VT, Sum,
                       DAG.getShiftAmountConstant(1, VT, dl));
  }

  // For scalars, see if we can efficiently extend/truncate to use add+shift.
  if (VT.isScalarInteger()) {
    unsigned BW = VT.getScalarSizeInBits();
    EVT ExtVT = VT.getIntegerVT(*DAG.getContext(), 2 * BW);
    if (isTypeLegal(ExtVT) && isTruncateFree(ExtVT, VT)) {
      LHS = DAG.getNode(ExtOpc, dl, ExtVT, LHS);
      RHS = DAG.getNode(ExtOpc, dl, ExtVT, RHS);
      SDValue Avg = DAG.getNode(ISD::ADD, dl, ExtVT, LHS, RHS);
      if (!IsFloor)
        Avg = DAG.getNode(ISD::ADD, dl, ExtVT, Avg,
                          DAG.getConstant(1, dl, ExtVT));
      // Just use SRL as we will be truncating away the extended sign bits.
      Avg = DAG.getNode(ISD::SRL, dl, ExtVT, Avg,
                        DAG.getShiftAmountConstant(1, ExtVT, dl));
      return DAG.getNode(ISD::TRUNCATE, dl, VT, Avg);
    }
  }

  // avgceils(lhs, rhs) -> sub(or(lhs,rhs),ashr(xor(lhs,rhs),1))
  // avgceilu(lhs, rhs) -> sub(or(lhs,rhs),lshr(xor(lhs,rhs),1))
  // avgfloors(lhs, rhs) -> add(and(lhs,rhs),ashr(xor(lhs,rhs),1))
  // avgflooru(lhs, rhs) -> add(and(lhs,rhs),lshr(xor(lhs,rhs),1))
  LHS = DAG.getFreeze(LHS);
  RHS = DAG.getFreeze(RHS);
  SDValue Sign = DAG.getNode(SignOpc, dl, VT, LHS, RHS);
  SDValue Xor = DAG.getNode(ISD::XOR, dl, VT, LHS, RHS);
  SDValue Shift =
      DAG.getNode(ShiftOpc, dl, VT, Xor, DAG.getShiftAmountConstant(1, VT, dl));
  return DAG.getNode(SumOpc, dl, VT, Sign, Shift);
}

SDValue TargetLowering::expandBSWAP(SDNode *N, SelectionDAG &DAG) const {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Op = N->getOperand(0);

  if (!VT.isSimple())
    return SDValue();

  EVT SHVT = getShiftAmountTy(VT, DAG.getDataLayout());
  SDValue Tmp1, Tmp2, Tmp3, Tmp4, Tmp5, Tmp6, Tmp7, Tmp8;
  switch (VT.getSimpleVT().getScalarType().SimpleTy) {
  default:
    return SDValue();
  case MVT::i16:
    // Use a rotate by 8. This can be further expanded if necessary.
    return DAG.getNode(ISD::ROTL, dl, VT, Op, DAG.getConstant(8, dl, SHVT));
  case MVT::i32:
    Tmp4 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(24, dl, SHVT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Op,
                       DAG.getConstant(0xFF00, dl, VT));
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, Tmp3, DAG.getConstant(8, dl, SHVT));
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(8, dl, SHVT));
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2, DAG.getConstant(0xFF00, dl, VT));
    Tmp1 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(24, dl, SHVT));
    Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp3);
    Tmp2 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp1);
    return DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp2);
  case MVT::i64:
    Tmp8 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(56, dl, SHVT));
    Tmp7 = DAG.getNode(ISD::AND, dl, VT, Op,
                       DAG.getConstant(255ULL<<8, dl, VT));
    Tmp7 = DAG.getNode(ISD::SHL, dl, VT, Tmp7, DAG.getConstant(40, dl, SHVT));
    Tmp6 = DAG.getNode(ISD::AND, dl, VT, Op,
                       DAG.getConstant(255ULL<<16, dl, VT));
    Tmp6 = DAG.getNode(ISD::SHL, dl, VT, Tmp6, DAG.getConstant(24, dl, SHVT));
    Tmp5 = DAG.getNode(ISD::AND, dl, VT, Op,
                       DAG.getConstant(255ULL<<24, dl, VT));
    Tmp5 = DAG.getNode(ISD::SHL, dl, VT, Tmp5, DAG.getConstant(8, dl, SHVT));
    Tmp4 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(8, dl, SHVT));
    Tmp4 = DAG.getNode(ISD::AND, dl, VT, Tmp4,
                       DAG.getConstant(255ULL<<24, dl, VT));
    Tmp3 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(24, dl, SHVT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp3,
                       DAG.getConstant(255ULL<<16, dl, VT));
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(40, dl, SHVT));
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2,
                       DAG.getConstant(255ULL<<8, dl, VT));
    Tmp1 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(56, dl, SHVT));
    Tmp8 = DAG.getNode(ISD::OR, dl, VT, Tmp8, Tmp7);
    Tmp6 = DAG.getNode(ISD::OR, dl, VT, Tmp6, Tmp5);
    Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp3);
    Tmp2 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp1);
    Tmp8 = DAG.getNode(ISD::OR, dl, VT, Tmp8, Tmp6);
    Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp2);
    return DAG.getNode(ISD::OR, dl, VT, Tmp8, Tmp4);
  }
}

SDValue TargetLowering::expandVPBSWAP(SDNode *N, SelectionDAG &DAG) const {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Op = N->getOperand(0);
  SDValue Mask = N->getOperand(1);
  SDValue EVL = N->getOperand(2);

  if (!VT.isSimple())
    return SDValue();

  EVT SHVT = getShiftAmountTy(VT, DAG.getDataLayout());
  SDValue Tmp1, Tmp2, Tmp3, Tmp4, Tmp5, Tmp6, Tmp7, Tmp8;
  switch (VT.getSimpleVT().getScalarType().SimpleTy) {
  default:
    return SDValue();
  case MVT::i16:
    Tmp1 = DAG.getNode(ISD::VP_SHL, dl, VT, Op, DAG.getConstant(8, dl, SHVT),
                       Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_SRL, dl, VT, Op, DAG.getConstant(8, dl, SHVT),
                       Mask, EVL);
    return DAG.getNode(ISD::VP_OR, dl, VT, Tmp1, Tmp2, Mask, EVL);
  case MVT::i32:
    Tmp4 = DAG.getNode(ISD::VP_SHL, dl, VT, Op, DAG.getConstant(24, dl, SHVT),
                       Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_AND, dl, VT, Op, DAG.getConstant(0xFF00, dl, VT),
                       Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_SHL, dl, VT, Tmp3, DAG.getConstant(8, dl, SHVT),
                       Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_SRL, dl, VT, Op, DAG.getConstant(8, dl, SHVT),
                       Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp2,
                       DAG.getConstant(0xFF00, dl, VT), Mask, EVL);
    Tmp1 = DAG.getNode(ISD::VP_SRL, dl, VT, Op, DAG.getConstant(24, dl, SHVT),
                       Mask, EVL);
    Tmp4 = DAG.getNode(ISD::VP_OR, dl, VT, Tmp4, Tmp3, Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_OR, dl, VT, Tmp2, Tmp1, Mask, EVL);
    return DAG.getNode(ISD::VP_OR, dl, VT, Tmp4, Tmp2, Mask, EVL);
  case MVT::i64:
    Tmp8 = DAG.getNode(ISD::VP_SHL, dl, VT, Op, DAG.getConstant(56, dl, SHVT),
                       Mask, EVL);
    Tmp7 = DAG.getNode(ISD::VP_AND, dl, VT, Op,
                       DAG.getConstant(255ULL << 8, dl, VT), Mask, EVL);
    Tmp7 = DAG.getNode(ISD::VP_SHL, dl, VT, Tmp7, DAG.getConstant(40, dl, SHVT),
                       Mask, EVL);
    Tmp6 = DAG.getNode(ISD::VP_AND, dl, VT, Op,
                       DAG.getConstant(255ULL << 16, dl, VT), Mask, EVL);
    Tmp6 = DAG.getNode(ISD::VP_SHL, dl, VT, Tmp6, DAG.getConstant(24, dl, SHVT),
                       Mask, EVL);
    Tmp5 = DAG.getNode(ISD::VP_AND, dl, VT, Op,
                       DAG.getConstant(255ULL << 24, dl, VT), Mask, EVL);
    Tmp5 = DAG.getNode(ISD::VP_SHL, dl, VT, Tmp5, DAG.getConstant(8, dl, SHVT),
                       Mask, EVL);
    Tmp4 = DAG.getNode(ISD::VP_SRL, dl, VT, Op, DAG.getConstant(8, dl, SHVT),
                       Mask, EVL);
    Tmp4 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp4,
                       DAG.getConstant(255ULL << 24, dl, VT), Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_SRL, dl, VT, Op, DAG.getConstant(24, dl, SHVT),
                       Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp3,
                       DAG.getConstant(255ULL << 16, dl, VT), Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_SRL, dl, VT, Op, DAG.getConstant(40, dl, SHVT),
                       Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp2,
                       DAG.getConstant(255ULL << 8, dl, VT), Mask, EVL);
    Tmp1 = DAG.getNode(ISD::VP_SRL, dl, VT, Op, DAG.getConstant(56, dl, SHVT),
                       Mask, EVL);
    Tmp8 = DAG.getNode(ISD::VP_OR, dl, VT, Tmp8, Tmp7, Mask, EVL);
    Tmp6 = DAG.getNode(ISD::VP_OR, dl, VT, Tmp6, Tmp5, Mask, EVL);
    Tmp4 = DAG.getNode(ISD::VP_OR, dl, VT, Tmp4, Tmp3, Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_OR, dl, VT, Tmp2, Tmp1, Mask, EVL);
    Tmp8 = DAG.getNode(ISD::VP_OR, dl, VT, Tmp8, Tmp6, Mask, EVL);
    Tmp4 = DAG.getNode(ISD::VP_OR, dl, VT, Tmp4, Tmp2, Mask, EVL);
    return DAG.getNode(ISD::VP_OR, dl, VT, Tmp8, Tmp4, Mask, EVL);
  }
}

SDValue TargetLowering::expandBITREVERSE(SDNode *N, SelectionDAG &DAG) const {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Op = N->getOperand(0);
  EVT SHVT = getShiftAmountTy(VT, DAG.getDataLayout());
  unsigned Sz = VT.getScalarSizeInBits();

  SDValue Tmp, Tmp2, Tmp3;

  // If we can, perform BSWAP first and then the mask+swap the i4, then i2
  // and finally the i1 pairs.
  // TODO: We can easily support i4/i2 legal types if any target ever does.
  if (Sz >= 8 && isPowerOf2_32(Sz)) {
    // Create the masks - repeating the pattern every byte.
    APInt Mask4 = APInt::getSplat(Sz, APInt(8, 0x0F));
    APInt Mask2 = APInt::getSplat(Sz, APInt(8, 0x33));
    APInt Mask1 = APInt::getSplat(Sz, APInt(8, 0x55));

    // BSWAP if the type is wider than a single byte.
    Tmp = (Sz > 8 ? DAG.getNode(ISD::BSWAP, dl, VT, Op) : Op);

    // swap i4: ((V >> 4) & 0x0F) | ((V & 0x0F) << 4)
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Tmp, DAG.getConstant(4, dl, SHVT));
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2, DAG.getConstant(Mask4, dl, VT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp, DAG.getConstant(Mask4, dl, VT));
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, Tmp3, DAG.getConstant(4, dl, SHVT));
    Tmp = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);

    // swap i2: ((V >> 2) & 0x33) | ((V & 0x33) << 2)
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Tmp, DAG.getConstant(2, dl, SHVT));
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2, DAG.getConstant(Mask2, dl, VT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp, DAG.getConstant(Mask2, dl, VT));
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, Tmp3, DAG.getConstant(2, dl, SHVT));
    Tmp = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);

    // swap i1: ((V >> 1) & 0x55) | ((V & 0x55) << 1)
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Tmp, DAG.getConstant(1, dl, SHVT));
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2, DAG.getConstant(Mask1, dl, VT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp, DAG.getConstant(Mask1, dl, VT));
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, Tmp3, DAG.getConstant(1, dl, SHVT));
    Tmp = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);
    return Tmp;
  }

  Tmp = DAG.getConstant(0, dl, VT);
  for (unsigned I = 0, J = Sz-1; I < Sz; ++I, --J) {
    if (I < J)
      Tmp2 =
          DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(J - I, dl, SHVT));
    else
      Tmp2 =
          DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(I - J, dl, SHVT));

    APInt Shift = APInt::getOneBitSet(Sz, J);
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2, DAG.getConstant(Shift, dl, VT));
    Tmp = DAG.getNode(ISD::OR, dl, VT, Tmp, Tmp2);
  }

  return Tmp;
}

SDValue TargetLowering::expandVPBITREVERSE(SDNode *N, SelectionDAG &DAG) const {
  assert(N->getOpcode() == ISD::VP_BITREVERSE);

  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Op = N->getOperand(0);
  SDValue Mask = N->getOperand(1);
  SDValue EVL = N->getOperand(2);
  EVT SHVT = getShiftAmountTy(VT, DAG.getDataLayout());
  unsigned Sz = VT.getScalarSizeInBits();

  SDValue Tmp, Tmp2, Tmp3;

  // If we can, perform BSWAP first and then the mask+swap the i4, then i2
  // and finally the i1 pairs.
  // TODO: We can easily support i4/i2 legal types if any target ever does.
  if (Sz >= 8 && isPowerOf2_32(Sz)) {
    // Create the masks - repeating the pattern every byte.
    APInt Mask4 = APInt::getSplat(Sz, APInt(8, 0x0F));
    APInt Mask2 = APInt::getSplat(Sz, APInt(8, 0x33));
    APInt Mask1 = APInt::getSplat(Sz, APInt(8, 0x55));

    // BSWAP if the type is wider than a single byte.
    Tmp = (Sz > 8 ? DAG.getNode(ISD::VP_BSWAP, dl, VT, Op, Mask, EVL) : Op);

    // swap i4: ((V >> 4) & 0x0F) | ((V & 0x0F) << 4)
    Tmp2 = DAG.getNode(ISD::VP_SRL, dl, VT, Tmp, DAG.getConstant(4, dl, SHVT),
                       Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp2,
                       DAG.getConstant(Mask4, dl, VT), Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp, DAG.getConstant(Mask4, dl, VT),
                       Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_SHL, dl, VT, Tmp3, DAG.getConstant(4, dl, SHVT),
                       Mask, EVL);
    Tmp = DAG.getNode(ISD::VP_OR, dl, VT, Tmp2, Tmp3, Mask, EVL);

    // swap i2: ((V >> 2) & 0x33) | ((V & 0x33) << 2)
    Tmp2 = DAG.getNode(ISD::VP_SRL, dl, VT, Tmp, DAG.getConstant(2, dl, SHVT),
                       Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp2,
                       DAG.getConstant(Mask2, dl, VT), Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp, DAG.getConstant(Mask2, dl, VT),
                       Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_SHL, dl, VT, Tmp3, DAG.getConstant(2, dl, SHVT),
                       Mask, EVL);
    Tmp = DAG.getNode(ISD::VP_OR, dl, VT, Tmp2, Tmp3, Mask, EVL);

    // swap i1: ((V >> 1) & 0x55) | ((V & 0x55) << 1)
    Tmp2 = DAG.getNode(ISD::VP_SRL, dl, VT, Tmp, DAG.getConstant(1, dl, SHVT),
                       Mask, EVL);
    Tmp2 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp2,
                       DAG.getConstant(Mask1, dl, VT), Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_AND, dl, VT, Tmp, DAG.getConstant(Mask1, dl, VT),
                       Mask, EVL);
    Tmp3 = DAG.getNode(ISD::VP_SHL, dl, VT, Tmp3, DAG.getConstant(1, dl, SHVT),
                       Mask, EVL);
    Tmp = DAG.getNode(ISD::VP_OR, dl, VT, Tmp2, Tmp3, Mask, EVL);
    return Tmp;
  }
  return SDValue();
}

std::pair<SDValue, SDValue>
TargetLowering::scalarizeVectorLoad(LoadSDNode *LD,
                                    SelectionDAG &DAG) const {
  SDLoc SL(LD);
  SDValue Chain = LD->getChain();
  SDValue BasePTR = LD->getBasePtr();
  EVT SrcVT = LD->getMemoryVT();
  EVT DstVT = LD->getValueType(0);
  ISD::LoadExtType ExtType = LD->getExtensionType();

  if (SrcVT.isScalableVector())
    report_fatal_error("Cannot scalarize scalable vector loads");

  unsigned NumElem = SrcVT.getVectorNumElements();

  EVT SrcEltVT = SrcVT.getScalarType();
  EVT DstEltVT = DstVT.getScalarType();

  // A vector must always be stored in memory as-is, i.e. without any padding
  // between the elements, since various code depend on it, e.g. in the
  // handling of a bitcast of a vector type to int, which may be done with a
  // vector store followed by an integer load. A vector that does not have
  // elements that are byte-sized must therefore be stored as an integer
  // built out of the extracted vector elements.
  if (!SrcEltVT.isByteSized()) {
    unsigned NumLoadBits = SrcVT.getStoreSizeInBits();
    EVT LoadVT = EVT::getIntegerVT(*DAG.getContext(), NumLoadBits);

    unsigned NumSrcBits = SrcVT.getSizeInBits();
    EVT SrcIntVT = EVT::getIntegerVT(*DAG.getContext(), NumSrcBits);

    unsigned SrcEltBits = SrcEltVT.getSizeInBits();
    SDValue SrcEltBitMask = DAG.getConstant(
        APInt::getLowBitsSet(NumLoadBits, SrcEltBits), SL, LoadVT);

    // Load the whole vector and avoid masking off the top bits as it makes
    // the codegen worse.
    SDValue Load =
        DAG.getExtLoad(ISD::EXTLOAD, SL, LoadVT, Chain, BasePTR,
                       LD->getPointerInfo(), SrcIntVT, LD->getOriginalAlign(),
                       LD->getMemOperand()->getFlags(), LD->getAAInfo());

    SmallVector<SDValue, 8> Vals;
    for (unsigned Idx = 0; Idx < NumElem; ++Idx) {
      unsigned ShiftIntoIdx =
          (DAG.getDataLayout().isBigEndian() ? (NumElem - 1) - Idx : Idx);
      SDValue ShiftAmount = DAG.getShiftAmountConstant(
          ShiftIntoIdx * SrcEltVT.getSizeInBits(), LoadVT, SL);
      SDValue ShiftedElt = DAG.getNode(ISD::SRL, SL, LoadVT, Load, ShiftAmount);
      SDValue Elt =
          DAG.getNode(ISD::AND, SL, LoadVT, ShiftedElt, SrcEltBitMask);
      SDValue Scalar = DAG.getNode(ISD::TRUNCATE, SL, SrcEltVT, Elt);

      if (ExtType != ISD::NON_EXTLOAD) {
        unsigned ExtendOp = ISD::getExtForLoadExtType(false, ExtType);
        Scalar = DAG.getNode(ExtendOp, SL, DstEltVT, Scalar);
      }

      Vals.push_back(Scalar);
    }

    SDValue Value = DAG.getBuildVector(DstVT, SL, Vals);
    return std::make_pair(Value, Load.getValue(1));
  }

  unsigned Stride = SrcEltVT.getSizeInBits() / 8;
  assert(SrcEltVT.isByteSized());

  SmallVector<SDValue, 8> Vals;
  SmallVector<SDValue, 8> LoadChains;

  for (unsigned Idx = 0; Idx < NumElem; ++Idx) {
    SDValue ScalarLoad =
        DAG.getExtLoad(ExtType, SL, DstEltVT, Chain, BasePTR,
                       LD->getPointerInfo().getWithOffset(Idx * Stride),
                       SrcEltVT, LD->getOriginalAlign(),
                       LD->getMemOperand()->getFlags(), LD->getAAInfo());

    BasePTR = DAG.getObjectPtrOffset(SL, BasePTR, TypeSize::getFixed(Stride));

    Vals.push_back(ScalarLoad.getValue(0));
    LoadChains.push_back(ScalarLoad.getValue(1));
  }

  SDValue NewChain = DAG.getNode(ISD::TokenFactor, SL, MVT::Other, LoadChains);
  SDValue Value = DAG.getBuildVector(DstVT, SL, Vals);

  return std::make_pair(Value, NewChain);
}

SDValue TargetLowering::scalarizeVectorStore(StoreSDNode *ST,
                                             SelectionDAG &DAG) const {
  SDLoc SL(ST);

  SDValue Chain = ST->getChain();
  SDValue BasePtr = ST->getBasePtr();
  SDValue Value = ST->getValue();
  EVT StVT = ST->getMemoryVT();

  if (StVT.isScalableVector())
    report_fatal_error("Cannot scalarize scalable vector stores");

  // The type of the data we want to save
  EVT RegVT = Value.getValueType();
  EVT RegSclVT = RegVT.getScalarType();

  // The type of data as saved in memory.
  EVT MemSclVT = StVT.getScalarType();

  unsigned NumElem = StVT.getVectorNumElements();

  // A vector must always be stored in memory as-is, i.e. without any padding
  // between the elements, since various code depend on it, e.g. in the
  // handling of a bitcast of a vector type to int, which may be done with a
  // vector store followed by an integer load. A vector that does not have
  // elements that are byte-sized must therefore be stored as an integer
  // built out of the extracted vector elements.
  if (!MemSclVT.isByteSized()) {
    unsigned NumBits = StVT.getSizeInBits();
    EVT IntVT = EVT::getIntegerVT(*DAG.getContext(), NumBits);

    SDValue CurrVal = DAG.getConstant(0, SL, IntVT);

    for (unsigned Idx = 0; Idx < NumElem; ++Idx) {
      SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, RegSclVT, Value,
                                DAG.getVectorIdxConstant(Idx, SL));
      SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SL, MemSclVT, Elt);
      SDValue ExtElt = DAG.getNode(ISD::ZERO_EXTEND, SL, IntVT, Trunc);
      unsigned ShiftIntoIdx =
          (DAG.getDataLayout().isBigEndian() ? (NumElem - 1) - Idx : Idx);
      SDValue ShiftAmount =
          DAG.getConstant(ShiftIntoIdx * MemSclVT.getSizeInBits(), SL, IntVT);
      SDValue ShiftedElt =
          DAG.getNode(ISD::SHL, SL, IntVT, ExtElt, ShiftAmount);
      CurrVal = DAG.getNode(ISD::OR, SL, IntVT, CurrVal, ShiftedElt);
    }

    return DAG.getStore(Chain, SL, CurrVal, BasePtr, ST->getPointerInfo(),
                        ST->getOriginalAlign(), ST->getMemOperand()->getFlags(),
                        ST->getAAInfo());
  }

  // Store Stride in bytes
  unsigned Stride = MemSclVT.getSizeInBits() / 8;
  assert(Stride && "Zero stride!");
  // Extract each of the elements from the original vector and save them into
  // memory individually.
  SmallVector<SDValue, 8> Stores;
  for (unsigned Idx = 0; Idx < NumElem; ++Idx) {
    SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, RegSclVT, Value,
                              DAG.getVectorIdxConstant(Idx, SL));

    SDValue Ptr =
        DAG.getObjectPtrOffset(SL, BasePtr, TypeSize::getFixed(Idx * Stride));

    // This scalar TruncStore may be illegal, but we legalize it later.
    SDValue Store = DAG.getTruncStore(
        Chain, SL, Elt, Ptr, ST->getPointerInfo().getWithOffset(Idx * Stride),
        MemSclVT, ST->getOriginalAlign(), ST->getMemOperand()->getFlags(),
        ST->getAAInfo());

    Stores.push_back(Store);
  }

  return DAG.getNode(ISD::TokenFactor, SL, MVT::Other, Stores);
}

std::pair<SDValue, SDValue>
TargetLowering::expandUnalignedLoad(LoadSDNode *LD, SelectionDAG &DAG) const {
  assert(LD->getAddressingMode() == ISD::UNINDEXED &&
         "unaligned indexed loads not implemented!");
  SDValue Chain = LD->getChain();
  SDValue Ptr = LD->getBasePtr();
  EVT VT = LD->getValueType(0);
  EVT LoadedVT = LD->getMemoryVT();
  SDLoc dl(LD);
  auto &MF = DAG.getMachineFunction();

  if (VT.isFloatingPoint() || VT.isVector()) {
    EVT intVT = EVT::getIntegerVT(*DAG.getContext(), LoadedVT.getSizeInBits());
    if (isTypeLegal(intVT) && isTypeLegal(LoadedVT)) {
      if (!isOperationLegalOrCustom(ISD::LOAD, intVT) &&
          LoadedVT.isVector()) {
        // Scalarize the load and let the individual components be handled.
        return scalarizeVectorLoad(LD, DAG);
      }

      // Expand to a (misaligned) integer load of the same size,
      // then bitconvert to floating point or vector.
      SDValue newLoad = DAG.getLoad(intVT, dl, Chain, Ptr,
                                    LD->getMemOperand());
      SDValue Result = DAG.getNode(ISD::BITCAST, dl, LoadedVT, newLoad);
      if (LoadedVT != VT)
        Result = DAG.getNode(VT.isFloatingPoint() ? ISD::FP_EXTEND :
                             ISD::ANY_EXTEND, dl, VT, Result);

      return std::make_pair(Result, newLoad.getValue(1));
    }

    // Copy the value to a (aligned) stack slot using (unaligned) integer
    // loads and stores, then do a (aligned) load from the stack slot.
    MVT RegVT = getRegisterType(*DAG.getContext(), intVT);
    unsigned LoadedBytes = LoadedVT.getStoreSize();
    unsigned RegBytes = RegVT.getSizeInBits() / 8;
    unsigned NumRegs = (LoadedBytes + RegBytes - 1) / RegBytes;

    // Make sure the stack slot is also aligned for the register type.
    SDValue StackBase = DAG.CreateStackTemporary(LoadedVT, RegVT);
    auto FrameIndex = cast<FrameIndexSDNode>(StackBase.getNode())->getIndex();
    SmallVector<SDValue, 8> Stores;
    SDValue StackPtr = StackBase;
    unsigned Offset = 0;

    EVT PtrVT = Ptr.getValueType();
    EVT StackPtrVT = StackPtr.getValueType();

    SDValue PtrIncrement = DAG.getConstant(RegBytes, dl, PtrVT);
    SDValue StackPtrIncrement = DAG.getConstant(RegBytes, dl, StackPtrVT);

    // Do all but one copies using the full register width.
    for (unsigned i = 1; i < NumRegs; i++) {
      // Load one integer register's worth from the original location.
      SDValue Load = DAG.getLoad(
          RegVT, dl, Chain, Ptr, LD->getPointerInfo().getWithOffset(Offset),
          LD->getOriginalAlign(), LD->getMemOperand()->getFlags(),
          LD->getAAInfo());
      // Follow the load with a store to the stack slot.  Remember the store.
      Stores.push_back(DAG.getStore(
          Load.getValue(1), dl, Load, StackPtr,
          MachinePointerInfo::getFixedStack(MF, FrameIndex, Offset)));
      // Increment the pointers.
      Offset += RegBytes;

      Ptr = DAG.getObjectPtrOffset(dl, Ptr, PtrIncrement);
      StackPtr = DAG.getObjectPtrOffset(dl, StackPtr, StackPtrIncrement);
    }

    // The last copy may be partial.  Do an extending load.
    EVT MemVT = EVT::getIntegerVT(*DAG.getContext(),
                                  8 * (LoadedBytes - Offset));
    SDValue Load =
        DAG.getExtLoad(ISD::EXTLOAD, dl, RegVT, Chain, Ptr,
                       LD->getPointerInfo().getWithOffset(Offset), MemVT,
                       LD->getOriginalAlign(), LD->getMemOperand()->getFlags(),
                       LD->getAAInfo());
    // Follow the load with a store to the stack slot.  Remember the store.
    // On big-endian machines this requires a truncating store to ensure
    // that the bits end up in the right place.
    Stores.push_back(DAG.getTruncStore(
        Load.getValue(1), dl, Load, StackPtr,
        MachinePointerInfo::getFixedStack(MF, FrameIndex, Offset), MemVT));

    // The order of the stores doesn't matter - say it with a TokenFactor.
    SDValue TF = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Stores);

    // Finally, perform the original load only redirected to the stack slot.
    Load = DAG.getExtLoad(LD->getExtensionType(), dl, VT, TF, StackBase,
                          MachinePointerInfo::getFixedStack(MF, FrameIndex, 0),
                          LoadedVT);

    // Callers expect a MERGE_VALUES node.
    return std::make_pair(Load, TF);
  }

  assert(LoadedVT.isInteger() && !LoadedVT.isVector() &&
         "Unaligned load of unsupported type.");

  // Compute the new VT that is half the size of the old one.  This is an
  // integer MVT.
  unsigned NumBits = LoadedVT.getSizeInBits();
  EVT NewLoadedVT;
  NewLoadedVT = EVT::getIntegerVT(*DAG.getContext(), NumBits/2);
  NumBits >>= 1;

  Align Alignment = LD->getOriginalAlign();
  unsigned IncrementSize = NumBits / 8;
  ISD::LoadExtType HiExtType = LD->getExtensionType();

  // If the original load is NON_EXTLOAD, the hi part load must be ZEXTLOAD.
  if (HiExtType == ISD::NON_EXTLOAD)
    HiExtType = ISD::ZEXTLOAD;

  // Load the value in two parts
  SDValue Lo, Hi;
  if (DAG.getDataLayout().isLittleEndian()) {
    Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, VT, Chain, Ptr, LD->getPointerInfo(),
                        NewLoadedVT, Alignment, LD->getMemOperand()->getFlags(),
                        LD->getAAInfo());

    Ptr = DAG.getObjectPtrOffset(dl, Ptr, TypeSize::getFixed(IncrementSize));
    Hi = DAG.getExtLoad(HiExtType, dl, VT, Chain, Ptr,
                        LD->getPointerInfo().getWithOffset(IncrementSize),
                        NewLoadedVT, Alignment, LD->getMemOperand()->getFlags(),
                        LD->getAAInfo());
  } else {
    Hi = DAG.getExtLoad(HiExtType, dl, VT, Chain, Ptr, LD->getPointerInfo(),
                        NewLoadedVT, Alignment, LD->getMemOperand()->getFlags(),
                        LD->getAAInfo());

    Ptr = DAG.getObjectPtrOffset(dl, Ptr, TypeSize::getFixed(IncrementSize));
    Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, VT, Chain, Ptr,
                        LD->getPointerInfo().getWithOffset(IncrementSize),
                        NewLoadedVT, Alignment, LD->getMemOperand()->getFlags(),
                        LD->getAAInfo());
  }

  // aggregate the two parts
  SDValue ShiftAmount = DAG.getShiftAmountConstant(NumBits, VT, dl);
  SDValue Result = DAG.getNode(ISD::SHL, dl, VT, Hi, ShiftAmount);
  Result = DAG.getNode(ISD::OR, dl, VT, Result, Lo);

  SDValue TF = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                             Hi.getValue(1));

  return std::make_pair(Result, TF);
}

SDValue TargetLowering::expandUnalignedStore(StoreSDNode *ST,
                                             SelectionDAG &DAG) const {
  assert(ST->getAddressingMode() == ISD::UNINDEXED &&
         "unaligned indexed stores not implemented!");
  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();
  SDValue Val = ST->getValue();
  EVT VT = Val.getValueType();
  Align Alignment = ST->getOriginalAlign();
  auto &MF = DAG.getMachineFunction();
  EVT StoreMemVT = ST->getMemoryVT();

  SDLoc dl(ST);
  if (StoreMemVT.isFloatingPoint() || StoreMemVT.isVector()) {
    EVT intVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());
    if (isTypeLegal(intVT)) {
      if (!isOperationLegalOrCustom(ISD::STORE, intVT) &&
          StoreMemVT.isVector()) {
        // Scalarize the store and let the individual components be handled.
        SDValue Result = scalarizeVectorStore(ST, DAG);
        return Result;
      }
      // Expand to a bitconvert of the value to the integer type of the
      // same size, then a (misaligned) int store.
      // FIXME: Does not handle truncating floating point stores!
      SDValue Result = DAG.getNode(ISD::BITCAST, dl, intVT, Val);
      Result = DAG.getStore(Chain, dl, Result, Ptr, ST->getPointerInfo(),
                            Alignment, ST->getMemOperand()->getFlags());
      return Result;
    }
    // Do a (aligned) store to a stack slot, then copy from the stack slot
    // to the final destination using (unaligned) integer loads and stores.
    MVT RegVT = getRegisterType(
        *DAG.getContext(),
        EVT::getIntegerVT(*DAG.getContext(), StoreMemVT.getSizeInBits()));
    EVT PtrVT = Ptr.getValueType();
    unsigned StoredBytes = StoreMemVT.getStoreSize();
    unsigned RegBytes = RegVT.getSizeInBits() / 8;
    unsigned NumRegs = (StoredBytes + RegBytes - 1) / RegBytes;

    // Make sure the stack slot is also aligned for the register type.
    SDValue StackPtr = DAG.CreateStackTemporary(StoreMemVT, RegVT);
    auto FrameIndex = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();

    // Perform the original store, only redirected to the stack slot.
    SDValue Store = DAG.getTruncStore(
        Chain, dl, Val, StackPtr,
        MachinePointerInfo::getFixedStack(MF, FrameIndex, 0), StoreMemVT);

    EVT StackPtrVT = StackPtr.getValueType();

    SDValue PtrIncrement = DAG.getConstant(RegBytes, dl, PtrVT);
    SDValue StackPtrIncrement = DAG.getConstant(RegBytes, dl, StackPtrVT);
    SmallVector<SDValue, 8> Stores;
    unsigned Offset = 0;

    // Do all but one copies using the full register width.
    for (unsigned i = 1; i < NumRegs; i++) {
      // Load one integer register's worth from the stack slot.
      SDValue Load = DAG.getLoad(
          RegVT, dl, Store, StackPtr,
          MachinePointerInfo::getFixedStack(MF, FrameIndex, Offset));
      // Store it to the final location.  Remember the store.
      Stores.push_back(DAG.getStore(Load.getValue(1), dl, Load, Ptr,
                                    ST->getPointerInfo().getWithOffset(Offset),
                                    ST->getOriginalAlign(),
                                    ST->getMemOperand()->getFlags()));
      // Increment the pointers.
      Offset += RegBytes;
      StackPtr = DAG.getObjectPtrOffset(dl, StackPtr, StackPtrIncrement);
      Ptr = DAG.getObjectPtrOffset(dl, Ptr, PtrIncrement);
    }

    // The last store may be partial.  Do a truncating store.  On big-endian
    // machines this requires an extending load from the stack slot to ensure
    // that the bits are in the right place.
    EVT LoadMemVT =
        EVT::getIntegerVT(*DAG.getContext(), 8 * (StoredBytes - Offset));

    // Load from the stack slot.
    SDValue Load = DAG.getExtLoad(
        ISD::EXTLOAD, dl, RegVT, Store, StackPtr,
        MachinePointerInfo::getFixedStack(MF, FrameIndex, Offset), LoadMemVT);

    Stores.push_back(
        DAG.getTruncStore(Load.getValue(1), dl, Load, Ptr,
                          ST->getPointerInfo().getWithOffset(Offset), LoadMemVT,
                          ST->getOriginalAlign(),
                          ST->getMemOperand()->getFlags(), ST->getAAInfo()));
    // The order of the stores doesn't matter - say it with a TokenFactor.
    SDValue Result = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Stores);
    return Result;
  }

  assert(StoreMemVT.isInteger() && !StoreMemVT.isVector() &&
         "Unaligned store of unknown type.");
  // Get the half-size VT
  EVT NewStoredVT = StoreMemVT.getHalfSizedIntegerVT(*DAG.getContext());
  unsigned NumBits = NewStoredVT.getFixedSizeInBits();
  unsigned IncrementSize = NumBits / 8;

  // Divide the stored value in two parts.
  SDValue ShiftAmount =
      DAG.getShiftAmountConstant(NumBits, Val.getValueType(), dl);
  SDValue Lo = Val;
  // If Val is a constant, replace the upper bits with 0. The SRL will constant
  // fold and not use the upper bits. A smaller constant may be easier to
  // materialize.
  if (auto *C = dyn_cast<ConstantSDNode>(Lo); C && !C->isOpaque())
    Lo = DAG.getNode(
        ISD::AND, dl, VT, Lo,
        DAG.getConstant(APInt::getLowBitsSet(VT.getSizeInBits(), NumBits), dl,
                        VT));
  SDValue Hi = DAG.getNode(ISD::SRL, dl, VT, Val, ShiftAmount);

  // Store the two parts
  SDValue Store1, Store2;
  Store1 = DAG.getTruncStore(Chain, dl,
                             DAG.getDataLayout().isLittleEndian() ? Lo : Hi,
                             Ptr, ST->getPointerInfo(), NewStoredVT, Alignment,
                             ST->getMemOperand()->getFlags());

  Ptr = DAG.getObjectPtrOffset(dl, Ptr, TypeSize::getFixed(IncrementSize));
  Store2 = DAG.getTruncStore(
      Chain, dl, DAG.getDataLayout().isLittleEndian() ? Hi : Lo, Ptr,
      ST->getPointerInfo().getWithOffset(IncrementSize), NewStoredVT, Alignment,
      ST->getMemOperand()->getFlags(), ST->getAAInfo());

  SDValue Result =
      DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Store1, Store2);
  return Result;
}

SDValue
TargetLowering::IncrementMemoryAddress(SDValue Addr, SDValue Mask,
                                       const SDLoc &DL, EVT DataVT,
                                       SelectionDAG &DAG,
                                       bool IsCompressedMemory) const {
  SDValue Increment;
  EVT AddrVT = Addr.getValueType();
  EVT MaskVT = Mask.getValueType();
  assert(DataVT.getVectorElementCount() == MaskVT.getVectorElementCount() &&
         "Incompatible types of Data and Mask");
  if (IsCompressedMemory) {
    if (DataVT.isScalableVector())
      report_fatal_error(
          "Cannot currently handle compressed memory with scalable vectors");
    // Incrementing the pointer according to number of '1's in the mask.
    EVT MaskIntVT = EVT::getIntegerVT(*DAG.getContext(), MaskVT.getSizeInBits());
    SDValue MaskInIntReg = DAG.getBitcast(MaskIntVT, Mask);
    if (MaskIntVT.getSizeInBits() < 32) {
      MaskInIntReg = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, MaskInIntReg);
      MaskIntVT = MVT::i32;
    }

    // Count '1's with POPCNT.
    Increment = DAG.getNode(ISD::CTPOP, DL, MaskIntVT, MaskInIntReg);
    Increment = DAG.getZExtOrTrunc(Increment, DL, AddrVT);
    // Scale is an element size in bytes.
    SDValue Scale = DAG.getConstant(DataVT.getScalarSizeInBits() / 8, DL,
                                    AddrVT);
    Increment = DAG.getNode(ISD::MUL, DL, AddrVT, Increment, Scale);
  } else if (DataVT.isScalableVector()) {
    Increment = DAG.getVScale(DL, AddrVT,
                              APInt(AddrVT.getFixedSizeInBits(),
                                    DataVT.getStoreSize().getKnownMinValue()));
  } else
    Increment = DAG.getConstant(DataVT.getStoreSize(), DL, AddrVT);

  return DAG.getNode(ISD::ADD, DL, AddrVT, Addr, Increment);
}

static SDValue clampDynamicVectorIndex(SelectionDAG &DAG, SDValue Idx,
                                       EVT VecVT, const SDLoc &dl,
                                       ElementCount SubEC) {
  assert(!(SubEC.isScalable() && VecVT.isFixedLengthVector()) &&
         "Cannot index a scalable vector within a fixed-width vector");

  unsigned NElts = VecVT.getVectorMinNumElements();
  unsigned NumSubElts = SubEC.getKnownMinValue();
  EVT IdxVT = Idx.getValueType();

  if (VecVT.isScalableVector() && !SubEC.isScalable()) {
    // If this is a constant index and we know the value plus the number of the
    // elements in the subvector minus one is less than the minimum number of
    // elements then it's safe to return Idx.
    if (auto *IdxCst = dyn_cast<ConstantSDNode>(Idx))
      if (IdxCst->getZExtValue() + (NumSubElts - 1) < NElts)
        return Idx;
    SDValue VS =
        DAG.getVScale(dl, IdxVT, APInt(IdxVT.getFixedSizeInBits(), NElts));
    unsigned SubOpcode = NumSubElts <= NElts ? ISD::SUB : ISD::USUBSAT;
    SDValue Sub = DAG.getNode(SubOpcode, dl, IdxVT, VS,
                              DAG.getConstant(NumSubElts, dl, IdxVT));
    return DAG.getNode(ISD::UMIN, dl, IdxVT, Idx, Sub);
  }
  if (isPowerOf2_32(NElts) && NumSubElts == 1) {
    APInt Imm = APInt::getLowBitsSet(IdxVT.getSizeInBits(), Log2_32(NElts));
    return DAG.getNode(ISD::AND, dl, IdxVT, Idx,
                       DAG.getConstant(Imm, dl, IdxVT));
  }
  unsigned MaxIndex = NumSubElts < NElts ? NElts - NumSubElts : 0;
  return DAG.getNode(ISD::UMIN, dl, IdxVT, Idx,
                     DAG.getConstant(MaxIndex, dl, IdxVT));
}

SDValue TargetLowering::getVectorElementPointer(SelectionDAG &DAG,
                                                SDValue VecPtr, EVT VecVT,
                                                SDValue Index) const {
  return getVectorSubVecPointer(
      DAG, VecPtr, VecVT,
      EVT::getVectorVT(*DAG.getContext(), VecVT.getVectorElementType(), 1),
      Index);
}

SDValue TargetLowering::getVectorSubVecPointer(SelectionDAG &DAG,
                                               SDValue VecPtr, EVT VecVT,
                                               EVT SubVecVT,
                                               SDValue Index) const {
  SDLoc dl(Index);
  // Make sure the index type is big enough to compute in.
  Index = DAG.getZExtOrTrunc(Index, dl, VecPtr.getValueType());

  EVT EltVT = VecVT.getVectorElementType();

  // Calculate the element offset and add it to the pointer.
  unsigned EltSize = EltVT.getFixedSizeInBits() / 8; // FIXME: should be ABI size.
  assert(EltSize * 8 == EltVT.getFixedSizeInBits() &&
         "Converting bits to bytes lost precision");
  assert(SubVecVT.getVectorElementType() == EltVT &&
         "Sub-vector must be a vector with matching element type");
  Index = clampDynamicVectorIndex(DAG, Index, VecVT, dl,
                                  SubVecVT.getVectorElementCount());

  EVT IdxVT = Index.getValueType();
  if (SubVecVT.isScalableVector())
    Index =
        DAG.getNode(ISD::MUL, dl, IdxVT, Index,
                    DAG.getVScale(dl, IdxVT, APInt(IdxVT.getSizeInBits(), 1)));

  Index = DAG.getNode(ISD::MUL, dl, IdxVT, Index,
                      DAG.getConstant(EltSize, dl, IdxVT));
  return DAG.getMemBasePlusOffset(VecPtr, Index, dl);
}

//===----------------------------------------------------------------------===//
// Implementation of Emulated TLS Model
//===----------------------------------------------------------------------===//

SDValue TargetLowering::LowerToTLSEmulatedModel(const GlobalAddressSDNode *GA,
                                                SelectionDAG &DAG) const {
  // Access to address of TLS varialbe xyz is lowered to a function call:
  //   __emutls_get_address( address of global variable named "__emutls_v.xyz" )
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  PointerType *VoidPtrType = PointerType::get(*DAG.getContext(), 0);
  SDLoc dl(GA);

  ArgListTy Args;
  ArgListEntry Entry;
  std::string NameString = ("__emutls_v." + GA->getGlobal()->getName()).str();
  Module *VariableModule = const_cast<Module*>(GA->getGlobal()->getParent());
  StringRef EmuTlsVarName(NameString);
  GlobalVariable *EmuTlsVar = VariableModule->getNamedGlobal(EmuTlsVarName);
  assert(EmuTlsVar && "Cannot find EmuTlsVar ");
  Entry.Node = DAG.getGlobalAddress(EmuTlsVar, dl, PtrVT);
  Entry.Ty = VoidPtrType;
  Args.push_back(Entry);

  SDValue EmuTlsGetAddr = DAG.getExternalSymbol("__emutls_get_address", PtrVT);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl).setChain(DAG.getEntryNode());
  CLI.setLibCallee(CallingConv::C, VoidPtrType, EmuTlsGetAddr, std::move(Args));
  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);

  // TLSADDR will be codegen'ed as call. Inform MFI that function has calls.
  // At last for X86 targets, maybe good for other targets too?
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setAdjustsStack(true); // Is this only for X86 target?
  MFI.setHasCalls(true);

  assert((GA->getOffset() == 0) &&
         "Emulated TLS must have zero offset in GlobalAddressSDNode");
  return CallResult.first;
}

SDValue TargetLowering::lowerCmpEqZeroToCtlzSrl(SDValue Op,
                                                SelectionDAG &DAG) const {
  assert((Op->getOpcode() == ISD::SETCC) && "Input has to be a SETCC node.");
  if (!isCtlzFast())
    return SDValue();
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDLoc dl(Op);
  if (isNullConstant(Op.getOperand(1)) && CC == ISD::SETEQ) {
    EVT VT = Op.getOperand(0).getValueType();
    SDValue Zext = Op.getOperand(0);
    if (VT.bitsLT(MVT::i32)) {
      VT = MVT::i32;
      Zext = DAG.getNode(ISD::ZERO_EXTEND, dl, VT, Op.getOperand(0));
    }
    unsigned Log2b = Log2_32(VT.getSizeInBits());
    SDValue Clz = DAG.getNode(ISD::CTLZ, dl, VT, Zext);
    SDValue Scc = DAG.getNode(ISD::SRL, dl, VT, Clz,
                              DAG.getConstant(Log2b, dl, MVT::i32));
    return DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Scc);
  }
  return SDValue();
}

SDValue TargetLowering::expandIntMINMAX(SDNode *Node, SelectionDAG &DAG) const {
  SDValue Op0 = Node->getOperand(0);
  SDValue Op1 = Node->getOperand(1);
  EVT VT = Op0.getValueType();
  EVT BoolVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  unsigned Opcode = Node->getOpcode();
  SDLoc DL(Node);

  // umax(x,1) --> sub(x,cmpeq(x,0)) iff cmp result is allbits
  if (Opcode == ISD::UMAX && llvm::isOneOrOneSplat(Op1, true) && BoolVT == VT &&
      getBooleanContents(VT) == ZeroOrNegativeOneBooleanContent) {
    Op0 = DAG.getFreeze(Op0);
    SDValue Zero = DAG.getConstant(0, DL, VT);
    return DAG.getNode(ISD::SUB, DL, VT, Op0,
                       DAG.getSetCC(DL, VT, Op0, Zero, ISD::SETEQ));
  }

  // umin(x,y) -> sub(x,usubsat(x,y))
  // TODO: Missing freeze(Op0)?
  if (Opcode == ISD::UMIN && isOperationLegal(ISD::SUB, VT) &&
      isOperationLegal(ISD::USUBSAT, VT)) {
    return DAG.getNode(ISD::SUB, DL, VT, Op0,
                       DAG.getNode(ISD::USUBSAT, DL, VT, Op0, Op1));
  }

  // umax(x,y) -> add(x,usubsat(y,x))
  // TODO: Missing freeze(Op0)?
  if (Opcode == ISD::UMAX && isOperationLegal(ISD::ADD, VT) &&
      isOperationLegal(ISD::USUBSAT, VT)) {
    return DAG.getNode(ISD::ADD, DL, VT, Op0,
                       DAG.getNode(ISD::USUBSAT, DL, VT, Op1, Op0));
  }

  // FIXME: Should really try to split the vector in case it's legal on a
  // subvector.
  if (VT.isVector() && !isOperationLegalOrCustom(ISD::VSELECT, VT))
    return DAG.UnrollVectorOp(Node);

  // Attempt to find an existing SETCC node that we can reuse.
  // TODO: Do we need a generic doesSETCCNodeExist?
  // TODO: Missing freeze(Op0)/freeze(Op1)?
  auto buildMinMax = [&](ISD::CondCode PrefCC, ISD::CondCode AltCC,
                         ISD::CondCode PrefCommuteCC,
                         ISD::CondCode AltCommuteCC) {
    SDVTList BoolVTList = DAG.getVTList(BoolVT);
    for (ISD::CondCode CC : {PrefCC, AltCC}) {
      if (DAG.doesNodeExist(ISD::SETCC, BoolVTList,
                            {Op0, Op1, DAG.getCondCode(CC)})) {
        SDValue Cond = DAG.getSetCC(DL, BoolVT, Op0, Op1, CC);
        return DAG.getSelect(DL, VT, Cond, Op0, Op1);
      }
    }
    for (ISD::CondCode CC : {PrefCommuteCC, AltCommuteCC}) {
      if (DAG.doesNodeExist(ISD::SETCC, BoolVTList,
                            {Op0, Op1, DAG.getCondCode(CC)})) {
        SDValue Cond = DAG.getSetCC(DL, BoolVT, Op0, Op1, CC);
        return DAG.getSelect(DL, VT, Cond, Op1, Op0);
      }
    }
    SDValue Cond = DAG.getSetCC(DL, BoolVT, Op0, Op1, PrefCC);
    return DAG.getSelect(DL, VT, Cond, Op0, Op1);
  };

  // Expand Y = MAX(A, B) -> Y = (A > B) ? A : B
  //                      -> Y = (A < B) ? B : A
  //                      -> Y = (A >= B) ? A : B
  //                      -> Y = (A <= B) ? B : A
  switch (Opcode) {
  case ISD::SMAX:
    return buildMinMax(ISD::SETGT, ISD::SETGE, ISD::SETLT, ISD::SETLE);
  case ISD::SMIN:
    return buildMinMax(ISD::SETLT, ISD::SETLE, ISD::SETGT, ISD::SETGE);
  case ISD::UMAX:
    return buildMinMax(ISD::SETUGT, ISD::SETUGE, ISD::SETULT, ISD::SETULE);
  case ISD::UMIN:
    return buildMinMax(ISD::SETULT, ISD::SETULE, ISD::SETUGT, ISD::SETUGE);
  }

  llvm_unreachable("How did we get here?");
}

SDValue TargetLowering::expandAddSubSat(SDNode *Node, SelectionDAG &DAG) const {
  unsigned Opcode = Node->getOpcode();
  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  EVT VT = LHS.getValueType();
  SDLoc dl(Node);

  assert(VT == RHS.getValueType() && "Expected operands to be the same type");
  assert(VT.isInteger() && "Expected operands to be integers");

  // usub.sat(a, b) -> umax(a, b) - b
  if (Opcode == ISD::USUBSAT && isOperationLegal(ISD::UMAX, VT)) {
    SDValue Max = DAG.getNode(ISD::UMAX, dl, VT, LHS, RHS);
    return DAG.getNode(ISD::SUB, dl, VT, Max, RHS);
  }

  // uadd.sat(a, b) -> umin(a, ~b) + b
  if (Opcode == ISD::UADDSAT && isOperationLegal(ISD::UMIN, VT)) {
    SDValue InvRHS = DAG.getNOT(dl, RHS, VT);
    SDValue Min = DAG.getNode(ISD::UMIN, dl, VT, LHS, InvRHS);
    return DAG.getNode(ISD::ADD, dl, VT, Min, RHS);
  }

  unsigned OverflowOp;
  switch (Opcode) {
  case ISD::SADDSAT:
    OverflowOp = ISD::SADDO;
    break;
  case ISD::UADDSAT:
    OverflowOp = ISD::UADDO;
    break;
  case ISD::SSUBSAT:
    OverflowOp = ISD::SSUBO;
    break;
  case ISD::USUBSAT:
    OverflowOp = ISD::USUBO;
    break;
  default:
    llvm_unreachable("Expected method to receive signed or unsigned saturation "
                     "addition or subtraction node.");
  }

  // FIXME: Should really try to split the vector in case it's legal on a
  // subvector.
  if (VT.isVector() && !isOperationLegalOrCustom(ISD::VSELECT, VT))
    return DAG.UnrollVectorOp(Node);

  unsigned BitWidth = LHS.getScalarValueSizeInBits();
  EVT BoolVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue Result = DAG.getNode(OverflowOp, dl, DAG.getVTList(VT, BoolVT), LHS, RHS);
  SDValue SumDiff = Result.getValue(0);
  SDValue Overflow = Result.getValue(1);
  SDValue Zero = DAG.getConstant(0, dl, VT);
  SDValue AllOnes = DAG.getAllOnesConstant(dl, VT);

  if (Opcode == ISD::UADDSAT) {
    if (getBooleanContents(VT) == ZeroOrNegativeOneBooleanContent) {
      // (LHS + RHS) | OverflowMask
      SDValue OverflowMask = DAG.getSExtOrTrunc(Overflow, dl, VT);
      return DAG.getNode(ISD::OR, dl, VT, SumDiff, OverflowMask);
    }
    // Overflow ? 0xffff.... : (LHS + RHS)
    return DAG.getSelect(dl, VT, Overflow, AllOnes, SumDiff);
  }

  if (Opcode == ISD::USUBSAT) {
    if (getBooleanContents(VT) == ZeroOrNegativeOneBooleanContent) {
      // (LHS - RHS) & ~OverflowMask
      SDValue OverflowMask = DAG.getSExtOrTrunc(Overflow, dl, VT);
      SDValue Not = DAG.getNOT(dl, OverflowMask, VT);
      return DAG.getNode(ISD::AND, dl, VT, SumDiff, Not);
    }
    // Overflow ? 0 : (LHS - RHS)
    return DAG.getSelect(dl, VT, Overflow, Zero, SumDiff);
  }

  if (Opcode == ISD::SADDSAT || Opcode == ISD::SSUBSAT) {
    APInt MinVal = APInt::getSignedMinValue(BitWidth);
    APInt MaxVal = APInt::getSignedMaxValue(BitWidth);

    KnownBits KnownLHS = DAG.computeKnownBits(LHS);
    KnownBits KnownRHS = DAG.computeKnownBits(RHS);

    // If either of the operand signs are known, then they are guaranteed to
    // only saturate in one direction. If non-negative they will saturate
    // towards SIGNED_MAX, if negative they will saturate towards SIGNED_MIN.
    //
    // In the case of ISD::SSUBSAT, 'x - y' is equivalent to 'x + (-y)', so the
    // sign of 'y' has to be flipped.

    bool LHSIsNonNegative = KnownLHS.isNonNegative();
    bool RHSIsNonNegative = Opcode == ISD::SADDSAT ? KnownRHS.isNonNegative()
                                                   : KnownRHS.isNegative();
    if (LHSIsNonNegative || RHSIsNonNegative) {
      SDValue SatMax = DAG.getConstant(MaxVal, dl, VT);
      return DAG.getSelect(dl, VT, Overflow, SatMax, SumDiff);
    }

    bool LHSIsNegative = KnownLHS.isNegative();
    bool RHSIsNegative = Opcode == ISD::SADDSAT ? KnownRHS.isNegative()
                                                : KnownRHS.isNonNegative();
    if (LHSIsNegative || RHSIsNegative) {
      SDValue SatMin = DAG.getConstant(MinVal, dl, VT);
      return DAG.getSelect(dl, VT, Overflow, SatMin, SumDiff);
    }
  }

  // Overflow ? (SumDiff >> BW) ^ MinVal : SumDiff
  APInt MinVal = APInt::getSignedMinValue(BitWidth);
  SDValue SatMin = DAG.getConstant(MinVal, dl, VT);
  SDValue Shift = DAG.getNode(ISD::SRA, dl, VT, SumDiff,
                              DAG.getConstant(BitWidth - 1, dl, VT));
  Result = DAG.getNode(ISD::XOR, dl, VT, Shift, SatMin);
  return DAG.getSelect(dl, VT, Overflow, Result, SumDiff);
}

SDValue TargetLowering::expandCMP(SDNode *Node, SelectionDAG &DAG) const {
  unsigned Opcode = Node->getOpcode();
  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  EVT VT = LHS.getValueType();
  EVT ResVT = Node->getValueType(0);
  EVT BoolVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDLoc dl(Node);

  auto LTPredicate = (Opcode == ISD::UCMP ? ISD::SETULT : ISD::SETLT);
  auto GTPredicate = (Opcode == ISD::UCMP ? ISD::SETUGT : ISD::SETGT);
  SDValue IsLT = DAG.getSetCC(dl, BoolVT, LHS, RHS, LTPredicate);
  SDValue IsGT = DAG.getSetCC(dl, BoolVT, LHS, RHS, GTPredicate);

  // We can't perform arithmetic on i1 values. Extending them would
  // probably result in worse codegen, so let's just use two selects instead.
  // Some targets are also just better off using selects rather than subtraction
  // because one of the conditions can be merged with one of the selects.
  // And finally, if we don't know the contents of high bits of a boolean value
  // we can't perform any arithmetic either.
  if (shouldExpandCmpUsingSelects() || BoolVT.getScalarSizeInBits() == 1 ||
      getBooleanContents(BoolVT) == UndefinedBooleanContent) {
    SDValue SelectZeroOrOne =
        DAG.getSelect(dl, ResVT, IsGT, DAG.getConstant(1, dl, ResVT),
                      DAG.getConstant(0, dl, ResVT));
    return DAG.getSelect(dl, ResVT, IsLT, DAG.getConstant(-1, dl, ResVT),
                         SelectZeroOrOne);
  }

  if (getBooleanContents(BoolVT) == ZeroOrNegativeOneBooleanContent)
    std::swap(IsGT, IsLT);
  return DAG.getSExtOrTrunc(DAG.getNode(ISD::SUB, dl, BoolVT, IsGT, IsLT), dl,
                            ResVT);
}

SDValue TargetLowering::expandShlSat(SDNode *Node, SelectionDAG &DAG) const {
  unsigned Opcode = Node->getOpcode();
  bool IsSigned = Opcode == ISD::SSHLSAT;
  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  EVT VT = LHS.getValueType();
  SDLoc dl(Node);

  assert((Node->getOpcode() == ISD::SSHLSAT ||
          Node->getOpcode() == ISD::USHLSAT) &&
          "Expected a SHLSAT opcode");
  assert(VT == RHS.getValueType() && "Expected operands to be the same type");
  assert(VT.isInteger() && "Expected operands to be integers");

  if (VT.isVector() && !isOperationLegalOrCustom(ISD::VSELECT, VT))
    return DAG.UnrollVectorOp(Node);

  // If LHS != (LHS << RHS) >> RHS, we have overflow and must saturate.

  unsigned BW = VT.getScalarSizeInBits();
  EVT BoolVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue Result = DAG.getNode(ISD::SHL, dl, VT, LHS, RHS);
  SDValue Orig =
      DAG.getNode(IsSigned ? ISD::SRA : ISD::SRL, dl, VT, Result, RHS);

  SDValue SatVal;
  if (IsSigned) {
    SDValue SatMin = DAG.getConstant(APInt::getSignedMinValue(BW), dl, VT);
    SDValue SatMax = DAG.getConstant(APInt::getSignedMaxValue(BW), dl, VT);
    SDValue Cond =
        DAG.getSetCC(dl, BoolVT, LHS, DAG.getConstant(0, dl, VT), ISD::SETLT);
    SatVal = DAG.getSelect(dl, VT, Cond, SatMin, SatMax);
  } else {
    SatVal = DAG.getConstant(APInt::getMaxValue(BW), dl, VT);
  }
  SDValue Cond = DAG.getSetCC(dl, BoolVT, LHS, Orig, ISD::SETNE);
  return DAG.getSelect(dl, VT, Cond, SatVal, Result);
}

void TargetLowering::forceExpandWideMUL(SelectionDAG &DAG, const SDLoc &dl,
                                        bool Signed, EVT WideVT,
                                        const SDValue LL, const SDValue LH,
                                        const SDValue RL, const SDValue RH,
                                        SDValue &Lo, SDValue &Hi) const {
  // We can fall back to a libcall with an illegal type for the MUL if we
  // have a libcall big enough.
  // Also, we can fall back to a division in some cases, but that's a big
  // performance hit in the general case.
  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  if (WideVT == MVT::i16)
    LC = RTLIB::MUL_I16;
  else if (WideVT == MVT::i32)
    LC = RTLIB::MUL_I32;
  else if (WideVT == MVT::i64)
    LC = RTLIB::MUL_I64;
  else if (WideVT == MVT::i128)
    LC = RTLIB::MUL_I128;

  if (LC == RTLIB::UNKNOWN_LIBCALL || !getLibcallName(LC)) {
    // We'll expand the multiplication by brute force because we have no other
    // options. This is a trivially-generalized version of the code from
    // Hacker's Delight (itself derived from Knuth's Algorithm M from section
    // 4.3.1).
    EVT VT = LL.getValueType();
    unsigned Bits = VT.getSizeInBits();
    unsigned HalfBits = Bits >> 1;
    SDValue Mask =
        DAG.getConstant(APInt::getLowBitsSet(Bits, HalfBits), dl, VT);
    SDValue LLL = DAG.getNode(ISD::AND, dl, VT, LL, Mask);
    SDValue RLL = DAG.getNode(ISD::AND, dl, VT, RL, Mask);

    SDValue T = DAG.getNode(ISD::MUL, dl, VT, LLL, RLL);
    SDValue TL = DAG.getNode(ISD::AND, dl, VT, T, Mask);

    SDValue Shift = DAG.getShiftAmountConstant(HalfBits, VT, dl);
    SDValue TH = DAG.getNode(ISD::SRL, dl, VT, T, Shift);
    SDValue LLH = DAG.getNode(ISD::SRL, dl, VT, LL, Shift);
    SDValue RLH = DAG.getNode(ISD::SRL, dl, VT, RL, Shift);

    SDValue U = DAG.getNode(ISD::ADD, dl, VT,
                            DAG.getNode(ISD::MUL, dl, VT, LLH, RLL), TH);
    SDValue UL = DAG.getNode(ISD::AND, dl, VT, U, Mask);
    SDValue UH = DAG.getNode(ISD::SRL, dl, VT, U, Shift);

    SDValue V = DAG.getNode(ISD::ADD, dl, VT,
                            DAG.getNode(ISD::MUL, dl, VT, LLL, RLH), UL);
    SDValue VH = DAG.getNode(ISD::SRL, dl, VT, V, Shift);

    SDValue W =
        DAG.getNode(ISD::ADD, dl, VT, DAG.getNode(ISD::MUL, dl, VT, LLH, RLH),
                    DAG.getNode(ISD::ADD, dl, VT, UH, VH));
    Lo = DAG.getNode(ISD::ADD, dl, VT, TL,
                     DAG.getNode(ISD::SHL, dl, VT, V, Shift));

    Hi = DAG.getNode(ISD::ADD, dl, VT, W,
                     DAG.getNode(ISD::ADD, dl, VT,
                                 DAG.getNode(ISD::MUL, dl, VT, RH, LL),
                                 DAG.getNode(ISD::MUL, dl, VT, RL, LH)));
  } else {
    // Attempt a libcall.
    SDValue Ret;
    TargetLowering::MakeLibCallOptions CallOptions;
    CallOptions.setSExt(Signed);
    CallOptions.setIsPostTypeLegalization(true);
    if (shouldSplitFunctionArgumentsAsLittleEndian(DAG.getDataLayout())) {
      // Halves of WideVT are packed into registers in different order
      // depending on platform endianness. This is usually handled by
      // the C calling convention, but we can't defer to it in
      // the legalizer.
      SDValue Args[] = {LL, LH, RL, RH};
      Ret = makeLibCall(DAG, LC, WideVT, Args, CallOptions, dl).first;
    } else {
      SDValue Args[] = {LH, LL, RH, RL};
      Ret = makeLibCall(DAG, LC, WideVT, Args, CallOptions, dl).first;
    }
    assert(Ret.getOpcode() == ISD::MERGE_VALUES &&
           "Ret value is a collection of constituent nodes holding result.");
    if (DAG.getDataLayout().isLittleEndian()) {
      // Same as above.
      Lo = Ret.getOperand(0);
      Hi = Ret.getOperand(1);
    } else {
      Lo = Ret.getOperand(1);
      Hi = Ret.getOperand(0);
    }
  }
}

void TargetLowering::forceExpandWideMUL(SelectionDAG &DAG, const SDLoc &dl,
                                        bool Signed, const SDValue LHS,
                                        const SDValue RHS, SDValue &Lo,
                                        SDValue &Hi) const {
  EVT VT = LHS.getValueType();
  assert(RHS.getValueType() == VT && "Mismatching operand types");

  SDValue HiLHS;
  SDValue HiRHS;
  if (Signed) {
    // The high part is obtained by SRA'ing all but one of the bits of low
    // part.
    unsigned LoSize = VT.getFixedSizeInBits();
    HiLHS = DAG.getNode(
        ISD::SRA, dl, VT, LHS,
        DAG.getConstant(LoSize - 1, dl, getPointerTy(DAG.getDataLayout())));
    HiRHS = DAG.getNode(
        ISD::SRA, dl, VT, RHS,
        DAG.getConstant(LoSize - 1, dl, getPointerTy(DAG.getDataLayout())));
  } else {
    HiLHS = DAG.getConstant(0, dl, VT);
    HiRHS = DAG.getConstant(0, dl, VT);
  }
  EVT WideVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits() * 2);
  forceExpandWideMUL(DAG, dl, Signed, WideVT, LHS, HiLHS, RHS, HiRHS, Lo, Hi);
}

SDValue
TargetLowering::expandFixedPointMul(SDNode *Node, SelectionDAG &DAG) const {
  assert((Node->getOpcode() == ISD::SMULFIX ||
          Node->getOpcode() == ISD::UMULFIX ||
          Node->getOpcode() == ISD::SMULFIXSAT ||
          Node->getOpcode() == ISD::UMULFIXSAT) &&
         "Expected a fixed point multiplication opcode");

  SDLoc dl(Node);
  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  EVT VT = LHS.getValueType();
  unsigned Scale = Node->getConstantOperandVal(2);
  bool Saturating = (Node->getOpcode() == ISD::SMULFIXSAT ||
                     Node->getOpcode() == ISD::UMULFIXSAT);
  bool Signed = (Node->getOpcode() == ISD::SMULFIX ||
                 Node->getOpcode() == ISD::SMULFIXSAT);
  EVT BoolVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  unsigned VTSize = VT.getScalarSizeInBits();

  if (!Scale) {
    // [us]mul.fix(a, b, 0) -> mul(a, b)
    if (!Saturating) {
      if (isOperationLegalOrCustom(ISD::MUL, VT))
        return DAG.getNode(ISD::MUL, dl, VT, LHS, RHS);
    } else if (Signed && isOperationLegalOrCustom(ISD::SMULO, VT)) {
      SDValue Result =
          DAG.getNode(ISD::SMULO, dl, DAG.getVTList(VT, BoolVT), LHS, RHS);
      SDValue Product = Result.getValue(0);
      SDValue Overflow = Result.getValue(1);
      SDValue Zero = DAG.getConstant(0, dl, VT);

      APInt MinVal = APInt::getSignedMinValue(VTSize);
      APInt MaxVal = APInt::getSignedMaxValue(VTSize);
      SDValue SatMin = DAG.getConstant(MinVal, dl, VT);
      SDValue SatMax = DAG.getConstant(MaxVal, dl, VT);
      // Xor the inputs, if resulting sign bit is 0 the product will be
      // positive, else negative.
      SDValue Xor = DAG.getNode(ISD::XOR, dl, VT, LHS, RHS);
      SDValue ProdNeg = DAG.getSetCC(dl, BoolVT, Xor, Zero, ISD::SETLT);
      Result = DAG.getSelect(dl, VT, ProdNeg, SatMin, SatMax);
      return DAG.getSelect(dl, VT, Overflow, Result, Product);
    } else if (!Signed && isOperationLegalOrCustom(ISD::UMULO, VT)) {
      SDValue Result =
          DAG.getNode(ISD::UMULO, dl, DAG.getVTList(VT, BoolVT), LHS, RHS);
      SDValue Product = Result.getValue(0);
      SDValue Overflow = Result.getValue(1);

      APInt MaxVal = APInt::getMaxValue(VTSize);
      SDValue SatMax = DAG.getConstant(MaxVal, dl, VT);
      return DAG.getSelect(dl, VT, Overflow, SatMax, Product);
    }
  }

  assert(((Signed && Scale < VTSize) || (!Signed && Scale <= VTSize)) &&
         "Expected scale to be less than the number of bits if signed or at "
         "most the number of bits if unsigned.");
  assert(LHS.getValueType() == RHS.getValueType() &&
         "Expected both operands to be the same type");

  // Get the upper and lower bits of the result.
  SDValue Lo, Hi;
  unsigned LoHiOp = Signed ? ISD::SMUL_LOHI : ISD::UMUL_LOHI;
  unsigned HiOp = Signed ? ISD::MULHS : ISD::MULHU;
  EVT WideVT = EVT::getIntegerVT(*DAG.getContext(), VTSize * 2);
  if (isOperationLegalOrCustom(LoHiOp, VT)) {
    SDValue Result = DAG.getNode(LoHiOp, dl, DAG.getVTList(VT, VT), LHS, RHS);
    Lo = Result.getValue(0);
    Hi = Result.getValue(1);
  } else if (isOperationLegalOrCustom(HiOp, VT)) {
    Lo = DAG.getNode(ISD::MUL, dl, VT, LHS, RHS);
    Hi = DAG.getNode(HiOp, dl, VT, LHS, RHS);
  } else if (isOperationLegalOrCustom(ISD::MUL, WideVT)) {
    // Try for a multiplication using a wider type.
    unsigned Ext = Signed ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    SDValue LHSExt = DAG.getNode(Ext, dl, WideVT, LHS);
    SDValue RHSExt = DAG.getNode(Ext, dl, WideVT, RHS);
    SDValue Res = DAG.getNode(ISD::MUL, dl, WideVT, LHSExt, RHSExt);
    Lo = DAG.getNode(ISD::TRUNCATE, dl, VT, Res);
    SDValue Shifted =
        DAG.getNode(ISD::SRA, dl, WideVT, Res,
                    DAG.getShiftAmountConstant(VTSize, WideVT, dl));
    Hi = DAG.getNode(ISD::TRUNCATE, dl, VT, Shifted);
  } else if (VT.isVector()) {
    return SDValue();
  } else {
    forceExpandWideMUL(DAG, dl, Signed, LHS, RHS, Lo, Hi);
  }

  if (Scale == VTSize)
    // Result is just the top half since we'd be shifting by the width of the
    // operand. Overflow impossible so this works for both UMULFIX and
    // UMULFIXSAT.
    return Hi;

  // The result will need to be shifted right by the scale since both operands
  // are scaled. The result is given to us in 2 halves, so we only want part of
  // both in the result.
  SDValue Result = DAG.getNode(ISD::FSHR, dl, VT, Hi, Lo,
                               DAG.getShiftAmountConstant(Scale, VT, dl));
  if (!Saturating)
    return Result;

  if (!Signed) {
    // Unsigned overflow happened if the upper (VTSize - Scale) bits (of the
    // widened multiplication) aren't all zeroes.

    // Saturate to max if ((Hi >> Scale) != 0),
    // which is the same as if (Hi > ((1 << Scale) - 1))
    APInt MaxVal = APInt::getMaxValue(VTSize);
    SDValue LowMask = DAG.getConstant(APInt::getLowBitsSet(VTSize, Scale),
                                      dl, VT);
    Result = DAG.getSelectCC(dl, Hi, LowMask,
                             DAG.getConstant(MaxVal, dl, VT), Result,
                             ISD::SETUGT);

    return Result;
  }

  // Signed overflow happened if the upper (VTSize - Scale + 1) bits (of the
  // widened multiplication) aren't all ones or all zeroes.

  SDValue SatMin = DAG.getConstant(APInt::getSignedMinValue(VTSize), dl, VT);
  SDValue SatMax = DAG.getConstant(APInt::getSignedMaxValue(VTSize), dl, VT);

  if (Scale == 0) {
    SDValue Sign = DAG.getNode(ISD::SRA, dl, VT, Lo,
                               DAG.getShiftAmountConstant(VTSize - 1, VT, dl));
    SDValue Overflow = DAG.getSetCC(dl, BoolVT, Hi, Sign, ISD::SETNE);
    // Saturated to SatMin if wide product is negative, and SatMax if wide
    // product is positive ...
    SDValue Zero = DAG.getConstant(0, dl, VT);
    SDValue ResultIfOverflow = DAG.getSelectCC(dl, Hi, Zero, SatMin, SatMax,
                                               ISD::SETLT);
    // ... but only if we overflowed.
    return DAG.getSelect(dl, VT, Overflow, ResultIfOverflow, Result);
  }

  //  We handled Scale==0 above so all the bits to examine is in Hi.

  // Saturate to max if ((Hi >> (Scale - 1)) > 0),
  // which is the same as if (Hi > (1 << (Scale - 1)) - 1)
  SDValue LowMask = DAG.getConstant(APInt::getLowBitsSet(VTSize, Scale - 1),
                                    dl, VT);
  Result = DAG.getSelectCC(dl, Hi, LowMask, SatMax, Result, ISD::SETGT);
  // Saturate to min if (Hi >> (Scale - 1)) < -1),
  // which is the same as if (HI < (-1 << (Scale - 1))
  SDValue HighMask =
      DAG.getConstant(APInt::getHighBitsSet(VTSize, VTSize - Scale + 1),
                      dl, VT);
  Result = DAG.getSelectCC(dl, Hi, HighMask, SatMin, Result, ISD::SETLT);
  return Result;
}

SDValue
TargetLowering::expandFixedPointDiv(unsigned Opcode, const SDLoc &dl,
                                    SDValue LHS, SDValue RHS,
                                    unsigned Scale, SelectionDAG &DAG) const {
  assert((Opcode == ISD::SDIVFIX || Opcode == ISD::SDIVFIXSAT ||
          Opcode == ISD::UDIVFIX || Opcode == ISD::UDIVFIXSAT) &&
         "Expected a fixed point division opcode");

  EVT VT = LHS.getValueType();
  bool Signed = Opcode == ISD::SDIVFIX || Opcode == ISD::SDIVFIXSAT;
  bool Saturating = Opcode == ISD::SDIVFIXSAT || Opcode == ISD::UDIVFIXSAT;
  EVT BoolVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  // If there is enough room in the type to upscale the LHS or downscale the
  // RHS before the division, we can perform it in this type without having to
  // resize. For signed operations, the LHS headroom is the number of
  // redundant sign bits, and for unsigned ones it is the number of zeroes.
  // The headroom for the RHS is the number of trailing zeroes.
  unsigned LHSLead = Signed ? DAG.ComputeNumSignBits(LHS) - 1
                            : DAG.computeKnownBits(LHS).countMinLeadingZeros();
  unsigned RHSTrail = DAG.computeKnownBits(RHS).countMinTrailingZeros();

  // For signed saturating operations, we need to be able to detect true integer
  // division overflow; that is, when you have MIN / -EPS. However, this
  // is undefined behavior and if we emit divisions that could take such
  // values it may cause undesired behavior (arithmetic exceptions on x86, for
  // example).
  // Avoid this by requiring an extra bit so that we never get this case.
  // FIXME: This is a bit unfortunate as it means that for an 8-bit 7-scale
  // signed saturating division, we need to emit a whopping 32-bit division.
  if (LHSLead + RHSTrail < Scale + (unsigned)(Saturating && Signed))
    return SDValue();

  unsigned LHSShift = std::min(LHSLead, Scale);
  unsigned RHSShift = Scale - LHSShift;

  // At this point, we know that if we shift the LHS up by LHSShift and the
  // RHS down by RHSShift, we can emit a regular division with a final scaling
  // factor of Scale.

  if (LHSShift)
    LHS = DAG.getNode(ISD::SHL, dl, VT, LHS,
                      DAG.getShiftAmountConstant(LHSShift, VT, dl));
  if (RHSShift)
    RHS = DAG.getNode(Signed ? ISD::SRA : ISD::SRL, dl, VT, RHS,
                      DAG.getShiftAmountConstant(RHSShift, VT, dl));

  SDValue Quot;
  if (Signed) {
    // For signed operations, if the resulting quotient is negative and the
    // remainder is nonzero, subtract 1 from the quotient to round towards
    // negative infinity.
    SDValue Rem;
    // FIXME: Ideally we would always produce an SDIVREM here, but if the
    // type isn't legal, SDIVREM cannot be expanded. There is no reason why
    // we couldn't just form a libcall, but the type legalizer doesn't do it.
    if (isTypeLegal(VT) &&
        isOperationLegalOrCustom(ISD::SDIVREM, VT)) {
      Quot = DAG.getNode(ISD::SDIVREM, dl,
                         DAG.getVTList(VT, VT),
                         LHS, RHS);
      Rem = Quot.getValue(1);
      Quot = Quot.getValue(0);
    } else {
      Quot = DAG.getNode(ISD::SDIV, dl, VT,
                         LHS, RHS);
      Rem = DAG.getNode(ISD::SREM, dl, VT,
                        LHS, RHS);
    }
    SDValue Zero = DAG.getConstant(0, dl, VT);
    SDValue RemNonZero = DAG.getSetCC(dl, BoolVT, Rem, Zero, ISD::SETNE);
    SDValue LHSNeg = DAG.getSetCC(dl, BoolVT, LHS, Zero, ISD::SETLT);
    SDValue RHSNeg = DAG.getSetCC(dl, BoolVT, RHS, Zero, ISD::SETLT);
    SDValue QuotNeg = DAG.getNode(ISD::XOR, dl, BoolVT, LHSNeg, RHSNeg);
    SDValue Sub1 = DAG.getNode(ISD::SUB, dl, VT, Quot,
                               DAG.getConstant(1, dl, VT));
    Quot = DAG.getSelect(dl, VT,
                         DAG.getNode(ISD::AND, dl, BoolVT, RemNonZero, QuotNeg),
                         Sub1, Quot);
  } else
    Quot = DAG.getNode(ISD::UDIV, dl, VT,
                       LHS, RHS);

  return Quot;
}

void TargetLowering::expandUADDSUBO(
    SDNode *Node, SDValue &Result, SDValue &Overflow, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  bool IsAdd = Node->getOpcode() == ISD::UADDO;

  // If UADDO_CARRY/SUBO_CARRY is legal, use that instead.
  unsigned OpcCarry = IsAdd ? ISD::UADDO_CARRY : ISD::USUBO_CARRY;
  if (isOperationLegalOrCustom(OpcCarry, Node->getValueType(0))) {
    SDValue CarryIn = DAG.getConstant(0, dl, Node->getValueType(1));
    SDValue NodeCarry = DAG.getNode(OpcCarry, dl, Node->getVTList(),
                                    { LHS, RHS, CarryIn });
    Result = SDValue(NodeCarry.getNode(), 0);
    Overflow = SDValue(NodeCarry.getNode(), 1);
    return;
  }

  Result = DAG.getNode(IsAdd ? ISD::ADD : ISD::SUB, dl,
                            LHS.getValueType(), LHS, RHS);

  EVT ResultType = Node->getValueType(1);
  EVT SetCCType = getSetCCResultType(
      DAG.getDataLayout(), *DAG.getContext(), Node->getValueType(0));
  SDValue SetCC;
  if (IsAdd && isOneConstant(RHS)) {
    // Special case: uaddo X, 1 overflowed if X+1 is 0. This potential reduces
    // the live range of X. We assume comparing with 0 is cheap.
    // The general case (X + C) < C is not necessarily beneficial. Although we
    // reduce the live range of X, we may introduce the materialization of
    // constant C.
    SetCC =
        DAG.getSetCC(dl, SetCCType, Result,
                     DAG.getConstant(0, dl, Node->getValueType(0)), ISD::SETEQ);
  } else if (IsAdd && isAllOnesConstant(RHS)) {
    // Special case: uaddo X, -1 overflows if X != 0.
    SetCC =
        DAG.getSetCC(dl, SetCCType, LHS,
                     DAG.getConstant(0, dl, Node->getValueType(0)), ISD::SETNE);
  } else {
    ISD::CondCode CC = IsAdd ? ISD::SETULT : ISD::SETUGT;
    SetCC = DAG.getSetCC(dl, SetCCType, Result, LHS, CC);
  }
  Overflow = DAG.getBoolExtOrTrunc(SetCC, dl, ResultType, ResultType);
}

void TargetLowering::expandSADDSUBO(
    SDNode *Node, SDValue &Result, SDValue &Overflow, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  bool IsAdd = Node->getOpcode() == ISD::SADDO;

  Result = DAG.getNode(IsAdd ? ISD::ADD : ISD::SUB, dl,
                            LHS.getValueType(), LHS, RHS);

  EVT ResultType = Node->getValueType(1);
  EVT OType = getSetCCResultType(
      DAG.getDataLayout(), *DAG.getContext(), Node->getValueType(0));

  // If SADDSAT/SSUBSAT is legal, compare results to detect overflow.
  unsigned OpcSat = IsAdd ? ISD::SADDSAT : ISD::SSUBSAT;
  if (isOperationLegal(OpcSat, LHS.getValueType())) {
    SDValue Sat = DAG.getNode(OpcSat, dl, LHS.getValueType(), LHS, RHS);
    SDValue SetCC = DAG.getSetCC(dl, OType, Result, Sat, ISD::SETNE);
    Overflow = DAG.getBoolExtOrTrunc(SetCC, dl, ResultType, ResultType);
    return;
  }

  SDValue Zero = DAG.getConstant(0, dl, LHS.getValueType());

  // For an addition, the result should be less than one of the operands (LHS)
  // if and only if the other operand (RHS) is negative, otherwise there will
  // be overflow.
  // For a subtraction, the result should be less than one of the operands
  // (LHS) if and only if the other operand (RHS) is (non-zero) positive,
  // otherwise there will be overflow.
  SDValue ResultLowerThanLHS = DAG.getSetCC(dl, OType, Result, LHS, ISD::SETLT);
  SDValue ConditionRHS =
      DAG.getSetCC(dl, OType, RHS, Zero, IsAdd ? ISD::SETLT : ISD::SETGT);

  Overflow = DAG.getBoolExtOrTrunc(
      DAG.getNode(ISD::XOR, dl, OType, ConditionRHS, ResultLowerThanLHS), dl,
      ResultType, ResultType);
}

bool TargetLowering::expandMULO(SDNode *Node, SDValue &Result,
                                SDValue &Overflow, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  bool isSigned = Node->getOpcode() == ISD::SMULO;

  // For power-of-two multiplications we can use a simpler shift expansion.
  if (ConstantSDNode *RHSC = isConstOrConstSplat(RHS)) {
    const APInt &C = RHSC->getAPIntValue();
    // mulo(X, 1 << S) -> { X << S, (X << S) >> S != X }
    if (C.isPowerOf2()) {
      // smulo(x, signed_min) is same as umulo(x, signed_min).
      bool UseArithShift = isSigned && !C.isMinSignedValue();
      SDValue ShiftAmt = DAG.getShiftAmountConstant(C.logBase2(), VT, dl);
      Result = DAG.getNode(ISD::SHL, dl, VT, LHS, ShiftAmt);
      Overflow = DAG.getSetCC(dl, SetCCVT,
          DAG.getNode(UseArithShift ? ISD::SRA : ISD::SRL,
                      dl, VT, Result, ShiftAmt),
          LHS, ISD::SETNE);
      return true;
    }
  }

  EVT WideVT = EVT::getIntegerVT(*DAG.getContext(), VT.getScalarSizeInBits() * 2);
  if (VT.isVector())
    WideVT =
        EVT::getVectorVT(*DAG.getContext(), WideVT, VT.getVectorElementCount());

  SDValue BottomHalf;
  SDValue TopHalf;
  static const unsigned Ops[2][3] =
      { { ISD::MULHU, ISD::UMUL_LOHI, ISD::ZERO_EXTEND },
        { ISD::MULHS, ISD::SMUL_LOHI, ISD::SIGN_EXTEND }};
  if (isOperationLegalOrCustom(Ops[isSigned][0], VT)) {
    BottomHalf = DAG.getNode(ISD::MUL, dl, VT, LHS, RHS);
    TopHalf = DAG.getNode(Ops[isSigned][0], dl, VT, LHS, RHS);
  } else if (isOperationLegalOrCustom(Ops[isSigned][1], VT)) {
    BottomHalf = DAG.getNode(Ops[isSigned][1], dl, DAG.getVTList(VT, VT), LHS,
                             RHS);
    TopHalf = BottomHalf.getValue(1);
  } else if (isTypeLegal(WideVT)) {
    LHS = DAG.getNode(Ops[isSigned][2], dl, WideVT, LHS);
    RHS = DAG.getNode(Ops[isSigned][2], dl, WideVT, RHS);
    SDValue Mul = DAG.getNode(ISD::MUL, dl, WideVT, LHS, RHS);
    BottomHalf = DAG.getNode(ISD::TRUNCATE, dl, VT, Mul);
    SDValue ShiftAmt =
        DAG.getShiftAmountConstant(VT.getScalarSizeInBits(), WideVT, dl);
    TopHalf = DAG.getNode(ISD::TRUNCATE, dl, VT,
                          DAG.getNode(ISD::SRL, dl, WideVT, Mul, ShiftAmt));
  } else {
    if (VT.isVector())
      return false;

    forceExpandWideMUL(DAG, dl, isSigned, LHS, RHS, BottomHalf, TopHalf);
  }

  Result = BottomHalf;
  if (isSigned) {
    SDValue ShiftAmt = DAG.getShiftAmountConstant(
        VT.getScalarSizeInBits() - 1, BottomHalf.getValueType(), dl);
    SDValue Sign = DAG.getNode(ISD::SRA, dl, VT, BottomHalf, ShiftAmt);
    Overflow = DAG.getSetCC(dl, SetCCVT, TopHalf, Sign, ISD::SETNE);
  } else {
    Overflow = DAG.getSetCC(dl, SetCCVT, TopHalf,
                            DAG.getConstant(0, dl, VT), ISD::SETNE);
  }

  // Truncate the result if SetCC returns a larger type than needed.
  EVT RType = Node->getValueType(1);
  if (RType.bitsLT(Overflow.getValueType()))
    Overflow = DAG.getNode(ISD::TRUNCATE, dl, RType, Overflow);

  assert(RType.getSizeInBits() == Overflow.getValueSizeInBits() &&
         "Unexpected result type for S/UMULO legalization");
  return true;
}

SDValue TargetLowering::expandVecReduce(SDNode *Node, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  unsigned BaseOpcode = ISD::getVecReduceBaseOpcode(Node->getOpcode());
  SDValue Op = Node->getOperand(0);
  EVT VT = Op.getValueType();

  if (VT.isScalableVector())
    report_fatal_error(
        "Expanding reductions for scalable vectors is undefined.");

  // Try to use a shuffle reduction for power of two vectors.
  if (VT.isPow2VectorType()) {
    while (VT.getVectorNumElements() > 1) {
      EVT HalfVT = VT.getHalfNumVectorElementsVT(*DAG.getContext());
      if (!isOperationLegalOrCustom(BaseOpcode, HalfVT))
        break;

      SDValue Lo, Hi;
      std::tie(Lo, Hi) = DAG.SplitVector(Op, dl);
      Op = DAG.getNode(BaseOpcode, dl, HalfVT, Lo, Hi, Node->getFlags());
      VT = HalfVT;
    }
  }

  EVT EltVT = VT.getVectorElementType();
  unsigned NumElts = VT.getVectorNumElements();

  SmallVector<SDValue, 8> Ops;
  DAG.ExtractVectorElements(Op, Ops, 0, NumElts);

  SDValue Res = Ops[0];
  for (unsigned i = 1; i < NumElts; i++)
    Res = DAG.getNode(BaseOpcode, dl, EltVT, Res, Ops[i], Node->getFlags());

  // Result type may be wider than element type.
  if (EltVT != Node->getValueType(0))
    Res = DAG.getNode(ISD::ANY_EXTEND, dl, Node->getValueType(0), Res);
  return Res;
}

SDValue TargetLowering::expandVecReduceSeq(SDNode *Node, SelectionDAG &DAG) const {
  SDLoc dl(Node);
  SDValue AccOp = Node->getOperand(0);
  SDValue VecOp = Node->getOperand(1);
  SDNodeFlags Flags = Node->getFlags();

  EVT VT = VecOp.getValueType();
  EVT EltVT = VT.getVectorElementType();

  if (VT.isScalableVector())
    report_fatal_error(
        "Expanding reductions for scalable vectors is undefined.");

  unsigned NumElts = VT.getVectorNumElements();

  SmallVector<SDValue, 8> Ops;
  DAG.ExtractVectorElements(VecOp, Ops, 0, NumElts);

  unsigned BaseOpcode = ISD::getVecReduceBaseOpcode(Node->getOpcode());

  SDValue Res = AccOp;
  for (unsigned i = 0; i < NumElts; i++)
    Res = DAG.getNode(BaseOpcode, dl, EltVT, Res, Ops[i], Flags);

  return Res;
}

bool TargetLowering::expandREM(SDNode *Node, SDValue &Result,
                               SelectionDAG &DAG) const {
  EVT VT = Node->getValueType(0);
  SDLoc dl(Node);
  bool isSigned = Node->getOpcode() == ISD::SREM;
  unsigned DivOpc = isSigned ? ISD::SDIV : ISD::UDIV;
  unsigned DivRemOpc = isSigned ? ISD::SDIVREM : ISD::UDIVREM;
  SDValue Dividend = Node->getOperand(0);
  SDValue Divisor = Node->getOperand(1);
  if (isOperationLegalOrCustom(DivRemOpc, VT)) {
    SDVTList VTs = DAG.getVTList(VT, VT);
    Result = DAG.getNode(DivRemOpc, dl, VTs, Dividend, Divisor).getValue(1);
    return true;
  }
  if (isOperationLegalOrCustom(DivOpc, VT)) {
    // X % Y -> X-X/Y*Y
    SDValue Divide = DAG.getNode(DivOpc, dl, VT, Dividend, Divisor);
    SDValue Mul = DAG.getNode(ISD::MUL, dl, VT, Divide, Divisor);
    Result = DAG.getNode(ISD::SUB, dl, VT, Dividend, Mul);
    return true;
  }
  return false;
}

SDValue TargetLowering::expandFP_TO_INT_SAT(SDNode *Node,
                                            SelectionDAG &DAG) const {
  bool IsSigned = Node->getOpcode() == ISD::FP_TO_SINT_SAT;
  SDLoc dl(SDValue(Node, 0));
  SDValue Src = Node->getOperand(0);

  // DstVT is the result type, while SatVT is the size to which we saturate
  EVT SrcVT = Src.getValueType();
  EVT DstVT = Node->getValueType(0);

  EVT SatVT = cast<VTSDNode>(Node->getOperand(1))->getVT();
  unsigned SatWidth = SatVT.getScalarSizeInBits();
  unsigned DstWidth = DstVT.getScalarSizeInBits();
  assert(SatWidth <= DstWidth &&
         "Expected saturation width smaller than result width");

  // Determine minimum and maximum integer values and their corresponding
  // floating-point values.
  APInt MinInt, MaxInt;
  if (IsSigned) {
    MinInt = APInt::getSignedMinValue(SatWidth).sext(DstWidth);
    MaxInt = APInt::getSignedMaxValue(SatWidth).sext(DstWidth);
  } else {
    MinInt = APInt::getMinValue(SatWidth).zext(DstWidth);
    MaxInt = APInt::getMaxValue(SatWidth).zext(DstWidth);
  }

  // We cannot risk emitting FP_TO_XINT nodes with a source VT of [b]f16, as
  // libcall emission cannot handle this. Large result types will fail.
  if (SrcVT == MVT::f16 || SrcVT == MVT::bf16) {
    Src = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f32, Src);
    SrcVT = Src.getValueType();
  }

  APFloat MinFloat(DAG.EVTToAPFloatSemantics(SrcVT));
  APFloat MaxFloat(DAG.EVTToAPFloatSemantics(SrcVT));

  APFloat::opStatus MinStatus =
      MinFloat.convertFromAPInt(MinInt, IsSigned, APFloat::rmTowardZero);
  APFloat::opStatus MaxStatus =
      MaxFloat.convertFromAPInt(MaxInt, IsSigned, APFloat::rmTowardZero);
  bool AreExactFloatBounds = !(MinStatus & APFloat::opStatus::opInexact) &&
                             !(MaxStatus & APFloat::opStatus::opInexact);

  SDValue MinFloatNode = DAG.getConstantFP(MinFloat, dl, SrcVT);
  SDValue MaxFloatNode = DAG.getConstantFP(MaxFloat, dl, SrcVT);

  // If the integer bounds are exactly representable as floats and min/max are
  // legal, emit a min+max+fptoi sequence. Otherwise we have to use a sequence
  // of comparisons and selects.
  bool MinMaxLegal = isOperationLegal(ISD::FMINNUM, SrcVT) &&
                     isOperationLegal(ISD::FMAXNUM, SrcVT);
  if (AreExactFloatBounds && MinMaxLegal) {
    SDValue Clamped = Src;

    // Clamp Src by MinFloat from below. If Src is NaN the result is MinFloat.
    Clamped = DAG.getNode(ISD::FMAXNUM, dl, SrcVT, Clamped, MinFloatNode);
    // Clamp by MaxFloat from above. NaN cannot occur.
    Clamped = DAG.getNode(ISD::FMINNUM, dl, SrcVT, Clamped, MaxFloatNode);
    // Convert clamped value to integer.
    SDValue FpToInt = DAG.getNode(IsSigned ? ISD::FP_TO_SINT : ISD::FP_TO_UINT,
                                  dl, DstVT, Clamped);

    // In the unsigned case we're done, because we mapped NaN to MinFloat,
    // which will cast to zero.
    if (!IsSigned)
      return FpToInt;

    // Otherwise, select 0 if Src is NaN.
    SDValue ZeroInt = DAG.getConstant(0, dl, DstVT);
    EVT SetCCVT =
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), SrcVT);
    SDValue IsNan = DAG.getSetCC(dl, SetCCVT, Src, Src, ISD::CondCode::SETUO);
    return DAG.getSelect(dl, DstVT, IsNan, ZeroInt, FpToInt);
  }

  SDValue MinIntNode = DAG.getConstant(MinInt, dl, DstVT);
  SDValue MaxIntNode = DAG.getConstant(MaxInt, dl, DstVT);

  // Result of direct conversion. The assumption here is that the operation is
  // non-trapping and it's fine to apply it to an out-of-range value if we
  // select it away later.
  SDValue FpToInt =
      DAG.getNode(IsSigned ? ISD::FP_TO_SINT : ISD::FP_TO_UINT, dl, DstVT, Src);

  SDValue Select = FpToInt;

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), SrcVT);

  // If Src ULT MinFloat, select MinInt. In particular, this also selects
  // MinInt if Src is NaN.
  SDValue ULT = DAG.getSetCC(dl, SetCCVT, Src, MinFloatNode, ISD::SETULT);
  Select = DAG.getSelect(dl, DstVT, ULT, MinIntNode, Select);
  // If Src OGT MaxFloat, select MaxInt.
  SDValue OGT = DAG.getSetCC(dl, SetCCVT, Src, MaxFloatNode, ISD::SETOGT);
  Select = DAG.getSelect(dl, DstVT, OGT, MaxIntNode, Select);

  // In the unsigned case we are done, because we mapped NaN to MinInt, which
  // is already zero.
  if (!IsSigned)
    return Select;

  // Otherwise, select 0 if Src is NaN.
  SDValue ZeroInt = DAG.getConstant(0, dl, DstVT);
  SDValue IsNan = DAG.getSetCC(dl, SetCCVT, Src, Src, ISD::CondCode::SETUO);
  return DAG.getSelect(dl, DstVT, IsNan, ZeroInt, Select);
}

SDValue TargetLowering::expandRoundInexactToOdd(EVT ResultVT, SDValue Op,
                                                const SDLoc &dl,
                                                SelectionDAG &DAG) const {
  EVT OperandVT = Op.getValueType();
  if (OperandVT.getScalarType() == ResultVT.getScalarType())
    return Op;
  EVT ResultIntVT = ResultVT.changeTypeToInteger();
  // We are rounding binary64/binary128 -> binary32 -> bfloat16. This
  // can induce double-rounding which may alter the results. We can
  // correct for this using a trick explained in: Boldo, Sylvie, and
  // Guillaume Melquiond. "When double rounding is odd." 17th IMACS
  // World Congress. 2005.
  unsigned BitSize = OperandVT.getScalarSizeInBits();
  EVT WideIntVT = OperandVT.changeTypeToInteger();
  SDValue OpAsInt = DAG.getBitcast(WideIntVT, Op);
  SDValue SignBit =
      DAG.getNode(ISD::AND, dl, WideIntVT, OpAsInt,
                  DAG.getConstant(APInt::getSignMask(BitSize), dl, WideIntVT));
  SDValue AbsWide;
  if (isOperationLegalOrCustom(ISD::FABS, OperandVT)) {
    AbsWide = DAG.getNode(ISD::FABS, dl, OperandVT, Op);
  } else {
    SDValue ClearedSign = DAG.getNode(
        ISD::AND, dl, WideIntVT, OpAsInt,
        DAG.getConstant(APInt::getSignedMaxValue(BitSize), dl, WideIntVT));
    AbsWide = DAG.getBitcast(OperandVT, ClearedSign);
  }
  SDValue AbsNarrow = DAG.getFPExtendOrRound(AbsWide, dl, ResultVT);
  SDValue AbsNarrowAsWide = DAG.getFPExtendOrRound(AbsNarrow, dl, OperandVT);

  // We can keep the narrow value as-is if narrowing was exact (no
  // rounding error), the wide value was NaN (the narrow value is also
  // NaN and should be preserved) or if we rounded to the odd value.
  SDValue NarrowBits = DAG.getNode(ISD::BITCAST, dl, ResultIntVT, AbsNarrow);
  SDValue One = DAG.getConstant(1, dl, ResultIntVT);
  SDValue NegativeOne = DAG.getAllOnesConstant(dl, ResultIntVT);
  SDValue And = DAG.getNode(ISD::AND, dl, ResultIntVT, NarrowBits, One);
  EVT ResultIntVTCCVT = getSetCCResultType(
      DAG.getDataLayout(), *DAG.getContext(), And.getValueType());
  SDValue Zero = DAG.getConstant(0, dl, ResultIntVT);
  // The result is already odd so we don't need to do anything.
  SDValue AlreadyOdd = DAG.getSetCC(dl, ResultIntVTCCVT, And, Zero, ISD::SETNE);

  EVT WideSetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                                       AbsWide.getValueType());
  // We keep results which are exact, odd or NaN.
  SDValue KeepNarrow =
      DAG.getSetCC(dl, WideSetCCVT, AbsWide, AbsNarrowAsWide, ISD::SETUEQ);
  KeepNarrow = DAG.getNode(ISD::OR, dl, WideSetCCVT, KeepNarrow, AlreadyOdd);
  // We morally performed a round-down if AbsNarrow is smaller than
  // AbsWide.
  SDValue NarrowIsRd =
      DAG.getSetCC(dl, WideSetCCVT, AbsWide, AbsNarrowAsWide, ISD::SETOGT);
  // If the narrow value is odd or exact, pick it.
  // Otherwise, narrow is even and corresponds to either the rounded-up
  // or rounded-down value. If narrow is the rounded-down value, we want
  // the rounded-up value as it will be odd.
  SDValue Adjust = DAG.getSelect(dl, ResultIntVT, NarrowIsRd, One, NegativeOne);
  SDValue Adjusted = DAG.getNode(ISD::ADD, dl, ResultIntVT, NarrowBits, Adjust);
  Op = DAG.getSelect(dl, ResultIntVT, KeepNarrow, NarrowBits, Adjusted);
  int ShiftAmount = BitSize - ResultVT.getScalarSizeInBits();
  SDValue ShiftCnst = DAG.getShiftAmountConstant(ShiftAmount, WideIntVT, dl);
  SignBit = DAG.getNode(ISD::SRL, dl, WideIntVT, SignBit, ShiftCnst);
  SignBit = DAG.getNode(ISD::TRUNCATE, dl, ResultIntVT, SignBit);
  Op = DAG.getNode(ISD::OR, dl, ResultIntVT, Op, SignBit);
  return DAG.getNode(ISD::BITCAST, dl, ResultVT, Op);
}

SDValue TargetLowering::expandFP_ROUND(SDNode *Node, SelectionDAG &DAG) const {
  assert(Node->getOpcode() == ISD::FP_ROUND && "Unexpected opcode!");
  SDValue Op = Node->getOperand(0);
  EVT VT = Node->getValueType(0);
  SDLoc dl(Node);
  if (VT.getScalarType() == MVT::bf16) {
    if (Node->getConstantOperandVal(1) == 1) {
      return DAG.getNode(ISD::FP_TO_BF16, dl, VT, Node->getOperand(0));
    }
    EVT OperandVT = Op.getValueType();
    SDValue IsNaN = DAG.getSetCC(
        dl,
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), OperandVT),
        Op, Op, ISD::SETUO);

    // We are rounding binary64/binary128 -> binary32 -> bfloat16. This
    // can induce double-rounding which may alter the results. We can
    // correct for this using a trick explained in: Boldo, Sylvie, and
    // Guillaume Melquiond. "When double rounding is odd." 17th IMACS
    // World Congress. 2005.
    EVT F32 = VT.isVector() ? VT.changeVectorElementType(MVT::f32) : MVT::f32;
    EVT I32 = F32.changeTypeToInteger();
    Op = expandRoundInexactToOdd(F32, Op, dl, DAG);
    Op = DAG.getNode(ISD::BITCAST, dl, I32, Op);

    // Conversions should set NaN's quiet bit. This also prevents NaNs from
    // turning into infinities.
    SDValue NaN =
        DAG.getNode(ISD::OR, dl, I32, Op, DAG.getConstant(0x400000, dl, I32));

    // Factor in the contribution of the low 16 bits.
    SDValue One = DAG.getConstant(1, dl, I32);
    SDValue Lsb = DAG.getNode(ISD::SRL, dl, I32, Op,
                              DAG.getShiftAmountConstant(16, I32, dl));
    Lsb = DAG.getNode(ISD::AND, dl, I32, Lsb, One);
    SDValue RoundingBias =
        DAG.getNode(ISD::ADD, dl, I32, DAG.getConstant(0x7fff, dl, I32), Lsb);
    SDValue Add = DAG.getNode(ISD::ADD, dl, I32, Op, RoundingBias);

    // Don't round if we had a NaN, we don't want to turn 0x7fffffff into
    // 0x80000000.
    Op = DAG.getSelect(dl, I32, IsNaN, NaN, Add);

    // Now that we have rounded, shift the bits into position.
    Op = DAG.getNode(ISD::SRL, dl, I32, Op,
                     DAG.getShiftAmountConstant(16, I32, dl));
    Op = DAG.getNode(ISD::BITCAST, dl, I32, Op);
    EVT I16 = I32.isVector() ? I32.changeVectorElementType(MVT::i16) : MVT::i16;
    Op = DAG.getNode(ISD::TRUNCATE, dl, I16, Op);
    return DAG.getNode(ISD::BITCAST, dl, VT, Op);
  }
  return SDValue();
}

SDValue TargetLowering::expandVectorSplice(SDNode *Node,
                                           SelectionDAG &DAG) const {
  assert(Node->getOpcode() == ISD::VECTOR_SPLICE && "Unexpected opcode!");
  assert(Node->getValueType(0).isScalableVector() &&
         "Fixed length vector types expected to use SHUFFLE_VECTOR!");

  EVT VT = Node->getValueType(0);
  SDValue V1 = Node->getOperand(0);
  SDValue V2 = Node->getOperand(1);
  int64_t Imm = cast<ConstantSDNode>(Node->getOperand(2))->getSExtValue();
  SDLoc DL(Node);

  // Expand through memory thusly:
  //  Alloca CONCAT_VECTORS_TYPES(V1, V2) Ptr
  //  Store V1, Ptr
  //  Store V2, Ptr + sizeof(V1)
  //  If (Imm < 0)
  //    TrailingElts = -Imm
  //    Ptr = Ptr + sizeof(V1) - (TrailingElts * sizeof(VT.Elt))
  //  else
  //    Ptr = Ptr + (Imm * sizeof(VT.Elt))
  //  Res = Load Ptr

  Align Alignment = DAG.getReducedAlign(VT, /*UseABI=*/false);

  EVT MemVT = EVT::getVectorVT(*DAG.getContext(), VT.getVectorElementType(),
                               VT.getVectorElementCount() * 2);
  SDValue StackPtr = DAG.CreateStackTemporary(MemVT.getStoreSize(), Alignment);
  EVT PtrVT = StackPtr.getValueType();
  auto &MF = DAG.getMachineFunction();
  auto FrameIndex = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  auto PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIndex);

  // Store the lo part of CONCAT_VECTORS(V1, V2)
  SDValue StoreV1 = DAG.getStore(DAG.getEntryNode(), DL, V1, StackPtr, PtrInfo);
  // Store the hi part of CONCAT_VECTORS(V1, V2)
  SDValue OffsetToV2 = DAG.getVScale(
      DL, PtrVT,
      APInt(PtrVT.getFixedSizeInBits(), VT.getStoreSize().getKnownMinValue()));
  SDValue StackPtr2 = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr, OffsetToV2);
  SDValue StoreV2 = DAG.getStore(StoreV1, DL, V2, StackPtr2, PtrInfo);

  if (Imm >= 0) {
    // Load back the required element. getVectorElementPointer takes care of
    // clamping the index if it's out-of-bounds.
    StackPtr = getVectorElementPointer(DAG, StackPtr, VT, Node->getOperand(2));
    // Load the spliced result
    return DAG.getLoad(VT, DL, StoreV2, StackPtr,
                       MachinePointerInfo::getUnknownStack(MF));
  }

  uint64_t TrailingElts = -Imm;

  // NOTE: TrailingElts must be clamped so as not to read outside of V1:V2.
  TypeSize EltByteSize = VT.getVectorElementType().getStoreSize();
  SDValue TrailingBytes =
      DAG.getConstant(TrailingElts * EltByteSize, DL, PtrVT);

  if (TrailingElts > VT.getVectorMinNumElements()) {
    SDValue VLBytes =
        DAG.getVScale(DL, PtrVT,
                      APInt(PtrVT.getFixedSizeInBits(),
                            VT.getStoreSize().getKnownMinValue()));
    TrailingBytes = DAG.getNode(ISD::UMIN, DL, PtrVT, TrailingBytes, VLBytes);
  }

  // Calculate the start address of the spliced result.
  StackPtr2 = DAG.getNode(ISD::SUB, DL, PtrVT, StackPtr2, TrailingBytes);

  // Load the spliced result
  return DAG.getLoad(VT, DL, StoreV2, StackPtr2,
                     MachinePointerInfo::getUnknownStack(MF));
}

SDValue TargetLowering::expandVECTOR_COMPRESS(SDNode *Node,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Node);
  SDValue Vec = Node->getOperand(0);
  SDValue Mask = Node->getOperand(1);
  SDValue Passthru = Node->getOperand(2);

  EVT VecVT = Vec.getValueType();
  EVT ScalarVT = VecVT.getScalarType();
  EVT MaskVT = Mask.getValueType();
  EVT MaskScalarVT = MaskVT.getScalarType();

  // Needs to be handled by targets that have scalable vector types.
  if (VecVT.isScalableVector())
    report_fatal_error("Cannot expand masked_compress for scalable vectors.");

  SDValue StackPtr = DAG.CreateStackTemporary(
      VecVT.getStoreSize(), DAG.getReducedAlign(VecVT, /*UseABI=*/false));
  int FI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);

  MVT PositionVT = getVectorIdxTy(DAG.getDataLayout());
  SDValue Chain = DAG.getEntryNode();
  SDValue OutPos = DAG.getConstant(0, DL, PositionVT);

  bool HasPassthru = !Passthru.isUndef();

  // If we have a passthru vector, store it on the stack, overwrite the matching
  // positions and then re-write the last element that was potentially
  // overwritten even though mask[i] = false.
  if (HasPassthru)
    Chain = DAG.getStore(Chain, DL, Passthru, StackPtr, PtrInfo);

  SDValue LastWriteVal;
  APInt PassthruSplatVal;
  bool IsSplatPassthru =
      ISD::isConstantSplatVector(Passthru.getNode(), PassthruSplatVal);

  if (IsSplatPassthru) {
    // As we do not know which position we wrote to last, we cannot simply
    // access that index from the passthru vector. So we first check if passthru
    // is a splat vector, to use any element ...
    LastWriteVal = DAG.getConstant(PassthruSplatVal, DL, ScalarVT);
  } else if (HasPassthru) {
    // ... if it is not a splat vector, we need to get the passthru value at
    // position = popcount(mask) and re-load it from the stack before it is
    // overwritten in the loop below.
    SDValue Popcount = DAG.getNode(
        ISD::TRUNCATE, DL, MaskVT.changeVectorElementType(MVT::i1), Mask);
    Popcount = DAG.getNode(ISD::ZERO_EXTEND, DL,
                           MaskVT.changeVectorElementType(ScalarVT), Popcount);
    Popcount = DAG.getNode(ISD::VECREDUCE_ADD, DL, ScalarVT, Popcount);
    SDValue LastElmtPtr =
        getVectorElementPointer(DAG, StackPtr, VecVT, Popcount);
    LastWriteVal = DAG.getLoad(
        ScalarVT, DL, Chain, LastElmtPtr,
        MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()));
    Chain = LastWriteVal.getValue(1);
  }

  unsigned NumElms = VecVT.getVectorNumElements();
  for (unsigned I = 0; I < NumElms; I++) {
    SDValue Idx = DAG.getVectorIdxConstant(I, DL);

    SDValue ValI = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, ScalarVT, Vec, Idx);
    SDValue OutPtr = getVectorElementPointer(DAG, StackPtr, VecVT, OutPos);
    Chain = DAG.getStore(
        Chain, DL, ValI, OutPtr,
        MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()));

    // Get the mask value and add it to the current output position. This
    // either increments by 1 if MaskI is true or adds 0 otherwise.
    // Freeze in case we have poison/undef mask entries.
    SDValue MaskI = DAG.getFreeze(
        DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MaskScalarVT, Mask, Idx));
    MaskI = DAG.getFreeze(MaskI);
    MaskI = DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, MaskI);
    MaskI = DAG.getNode(ISD::ZERO_EXTEND, DL, PositionVT, MaskI);
    OutPos = DAG.getNode(ISD::ADD, DL, PositionVT, OutPos, MaskI);

    if (HasPassthru && I == NumElms - 1) {
      SDValue EndOfVector =
          DAG.getConstant(VecVT.getVectorNumElements() - 1, DL, PositionVT);
      SDValue AllLanesSelected =
          DAG.getSetCC(DL, MVT::i1, OutPos, EndOfVector, ISD::CondCode::SETUGT);
      OutPos = DAG.getNode(ISD::UMIN, DL, PositionVT, OutPos, EndOfVector);
      OutPtr = getVectorElementPointer(DAG, StackPtr, VecVT, OutPos);

      // Re-write the last ValI if all lanes were selected. Otherwise,
      // overwrite the last write it with the passthru value.
      LastWriteVal =
          DAG.getSelect(DL, ScalarVT, AllLanesSelected, ValI, LastWriteVal);
      Chain = DAG.getStore(
          Chain, DL, LastWriteVal, OutPtr,
          MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()));
    }
  }

  return DAG.getLoad(VecVT, DL, Chain, StackPtr, PtrInfo);
}

bool TargetLowering::LegalizeSetCCCondCode(SelectionDAG &DAG, EVT VT,
                                           SDValue &LHS, SDValue &RHS,
                                           SDValue &CC, SDValue Mask,
                                           SDValue EVL, bool &NeedInvert,
                                           const SDLoc &dl, SDValue &Chain,
                                           bool IsSignaling) const {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  MVT OpVT = LHS.getSimpleValueType();
  ISD::CondCode CCCode = cast<CondCodeSDNode>(CC)->get();
  NeedInvert = false;
  assert(!EVL == !Mask && "VP Mask and EVL must either both be set or unset");
  bool IsNonVP = !EVL;
  switch (TLI.getCondCodeAction(CCCode, OpVT)) {
  default:
    llvm_unreachable("Unknown condition code action!");
  case TargetLowering::Legal:
    // Nothing to do.
    break;
  case TargetLowering::Expand: {
    ISD::CondCode InvCC = ISD::getSetCCSwappedOperands(CCCode);
    if (TLI.isCondCodeLegalOrCustom(InvCC, OpVT)) {
      std::swap(LHS, RHS);
      CC = DAG.getCondCode(InvCC);
      return true;
    }
    // Swapping operands didn't work. Try inverting the condition.
    bool NeedSwap = false;
    InvCC = getSetCCInverse(CCCode, OpVT);
    if (!TLI.isCondCodeLegalOrCustom(InvCC, OpVT)) {
      // If inverting the condition is not enough, try swapping operands
      // on top of it.
      InvCC = ISD::getSetCCSwappedOperands(InvCC);
      NeedSwap = true;
    }
    if (TLI.isCondCodeLegalOrCustom(InvCC, OpVT)) {
      CC = DAG.getCondCode(InvCC);
      NeedInvert = true;
      if (NeedSwap)
        std::swap(LHS, RHS);
      return true;
    }

    ISD::CondCode CC1 = ISD::SETCC_INVALID, CC2 = ISD::SETCC_INVALID;
    unsigned Opc = 0;
    switch (CCCode) {
    default:
      llvm_unreachable("Don't know how to expand this condition!");
    case ISD::SETUO:
      if (TLI.isCondCodeLegal(ISD::SETUNE, OpVT)) {
        CC1 = ISD::SETUNE;
        CC2 = ISD::SETUNE;
        Opc = ISD::OR;
        break;
      }
      assert(TLI.isCondCodeLegal(ISD::SETOEQ, OpVT) &&
             "If SETUE is expanded, SETOEQ or SETUNE must be legal!");
      NeedInvert = true;
      [[fallthrough]];
    case ISD::SETO:
      assert(TLI.isCondCodeLegal(ISD::SETOEQ, OpVT) &&
             "If SETO is expanded, SETOEQ must be legal!");
      CC1 = ISD::SETOEQ;
      CC2 = ISD::SETOEQ;
      Opc = ISD::AND;
      break;
    case ISD::SETONE:
    case ISD::SETUEQ:
      // If the SETUO or SETO CC isn't legal, we might be able to use
      // SETOGT || SETOLT, inverting the result for SETUEQ. We only need one
      // of SETOGT/SETOLT to be legal, the other can be emulated by swapping
      // the operands.
      CC2 = ((unsigned)CCCode & 0x8U) ? ISD::SETUO : ISD::SETO;
      if (!TLI.isCondCodeLegal(CC2, OpVT) &&
          (TLI.isCondCodeLegal(ISD::SETOGT, OpVT) ||
           TLI.isCondCodeLegal(ISD::SETOLT, OpVT))) {
        CC1 = ISD::SETOGT;
        CC2 = ISD::SETOLT;
        Opc = ISD::OR;
        NeedInvert = ((unsigned)CCCode & 0x8U);
        break;
      }
      [[fallthrough]];
    case ISD::SETOEQ:
    case ISD::SETOGT:
    case ISD::SETOGE:
    case ISD::SETOLT:
    case ISD::SETOLE:
    case ISD::SETUNE:
    case ISD::SETUGT:
    case ISD::SETUGE:
    case ISD::SETULT:
    case ISD::SETULE:
      // If we are floating point, assign and break, otherwise fall through.
      if (!OpVT.isInteger()) {
        // We can use the 4th bit to tell if we are the unordered
        // or ordered version of the opcode.
        CC2 = ((unsigned)CCCode & 0x8U) ? ISD::SETUO : ISD::SETO;
        Opc = ((unsigned)CCCode & 0x8U) ? ISD::OR : ISD::AND;
        CC1 = (ISD::CondCode)(((int)CCCode & 0x7) | 0x10);
        break;
      }
      // Fallthrough if we are unsigned integer.
      [[fallthrough]];
    case ISD::SETLE:
    case ISD::SETGT:
    case ISD::SETGE:
    case ISD::SETLT:
    case ISD::SETNE:
    case ISD::SETEQ:
      // If all combinations of inverting the condition and swapping operands
      // didn't work then we have no means to expand the condition.
      llvm_unreachable("Don't know how to expand this condition!");
    }

    SDValue SetCC1, SetCC2;
    if (CCCode != ISD::SETO && CCCode != ISD::SETUO) {
      // If we aren't the ordered or unorder operation,
      // then the pattern is (LHS CC1 RHS) Opc (LHS CC2 RHS).
      if (IsNonVP) {
        SetCC1 = DAG.getSetCC(dl, VT, LHS, RHS, CC1, Chain, IsSignaling);
        SetCC2 = DAG.getSetCC(dl, VT, LHS, RHS, CC2, Chain, IsSignaling);
      } else {
        SetCC1 = DAG.getSetCCVP(dl, VT, LHS, RHS, CC1, Mask, EVL);
        SetCC2 = DAG.getSetCCVP(dl, VT, LHS, RHS, CC2, Mask, EVL);
      }
    } else {
      // Otherwise, the pattern is (LHS CC1 LHS) Opc (RHS CC2 RHS)
      if (IsNonVP) {
        SetCC1 = DAG.getSetCC(dl, VT, LHS, LHS, CC1, Chain, IsSignaling);
        SetCC2 = DAG.getSetCC(dl, VT, RHS, RHS, CC2, Chain, IsSignaling);
      } else {
        SetCC1 = DAG.getSetCCVP(dl, VT, LHS, LHS, CC1, Mask, EVL);
        SetCC2 = DAG.getSetCCVP(dl, VT, RHS, RHS, CC2, Mask, EVL);
      }
    }
    if (Chain)
      Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, SetCC1.getValue(1),
                          SetCC2.getValue(1));
    if (IsNonVP)
      LHS = DAG.getNode(Opc, dl, VT, SetCC1, SetCC2);
    else {
      // Transform the binary opcode to the VP equivalent.
      assert((Opc == ISD::OR || Opc == ISD::AND) && "Unexpected opcode");
      Opc = Opc == ISD::OR ? ISD::VP_OR : ISD::VP_AND;
      LHS = DAG.getNode(Opc, dl, VT, SetCC1, SetCC2, Mask, EVL);
    }
    RHS = SDValue();
    CC = SDValue();
    return true;
  }
  }
  return false;
}
