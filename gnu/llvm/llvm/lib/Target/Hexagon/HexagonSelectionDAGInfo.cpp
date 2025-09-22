//===-- HexagonSelectionDAGInfo.cpp - Hexagon SelectionDAG Info -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the HexagonSelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#include "HexagonTargetMachine.h"
#include "llvm/CodeGen/SelectionDAG.h"
using namespace llvm;

#define DEBUG_TYPE "hexagon-selectiondag-info"

SDValue HexagonSelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  ConstantSDNode *ConstantSize = dyn_cast<ConstantSDNode>(Size);
  if (AlwaysInline || Alignment < Align(4) || !ConstantSize)
    return SDValue();

  uint64_t SizeVal = ConstantSize->getZExtValue();
  if (SizeVal < 32 || (SizeVal % 8) != 0)
    return SDValue();

  // Special case aligned memcpys with size >= 32 bytes and a multiple of 8.
  //
  const TargetLowering &TLI = *DAG.getSubtarget().getTargetLowering();
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  Entry.Ty = DAG.getDataLayout().getIntPtrType(*DAG.getContext());
  Entry.Node = Dst;
  Args.push_back(Entry);
  Entry.Node = Src;
  Args.push_back(Entry);
  Entry.Node = Size;
  Args.push_back(Entry);

  const char *SpecialMemcpyName =
      "__hexagon_memcpy_likely_aligned_min32bytes_mult8bytes";
  const MachineFunction &MF = DAG.getMachineFunction();
  bool LongCalls = MF.getSubtarget<HexagonSubtarget>().useLongCalls();
  unsigned Flags = LongCalls ? HexagonII::HMOTF_ConstExtended : 0;

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(Chain)
      .setLibCallee(
          TLI.getLibcallCallingConv(RTLIB::MEMCPY),
          Type::getVoidTy(*DAG.getContext()),
          DAG.getTargetExternalSymbol(
              SpecialMemcpyName, TLI.getPointerTy(DAG.getDataLayout()), Flags),
          std::move(Args))
      .setDiscardResult();

  std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);
  return CallResult.second;
}
