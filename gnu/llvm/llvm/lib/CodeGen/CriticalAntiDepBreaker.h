//===- llvm/CodeGen/CriticalAntiDepBreaker.h - Anti-Dep Support -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CriticalAntiDepBreaker class, which
// implements register anti-dependence breaking along a blocks
// critical path during post-RA scheduler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_CRITICALANTIDEPBREAKER_H
#define LLVM_LIB_CODEGEN_CRITICALANTIDEPBREAKER_H

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/AntiDepBreaker.h"
#include "llvm/Support/Compiler.h"
#include <map>
#include <vector>

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MachineOperand;
class MachineRegisterInfo;
class RegisterClassInfo;
class TargetInstrInfo;
class TargetRegisterClass;
class TargetRegisterInfo;

class LLVM_LIBRARY_VISIBILITY CriticalAntiDepBreaker : public AntiDepBreaker {
    MachineFunction& MF;
    MachineRegisterInfo &MRI;
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    const RegisterClassInfo &RegClassInfo;

    /// The set of allocatable registers.
    /// We'll be ignoring anti-dependencies on non-allocatable registers,
    /// because they may not be safe to break.
    const BitVector AllocatableSet;

    /// For live regs that are only used in one register class in a
    /// live range, the register class. If the register is not live, the
    /// corresponding value is null. If the register is live but used in
    /// multiple register classes, the corresponding value is -1 casted to a
    /// pointer.
    std::vector<const TargetRegisterClass *> Classes;

    /// Map registers to all their references within a live range.
    std::multimap<unsigned, MachineOperand *> RegRefs;

    using RegRefIter =
        std::multimap<unsigned, MachineOperand *>::const_iterator;

    /// The index of the most recent kill (proceeding bottom-up),
    /// or ~0u if the register is not live.
    std::vector<unsigned> KillIndices;

    /// The index of the most recent complete def (proceeding
    /// bottom up), or ~0u if the register is live.
    std::vector<unsigned> DefIndices;

    /// A set of registers which are live and cannot be changed to
    /// break anti-dependencies.
    BitVector KeepRegs;

  public:
    CriticalAntiDepBreaker(MachineFunction& MFi, const RegisterClassInfo &RCI);
    ~CriticalAntiDepBreaker() override;

    /// Initialize anti-dep breaking for a new basic block.
    void StartBlock(MachineBasicBlock *BB) override;

    /// Identifiy anti-dependencies along the critical path
    /// of the ScheduleDAG and break them by renaming registers.
    unsigned BreakAntiDependencies(const std::vector<SUnit> &SUnits,
                                   MachineBasicBlock::iterator Begin,
                                   MachineBasicBlock::iterator End,
                                   unsigned InsertPosIndex,
                                   DbgValueVector &DbgValues) override;

    /// Update liveness information to account for the current
    /// instruction, which will not be scheduled.
    void Observe(MachineInstr &MI, unsigned Count,
                 unsigned InsertPosIndex) override;

    /// Finish anti-dep breaking for a basic block.
    void FinishBlock() override;

  private:
    void PrescanInstruction(MachineInstr &MI);
    void ScanInstruction(MachineInstr &MI, unsigned Count);
    bool isNewRegClobberedByRefs(RegRefIter RegRefBegin,
                                 RegRefIter RegRefEnd,
                                 unsigned NewReg);
    unsigned findSuitableFreeRegister(RegRefIter RegRefBegin,
                                      RegRefIter RegRefEnd,
                                      unsigned AntiDepReg,
                                      unsigned LastNewReg,
                                      const TargetRegisterClass *RC,
                                      SmallVectorImpl<unsigned> &Forbid);
  };

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_CRITICALANTIDEPBREAKER_H
