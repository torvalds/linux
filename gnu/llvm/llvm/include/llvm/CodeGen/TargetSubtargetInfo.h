//===- llvm/CodeGen/TargetSubtargetInfo.h - Target Information --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the subtarget options of a Target machine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETSUBTARGETINFO_H
#define LLVM_CODEGEN_TARGETSUBTARGETINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/CodeGen/PBQPRAConstraint.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CodeGen.h"
#include <memory>
#include <vector>

namespace llvm {

class APInt;
class MachineFunction;
class ScheduleDAGMutation;
class CallLowering;
class GlobalValue;
class InlineAsmLowering;
class InstrItineraryData;
struct InstrStage;
class InstructionSelector;
class LegalizerInfo;
class MachineInstr;
struct MachineSchedPolicy;
struct MCReadAdvanceEntry;
struct MCWriteLatencyEntry;
struct MCWriteProcResEntry;
class RegisterBankInfo;
class SDep;
class SelectionDAGTargetInfo;
class SUnit;
class TargetFrameLowering;
class TargetInstrInfo;
class TargetLowering;
class TargetRegisterClass;
class TargetRegisterInfo;
class TargetSchedModel;
class Triple;

//===----------------------------------------------------------------------===//
///
/// TargetSubtargetInfo - Generic base class for all target subtargets.  All
/// Target-specific options that control code generation and printing should
/// be exposed through a TargetSubtargetInfo-derived class.
///
class TargetSubtargetInfo : public MCSubtargetInfo {
protected: // Can only create subclasses...
  TargetSubtargetInfo(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                      StringRef FS, ArrayRef<SubtargetFeatureKV> PF,
                      ArrayRef<SubtargetSubTypeKV> PD,
                      const MCWriteProcResEntry *WPR,
                      const MCWriteLatencyEntry *WL,
                      const MCReadAdvanceEntry *RA, const InstrStage *IS,
                      const unsigned *OC, const unsigned *FP);

public:
  // AntiDepBreakMode - Type of anti-dependence breaking that should
  // be performed before post-RA scheduling.
  using AntiDepBreakMode = enum { ANTIDEP_NONE, ANTIDEP_CRITICAL, ANTIDEP_ALL };
  using RegClassVector = SmallVectorImpl<const TargetRegisterClass *>;

  TargetSubtargetInfo() = delete;
  TargetSubtargetInfo(const TargetSubtargetInfo &) = delete;
  TargetSubtargetInfo &operator=(const TargetSubtargetInfo &) = delete;
  ~TargetSubtargetInfo() override;

  virtual bool isXRaySupported() const { return false; }

  // Interfaces to the major aspects of target machine information:
  //
  // -- Instruction opcode and operand information
  // -- Pipelines and scheduling information
  // -- Stack frame information
  // -- Selection DAG lowering information
  // -- Call lowering information
  //
  // N.B. These objects may change during compilation. It's not safe to cache
  // them between functions.
  virtual const TargetInstrInfo *getInstrInfo() const { return nullptr; }
  virtual const TargetFrameLowering *getFrameLowering() const {
    return nullptr;
  }
  virtual const TargetLowering *getTargetLowering() const { return nullptr; }
  virtual const SelectionDAGTargetInfo *getSelectionDAGInfo() const {
    return nullptr;
  }
  virtual const CallLowering *getCallLowering() const { return nullptr; }

  virtual const InlineAsmLowering *getInlineAsmLowering() const {
    return nullptr;
  }

  // FIXME: This lets targets specialize the selector by subtarget (which lets
  // us do things like a dedicated avx512 selector).  However, we might want
  // to also specialize selectors by MachineFunction, which would let us be
  // aware of optsize/optnone and such.
  virtual InstructionSelector *getInstructionSelector() const {
    return nullptr;
  }

  /// Target can subclass this hook to select a different DAG scheduler.
  virtual RegisterScheduler::FunctionPassCtor
  getDAGScheduler(CodeGenOptLevel) const {
    return nullptr;
  }

  virtual const LegalizerInfo *getLegalizerInfo() const { return nullptr; }

  /// getRegisterInfo - If register information is available, return it.  If
  /// not, return null.
  virtual const TargetRegisterInfo *getRegisterInfo() const { return nullptr; }

