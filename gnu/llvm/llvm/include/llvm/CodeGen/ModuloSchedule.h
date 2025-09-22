//===- ModuloSchedule.h - Software pipeline schedule expansion ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Software pipelining (SWP) is an instruction scheduling technique for loops
// that overlaps loop iterations and exploits ILP via compiler transformations.
//
// There are multiple methods for analyzing a loop and creating a schedule.
// An example algorithm is Swing Modulo Scheduling (implemented by the
// MachinePipeliner). The details of how a schedule is arrived at are irrelevant
// for the task of actually rewriting a loop to adhere to the schedule, which
// is what this file does.
//
// A schedule is, for every instruction in a block, a Cycle and a Stage. Note
// that we only support single-block loops, so "block" and "loop" can be used
// interchangably.
//
// The Cycle of an instruction defines a partial order of the instructions in
// the remapped loop. Instructions within a cycle must not consume the output
// of any instruction in the same cycle. Cycle information is assumed to have
// been calculated such that the processor will execute instructions in
// lock-step (for example in a VLIW ISA).
//
// The Stage of an instruction defines the mapping between logical loop
// iterations and pipelined loop iterations. An example (unrolled) pipeline
// may look something like:
//
//  I0[0]                      Execute instruction I0 of iteration 0
//  I1[0], I0[1]               Execute I0 of iteration 1 and I1 of iteration 1
//         I1[1], I0[2]
//                I1[2], I0[3]
//
// In the schedule for this unrolled sequence we would say that I0 was scheduled
// in stage 0 and I1 in stage 1:
//
//  loop:
//    [stage 0] x = I0
//    [stage 1] I1 x (from stage 0)
//
// And to actually generate valid code we must insert a phi:
//
//  loop:
//    x' = phi(x)
//    x = I0
//    I1 x'
//
// This is a simple example; the rules for how to generate correct code given
// an arbitrary schedule containing loop-carried values are complex.
//
// Note that these examples only mention the steady-state kernel of the
// generated loop; prologs and epilogs must be generated also that prime and
// flush the pipeline. Doing so is nontrivial.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MODULOSCHEDULE_H
#define LLVM_CODEGEN_MODULOSCHEDULE_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopUtils.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <deque>
#include <map>
#include <vector>

namespace llvm {
class MachineBasicBlock;
class MachineLoop;
class MachineRegisterInfo;
class MachineInstr;
class LiveIntervals;

/// Represents a schedule for a single-block loop. For every instruction we
/// maintain a Cycle and Stage.
class ModuloSchedule {
private:
  /// The block containing the loop instructions.
  MachineLoop *Loop;

  /// The instructions to be generated, in total order. Cycle provides a partial
  /// order; the total order within cycles has been decided by the schedule
  /// producer.
  std::vector<MachineInstr *> ScheduledInstrs;

  /// The cycle for each instruction.
  DenseMap<MachineInstr *, int> Cycle;

  /// The stage for each instruction.
  DenseMap<MachineInstr *, int> Stage;

  /// The number of stages in this schedule (Max(Stage) + 1).
  int NumStages;

public:
  /// Create a new ModuloSchedule.
  /// \arg ScheduledInstrs The new loop instructions, in total resequenced
  ///    order.
  /// \arg Cycle Cycle index for all instructions in ScheduledInstrs. Cycle does
  ///    not need to start at zero. ScheduledInstrs must be partially ordered by
  ///    Cycle.
  /// \arg Stage Stage index for all instructions in ScheduleInstrs.
  ModuloSchedule(MachineFunction &MF, MachineLoop *Loop,
                 std::vector<MachineInstr *> ScheduledInstrs,
                 DenseMap<MachineInstr *, int> Cycle,
                 DenseMap<MachineInstr *, int> Stage)
      : Loop(Loop), ScheduledInstrs(ScheduledInstrs), Cycle(std::move(Cycle)),
        Stage(std::move(Stage)) {
    NumStages = 0;
    for (auto &KV : this->Stage)
      NumStages = std::max(NumStages, KV.second);
    ++NumStages;
  }

  /// Return the single-block loop being scheduled.
  MachineLoop *getLoop() const { return Loop; }

