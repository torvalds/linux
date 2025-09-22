//===-- SystemZSelectionDAGInfo.h - SystemZ SelectionDAG Info ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SystemZ subclass for SelectionDAGTargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZSELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZSELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

namespace llvm {

class SystemZSelectionDAGInfo : public SelectionDAGTargetInfo {
public:
  explicit SystemZSelectionDAGInfo() = default;

  SDValue EmitTargetCodeForMemcpy(SelectionDAG &DAG, const SDLoc &DL,
                                  SDValue Chain, SDValue Dst, SDValue Src,
                                  SDValue Size, Align Alignment,
                                  bool IsVolatile, bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo,
                                  MachinePointerInfo SrcPtrInfo) const override;

  SDValue EmitTargetCodeForMemset(SelectionDAG &DAG, const SDLoc &DL,
                                  SDValue Chain, SDValue Dst, SDValue Byte,
                                  SDValue Size, Align Alignment,
                                  bool IsVolatile, bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo) const override;

  std::pair<SDValue, SDValue>
  EmitTargetCodeForMemcmp(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                          SDValue Src1, SDValue Src2, SDValue Size,
                          MachinePointerInfo Op1PtrInfo,
                          MachinePointerInfo Op2PtrInfo) const override;

  std::pair<SDValue, SDValue>
  EmitTargetCodeForMemchr(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                          SDValue Src, SDValue Char, SDValue Length,
                          MachinePointerInfo SrcPtrInfo) const override;

  std::pair<SDValue, SDValue> EmitTargetCodeForStrcpy(
      SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dest,
      SDValue Src, MachinePointerInfo DestPtrInfo,
      MachinePointerInfo SrcPtrInfo, bool isStpcpy) const override;

  std::pair<SDValue, SDValue>
  EmitTargetCodeForStrcmp(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                          SDValue Src1, SDValue Src2,
                          MachinePointerInfo Op1PtrInfo,
                          MachinePointerInfo Op2PtrInfo) const override;

  std::pair<SDValue, SDValue>
  EmitTargetCodeForStrlen(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                          SDValue Src,
                          MachinePointerInfo SrcPtrInfo) const override;

  std::pair<SDValue, SDValue>
  EmitTargetCodeForStrnlen(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                           SDValue Src, SDValue MaxLength,
                           MachinePointerInfo SrcPtrInfo) const override;
};

} // end namespace llvm

#endif
