//===-- SystemZSelectionDAGInfo.cpp - SystemZ SelectionDAG Info -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SystemZSelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#include "SystemZTargetMachine.h"
#include "llvm/CodeGen/SelectionDAG.h"

using namespace llvm;

#define DEBUG_TYPE "systemz-selectiondag-info"

static unsigned getMemMemLenAdj(unsigned Op) {
  return Op == SystemZISD::MEMSET_MVC ? 2 : 1;
}

static SDValue createMemMemNode(SelectionDAG &DAG, const SDLoc &DL, unsigned Op,
                                SDValue Chain, SDValue Dst, SDValue Src,
                                SDValue LenAdj, SDValue Byte) {
  SDVTList VTs = Op == SystemZISD::CLC ? DAG.getVTList(MVT::i32, MVT::Other)
                                       : DAG.getVTList(MVT::Other);
  SmallVector<SDValue, 6> Ops;
  if (Op == SystemZISD::MEMSET_MVC)
    Ops = { Chain, Dst, LenAdj, Byte };
  else
    Ops = { Chain, Dst, Src, LenAdj };
  return DAG.getNode(Op, DL, VTs, Ops);
}

// Emit a mem-mem operation after subtracting one (or two for memset) from
// size, which will be added back during pseudo expansion. As the Reg case
// emitted here may be converted by DAGCombiner into having an Imm length,
// they are both emitted the same way.
static SDValue emitMemMemImm(SelectionDAG &DAG, const SDLoc &DL, unsigned Op,
                             SDValue Chain, SDValue Dst, SDValue Src,
                             uint64_t Size, SDValue Byte = SDValue()) {
  unsigned Adj = getMemMemLenAdj(Op);
  assert(Size >= Adj && "Adjusted length overflow.");
  SDValue LenAdj = DAG.getConstant(Size - Adj, DL, Dst.getValueType());
  return createMemMemNode(DAG, DL, Op, Chain, Dst, Src, LenAdj, Byte);
}

static SDValue emitMemMemReg(SelectionDAG &DAG, const SDLoc &DL, unsigned Op,
                             SDValue Chain, SDValue Dst, SDValue Src,
                             SDValue Size, SDValue Byte = SDValue()) {
  int64_t Adj = getMemMemLenAdj(Op);
  SDValue LenAdj = DAG.getNode(ISD::ADD, DL, MVT::i64,
                               DAG.getZExtOrTrunc(Size, DL, MVT::i64),
                               DAG.getConstant(0 - Adj, DL, MVT::i64));
  return createMemMemNode(DAG, DL, Op, Chain, Dst, Src, LenAdj, Byte);
}

SDValue SystemZSelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool IsVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  if (IsVolatile)
    return SDValue();

  if (auto *CSize = dyn_cast<ConstantSDNode>(Size))
    return emitMemMemImm(DAG, DL, SystemZISD::MVC, Chain, Dst, Src,
                         CSize->getZExtValue());

  return emitMemMemReg(DAG, DL, SystemZISD::MVC, Chain, Dst, Src, Size);
}

// Handle a memset of 1, 2, 4 or 8 bytes with the operands given by
// Chain, Dst, ByteVal and Size.  These cases are expected to use
// MVI, MVHHI, MVHI and MVGHI respectively.
static SDValue memsetStore(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                           SDValue Dst, uint64_t ByteVal, uint64_t Size,
                           Align Alignment, MachinePointerInfo DstPtrInfo) {
  uint64_t StoreVal = ByteVal;
  for (unsigned I = 1; I < Size; ++I)
    StoreVal |= ByteVal << (I * 8);
  return DAG.getStore(
      Chain, DL, DAG.getConstant(StoreVal, DL, MVT::getIntegerVT(Size * 8)),
      Dst, DstPtrInfo, Alignment);
}

