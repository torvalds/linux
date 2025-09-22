//===-- ARMSelectionDAGInfo.cpp - ARM SelectionDAG Info -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ARMSelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARMTargetMachine.h"
#include "ARMTargetTransformInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/CommandLine.h"
using namespace llvm;

#define DEBUG_TYPE "arm-selectiondag-info"

cl::opt<TPLoop::MemTransfer> EnableMemtransferTPLoop(
    "arm-memtransfer-tploop", cl::Hidden,
    cl::desc("Control conversion of memcpy to "
             "Tail predicated loops (WLSTP)"),
    cl::init(TPLoop::ForceDisabled),
    cl::values(clEnumValN(TPLoop::ForceDisabled, "force-disabled",
                          "Don't convert memcpy to TP loop."),
               clEnumValN(TPLoop::ForceEnabled, "force-enabled",
                          "Always convert memcpy to TP loop."),
               clEnumValN(TPLoop::Allow, "allow",
                          "Allow (may be subject to certain conditions) "
                          "conversion of memcpy to TP loop.")));

// Emit, if possible, a specialized version of the given Libcall. Typically this
// means selecting the appropriately aligned version, but we also convert memset
// of 0 into memclr.
SDValue ARMSelectionDAGInfo::EmitSpecializedLibcall(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, unsigned Align, RTLIB::Libcall LC) const {
  const ARMSubtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<ARMSubtarget>();
  const ARMTargetLowering *TLI = Subtarget.getTargetLowering();

  // Only use a specialized AEABI function if the default version of this
  // Libcall is an AEABI function.
  if (std::strncmp(TLI->getLibcallName(LC), "__aeabi", 7) != 0)
    return SDValue();

  // Translate RTLIB::Libcall to AEABILibcall. We only do this in order to be
  // able to translate memset to memclr and use the value to index the function
  // name array.
  enum {
    AEABI_MEMCPY = 0,
    AEABI_MEMMOVE,
    AEABI_MEMSET,
    AEABI_MEMCLR
  } AEABILibcall;
  switch (LC) {
  case RTLIB::MEMCPY:
    AEABILibcall = AEABI_MEMCPY;
    break;
  case RTLIB::MEMMOVE:
    AEABILibcall = AEABI_MEMMOVE;
    break;
  case RTLIB::MEMSET:
    AEABILibcall = AEABI_MEMSET;
    if (isNullConstant(Src))
      AEABILibcall = AEABI_MEMCLR;
    break;
  default:
    return SDValue();
  }

  // Choose the most-aligned libcall variant that we can
  enum {
    ALIGN1 = 0,
    ALIGN4,
    ALIGN8
  } AlignVariant;
  if ((Align & 7) == 0)
    AlignVariant = ALIGN8;
  else if ((Align & 3) == 0)
    AlignVariant = ALIGN4;
  else
    AlignVariant = ALIGN1;

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  Entry.Ty = DAG.getDataLayout().getIntPtrType(*DAG.getContext());
  Entry.Node = Dst;
  Args.push_back(Entry);
  if (AEABILibcall == AEABI_MEMCLR) {
    Entry.Node = Size;
    Args.push_back(Entry);
  } else if (AEABILibcall == AEABI_MEMSET) {
    // Adjust parameters for memset, EABI uses format (ptr, size, value),
    // GNU library uses (ptr, value, size)
    // See RTABI section 4.3.4
    Entry.Node = Size;
    Args.push_back(Entry);

    // Extend or truncate the argument to be an i32 value for the call.
    if (Src.getValueType().bitsGT(MVT::i32))
      Src = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Src);
    else if (Src.getValueType().bitsLT(MVT::i32))
      Src = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i32, Src);

    Entry.Node = Src;
    Entry.Ty = Type::getInt32Ty(*DAG.getContext());
    Entry.IsSExt = false;
    Args.push_back(Entry);
  } else {
    Entry.Node = Src;
    Args.push_back(Entry);

    Entry.Node = Size;
    Args.push_back(Entry);
  }

  char const *FunctionNames[4][3] = {
    { "__aeabi_memcpy",  "__aeabi_memcpy4",  "__aeabi_memcpy8"  },
    { "__aeabi_memmove", "__aeabi_memmove4", "__aeabi_memmove8" },
    { "__aeabi_memset",  "__aeabi_memset4",  "__aeabi_memset8"  },
    { "__aeabi_memclr",  "__aeabi_memclr4",  "__aeabi_memclr8"  }
  };
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(Chain)
      .setLibCallee(
          TLI->getLibcallCallingConv(LC), Type::getVoidTy(*DAG.getContext()),
          DAG.getExternalSymbol(FunctionNames[AEABILibcall][AlignVariant],
                                TLI->getPointerTy(DAG.getDataLayout())),
          std::move(Args))
      .setDiscardResult();
  std::pair<SDValue,SDValue> CallResult = TLI->LowerCallTo(CLI);

  return CallResult.second;
}