  /// If the information for the register banks is available, return it.
  /// Otherwise return nullptr.
  virtual const RegisterBankInfo *getRegBankInfo() const { return nullptr; }

  /// getInstrItineraryData - Returns instruction itinerary data for the target
  /// or specific subtarget.
  virtual const InstrItineraryData *getInstrItineraryData() const {
    return nullptr;
  }

  /// Resolve a SchedClass at runtime, where SchedClass identifies an
  /// MCSchedClassDesc with the isVariant property. This may return the ID of
  /// another variant SchedClass, but repeated invocation must quickly terminate
  /// in a nonvariant SchedClass.
  virtual unsigned resolveSchedClass(unsigned SchedClass,
                                     const MachineInstr *MI,
                                     const TargetSchedModel *SchedModel) const {
    return 0;
  }

  /// Returns true if MI is a dependency breaking zero-idiom instruction for the
  /// subtarget.
  ///
  /// This function also sets bits in Mask related to input operands that
  /// are not in a data dependency relationship.  There is one bit for each
  /// machine operand; implicit operands follow explicit operands in the bit
  /// representation used for Mask.  An empty (i.e. a mask with all bits
  /// cleared) means: data dependencies are "broken" for all the explicit input
  /// machine operands of MI.
  virtual bool isZeroIdiom(const MachineInstr *MI, APInt &Mask) const {
    return false;
  }

  /// Returns true if MI is a dependency breaking instruction for the subtarget.
  ///
  /// Similar in behavior to `isZeroIdiom`. However, it knows how to identify
  /// all dependency breaking instructions (i.e. not just zero-idioms).
  /// 
  /// As for `isZeroIdiom`, this method returns a mask of "broken" dependencies.
  /// (See method `isZeroIdiom` for a detailed description of Mask).
  virtual bool isDependencyBreaking(const MachineInstr *MI, APInt &Mask) const {
    return isZeroIdiom(MI, Mask);
  }

  /// Returns true if MI is a candidate for move elimination.
  ///
  /// A candidate for move elimination may be optimized out at register renaming
  /// stage. Subtargets can specify the set of optimizable moves by
  /// instantiating tablegen class `IsOptimizableRegisterMove` (see
  /// llvm/Target/TargetInstrPredicate.td).
  ///
  /// SubtargetEmitter is responsible for processing all the definitions of class
  /// IsOptimizableRegisterMove, and auto-generate an override for this method.
  virtual bool isOptimizableRegisterMove(const MachineInstr *MI) const {
    return false;
  }

  /// True if the subtarget should run MachineScheduler after aggressive
  /// coalescing.
  ///
  /// This currently replaces the SelectionDAG scheduler with the "source" order
  /// scheduler (though see below for an option to turn this off and use the
  /// TargetLowering preference). It does not yet disable the postRA scheduler.
  virtual bool enableMachineScheduler() const;

  /// True if the machine scheduler should disable the TLI preference
  /// for preRA scheduling with the source level scheduler.
  virtual bool enableMachineSchedDefaultSched() const { return true; }

  /// True if the subtarget should run MachinePipeliner
  virtual bool enableMachinePipeliner() const { return true; };

  /// True if the subtarget should run WindowScheduler.
  virtual bool enableWindowScheduler() const { return true; }

  /// True if the subtarget should enable joining global copies.
  ///
  /// By default this is enabled if the machine scheduler is enabled, but
  /// can be overridden.
  virtual bool enableJoinGlobalCopies() const;

  /// True if the subtarget should run a scheduler after register allocation.
  ///
  /// By default this queries the PostRAScheduling bit in the scheduling model
  /// which is the preferred way to influence this.
  virtual bool enablePostRAScheduler() const;

  /// True if the subtarget should run a machine scheduler after register
  /// allocation.
  virtual bool enablePostRAMachineScheduler() const;

  /// True if the subtarget should run the atomic expansion pass.
  virtual bool enableAtomicExpand() const;

  /// True if the subtarget should run the indirectbr expansion pass.
  virtual bool enableIndirectBrExpand() const;

  /// Override generic scheduling policy within a region.
  ///
  /// This is a convenient way for targets that don't provide any custom
  /// scheduling heuristics (no custom MachineSchedStrategy) to make
  /// changes to the generic scheduling policy.
  virtual void overrideSchedPolicy(MachineSchedPolicy &Policy,
                                   unsigned NumRegionInstrs) const {}