  /// Return the number of stages contained in this schedule, which is the
  /// largest stage index + 1.
  int getNumStages() const { return NumStages; }

  /// Return the first cycle in the schedule, which is the cycle index of the
  /// first instruction.
  int getFirstCycle() { return Cycle[ScheduledInstrs.front()]; }

  /// Return the final cycle in the schedule, which is the cycle index of the
  /// last instruction.
  int getFinalCycle() { return Cycle[ScheduledInstrs.back()]; }

  /// Return the stage that MI is scheduled in, or -1.
  int getStage(MachineInstr *MI) {
    auto I = Stage.find(MI);
    return I == Stage.end() ? -1 : I->second;
  }

  /// Return the cycle that MI is scheduled at, or -1.
  int getCycle(MachineInstr *MI) {
    auto I = Cycle.find(MI);
    return I == Cycle.end() ? -1 : I->second;
  }

  /// Set the stage of a newly created instruction.
  void setStage(MachineInstr *MI, int MIStage) {
    assert(Stage.count(MI) == 0);
    Stage[MI] = MIStage;
  }

  /// Return the rescheduled instructions in order.
  ArrayRef<MachineInstr *> getInstructions() { return ScheduledInstrs; }

  void dump() { print(dbgs()); }
  void print(raw_ostream &OS);
};

/// The ModuloScheduleExpander takes a ModuloSchedule and expands it in-place,
/// rewriting the old loop and inserting prologs and epilogs as required.
class ModuloScheduleExpander {
public:
  using InstrChangesTy = DenseMap<MachineInstr *, std::pair<unsigned, int64_t>>;

private:
  using ValueMapTy = DenseMap<unsigned, unsigned>;
  using MBBVectorTy = SmallVectorImpl<MachineBasicBlock *>;
  using InstrMapTy = DenseMap<MachineInstr *, MachineInstr *>;

  ModuloSchedule &Schedule;
  MachineFunction &MF;
  const TargetSubtargetInfo &ST;
  MachineRegisterInfo &MRI;
  const TargetInstrInfo *TII = nullptr;
  LiveIntervals &LIS;

  MachineBasicBlock *BB = nullptr;
  MachineBasicBlock *Preheader = nullptr;
  MachineBasicBlock *NewKernel = nullptr;
  std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo> LoopInfo;

  /// Map for each register and the max difference between its uses and def.
  /// The first element in the pair is the max difference in stages. The
  /// second is true if the register defines a Phi value and loop value is
  /// scheduled before the Phi.
  std::map<unsigned, std::pair<unsigned, bool>> RegToStageDiff;

  /// Instructions to change when emitting the final schedule.
  InstrChangesTy InstrChanges;

