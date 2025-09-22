//===-- X86SelectionDAGInfo.cpp - X86 SelectionDAG Info -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the X86SelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#include "X86SelectionDAGInfo.h"
#include "X86ISelLowering.h"
#include "X86InstrInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DerivedTypes.h"

using namespace llvm;

#define DEBUG_TYPE "x86-selectiondag-info"

static cl::opt<bool>
    UseFSRMForMemcpy("x86-use-fsrm-for-memcpy", cl::Hidden, cl::init(false),
                     cl::desc("Use fast short rep mov in memcpy lowering"));

bool X86SelectionDAGInfo::isBaseRegConflictPossible(
    SelectionDAG &DAG, ArrayRef<MCPhysReg> ClobberSet) const {
  // We cannot use TRI->hasBasePointer() until *after* we select all basic
  // blocks.  Legalization may introduce new stack temporaries with large
  // alignment requirements.  Fall back to generic code if there are any
  // dynamic stack adjustments (hopefully rare) and the base pointer would
  // conflict if we had to use it.
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  if (!MFI.hasVarSizedObjects() && !MFI.hasOpaqueSPAdjustment())
    return false;

  const X86RegisterInfo *TRI = static_cast<const X86RegisterInfo *>(
      DAG.getSubtarget().getRegisterInfo());
  return llvm::is_contained(ClobberSet, TRI->getBaseRegister());
}

SDValue X86SelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Val,
    SDValue Size, Align Alignment, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo) const {
  // If to a segment-relative address space, use the default lowering.
  if (DstPtrInfo.getAddrSpace() >= 256)
    return SDValue();

  // If the base register might conflict with our physical registers, bail out.
  const MCPhysReg ClobberSet[] = {X86::RCX, X86::RAX, X86::RDI,
                                  X86::ECX, X86::EAX, X86::EDI};
  if (isBaseRegConflictPossible(DAG, ClobberSet))
    return SDValue();

  ConstantSDNode *ConstantSize = dyn_cast<ConstantSDNode>(Size);
  const X86Subtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<X86Subtarget>();

  // If not DWORD aligned or size is more than the threshold, call the library.
  // The libc version is likely to be faster for these cases. It can use the
  // address value and run time information about the CPU.
  if (Alignment < Align(4) || !ConstantSize ||
      ConstantSize->getZExtValue() > Subtarget.getMaxInlineSizeThreshold())
    return SDValue();

  uint64_t SizeVal = ConstantSize->getZExtValue();
  SDValue InGlue;
  EVT AVT;
  SDValue Count;
  unsigned BytesLeft = 0;
  if (auto *ValC = dyn_cast<ConstantSDNode>(Val)) {
    unsigned ValReg;
    uint64_t Val = ValC->getZExtValue() & 255;

    // If the value is a constant, then we can potentially use larger sets.
    if (Alignment >= Align(4)) {
      // DWORD aligned
      AVT = MVT::i32;
      ValReg = X86::EAX;
      Val = (Val << 8)  | Val;
      Val = (Val << 16) | Val;
      if (Subtarget.is64Bit() && Alignment >= Align(8)) { // QWORD aligned
        AVT = MVT::i64;
        ValReg = X86::RAX;
        Val = (Val << 32) | Val;
      }
    } else if (Alignment == Align(2)) {
      // WORD aligned
      AVT = MVT::i16;
      ValReg = X86::AX;
      Val = (Val << 8) | Val;
    } else {
      // Byte aligned
      AVT = MVT::i8;
      ValReg = X86::AL;
      Count = DAG.getIntPtrConstant(SizeVal, dl);
    }

    if (AVT.bitsGT(MVT::i8)) {
      unsigned UBytes = AVT.getSizeInBits() / 8;
      Count = DAG.getIntPtrConstant(SizeVal / UBytes, dl);
      BytesLeft = SizeVal % UBytes;
    }

    Chain = DAG.getCopyToReg(Chain, dl, ValReg, DAG.getConstant(Val, dl, AVT),
                             InGlue);
    InGlue = Chain.getValue(1);
  } else {
    AVT = MVT::i8;
    Count  = DAG.getIntPtrConstant(SizeVal, dl);
    Chain  = DAG.getCopyToReg(Chain, dl, X86::AL, Val, InGlue);
    InGlue = Chain.getValue(1);
  }

  bool Use64BitRegs = Subtarget.isTarget64BitLP64();
  Chain = DAG.getCopyToReg(Chain, dl, Use64BitRegs ? X86::RCX : X86::ECX,
                           Count, InGlue);
  InGlue = Chain.getValue(1);
  Chain = DAG.getCopyToReg(Chain, dl, Use64BitRegs ? X86::RDI : X86::EDI,
                           Dst, InGlue);
  InGlue = Chain.getValue(1);

  SDVTList Tys = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue Ops[] = {Chain, DAG.getValueType(AVT), InGlue};
  SDValue RepStos = DAG.getNode(X86ISD::REP_STOS, dl, Tys, Ops);

  /// RepStos can process the whole length.
  if (BytesLeft == 0)
    return RepStos;

  // Handle the last 1 - 7 bytes.
  SmallVector<SDValue, 4> Results;
  Results.push_back(RepStos);
  unsigned Offset = SizeVal - BytesLeft;
  EVT AddrVT = Dst.getValueType();
  EVT SizeVT = Size.getValueType();

  Results.push_back(
      DAG.getMemset(Chain, dl,
                    DAG.getNode(ISD::ADD, dl, AddrVT, Dst,
                                DAG.getConstant(Offset, dl, AddrVT)),
                    Val, DAG.getConstant(BytesLeft, dl, SizeVT), Alignment,
                    isVolatile, AlwaysInline,
                    /* CI */ nullptr, DstPtrInfo.getWithOffset(Offset)));

  return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Results);
}