SDValue SystemZSelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst,
    SDValue Byte, SDValue Size, Align Alignment, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo) const {
  EVT PtrVT = Dst.getValueType();

  if (IsVolatile)
    return SDValue();

  auto *CByte = dyn_cast<ConstantSDNode>(Byte);
  if (auto *CSize = dyn_cast<ConstantSDNode>(Size)) {
    uint64_t Bytes = CSize->getZExtValue();
    if (Bytes == 0)
      return SDValue();
    if (CByte) {
      // Handle cases that can be done using at most two of
      // MVI, MVHI, MVHHI and MVGHI.  The latter two can only be
      // used if ByteVal is all zeros or all ones; in other cases,
      // we can move at most 2 halfwords.
      uint64_t ByteVal = CByte->getZExtValue();
      if (ByteVal == 0 || ByteVal == 255
              ? Bytes <= 16 && llvm::popcount(Bytes) <= 2
              : Bytes <= 4) {
        unsigned Size1 = Bytes == 16 ? 8 : llvm::bit_floor(Bytes);
        unsigned Size2 = Bytes - Size1;
        SDValue Chain1 = memsetStore(DAG, DL, Chain, Dst, ByteVal, Size1,
                                     Alignment, DstPtrInfo);
        if (Size2 == 0)
          return Chain1;
        Dst = DAG.getNode(ISD::ADD, DL, PtrVT, Dst,
                          DAG.getConstant(Size1, DL, PtrVT));
        DstPtrInfo = DstPtrInfo.getWithOffset(Size1);
        SDValue Chain2 =
            memsetStore(DAG, DL, Chain, Dst, ByteVal, Size2,
                        std::min(Alignment, Align(Size1)), DstPtrInfo);
        return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chain1, Chain2);
      }
    } else {
      // Handle one and two bytes using STC.
      if (Bytes <= 2) {
        SDValue Chain1 =
            DAG.getStore(Chain, DL, Byte, Dst, DstPtrInfo, Alignment);
        if (Bytes == 1)
          return Chain1;
        SDValue Dst2 = DAG.getNode(ISD::ADD, DL, PtrVT, Dst,
                                   DAG.getConstant(1, DL, PtrVT));
        SDValue Chain2 = DAG.getStore(Chain, DL, Byte, Dst2,
                                      DstPtrInfo.getWithOffset(1), Align(1));
        return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chain1, Chain2);
      }
    }
    assert(Bytes >= 2 && "Should have dealt with 0- and 1-byte cases already");

    // Handle the special case of a memset of 0, which can use XC.
    if (CByte && CByte->getZExtValue() == 0)
      return emitMemMemImm(DAG, DL, SystemZISD::XC, Chain, Dst, Dst, Bytes);

    return emitMemMemImm(DAG, DL, SystemZISD::MEMSET_MVC, Chain, Dst, SDValue(),
                         Bytes, DAG.getAnyExtOrTrunc(Byte, DL, MVT::i32));
  }

  // Variable length
  if (CByte && CByte->getZExtValue() == 0)
    // Handle the special case of a variable length memset of 0 with XC.
    return emitMemMemReg(DAG, DL, SystemZISD::XC, Chain, Dst, Dst, Size);

  return emitMemMemReg(DAG, DL, SystemZISD::MEMSET_MVC, Chain, Dst, SDValue(),
                       Size, DAG.getAnyExtOrTrunc(Byte, DL, MVT::i32));
}