  void generatePipelinedLoop();
  void generateProlog(unsigned LastStage, MachineBasicBlock *KernelBB,
                      ValueMapTy *VRMap, MBBVectorTy &PrologBBs);
  void generateEpilog(unsigned LastStage, MachineBasicBlock *KernelBB,
                      MachineBasicBlock *OrigBB, ValueMapTy *VRMap,
                      ValueMapTy *VRMapPhi, MBBVectorTy &EpilogBBs,
                      MBBVectorTy &PrologBBs);
  void generateExistingPhis(MachineBasicBlock *NewBB, MachineBasicBlock *BB1,
                            MachineBasicBlock *BB2, MachineBasicBlock *KernelBB,
                            ValueMapTy *VRMap, InstrMapTy &InstrMap,
                            unsigned LastStageNum, unsigned CurStageNum,
                            bool IsLast);
  void generatePhis(MachineBasicBlock *NewBB, MachineBasicBlock *BB1,
                    MachineBasicBlock *BB2, MachineBasicBlock *KernelBB,
                    ValueMapTy *VRMap, ValueMapTy *VRMapPhi,
                    InstrMapTy &InstrMap, unsigned LastStageNum,
                    unsigned CurStageNum, bool IsLast);
  void removeDeadInstructions(MachineBasicBlock *KernelBB,
                              MBBVectorTy &EpilogBBs);
  void splitLifetimes(MachineBasicBlock *KernelBB, MBBVectorTy &EpilogBBs);
  void addBranches(MachineBasicBlock &PreheaderBB, MBBVectorTy &PrologBBs,
                   MachineBasicBlock *KernelBB, MBBVectorTy &EpilogBBs,
                   ValueMapTy *VRMap);
  bool computeDelta(MachineInstr &MI, unsigned &Delta);
  void updateMemOperands(MachineInstr &NewMI, MachineInstr &OldMI,
                         unsigned Num);
  MachineInstr *cloneInstr(MachineInstr *OldMI, unsigned CurStageNum,
                           unsigned InstStageNum);
  MachineInstr *cloneAndChangeInstr(MachineInstr *OldMI, unsigned CurStageNum,
                                    unsigned InstStageNum);
  void updateInstruction(MachineInstr *NewMI, bool LastDef,
                         unsigned CurStageNum, unsigned InstrStageNum,
                         ValueMapTy *VRMap);
  MachineInstr *findDefInLoop(unsigned Reg);
  unsigned getPrevMapVal(unsigned StageNum, unsigned PhiStage, unsigned LoopVal,
                         unsigned LoopStage, ValueMapTy *VRMap,
                         MachineBasicBlock *BB);
  void rewritePhiValues(MachineBasicBlock *NewBB, unsigned StageNum,
                        ValueMapTy *VRMap, InstrMapTy &InstrMap);
  void rewriteScheduledInstr(MachineBasicBlock *BB, InstrMapTy &InstrMap,
                             unsigned CurStageNum, unsigned PhiNum,
                             MachineInstr *Phi, unsigned OldReg,
                             unsigned NewReg, unsigned PrevReg = 0);
  bool isLoopCarried(MachineInstr &Phi);

  /// Return the max. number of stages/iterations that can occur between a
  /// register definition and its uses.
  unsigned getStagesForReg(int Reg, unsigned CurStage) {
    std::pair<unsigned, bool> Stages = RegToStageDiff[Reg];
    if ((int)CurStage > Schedule.getNumStages() - 1 && Stages.first == 0 &&
        Stages.second)
      return 1;
    return Stages.first;
  }

  /// The number of stages for a Phi is a little different than other
  /// instructions. The minimum value computed in RegToStageDiff is 1
  /// because we assume the Phi is needed for at least 1 iteration.
  /// This is not the case if the loop value is scheduled prior to the
  /// Phi in the same stage.  This function returns the number of stages
  /// or iterations needed between the Phi definition and any uses.
  unsigned getStagesForPhi(int Reg) {
    std::pair<unsigned, bool> Stages = RegToStageDiff[Reg];
    if (Stages.second)
      return Stages.first;
    return Stages.first - 1;
  }

public:
  /// Create a new ModuloScheduleExpander.
  /// \arg InstrChanges Modifications to make to instructions with memory
  ///   operands.
  /// FIXME: InstrChanges is opaque and is an implementation detail of an
  ///   optimization in MachinePipeliner that crosses abstraction boundaries.
  ModuloScheduleExpander(MachineFunction &MF, ModuloSchedule &S,
                         LiveIntervals &LIS, InstrChangesTy InstrChanges)
      : Schedule(S), MF(MF), ST(MF.getSubtarget()), MRI(MF.getRegInfo()),
        TII(ST.getInstrInfo()), LIS(LIS),
        InstrChanges(std::move(InstrChanges)) {}

  /// Performs the actual expansion.
  void expand();
  /// Performs final cleanup after expansion.
  void cleanup();

  /// Returns the newly rewritten kernel block, or nullptr if this was
  /// optimized away.
  MachineBasicBlock *getRewrittenKernel() { return NewKernel; }
};

/// A reimplementation of ModuloScheduleExpander. It works by generating a
/// standalone kernel loop and peeling out the prologs and epilogs.
class PeelingModuloScheduleExpander {
public:
  PeelingModuloScheduleExpander(MachineFunction &MF, ModuloSchedule &S,
                                LiveIntervals *LIS)
      : Schedule(S), MF(MF), ST(MF.getSubtarget()), MRI(MF.getRegInfo()),
        TII(ST.getInstrInfo()), LIS(LIS) {}

  void expand();