/// Emit a single REP MOVS{B,W,D,Q} instruction.
static SDValue emitRepmovs(const X86Subtarget &Subtarget, SelectionDAG &DAG,
                           const SDLoc &dl, SDValue Chain, SDValue Dst,
                           SDValue Src, SDValue Size, MVT AVT) {
  const bool Use64BitRegs = Subtarget.isTarget64BitLP64();
  const unsigned CX = Use64BitRegs ? X86::RCX : X86::ECX;
  const unsigned DI = Use64BitRegs ? X86::RDI : X86::EDI;
  const unsigned SI = Use64BitRegs ? X86::RSI : X86::ESI;

  SDValue InGlue;
  Chain = DAG.getCopyToReg(Chain, dl, CX, Size, InGlue);
  InGlue = Chain.getValue(1);
  Chain = DAG.getCopyToReg(Chain, dl, DI, Dst, InGlue);
  InGlue = Chain.getValue(1);
  Chain = DAG.getCopyToReg(Chain, dl, SI, Src, InGlue);
  InGlue = Chain.getValue(1);

  SDVTList Tys = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue Ops[] = {Chain, DAG.getValueType(AVT), InGlue};
  return DAG.getNode(X86ISD::REP_MOVS, dl, Tys, Ops);
}

/// Emit a single REP MOVSB instruction for a particular constant size.
static SDValue emitRepmovsB(const X86Subtarget &Subtarget, SelectionDAG &DAG,
                            const SDLoc &dl, SDValue Chain, SDValue Dst,
                            SDValue Src, uint64_t Size) {
  return emitRepmovs(Subtarget, DAG, dl, Chain, Dst, Src,
                     DAG.getIntPtrConstant(Size, dl), MVT::i8);
}

/// Returns the best type to use with repmovs depending on alignment.
static MVT getOptimalRepmovsType(const X86Subtarget &Subtarget,
                                 Align Alignment) {
  uint64_t Align = Alignment.value();
  assert((Align != 0) && "Align is normalized");
  assert(isPowerOf2_64(Align) && "Align is a power of 2");
  switch (Align) {
  case 1:
    return MVT::i8;
  case 2:
    return MVT::i16;
  case 4:
    return MVT::i32;
  default:
    return Subtarget.is64Bit() ? MVT::i64 : MVT::i32;
  }
}

