//===- MachineSSAContext.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file declares a specialization of the GenericSSAContext<X>
/// template class for Machine IR.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESSACONTEXT_H
#define LLVM_CODEGEN_MACHINESSACONTEXT_H

#include "llvm/ADT/GenericSSAContext.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Support/Printable.h"

namespace llvm {
class MachineInstr;
class MachineFunction;
class Register;

inline unsigned succ_size(const MachineBasicBlock *BB) {
  return BB->succ_size();
}
inline unsigned pred_size(const MachineBasicBlock *BB) {
  return BB->pred_size();
}
inline auto instrs(const MachineBasicBlock &BB) { return BB.instrs(); }

template <> struct GenericSSATraits<MachineFunction> {
  using BlockT = MachineBasicBlock;
  using FunctionT = MachineFunction;
  using InstructionT = MachineInstr;
  using ValueRefT = Register;
  using ConstValueRefT = Register;
  using UseT = MachineOperand;
};

using MachineSSAContext = GenericSSAContext<MachineFunction>;
} // namespace llvm

#endif // LLVM_CODEGEN_MACHINESSACONTEXT_H