  /// Runs ModuloScheduleExpander and treats it as a golden input to validate
  /// aspects of the code generated by PeelingModuloScheduleExpander.
  void validateAgainstModuloScheduleExpander();

protected:
  ModuloSchedule &Schedule;
  MachineFunction &MF;
  const TargetSubtargetInfo &ST;
  MachineRegisterInfo &MRI;
  const TargetInstrInfo *TII = nullptr;
  LiveIntervals *LIS = nullptr;

  /// The original loop block that gets rewritten in-place.
  MachineBasicBlock *BB = nullptr;
  /// The original loop preheader.
  MachineBasicBlock *Preheader = nullptr;
  /// All prolog and epilog blocks.
  SmallVector<MachineBasicBlock *, 4> Prologs, Epilogs;
  /// For every block, the stages that are produced.
  DenseMap<MachineBasicBlock *, BitVector> LiveStages;
  /// For every block, the stages that are available. A stage can be available
  /// but not produced (in the epilog) or produced but not available (in the
  /// prolog).
  DenseMap<MachineBasicBlock *, BitVector> AvailableStages;
  /// When peeling the epilogue keep track of the distance between the phi
  /// nodes and the kernel.
  DenseMap<MachineInstr *, unsigned> PhiNodeLoopIteration;

  /// CanonicalMIs and BlockMIs form a bidirectional map between any of the
  /// loop kernel clones.
  DenseMap<MachineInstr *, MachineInstr *> CanonicalMIs;
  DenseMap<std::pair<MachineBasicBlock *, MachineInstr *>, MachineInstr *>
      BlockMIs;

  /// State passed from peelKernel to peelPrologAndEpilogs().
  std::deque<MachineBasicBlock *> PeeledFront, PeeledBack;
  /// Illegal phis that need to be deleted once we re-link stages.
  SmallVector<MachineInstr *, 4> IllegalPhisToDelete;

  /// Converts BB from the original loop body to the rewritten, pipelined
  /// steady-state.
  void rewriteKernel();

  /// Peels one iteration of the rewritten kernel (BB) in the specified
  /// direction.
  MachineBasicBlock *peelKernel(LoopPeelDirection LPD);
  // Delete instructions whose stage is less than MinStage in the given basic
  // block.
  void filterInstructions(MachineBasicBlock *MB, int MinStage);
  // Move instructions of the given stage from sourceBB to DestBB. Remap the phi
  // instructions to keep a valid IR.
  void moveStageBetweenBlocks(MachineBasicBlock *DestBB,
                              MachineBasicBlock *SourceBB, unsigned Stage);
  /// Peel the kernel forwards and backwards to produce prologs and epilogs,
  /// and stitch them together.
  void peelPrologAndEpilogs();
  /// All prolog and epilog blocks are clones of the kernel, so any produced
  /// register in one block has an corollary in all other blocks.
  Register getEquivalentRegisterIn(Register Reg, MachineBasicBlock *BB);
  /// Change all users of MI, if MI is predicated out
  /// (LiveStages[MI->getParent()] == false).
  void rewriteUsesOf(MachineInstr *MI);
  /// Insert branches between prologs, kernel and epilogs.
  void fixupBranches();
  /// Create a poor-man's LCSSA by cloning only the PHIs from the kernel block
  /// to a block dominated by all prologs and epilogs. This allows us to treat
  /// the loop exiting block as any other kernel clone.
  MachineBasicBlock *CreateLCSSAExitingBlock();
  /// Helper to get the stage of an instruction in the schedule.
  unsigned getStage(MachineInstr *MI) {
    if (CanonicalMIs.count(MI))
      MI = CanonicalMIs[MI];
    return Schedule.getStage(MI);
  }
  /// Helper function to find the right canonical register for a phi instruction
  /// coming from a peeled out prologue.
  Register getPhiCanonicalReg(MachineInstr* CanonicalPhi, MachineInstr* Phi);
  /// Target loop info before kernel peeling.
  std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo> LoopInfo;
};

/// Expand the kernel using modulo variable expansion algorithm (MVE).
/// It unrolls the kernel enough to avoid overlap of register lifetime.
class ModuloScheduleExpanderMVE {
private:
  using ValueMapTy = DenseMap<unsigned, unsigned>;
  using MBBVectorTy = SmallVectorImpl<MachineBasicBlock *>;
  using InstrMapTy = DenseMap<MachineInstr *, MachineInstr *>;