  // Perform target-specific adjustments to the latency of a schedule
  // dependency.
  // If a pair of operands is associated with the schedule dependency, DefOpIdx
  // and UseOpIdx are the indices of the operands in Def and Use, respectively.
  // Otherwise, either may be -1.
  virtual void adjustSchedDependency(SUnit *Def, int DefOpIdx, SUnit *Use,
                                     int UseOpIdx, SDep &Dep,
                                     const TargetSchedModel *SchedModel) const {
  }

  // For use with PostRAScheduling: get the anti-dependence breaking that should
  // be performed before post-RA scheduling.
  virtual AntiDepBreakMode getAntiDepBreakMode() const { return ANTIDEP_NONE; }

  // For use with PostRAScheduling: in CriticalPathRCs, return any register
  // classes that should only be considered for anti-dependence breaking if they
  // are on the critical path.
  virtual void getCriticalPathRCs(RegClassVector &CriticalPathRCs) const {
    return CriticalPathRCs.clear();
  }

  // Provide an ordered list of schedule DAG mutations for the post-RA
  // scheduler.
  virtual void getPostRAMutations(
      std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  }

  // Provide an ordered list of schedule DAG mutations for the machine
  // pipeliner.
  virtual void getSMSMutations(
      std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  }

  /// Default to DFA for resource management, return false when target will use
  /// ProcResource in InstrSchedModel instead.
  virtual bool useDFAforSMS() const { return true; }

  // For use with PostRAScheduling: get the minimum optimization level needed
  // to enable post-RA scheduling.
  virtual CodeGenOptLevel getOptLevelToEnablePostRAScheduler() const {
    return CodeGenOptLevel::Default;
  }

  /// True if the subtarget should run the local reassignment
  /// heuristic of the register allocator.
  /// This heuristic may be compile time intensive, \p OptLevel provides
  /// a finer grain to tune the register allocator.
  virtual bool enableRALocalReassignment(CodeGenOptLevel OptLevel) const;

  /// Enable use of alias analysis during code generation (during MI
  /// scheduling, DAGCombine, etc.).
  virtual bool useAA() const;

  /// \brief Sink addresses into blocks using GEP instructions rather than
  /// pointer casts and arithmetic.
  virtual bool addrSinkUsingGEPs() const {
    return useAA();
  }

  /// Enable the use of the early if conversion pass.
  virtual bool enableEarlyIfConversion() const { return false; }

  /// Return PBQPConstraint(s) for the target.
  ///
  /// Override to provide custom PBQP constraints.
  virtual std::unique_ptr<PBQPRAConstraint> getCustomPBQPConstraints() const {
    return nullptr;
  }

  /// Enable tracking of subregister liveness in register allocator.
  /// Please use MachineRegisterInfo::subRegLivenessEnabled() instead where
  /// possible.
  virtual bool enableSubRegLiveness() const { return false; }

  /// This is called after a .mir file was loaded.
  virtual void mirFileLoaded(MachineFunction &MF) const;

  /// True if the register allocator should use the allocation orders exactly as
  /// written in the tablegen descriptions, false if it should allocate
  /// the specified physical register later if is it callee-saved.
  virtual bool ignoreCSRForAllocationOrder(const MachineFunction &MF,
                                           unsigned PhysReg) const {
    return false;
  }

  /// Classify a global function reference. This mainly used to fetch target
  /// special flags for lowering a function address. For example mark a function
  /// call should be plt or pc-related addressing.
  virtual unsigned char
  classifyGlobalFunctionReference(const GlobalValue *GV) const {
    return 0;
  }

  /// Enable spillage copy elimination in MachineCopyPropagation pass. This
  /// helps removing redundant copies generated by register allocator when
  /// handling complex eviction chains.
  virtual bool enableSpillageCopyElimination() const { return false; }

  /// Get the list of MacroFusion predicates.
  virtual std::vector<MacroFusionPredTy> getMacroFusions() const { return {}; };

  /// supportsInitUndef is used to determine if an architecture supports
  /// the Init Undef Pass. By default, it is assumed that it will not support
  /// the pass, with architecture specific overrides providing the information
  /// where they are implemented.
  virtual bool supportsInitUndef() const { return false; }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETSUBTARGETINFO_H