static bool shouldGenerateInlineTPLoop(const ARMSubtarget &Subtarget,
                                       const SelectionDAG &DAG,
                                       ConstantSDNode *ConstantSize,
                                       Align Alignment, bool IsMemcpy) {
  auto &F = DAG.getMachineFunction().getFunction();
  if (!EnableMemtransferTPLoop)
    return false;
  if (EnableMemtransferTPLoop == TPLoop::ForceEnabled)
    return true;
  // Do not generate inline TP loop if optimizations is disabled,
  // or if optimization for size (-Os or -Oz) is on.
  if (F.hasOptNone() || F.hasOptSize())
    return false;
  // If cli option is unset, for memset always generate inline TP.
  // For memcpy, check some conditions
  if (!IsMemcpy)
    return true;
  if (!ConstantSize && Alignment >= Align(4))
    return true;
  if (ConstantSize &&
      ConstantSize->getZExtValue() > Subtarget.getMaxInlineSizeThreshold() &&
      ConstantSize->getZExtValue() <
          Subtarget.getMaxMemcpyTPInlineSizeThreshold())
    return true;
  return false;
}

SDValue ARMSelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  const ARMSubtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<ARMSubtarget>();
  ConstantSDNode *ConstantSize = dyn_cast<ConstantSDNode>(Size);

  if (Subtarget.hasMVEIntegerOps() &&
      shouldGenerateInlineTPLoop(Subtarget, DAG, ConstantSize, Alignment, true))
    return DAG.getNode(ARMISD::MEMCPYLOOP, dl, MVT::Other, Chain, Dst, Src,
                       DAG.getZExtOrTrunc(Size, dl, MVT::i32));

  // Do repeated 4-byte loads and stores. To be improved.
  // This requires 4-byte alignment.
  if (Alignment < Align(4))
    return SDValue();
  // This requires the copy size to be a constant, preferably
  // within a subtarget-specific limit.
  if (!ConstantSize)
    return EmitSpecializedLibcall(DAG, dl, Chain, Dst, Src, Size,
                                  Alignment.value(), RTLIB::MEMCPY);
  uint64_t SizeVal = ConstantSize->getZExtValue();
  if (!AlwaysInline && SizeVal > Subtarget.getMaxInlineSizeThreshold())
    return EmitSpecializedLibcall(DAG, dl, Chain, Dst, Src, Size,
                                  Alignment.value(), RTLIB::MEMCPY);

  unsigned BytesLeft = SizeVal & 3;
  unsigned NumMemOps = SizeVal >> 2;
  unsigned EmittedNumMemOps = 0;
  EVT VT = MVT::i32;
  unsigned VTSize = 4;
  unsigned i = 0;
  // Emit a maximum of 4 loads in Thumb1 since we have fewer registers
  const unsigned MaxLoadsInLDM = Subtarget.isThumb1Only() ? 4 : 6;
  SDValue TFOps[6];
  SDValue Loads[6];
  uint64_t SrcOff = 0, DstOff = 0;

  // FIXME: We should invent a VMEMCPY pseudo-instruction that lowers to
  // VLDM/VSTM and make this code emit it when appropriate. This would reduce
  // pressure on the general purpose registers. However this seems harder to map
  // onto the register allocator's view of the world.

  // The number of MEMCPY pseudo-instructions to emit. We use up to
  // MaxLoadsInLDM registers per mcopy, which will get lowered into ldm/stm
  // later on. This is a lower bound on the number of MEMCPY operations we must
  // emit.
  unsigned NumMEMCPYs = (NumMemOps + MaxLoadsInLDM - 1) / MaxLoadsInLDM;

  // Code size optimisation: do not inline memcpy if expansion results in
  // more instructions than the libary call.
  if (NumMEMCPYs > 1 && Subtarget.hasMinSize()) {
    return SDValue();
  }

  SDVTList VTs = DAG.getVTList(MVT::i32, MVT::i32, MVT::Other, MVT::Glue);

  for (unsigned I = 0; I != NumMEMCPYs; ++I) {
    // Evenly distribute registers among MEMCPY operations to reduce register
    // pressure.
    unsigned NextEmittedNumMemOps = NumMemOps * (I + 1) / NumMEMCPYs;
    unsigned NumRegs = NextEmittedNumMemOps - EmittedNumMemOps;

    Dst = DAG.getNode(ARMISD::MEMCPY, dl, VTs, Chain, Dst, Src,
                      DAG.getConstant(NumRegs, dl, MVT::i32));
    Src = Dst.getValue(1);
    Chain = Dst.getValue(2);

    DstPtrInfo = DstPtrInfo.getWithOffset(NumRegs * VTSize);
    SrcPtrInfo = SrcPtrInfo.getWithOffset(NumRegs * VTSize);

    EmittedNumMemOps = NextEmittedNumMemOps;
  }

  if (BytesLeft == 0)
    return Chain;

  // Issue loads / stores for the trailing (1 - 3) bytes.
  auto getRemainingValueType = [](unsigned BytesLeft) {
    return (BytesLeft >= 2) ? MVT::i16 : MVT::i8;
  };
  auto getRemainingSize = [](unsigned BytesLeft) {
    return (BytesLeft >= 2) ? 2 : 1;
  };

  unsigned BytesLeftSave = BytesLeft;
  i = 0;
  while (BytesLeft) {
    VT = getRemainingValueType(BytesLeft);
    VTSize = getRemainingSize(BytesLeft);
    Loads[i] = DAG.getLoad(VT, dl, Chain,
                           DAG.getNode(ISD::ADD, dl, MVT::i32, Src,
                                       DAG.getConstant(SrcOff, dl, MVT::i32)),
                           SrcPtrInfo.getWithOffset(SrcOff));
    TFOps[i] = Loads[i].getValue(1);
    ++i;
    SrcOff += VTSize;
    BytesLeft -= VTSize;
  }
  Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, ArrayRef(TFOps, i));

  i = 0;
  BytesLeft = BytesLeftSave;
  while (BytesLeft) {
    VT = getRemainingValueType(BytesLeft);
    VTSize = getRemainingSize(BytesLeft);
    TFOps[i] = DAG.getStore(Chain, dl, Loads[i],
                            DAG.getNode(ISD::ADD, dl, MVT::i32, Dst,
                                        DAG.getConstant(DstOff, dl, MVT::i32)),
                            DstPtrInfo.getWithOffset(DstOff));
    ++i;
    DstOff += VTSize;
    BytesLeft -= VTSize;
  }
  return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, ArrayRef(TFOps, i));
}

SDValue ARMSelectionDAGInfo::EmitTargetCodeForMemmove(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool isVolatile,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  return EmitSpecializedLibcall(DAG, dl, Chain, Dst, Src, Size,
                                Alignment.value(), RTLIB::MEMMOVE);
}

SDValue ARMSelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo) const {

  const ARMSubtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<ARMSubtarget>();

  ConstantSDNode *ConstantSize = dyn_cast<ConstantSDNode>(Size);

  // Generate TP loop for llvm.memset
  if (Subtarget.hasMVEIntegerOps() &&
      shouldGenerateInlineTPLoop(Subtarget, DAG, ConstantSize, Alignment,
                                 false)) {
    Src = DAG.getSplatBuildVector(MVT::v16i8, dl,
                                  DAG.getNode(ISD::TRUNCATE, dl, MVT::i8, Src));
    return DAG.getNode(ARMISD::MEMSETLOOP, dl, MVT::Other, Chain, Dst, Src,
                       DAG.getZExtOrTrunc(Size, dl, MVT::i32));
  }

  if (!AlwaysInline)
    return EmitSpecializedLibcall(DAG, dl, Chain, Dst, Src, Size,
                                  Alignment.value(), RTLIB::MEMSET);

  return SDValue();
}
