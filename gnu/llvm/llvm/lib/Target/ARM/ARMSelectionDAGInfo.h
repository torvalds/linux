//===-- ARMSelectionDAGInfo.h - ARM SelectionDAG Info -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ARM subclass for SelectionDAGTargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMSELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_ARM_ARMSELECTIONDAGINFO_H

#include "MCTargetDesc/ARMAddressingModes.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

namespace llvm {

namespace ARM_AM {
  static inline ShiftOpc getShiftOpcForNode(unsigned Opcode) {
    switch (Opcode) {
    default:          return ARM_AM::no_shift;
    case ISD::SHL:    return ARM_AM::lsl;
    case ISD::SRL:    return ARM_AM::lsr;
    case ISD::SRA:    return ARM_AM::asr;
    case ISD::ROTR:   return ARM_AM::ror;
    //case ISD::ROTL:  // Only if imm -> turn into ROTR.
    // Can't handle RRX here, because it would require folding a flag into
    // the addressing mode.  :(  This causes us to miss certain things.
    //case ARMISD::RRX: return ARM_AM::rrx;
    }
  }
}  // end namespace ARM_AM

class ARMSelectionDAGInfo : public SelectionDAGTargetInfo {
public:
  SDValue EmitTargetCodeForMemcpy(SelectionDAG &DAG, const SDLoc &dl,
                                  SDValue Chain, SDValue Dst, SDValue Src,
                                  SDValue Size, Align Alignment,
                                  bool isVolatile, bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo,
                                  MachinePointerInfo SrcPtrInfo) const override;

  SDValue
  EmitTargetCodeForMemmove(SelectionDAG &DAG, const SDLoc &dl, SDValue Chain,
                           SDValue Dst, SDValue Src, SDValue Size,
                           Align Alignment, bool isVolatile,
                           MachinePointerInfo DstPtrInfo,
                           MachinePointerInfo SrcPtrInfo) const override;

  // Adjust parameters for memset, see RTABI section 4.3.4
  SDValue EmitTargetCodeForMemset(SelectionDAG &DAG, const SDLoc &dl,
                                  SDValue Chain, SDValue Op1, SDValue Op2,
                                  SDValue Op3, Align Alignment, bool isVolatile,
                                  bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo) const override;

  SDValue EmitSpecializedLibcall(SelectionDAG &DAG, const SDLoc &dl,
                                 SDValue Chain, SDValue Dst, SDValue Src,
                                 SDValue Size, unsigned Align,
                                 RTLIB::Libcall LC) const;
};

}

#endif
