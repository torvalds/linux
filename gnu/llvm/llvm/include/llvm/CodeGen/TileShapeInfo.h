//===- llvm/CodeGen/TileShapeInfo.h - ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Shape utility for AMX.
/// AMX hardware requires to config the shape of tile data register before use.
/// The 2D shape includes row and column. In AMX intrinsics interface the shape
/// is passed as 1st and 2nd parameter and they are lowered as the 1st and 2nd
/// machine operand of AMX pseudo instructions. ShapeT class is to facilitate
/// tile config and register allocator. The row and column are machine operand
/// of AMX pseudo instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TILESHAPEINFO_H
#define LLVM_CODEGEN_TILESHAPEINFO_H

#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class ShapeT {
public:
  ShapeT(MachineOperand *Row, MachineOperand *Col,
         const MachineRegisterInfo *MRI = nullptr)
      : Row(Row), Col(Col) {
    if (MRI)
      deduceImm(MRI);
  }
  ShapeT()
      : Row(nullptr), Col(nullptr), RowImm(InvalidImmShape),
        ColImm(InvalidImmShape) {}
  bool operator==(const ShapeT &Shape) const {
    MachineOperand *R = Shape.Row;
    MachineOperand *C = Shape.Col;
    if (!R || !C)
      return false;
    if (!Row || !Col)
      return false;
    if (Row->getReg() == R->getReg() && Col->getReg() == C->getReg())
      return true;
    if ((RowImm != InvalidImmShape) && (ColImm != InvalidImmShape))
      return RowImm == Shape.getRowImm() && ColImm == Shape.getColImm();
    return false;
  }

  bool operator!=(const ShapeT &Shape) const { return !(*this == Shape); }

  MachineOperand *getRow() const { return Row; }

  MachineOperand *getCol() const { return Col; }

  int64_t getRowImm() const { return RowImm; }

  int64_t getColImm() const { return ColImm; }

  bool isValid() { return (Row != nullptr) && (Col != nullptr); }

  void deduceImm(const MachineRegisterInfo *MRI) {
    // All def must be the same value, otherwise it is invalid MIs.
    // Find the immediate.
    // TODO copy propagation.
    auto GetImm = [&](Register Reg) {
      int64_t Imm = InvalidImmShape;
      for (const MachineOperand &DefMO : MRI->def_operands(Reg)) {
        const auto *MI = DefMO.getParent();
        if (MI->isMoveImmediate()) {
          Imm = MI->getOperand(1).getImm();
          break;
        }
      }
      return Imm;
    };
    RowImm = GetImm(Row->getReg());
    ColImm = GetImm(Col->getReg());
  }

private:
  static constexpr int64_t InvalidImmShape = -1;
  MachineOperand *Row;
  MachineOperand *Col;
  int64_t RowImm = -1;
  int64_t ColImm = -1;
};

} // namespace llvm

#endif
