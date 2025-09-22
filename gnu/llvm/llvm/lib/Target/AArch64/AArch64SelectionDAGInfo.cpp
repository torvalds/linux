//===-- AArch64SelectionDAGInfo.cpp - AArch64 SelectionDAG Info -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AArch64SelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#include "AArch64TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "aarch64-selectiondag-info"

static cl::opt<bool>
    LowerToSMERoutines("aarch64-lower-to-sme-routines", cl::Hidden,
                       cl::desc("Enable AArch64 SME memory operations "
                                "to lower to librt functions"),
                       cl::init(true));

SDValue AArch64SelectionDAGInfo::EmitMOPS(AArch64ISD::NodeType SDOpcode,
                                          SelectionDAG &DAG, const SDLoc &DL,
                                          SDValue Chain, SDValue Dst,
                                          SDValue SrcOrValue, SDValue Size,
                                          Align Alignment, bool isVolatile,
                                          MachinePointerInfo DstPtrInfo,
                                          MachinePointerInfo SrcPtrInfo) const {

  // Get the constant size of the copy/set.
  uint64_t ConstSize = 0;
  if (auto *C = dyn_cast<ConstantSDNode>(Size))
    ConstSize = C->getZExtValue();

  const bool IsSet = SDOpcode == AArch64ISD::MOPS_MEMSET ||
                     SDOpcode == AArch64ISD::MOPS_MEMSET_TAGGING;

  const auto MachineOpcode = [&]() {
    switch (SDOpcode) {
    case AArch64ISD::MOPS_MEMSET:
      return AArch64::MOPSMemorySetPseudo;
    case AArch64ISD::MOPS_MEMSET_TAGGING:
      return AArch64::MOPSMemorySetTaggingPseudo;
    case AArch64ISD::MOPS_MEMCOPY:
      return AArch64::MOPSMemoryCopyPseudo;
    case AArch64ISD::MOPS_MEMMOVE:
      return AArch64::MOPSMemoryMovePseudo;
    default:
      llvm_unreachable("Unhandled MOPS ISD Opcode");
    }
  }();

  MachineFunction &MF = DAG.getMachineFunction();

  auto Vol =
      isVolatile ? MachineMemOperand::MOVolatile : MachineMemOperand::MONone;
  auto DstFlags = MachineMemOperand::MOStore | Vol;
  auto *DstOp =
      MF.getMachineMemOperand(DstPtrInfo, DstFlags, ConstSize, Alignment);

  if (IsSet) {
    // Extend value to i64, if required.
    if (SrcOrValue.getValueType() != MVT::i64)
      SrcOrValue = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, SrcOrValue);
    SDValue Ops[] = {Dst, Size, SrcOrValue, Chain};
    const EVT ResultTys[] = {MVT::i64, MVT::i64, MVT::Other};
    MachineSDNode *Node = DAG.getMachineNode(MachineOpcode, DL, ResultTys, Ops);
    DAG.setNodeMemRefs(Node, {DstOp});
    return SDValue(Node, 2);
  } else {
    SDValue Ops[] = {Dst, SrcOrValue, Size, Chain};
    const EVT ResultTys[] = {MVT::i64, MVT::i64, MVT::i64, MVT::Other};
    MachineSDNode *Node = DAG.getMachineNode(MachineOpcode, DL, ResultTys, Ops);

    auto SrcFlags = MachineMemOperand::MOLoad | Vol;
    auto *SrcOp =
        MF.getMachineMemOperand(SrcPtrInfo, SrcFlags, ConstSize, Alignment);
    DAG.setNodeMemRefs(Node, {DstOp, SrcOp});
    return SDValue(Node, 3);
  }
}

