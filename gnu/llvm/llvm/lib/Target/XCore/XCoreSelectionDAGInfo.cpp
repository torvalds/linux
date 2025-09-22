//===-- XCoreSelectionDAGInfo.cpp - XCore SelectionDAG Info ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the XCoreSelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#include "XCoreTargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "xcore-selectiondag-info"

SDValue XCoreSelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  unsigned SizeBitWidth = Size.getValueSizeInBits();
  // Call __memcpy_4 if the src, dst and size are all 4 byte aligned.
  if (!AlwaysInline && Alignment >= Align(4) &&
      DAG.MaskedValueIsZero(Size, APInt(SizeBitWidth, 3))) {
    const TargetLowering &TLI = *DAG.getSubtarget().getTargetLowering();
    TargetLowering::ArgListTy Args;
    TargetLowering::ArgListEntry Entry;
    Entry.Ty = DAG.getDataLayout().getIntPtrType(*DAG.getContext());
    Entry.Node = Dst; Args.push_back(Entry);
    Entry.Node = Src; Args.push_back(Entry);
    Entry.Node = Size; Args.push_back(Entry);

    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(dl)
        .setChain(Chain)
        .setLibCallee(TLI.getLibcallCallingConv(RTLIB::MEMCPY),
                      Type::getVoidTy(*DAG.getContext()),
                      DAG.getExternalSymbol(
                          "__memcpy_4", TLI.getPointerTy(DAG.getDataLayout())),
                      std::move(Args))
        .setDiscardResult();

    std::pair<SDValue,SDValue> CallResult = TLI.LowerCallTo(CLI);
    return CallResult.second;
  }

  // Otherwise have the target-independent code call memcpy.
  return SDValue();
}