// Convert the current CC value into an integer that is 0 if CC == 0,
// greater than zero if CC == 1 and less than zero if CC >= 2.
// The sequence starts with IPM, which puts CC into bits 29 and 28
// of an integer and clears bits 30 and 31.
static SDValue addIPMSequence(const SDLoc &DL, SDValue CCReg,
                              SelectionDAG &DAG) {
  SDValue IPM = DAG.getNode(SystemZISD::IPM, DL, MVT::i32, CCReg);
  SDValue SHL = DAG.getNode(ISD::SHL, DL, MVT::i32, IPM,
                            DAG.getConstant(30 - SystemZ::IPM_CC, DL, MVT::i32));
  SDValue SRA = DAG.getNode(ISD::SRA, DL, MVT::i32, SHL,
                            DAG.getConstant(30, DL, MVT::i32));
  return SRA;
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::EmitTargetCodeForMemcmp(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Src1,
    SDValue Src2, SDValue Size, MachinePointerInfo Op1PtrInfo,
    MachinePointerInfo Op2PtrInfo) const {
  SDValue CCReg;
  // Swap operands to invert CC == 1 vs. CC == 2 cases.
  if (auto *CSize = dyn_cast<ConstantSDNode>(Size)) {
    uint64_t Bytes = CSize->getZExtValue();
    assert(Bytes > 0 && "Caller should have handled 0-size case");
    CCReg = emitMemMemImm(DAG, DL, SystemZISD::CLC, Chain, Src2, Src1, Bytes);
  } else
    CCReg = emitMemMemReg(DAG, DL, SystemZISD::CLC, Chain, Src2, Src1, Size);
  Chain = CCReg.getValue(1);
  return std::make_pair(addIPMSequence(DL, CCReg, DAG), Chain);
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::EmitTargetCodeForMemchr(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Src,
    SDValue Char, SDValue Length, MachinePointerInfo SrcPtrInfo) const {
  // Use SRST to find the character.  End is its address on success.
  EVT PtrVT = Src.getValueType();
  SDVTList VTs = DAG.getVTList(PtrVT, MVT::i32, MVT::Other);
  Length = DAG.getZExtOrTrunc(Length, DL, PtrVT);
  Char = DAG.getZExtOrTrunc(Char, DL, MVT::i32);
  Char = DAG.getNode(ISD::AND, DL, MVT::i32, Char,
                     DAG.getConstant(255, DL, MVT::i32));
  SDValue Limit = DAG.getNode(ISD::ADD, DL, PtrVT, Src, Length);
  SDValue End = DAG.getNode(SystemZISD::SEARCH_STRING, DL, VTs, Chain,
                            Limit, Src, Char);
  SDValue CCReg = End.getValue(1);
  Chain = End.getValue(2);

  // Now select between End and null, depending on whether the character
  // was found.
  SDValue Ops[] = {
      End, DAG.getConstant(0, DL, PtrVT),
      DAG.getTargetConstant(SystemZ::CCMASK_SRST, DL, MVT::i32),
      DAG.getTargetConstant(SystemZ::CCMASK_SRST_FOUND, DL, MVT::i32), CCReg};
  End = DAG.getNode(SystemZISD::SELECT_CCMASK, DL, PtrVT, Ops);
  return std::make_pair(End, Chain);
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::EmitTargetCodeForStrcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dest,
    SDValue Src, MachinePointerInfo DestPtrInfo, MachinePointerInfo SrcPtrInfo,
    bool isStpcpy) const {
  SDVTList VTs = DAG.getVTList(Dest.getValueType(), MVT::Other);
  SDValue EndDest = DAG.getNode(SystemZISD::STPCPY, DL, VTs, Chain, Dest, Src,
                                DAG.getConstant(0, DL, MVT::i32));
  return std::make_pair(isStpcpy ? EndDest : Dest, EndDest.getValue(1));
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::EmitTargetCodeForStrcmp(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Src1,
    SDValue Src2, MachinePointerInfo Op1PtrInfo,
    MachinePointerInfo Op2PtrInfo) const {
  SDVTList VTs = DAG.getVTList(Src1.getValueType(), MVT::i32, MVT::Other);
  // Swap operands to invert CC == 1 vs. CC == 2 cases.
  SDValue Unused = DAG.getNode(SystemZISD::STRCMP, DL, VTs, Chain, Src2, Src1,
                               DAG.getConstant(0, DL, MVT::i32));
  SDValue CCReg = Unused.getValue(1);
  Chain = Unused.getValue(2);
  return std::make_pair(addIPMSequence(DL, CCReg, DAG), Chain);
}

// Search from Src for a null character, stopping once Src reaches Limit.
// Return a pair of values, the first being the number of nonnull characters
// and the second being the out chain.
//
// This can be used for strlen by setting Limit to 0.
static std::pair<SDValue, SDValue> getBoundedStrlen(SelectionDAG &DAG,
                                                    const SDLoc &DL,
                                                    SDValue Chain, SDValue Src,
                                                    SDValue Limit) {
  EVT PtrVT = Src.getValueType();
  SDVTList VTs = DAG.getVTList(PtrVT, MVT::i32, MVT::Other);
  SDValue End = DAG.getNode(SystemZISD::SEARCH_STRING, DL, VTs, Chain,
                            Limit, Src, DAG.getConstant(0, DL, MVT::i32));
  Chain = End.getValue(2);
  SDValue Len = DAG.getNode(ISD::SUB, DL, PtrVT, End, Src);
  return std::make_pair(Len, Chain);
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::EmitTargetCodeForStrlen(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Src,
    MachinePointerInfo SrcPtrInfo) const {
  EVT PtrVT = Src.getValueType();
  return getBoundedStrlen(DAG, DL, Chain, Src, DAG.getConstant(0, DL, PtrVT));
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::EmitTargetCodeForStrnlen(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Src,
    SDValue MaxLength, MachinePointerInfo SrcPtrInfo) const {
  EVT PtrVT = Src.getValueType();
  MaxLength = DAG.getZExtOrTrunc(MaxLength, DL, PtrVT);
  SDValue Limit = DAG.getNode(ISD::ADD, DL, PtrVT, Src, MaxLength);
  return getBoundedStrlen(DAG, DL, Chain, Src, Limit);
}