SDValue AArch64SelectionDAGInfo::EmitStreamingCompatibleMemLibCall(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, RTLIB::Libcall LC) const {
  const AArch64Subtarget &STI =
      DAG.getMachineFunction().getSubtarget<AArch64Subtarget>();
  const AArch64TargetLowering *TLI = STI.getTargetLowering();
  SDValue Symbol;
  TargetLowering::ArgListEntry DstEntry;
  DstEntry.Ty = PointerType::getUnqual(*DAG.getContext());
  DstEntry.Node = Dst;
  TargetLowering::ArgListTy Args;
  Args.push_back(DstEntry);
  EVT PointerVT = TLI->getPointerTy(DAG.getDataLayout());

  switch (LC) {
  case RTLIB::MEMCPY: {
    TargetLowering::ArgListEntry Entry;
    Entry.Ty = PointerType::getUnqual(*DAG.getContext());
    Symbol = DAG.getExternalSymbol("__arm_sc_memcpy", PointerVT);
    Entry.Node = Src;
    Args.push_back(Entry);
    break;
  }
  case RTLIB::MEMMOVE: {
    TargetLowering::ArgListEntry Entry;
    Entry.Ty = PointerType::getUnqual(*DAG.getContext());
    Symbol = DAG.getExternalSymbol("__arm_sc_memmove", PointerVT);
    Entry.Node = Src;
    Args.push_back(Entry);
    break;
  }
  case RTLIB::MEMSET: {
    TargetLowering::ArgListEntry Entry;
    Entry.Ty = Type::getInt32Ty(*DAG.getContext());
    Symbol = DAG.getExternalSymbol("__arm_sc_memset", PointerVT);
    Src = DAG.getZExtOrTrunc(Src, DL, MVT::i32);
    Entry.Node = Src;
    Args.push_back(Entry);
    break;
  }
  default:
    return SDValue();
  }

  TargetLowering::ArgListEntry SizeEntry;
  SizeEntry.Node = Size;
  SizeEntry.Ty = DAG.getDataLayout().getIntPtrType(*DAG.getContext());
  Args.push_back(SizeEntry);
  assert(Symbol->getOpcode() == ISD::ExternalSymbol &&
         "Function name is not set");

  TargetLowering::CallLoweringInfo CLI(DAG);
  PointerType *RetTy = PointerType::getUnqual(*DAG.getContext());
  CLI.setDebugLoc(DL).setChain(Chain).setLibCallee(
      TLI->getLibcallCallingConv(LC), RetTy, Symbol, std::move(Args));
  return TLI->LowerCallTo(CLI).second;
}

SDValue AArch64SelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  const AArch64Subtarget &STI =
      DAG.getMachineFunction().getSubtarget<AArch64Subtarget>();

  if (STI.hasMOPS())
    return EmitMOPS(AArch64ISD::MOPS_MEMCOPY, DAG, DL, Chain, Dst, Src, Size,
                    Alignment, isVolatile, DstPtrInfo, SrcPtrInfo);

  SMEAttrs Attrs(DAG.getMachineFunction().getFunction());
  if (LowerToSMERoutines && !Attrs.hasNonStreamingInterfaceAndBody())
    return EmitStreamingCompatibleMemLibCall(DAG, DL, Chain, Dst, Src, Size,
                                             RTLIB::MEMCPY);
  return SDValue();
}

SDValue AArch64SelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo) const {
  const AArch64Subtarget &STI =
      DAG.getMachineFunction().getSubtarget<AArch64Subtarget>();

  if (STI.hasMOPS())
    return EmitMOPS(AArch64ISD::MOPS_MEMSET, DAG, dl, Chain, Dst, Src, Size,
                    Alignment, isVolatile, DstPtrInfo, MachinePointerInfo{});

  SMEAttrs Attrs(DAG.getMachineFunction().getFunction());
  if (LowerToSMERoutines && !Attrs.hasNonStreamingInterfaceAndBody())
    return EmitStreamingCompatibleMemLibCall(DAG, dl, Chain, Dst, Src, Size,
                                             RTLIB::MEMSET);
  return SDValue();
}

SDValue AArch64SelectionDAGInfo::EmitTargetCodeForMemmove(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool isVolatile,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  const AArch64Subtarget &STI =
      DAG.getMachineFunction().getSubtarget<AArch64Subtarget>();

  if (STI.hasMOPS())
    return EmitMOPS(AArch64ISD::MOPS_MEMMOVE, DAG, dl, Chain, Dst, Src, Size,
                    Alignment, isVolatile, DstPtrInfo, SrcPtrInfo);

  SMEAttrs Attrs(DAG.getMachineFunction().getFunction());
  if (LowerToSMERoutines && !Attrs.hasNonStreamingInterfaceAndBody())
    return EmitStreamingCompatibleMemLibCall(DAG, dl, Chain, Dst, Src, Size,
                                             RTLIB::MEMMOVE);
  return SDValue();
}

