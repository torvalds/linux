//===-- GCNHazardRecognizers.h - GCN Hazard Recognizers ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines hazard recognizers for scheduling on GCN processors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPUHAZARDRECOGNIZERS_H
#define LLVM_LIB_TARGET_AMDGPUHAZARDRECOGNIZERS_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include <list>

namespace llvm {

class MachineFunction;
class MachineInstr;
class MachineOperand;
class MachineRegisterInfo;
class SIInstrInfo;
class SIRegisterInfo;
class GCNSubtarget;

class GCNHazardRecognizer final : public ScheduleHazardRecognizer {
public:
  typedef function_ref<bool(const MachineInstr &)> IsHazardFn;

private:
  // Distinguish if we are called from scheduler or hazard recognizer
  bool IsHazardRecognizerMode;

  // This variable stores the instruction that has been emitted this cycle. It
  // will be added to EmittedInstrs, when AdvanceCycle() or RecedeCycle() is
  // called.
  MachineInstr *CurrCycleInstr;
  std::list<MachineInstr*> EmittedInstrs;
  const MachineFunction &MF;
  const GCNSubtarget &ST;
  const SIInstrInfo &TII;
  const SIRegisterInfo &TRI;
  TargetSchedModel TSchedModel;
  bool RunLdsBranchVmemWARHazardFixup;

  /// RegUnits of uses in the current soft memory clause.
  BitVector ClauseUses;

  /// RegUnits of defs in the current soft memory clause.
  BitVector ClauseDefs;

  void resetClause() {
    ClauseUses.reset();
    ClauseDefs.reset();
  }

  void addClauseInst(const MachineInstr &MI);

  /// \returns the number of wait states before another MFMA instruction can be
  /// issued after \p MI.
  unsigned getMFMAPipelineWaitStates(const MachineInstr &MI) const;

  // Advance over a MachineInstr bundle. Look for hazards in the bundled
  // instructions.
  void processBundle();

  // Run on an individual instruction in hazard recognizer mode. This can be
  // used on a newly inserted instruction before returning from PreEmitNoops.
  void runOnInstruction(MachineInstr *MI);

  int getWaitStatesSince(IsHazardFn IsHazard, int Limit);
  int getWaitStatesSinceDef(unsigned Reg, IsHazardFn IsHazardDef, int Limit);
  int getWaitStatesSinceSetReg(IsHazardFn IsHazard, int Limit);

  int checkSoftClauseHazards(MachineInstr *SMEM);
  int checkSMRDHazards(MachineInstr *SMRD);
  int checkVMEMHazards(MachineInstr* VMEM);
  int checkDPPHazards(MachineInstr *DPP);
  int checkDivFMasHazards(MachineInstr *DivFMas);
  int checkGetRegHazards(MachineInstr *GetRegInstr);
  int checkSetRegHazards(MachineInstr *SetRegInstr);
  int createsVALUHazard(const MachineInstr &MI);
  int checkVALUHazards(MachineInstr *VALU);
  int checkVALUHazardsHelper(const MachineOperand &Def, const MachineRegisterInfo &MRI);
  int checkRWLaneHazards(MachineInstr *RWLane);
  int checkRFEHazards(MachineInstr *RFE);
  int checkInlineAsmHazards(MachineInstr *IA);
  int checkReadM0Hazards(MachineInstr *SMovRel);
  int checkNSAtoVMEMHazard(MachineInstr *MI);
  int checkFPAtomicToDenormModeHazard(MachineInstr *MI);
  void fixHazards(MachineInstr *MI);
  bool fixVcmpxPermlaneHazards(MachineInstr *MI);
  bool fixVMEMtoScalarWriteHazards(MachineInstr *MI);
  bool fixSMEMtoVectorWriteHazards(MachineInstr *MI);
  bool fixVcmpxExecWARHazard(MachineInstr *MI);
  bool fixLdsBranchVmemWARHazard(MachineInstr *MI);
  bool fixLdsDirectVALUHazard(MachineInstr *MI);
  bool fixLdsDirectVMEMHazard(MachineInstr *MI);
  bool fixVALUPartialForwardingHazard(MachineInstr *MI);
  bool fixVALUTransUseHazard(MachineInstr *MI);
  bool fixWMMAHazards(MachineInstr *MI);
  bool fixShift64HighRegBug(MachineInstr *MI);
  bool fixVALUMaskWriteHazard(MachineInstr *MI);
  bool fixRequiredExportPriority(MachineInstr *MI);

  int checkMAIHazards(MachineInstr *MI);
  int checkMAIHazards908(MachineInstr *MI);
  int checkMAIHazards90A(MachineInstr *MI);
  /// Pad the latency between neighboring MFMA instructions with s_nops. The
  /// percentage of wait states to fill with s_nops is specified by the command
  /// line option '-amdgpu-mfma-padding-ratio'.
  ///
  /// For example, with '-amdgpu-mfma-padding-ratio=100':
  ///
  /// 2 pass MFMA instructions have a latency of 2 wait states. Therefore, a
  /// 'S_NOP 1' will be added between sequential MFMA instructions.
  ///
  /// V_MFMA_F32_4X4X1F32
  /// V_MFMA_F32_4X4X1F32
  ///-->
  /// V_MFMA_F32_4X4X1F32
  /// S_NOP 1
  /// V_MFMA_F32_4X4X1F32
  int checkMFMAPadding(MachineInstr *MI);
  int checkMAIVALUHazards(MachineInstr *MI);
  int checkMAILdStHazards(MachineInstr *MI);

public:
  GCNHazardRecognizer(const MachineFunction &MF);
  // We can only issue one instruction per cycle.
  bool atIssueLimit() const override { return true; }
  void EmitInstruction(SUnit *SU) override;
  void EmitInstruction(MachineInstr *MI) override;
  HazardType getHazardType(SUnit *SU, int Stalls) override;
  void EmitNoop() override;
  unsigned PreEmitNoops(MachineInstr *) override;
  unsigned PreEmitNoopsCommon(MachineInstr *);
  void AdvanceCycle() override;
  void RecedeCycle() override;
  bool ShouldPreferAnother(SUnit *SU) override;
  void Reset() override;
};

} // end namespace llvm

#endif //LLVM_LIB_TARGET_AMDGPUHAZARDRECOGNIZERS_H