/// Returns a REP MOVS instruction, possibly with a few load/stores to implement
/// a constant size memory copy. In some cases where we know REP MOVS is
/// inefficient we return an empty SDValue so the calling code can either
/// generate a load/store sequence or call the runtime memcpy function.
static SDValue emitConstantSizeRepmov(
    SelectionDAG &DAG, const X86Subtarget &Subtarget, const SDLoc &dl,
    SDValue Chain, SDValue Dst, SDValue Src, uint64_t Size, EVT SizeVT,
    Align Alignment, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) {

  /// TODO: Revisit next line: big copy with ERMSB on march >= haswell are very
  /// efficient.
  if (!AlwaysInline && Size > Subtarget.getMaxInlineSizeThreshold())
    return SDValue();

  /// If we have enhanced repmovs we use it.
  if (Subtarget.hasERMSB())
    return emitRepmovsB(Subtarget, DAG, dl, Chain, Dst, Src, Size);

  assert(!Subtarget.hasERMSB() && "No efficient RepMovs");
  /// We assume runtime memcpy will do a better job for unaligned copies when
  /// ERMS is not present.
  if (!AlwaysInline && (Alignment.value() & 3) != 0)
    return SDValue();

  const MVT BlockType = getOptimalRepmovsType(Subtarget, Alignment);
  const uint64_t BlockBytes = BlockType.getSizeInBits() / 8;
  const uint64_t BlockCount = Size / BlockBytes;
  const uint64_t BytesLeft = Size % BlockBytes;
  SDValue RepMovs =
      emitRepmovs(Subtarget, DAG, dl, Chain, Dst, Src,
                  DAG.getIntPtrConstant(BlockCount, dl), BlockType);

  /// RepMov can process the whole length.
  if (BytesLeft == 0)
    return RepMovs;

  assert(BytesLeft && "We have leftover at this point");

  /// In case we optimize for size we use repmovsb even if it's less efficient
  /// so we can save the loads/stores of the leftover.
  if (DAG.getMachineFunction().getFunction().hasMinSize())
    return emitRepmovsB(Subtarget, DAG, dl, Chain, Dst, Src, Size);

  // Handle the last 1 - 7 bytes.
  SmallVector<SDValue, 4> Results;
  Results.push_back(RepMovs);
  unsigned Offset = Size - BytesLeft;
  EVT DstVT = Dst.getValueType();
  EVT SrcVT = Src.getValueType();
  Results.push_back(DAG.getMemcpy(
      Chain, dl,
      DAG.getNode(ISD::ADD, dl, DstVT, Dst, DAG.getConstant(Offset, dl, DstVT)),
      DAG.getNode(ISD::ADD, dl, SrcVT, Src, DAG.getConstant(Offset, dl, SrcVT)),
      DAG.getConstant(BytesLeft, dl, SizeVT), Alignment, isVolatile,
      /*AlwaysInline*/ true, /*CI=*/nullptr, std::nullopt,
      DstPtrInfo.getWithOffset(Offset), SrcPtrInfo.getWithOffset(Offset)));
  return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Results);
}

SDValue X86SelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  // If to a segment-relative address space, use the default lowering.
  if (DstPtrInfo.getAddrSpace() >= 256 || SrcPtrInfo.getAddrSpace() >= 256)
    return SDValue();

  // If the base registers conflict with our physical registers, use the default
  // lowering.
  const MCPhysReg ClobberSet[] = {X86::RCX, X86::RSI, X86::RDI,
                                  X86::ECX, X86::ESI, X86::EDI};
  if (isBaseRegConflictPossible(DAG, ClobberSet))
    return SDValue();

  const X86Subtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<X86Subtarget>();

  // If enabled and available, use fast short rep mov.
  if (UseFSRMForMemcpy && Subtarget.hasFSRM())
    return emitRepmovs(Subtarget, DAG, dl, Chain, Dst, Src, Size, MVT::i8);

  /// Handle constant sizes,
  if (ConstantSDNode *ConstantSize = dyn_cast<ConstantSDNode>(Size))
    return emitConstantSizeRepmov(DAG, Subtarget, dl, Chain, Dst, Src,
                                  ConstantSize->getZExtValue(),
                                  Size.getValueType(), Alignment, isVolatile,
                                  AlwaysInline, DstPtrInfo, SrcPtrInfo);

  return SDValue();
}