static const int kSetTagLoopThreshold = 176;

static SDValue EmitUnrolledSetTag(SelectionDAG &DAG, const SDLoc &dl,
                                  SDValue Chain, SDValue Ptr, uint64_t ObjSize,
                                  const MachineMemOperand *BaseMemOperand,
                                  bool ZeroData) {
  MachineFunction &MF = DAG.getMachineFunction();
  unsigned ObjSizeScaled = ObjSize / 16;

  SDValue TagSrc = Ptr;
  if (Ptr.getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(Ptr)->getIndex();
    Ptr = DAG.getTargetFrameIndex(FI, MVT::i64);
    // A frame index operand may end up as [SP + offset] => it is fine to use SP
    // register as the tag source.
    TagSrc = DAG.getRegister(AArch64::SP, MVT::i64);
  }

  const unsigned OpCode1 = ZeroData ? AArch64ISD::STZG : AArch64ISD::STG;
  const unsigned OpCode2 = ZeroData ? AArch64ISD::STZ2G : AArch64ISD::ST2G;

  SmallVector<SDValue, 8> OutChains;
  unsigned OffsetScaled = 0;
  while (OffsetScaled < ObjSizeScaled) {
    if (ObjSizeScaled - OffsetScaled >= 2) {
      SDValue AddrNode = DAG.getMemBasePlusOffset(
          Ptr, TypeSize::getFixed(OffsetScaled * 16), dl);
      SDValue St = DAG.getMemIntrinsicNode(
          OpCode2, dl, DAG.getVTList(MVT::Other),
          {Chain, TagSrc, AddrNode},
          MVT::v4i64,
          MF.getMachineMemOperand(BaseMemOperand, OffsetScaled * 16, 16 * 2));
      OffsetScaled += 2;
      OutChains.push_back(St);
      continue;
    }

    if (ObjSizeScaled - OffsetScaled > 0) {
      SDValue AddrNode = DAG.getMemBasePlusOffset(
          Ptr, TypeSize::getFixed(OffsetScaled * 16), dl);
      SDValue St = DAG.getMemIntrinsicNode(
          OpCode1, dl, DAG.getVTList(MVT::Other),
          {Chain, TagSrc, AddrNode},
          MVT::v2i64,
          MF.getMachineMemOperand(BaseMemOperand, OffsetScaled * 16, 16));
      OffsetScaled += 1;
      OutChains.push_back(St);
    }
  }

  SDValue Res = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OutChains);
  return Res;
}

SDValue AArch64SelectionDAGInfo::EmitTargetCodeForSetTag(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Addr,
    SDValue Size, MachinePointerInfo DstPtrInfo, bool ZeroData) const {
  uint64_t ObjSize = Size->getAsZExtVal();
  assert(ObjSize % 16 == 0);

  MachineFunction &MF = DAG.getMachineFunction();
  MachineMemOperand *BaseMemOperand = MF.getMachineMemOperand(
      DstPtrInfo, MachineMemOperand::MOStore, ObjSize, Align(16));

  bool UseSetTagRangeLoop =
      kSetTagLoopThreshold >= 0 && (int)ObjSize >= kSetTagLoopThreshold;
  if (!UseSetTagRangeLoop)
    return EmitUnrolledSetTag(DAG, dl, Chain, Addr, ObjSize, BaseMemOperand,
                              ZeroData);

  const EVT ResTys[] = {MVT::i64, MVT::i64, MVT::Other};

  unsigned Opcode;
  if (Addr.getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(Addr)->getIndex();
    Addr = DAG.getTargetFrameIndex(FI, MVT::i64);
    Opcode = ZeroData ? AArch64::STZGloop : AArch64::STGloop;
  } else {
    Opcode = ZeroData ? AArch64::STZGloop_wback : AArch64::STGloop_wback;
  }
  SDValue Ops[] = {DAG.getTargetConstant(ObjSize, dl, MVT::i64), Addr, Chain};
  SDNode *St = DAG.getMachineNode(Opcode, dl, ResTys, Ops);

  DAG.setNodeMemRefs(cast<MachineSDNode>(St), {BaseMemOperand});
  return SDValue(St, 2);
}