  ModuloSchedule &Schedule;
  MachineFunction &MF;
  const TargetSubtargetInfo &ST;
  MachineRegisterInfo &MRI;
  const TargetInstrInfo *TII = nullptr;
  LiveIntervals &LIS;

  MachineBasicBlock *OrigKernel = nullptr;
  MachineBasicBlock *OrigPreheader = nullptr;
  MachineBasicBlock *OrigExit = nullptr;
  MachineBasicBlock *Check = nullptr;
  MachineBasicBlock *Prolog = nullptr;
  MachineBasicBlock *NewKernel = nullptr;
  MachineBasicBlock *Epilog = nullptr;
  MachineBasicBlock *NewPreheader = nullptr;
  MachineBasicBlock *NewExit = nullptr;
  std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo> LoopInfo;

  /// The number of unroll required to avoid overlap of live ranges.
  /// NumUnroll = 1 means no unrolling.
  int NumUnroll;

  void calcNumUnroll();
  void generatePipelinedLoop();
  void generateProlog(SmallVectorImpl<ValueMapTy> &VRMap);
  void generatePhi(MachineInstr *OrigMI, int UnrollNum,
                   SmallVectorImpl<ValueMapTy> &PrologVRMap,
                   SmallVectorImpl<ValueMapTy> &KernelVRMap,
                   SmallVectorImpl<ValueMapTy> &PhiVRMap);
  void generateKernel(SmallVectorImpl<ValueMapTy> &PrologVRMap,
                      SmallVectorImpl<ValueMapTy> &KernelVRMap,
                      InstrMapTy &LastStage0Insts);
  void generateEpilog(SmallVectorImpl<ValueMapTy> &KernelVRMap,
                      SmallVectorImpl<ValueMapTy> &EpilogVRMap,
                      InstrMapTy &LastStage0Insts);
  void mergeRegUsesAfterPipeline(Register OrigReg, Register NewReg);

  MachineInstr *cloneInstr(MachineInstr *OldMI);

  void updateInstrDef(MachineInstr *NewMI, ValueMapTy &VRMap, bool LastDef);

  void generateKernelPhi(Register OrigLoopVal, Register NewLoopVal,
                         unsigned UnrollNum,
                         SmallVectorImpl<ValueMapTy> &VRMapProlog,
                         SmallVectorImpl<ValueMapTy> &VRMapPhi);
  void updateInstrUse(MachineInstr *MI, int StageNum, int PhaseNum,
                      SmallVectorImpl<ValueMapTy> &CurVRMap,
                      SmallVectorImpl<ValueMapTy> *PrevVRMap);

  void insertCondBranch(MachineBasicBlock &MBB, int RequiredTC,
                        InstrMapTy &LastStage0Insts,
                        MachineBasicBlock &GreaterThan,
                        MachineBasicBlock &Otherwise);

public:
  ModuloScheduleExpanderMVE(MachineFunction &MF, ModuloSchedule &S,
                            LiveIntervals &LIS)
      : Schedule(S), MF(MF), ST(MF.getSubtarget()), MRI(MF.getRegInfo()),
        TII(ST.getInstrInfo()), LIS(LIS) {}

  void expand();
  static bool canApply(MachineLoop &L);
};

/// Expander that simply annotates each scheduled instruction with a post-instr
/// symbol that can be consumed by the ModuloScheduleTest pass.
///
/// The post-instr symbol is a way of annotating an instruction that can be
/// roundtripped in MIR. The syntax is:
///   MYINST %0, post-instr-symbol <mcsymbol Stage-1_Cycle-5>
class ModuloScheduleTestAnnotater {
  MachineFunction &MF;
  ModuloSchedule &S;

public:
  ModuloScheduleTestAnnotater(MachineFunction &MF, ModuloSchedule &S)
      : MF(MF), S(S) {}

  /// Performs the annotation.
  void annotate();
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MODULOSCHEDULE_H
