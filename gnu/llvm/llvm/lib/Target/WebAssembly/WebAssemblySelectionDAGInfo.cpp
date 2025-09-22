//===-- WebAssemblySelectionDAGInfo.cpp - WebAssembly SelectionDAG Info ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the WebAssemblySelectionDAGInfo class.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyTargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-selectiondag-info"

WebAssemblySelectionDAGInfo::~WebAssemblySelectionDAGInfo() = default; // anchor

SDValue WebAssemblySelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align Alignment, bool IsVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  auto &ST = DAG.getMachineFunction().getSubtarget<WebAssemblySubtarget>();
  if (!ST.hasBulkMemory())
    return SDValue();

  SDValue MemIdx = DAG.getConstant(0, DL, MVT::i32);
  auto LenMVT = ST.hasAddr64() ? MVT::i64 : MVT::i32;
  return DAG.getNode(WebAssemblyISD::MEMORY_COPY, DL, MVT::Other,
                     {Chain, MemIdx, MemIdx, Dst, Src,
                      DAG.getZExtOrTrunc(Size, DL, LenMVT)});
}

SDValue WebAssemblySelectionDAGInfo::EmitTargetCodeForMemmove(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Op1, SDValue Op2,
    SDValue Op3, Align Alignment, bool IsVolatile,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  return EmitTargetCodeForMemcpy(DAG, DL, Chain, Op1, Op2, Op3,
                                 Alignment, IsVolatile, false,
                                 DstPtrInfo, SrcPtrInfo);
}

SDValue WebAssemblySelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Val,
    SDValue Size, Align Alignment, bool IsVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo) const {
  auto &ST = DAG.getMachineFunction().getSubtarget<WebAssemblySubtarget>();
  if (!ST.hasBulkMemory())
    return SDValue();

  SDValue MemIdx = DAG.getConstant(0, DL, MVT::i32);
  auto LenMVT = ST.hasAddr64() ? MVT::i64 : MVT::i32;
  // Only low byte matters for val argument, so anyext the i8
  return DAG.getNode(WebAssemblyISD::MEMORY_FILL, DL, MVT::Other, Chain, MemIdx,
                     Dst, DAG.getAnyExtOrTrunc(Val, DL, MVT::i32),
                     DAG.getZExtOrTrunc(Size, DL, LenMVT));
}
