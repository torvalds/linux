//===---- ScheduleDAGInstrs.cpp - MachineInstr Rescheduling ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This implements the ScheduleDAGInstrs class, which implements
/// re-scheduling of MachineInstrs.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ScheduleDAGInstrs.h"

#include "llvm/ADT/IntEqClasses.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseSet.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDFS.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

static cl::opt<bool>
    EnableAASchedMI("enable-aa-sched-mi", cl::Hidden,
                    cl::desc("Enable use of AA during MI DAG construction"));

static cl::opt<bool> UseTBAA("use-tbaa-in-sched-mi", cl::Hidden,
    cl::init(true), cl::desc("Enable use of TBAA during MI DAG construction"));

// Note: the two options below might be used in tuning compile time vs
// output quality. Setting HugeRegion so large that it will never be
// reached means best-effort, but may be slow.

// When Stores and Loads maps (or NonAliasStores and NonAliasLoads)
// together hold this many SUs, a reduction of maps will be done.
static cl::opt<unsigned> HugeRegion("dag-maps-huge-region", cl::Hidden,
    cl::init(1000), cl::desc("The limit to use while constructing the DAG "
                             "prior to scheduling, at which point a trade-off "
                             "is made to avoid excessive compile time."));

static cl::opt<unsigned> ReductionSize(
    "dag-maps-reduction-size", cl::Hidden,
    cl::desc("A huge scheduling region will have maps reduced by this many "
             "nodes at a time. Defaults to HugeRegion / 2."));

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
static cl::opt<bool> SchedPrintCycles(
    "sched-print-cycles", cl::Hidden, cl::init(false),
    cl::desc("Report top/bottom cycles when dumping SUnit instances"));
#endif

static unsigned getReductionSize() {
  // Always reduce a huge region with half of the elements, except
  // when user sets this number explicitly.
  if (ReductionSize.getNumOccurrences() == 0)
    return HugeRegion / 2;
  return ReductionSize;
}

static void dumpSUList(const ScheduleDAGInstrs::SUList &L) {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  dbgs() << "{ ";
  for (const SUnit *SU : L) {
    dbgs() << "SU(" << SU->NodeNum << ")";
    if (SU != L.back())
      dbgs() << ", ";
  }
  dbgs() << "}\n";
#endif
}

ScheduleDAGInstrs::ScheduleDAGInstrs(MachineFunction &mf,
                                     const MachineLoopInfo *mli,
                                     bool RemoveKillFlags)
    : ScheduleDAG(mf), MLI(mli), MFI(mf.getFrameInfo()),
      RemoveKillFlags(RemoveKillFlags),
      UnknownValue(UndefValue::get(
                             Type::getVoidTy(mf.getFunction().getContext()))), Topo(SUnits, &ExitSU) {
  DbgValues.clear();

  const TargetSubtargetInfo &ST = mf.getSubtarget();
  SchedModel.init(&ST);
}

/// If this machine instr has memory reference information and it can be
/// tracked to a normal reference to a known object, return the Value
/// for that object. This function returns false the memory location is
/// unknown or may alias anything.
static bool getUnderlyingObjectsForInstr(const MachineInstr *MI,
                                         const MachineFrameInfo &MFI,
                                         UnderlyingObjectsVector &Objects,
                                         const DataLayout &DL) {
  auto AllMMOsOkay = [&]() {
    for (const MachineMemOperand *MMO : MI->memoperands()) {
      // TODO: Figure out whether isAtomic is really necessary (see D57601).
      if (MMO->isVolatile() || MMO->isAtomic())
        return false;

      if (const PseudoSourceValue *PSV = MMO->getPseudoValue()) {
        // Function that contain tail calls don't have unique PseudoSourceValue
        // objects. Two PseudoSourceValues might refer to the same or
        // overlapping locations. The client code calling this function assumes
        // this is not the case. So return a conservative answer of no known
        // object.
        if (MFI.hasTailCall())
          return false;

        // For now, ignore PseudoSourceValues which may alias LLVM IR values
        // because the code that uses this function has no way to cope with
        // such aliases.
        if (PSV->isAliased(&MFI))
          return false;

        bool MayAlias = PSV->mayAlias(&MFI);
        Objects.emplace_back(PSV, MayAlias);
      } else if (const Value *V = MMO->getValue()) {
        SmallVector<Value *, 4> Objs;
        if (!getUnderlyingObjectsForCodeGen(V, Objs))
          return false;

        for (Value *V : Objs) {
          assert(isIdentifiedObject(V));
          Objects.emplace_back(V, true);
        }
      } else
        return false;
    }
    return true;
  };

  if (!AllMMOsOkay()) {
    Objects.clear();
    return false;
  }

  return true;
}

void ScheduleDAGInstrs::startBlock(MachineBasicBlock *bb) {
  BB = bb;
}

void ScheduleDAGInstrs::finishBlock() {
  // Subclasses should no longer refer to the old block.
  BB = nullptr;
}

void ScheduleDAGInstrs::enterRegion(MachineBasicBlock *bb,
                                    MachineBasicBlock::iterator begin,
                                    MachineBasicBlock::iterator end,
                                    unsigned regioninstrs) {
  assert(bb == BB && "startBlock should set BB");
  RegionBegin = begin;
  RegionEnd = end;
  NumRegionInstrs = regioninstrs;
}

void ScheduleDAGInstrs::exitRegion() {
  // Nothing to do.
}

void ScheduleDAGInstrs::addSchedBarrierDeps() {
  MachineInstr *ExitMI =
      RegionEnd != BB->end()
          ? &*skipDebugInstructionsBackward(RegionEnd, RegionBegin)
          : nullptr;
  ExitSU.setInstr(ExitMI);
  // Add dependencies on the defs and uses of the instruction.
  if (ExitMI) {
    for (const MachineOperand &MO : ExitMI->all_uses()) {
      Register Reg = MO.getReg();
      if (Reg.isPhysical()) {
        for (MCRegUnit Unit : TRI->regunits(Reg))
          Uses.insert(PhysRegSUOper(&ExitSU, -1, Unit));
      } else if (Reg.isVirtual() && MO.readsReg()) {
        addVRegUseDeps(&ExitSU, MO.getOperandNo());
      }
    }
  }
  if (!ExitMI || (!ExitMI->isCall() && !ExitMI->isBarrier())) {
    // For others, e.g. fallthrough, conditional branch, assume the exit
    // uses all the registers that are livein to the successor blocks.
    for (const MachineBasicBlock *Succ : BB->successors()) {
      for (const auto &LI : Succ->liveins()) {
        for (MCRegUnitMaskIterator U(LI.PhysReg, TRI); U.isValid(); ++U) {
          auto [Unit, Mask] = *U;
          if ((Mask & LI.LaneMask).any() && !Uses.contains(Unit))
            Uses.insert(PhysRegSUOper(&ExitSU, -1, Unit));
        }
      }
    }
  }
}

/// MO is an operand of SU's instruction that defines a physical register. Adds
/// data dependencies from SU to any uses of the physical register.
void ScheduleDAGInstrs::addPhysRegDataDeps(SUnit *SU, unsigned OperIdx) {
  const MachineOperand &MO = SU->getInstr()->getOperand(OperIdx);
  assert(MO.isDef() && "expect physreg def");
  Register Reg = MO.getReg();

  // Ask the target if address-backscheduling is desirable, and if so how much.
  const TargetSubtargetInfo &ST = MF.getSubtarget();

  // Only use any non-zero latency for real defs/uses, in contrast to
  // "fake" operands added by regalloc.
  const MCInstrDesc &DefMIDesc = SU->getInstr()->getDesc();
  bool ImplicitPseudoDef = (OperIdx >= DefMIDesc.getNumOperands() &&
                            !DefMIDesc.hasImplicitDefOfPhysReg(Reg));
  for (MCRegUnit Unit : TRI->regunits(Reg)) {
    for (RegUnit2SUnitsMap::iterator I = Uses.find(Unit); I != Uses.end();
         ++I) {
      SUnit *UseSU = I->SU;
      if (UseSU == SU)
        continue;

      // Adjust the dependence latency using operand def/use information,
      // then allow the target to perform its own adjustments.
      MachineInstr *UseInstr = nullptr;
      int UseOpIdx = I->OpIdx;
      bool ImplicitPseudoUse = false;
      SDep Dep;
      if (UseOpIdx < 0) {
        Dep = SDep(SU, SDep::Artificial);
      } else {
        // Set the hasPhysRegDefs only for physreg defs that have a use within
        // the scheduling region.
        SU->hasPhysRegDefs = true;

        UseInstr = UseSU->getInstr();
        Register UseReg = UseInstr->getOperand(UseOpIdx).getReg();
        const MCInstrDesc &UseMIDesc = UseInstr->getDesc();
        ImplicitPseudoUse = UseOpIdx >= ((int)UseMIDesc.getNumOperands()) &&
                            !UseMIDesc.hasImplicitUseOfPhysReg(UseReg);

        Dep = SDep(SU, SDep::Data, UseReg);
      }
      if (!ImplicitPseudoDef && !ImplicitPseudoUse) {
        Dep.setLatency(SchedModel.computeOperandLatency(SU->getInstr(), OperIdx,
                                                        UseInstr, UseOpIdx));
      } else {
        Dep.setLatency(0);
      }
      ST.adjustSchedDependency(SU, OperIdx, UseSU, UseOpIdx, Dep, &SchedModel);
      UseSU->addPred(Dep);
    }
  }
}

/// Adds register dependencies (data, anti, and output) from this SUnit
/// to following instructions in the same scheduling region that depend the
/// physical register referenced at OperIdx.
void ScheduleDAGInstrs::addPhysRegDeps(SUnit *SU, unsigned OperIdx) {
  MachineInstr *MI = SU->getInstr();
  MachineOperand &MO = MI->getOperand(OperIdx);
  Register Reg = MO.getReg();
  // We do not need to track any dependencies for constant registers.
  if (MRI.isConstantPhysReg(Reg))
    return;

  const TargetSubtargetInfo &ST = MF.getSubtarget();

  // Optionally add output and anti dependencies. For anti
  // dependencies we use a latency of 0 because for a multi-issue
  // target we want to allow the defining instruction to issue
  // in the same cycle as the using instruction.
  // TODO: Using a latency of 1 here for output dependencies assumes
  //       there's no cost for reusing registers.
  SDep::Kind Kind = MO.isUse() ? SDep::Anti : SDep::Output;
  for (MCRegUnit Unit : TRI->regunits(Reg)) {
    for (RegUnit2SUnitsMap::iterator I = Defs.find(Unit); I != Defs.end();
         ++I) {
      SUnit *DefSU = I->SU;
      if (DefSU == &ExitSU)
        continue;
      MachineInstr *DefInstr = DefSU->getInstr();
      MachineOperand &DefMO = DefInstr->getOperand(I->OpIdx);
      if (DefSU != SU &&
          (Kind != SDep::Output || !MO.isDead() || !DefMO.isDead())) {
        SDep Dep(SU, Kind, DefMO.getReg());
        if (Kind != SDep::Anti) {
          Dep.setLatency(
              SchedModel.computeOutputLatency(MI, OperIdx, DefInstr));
        }
        ST.adjustSchedDependency(SU, OperIdx, DefSU, I->OpIdx, Dep,
                                 &SchedModel);
        DefSU->addPred(Dep);
      }
    }
  }

  if (MO.isUse()) {
    SU->hasPhysRegUses = true;
    // Either insert a new Reg2SUnits entry with an empty SUnits list, or
    // retrieve the existing SUnits list for this register's uses.
    // Push this SUnit on the use list.
    for (MCRegUnit Unit : TRI->regunits(Reg))
      Uses.insert(PhysRegSUOper(SU, OperIdx, Unit));
    if (RemoveKillFlags)
      MO.setIsKill(false);
  } else {
    addPhysRegDataDeps(SU, OperIdx);

    // Clear previous uses and defs of this register and its subregisters.
    for (MCRegUnit Unit : TRI->regunits(Reg)) {
      Uses.eraseAll(Unit);
      if (!MO.isDead())
        Defs.eraseAll(Unit);
    }

    if (MO.isDead() && SU->isCall) {
      // Calls will not be reordered because of chain dependencies (see
      // below). Since call operands are dead, calls may continue to be added
      // to the DefList making dependence checking quadratic in the size of
      // the block. Instead, we leave only one call at the back of the
      // DefList.
      for (MCRegUnit Unit : TRI->regunits(Reg)) {
        RegUnit2SUnitsMap::RangePair P = Defs.equal_range(Unit);
        RegUnit2SUnitsMap::iterator B = P.first;
        RegUnit2SUnitsMap::iterator I = P.second;
        for (bool isBegin = I == B; !isBegin; /* empty */) {
          isBegin = (--I) == B;
          if (!I->SU->isCall)
            break;
          I = Defs.erase(I);
        }
      }
    }

    // Defs are pushed in the order they are visited and never reordered.
    for (MCRegUnit Unit : TRI->regunits(Reg))
      Defs.insert(PhysRegSUOper(SU, OperIdx, Unit));
  }
}

LaneBitmask ScheduleDAGInstrs::getLaneMaskForMO(const MachineOperand &MO) const
{
  Register Reg = MO.getReg();
  // No point in tracking lanemasks if we don't have interesting subregisters.
  const TargetRegisterClass &RC = *MRI.getRegClass(Reg);
  if (!RC.HasDisjunctSubRegs)
    return LaneBitmask::getAll();

  unsigned SubReg = MO.getSubReg();
  if (SubReg == 0)
    return RC.getLaneMask();
  return TRI->getSubRegIndexLaneMask(SubReg);
}

bool ScheduleDAGInstrs::deadDefHasNoUse(const MachineOperand &MO) {
  auto RegUse = CurrentVRegUses.find(MO.getReg());
  if (RegUse == CurrentVRegUses.end())
    return true;
  return (RegUse->LaneMask & getLaneMaskForMO(MO)).none();
}

/// Adds register output and data dependencies from this SUnit to instructions
/// that occur later in the same scheduling region if they read from or write to
/// the virtual register defined at OperIdx.
///
/// TODO: Hoist loop induction variable increments. This has to be
/// reevaluated. Generally, IV scheduling should be done before coalescing.
void ScheduleDAGInstrs::addVRegDefDeps(SUnit *SU, unsigned OperIdx) {
  MachineInstr *MI = SU->getInstr();
  MachineOperand &MO = MI->getOperand(OperIdx);
  Register Reg = MO.getReg();

  LaneBitmask DefLaneMask;
  LaneBitmask KillLaneMask;
  if (TrackLaneMasks) {
    bool IsKill = MO.getSubReg() == 0 || MO.isUndef();
    DefLaneMask = getLaneMaskForMO(MO);
    // If we have a <read-undef> flag, none of the lane values comes from an
    // earlier instruction.
    KillLaneMask = IsKill ? LaneBitmask::getAll() : DefLaneMask;

    if (MO.getSubReg() != 0 && MO.isUndef()) {
      // There may be other subregister defs on the same instruction of the same
      // register in later operands. The lanes of other defs will now be live
      // after this instruction, so these should not be treated as killed by the
      // instruction even though they appear to be killed in this one operand.
      for (const MachineOperand &OtherMO :
           llvm::drop_begin(MI->operands(), OperIdx + 1))
        if (OtherMO.isReg() && OtherMO.isDef() && OtherMO.getReg() == Reg)
          KillLaneMask &= ~getLaneMaskForMO(OtherMO);
    }

    // Clear undef flag, we'll re-add it later once we know which subregister
    // Def is first.
    MO.setIsUndef(false);
  } else {
    DefLaneMask = LaneBitmask::getAll();
    KillLaneMask = LaneBitmask::getAll();
  }

  if (MO.isDead()) {
    assert(deadDefHasNoUse(MO) && "Dead defs should have no uses");
  } else {
    // Add data dependence to all uses we found so far.
    const TargetSubtargetInfo &ST = MF.getSubtarget();
    for (VReg2SUnitOperIdxMultiMap::iterator I = CurrentVRegUses.find(Reg),
         E = CurrentVRegUses.end(); I != E; /*empty*/) {
      LaneBitmask LaneMask = I->LaneMask;
      // Ignore uses of other lanes.
      if ((LaneMask & KillLaneMask).none()) {
        ++I;
        continue;
      }

      if ((LaneMask & DefLaneMask).any()) {
        SUnit *UseSU = I->SU;
        MachineInstr *Use = UseSU->getInstr();
        SDep Dep(SU, SDep::Data, Reg);
        Dep.setLatency(SchedModel.computeOperandLatency(MI, OperIdx, Use,
                                                        I->OperandIndex));
        ST.adjustSchedDependency(SU, OperIdx, UseSU, I->OperandIndex, Dep,
                                 &SchedModel);
        UseSU->addPred(Dep);
      }

      LaneMask &= ~KillLaneMask;
      // If we found a Def for all lanes of this use, remove it from the list.
      if (LaneMask.any()) {
        I->LaneMask = LaneMask;
        ++I;
      } else
        I = CurrentVRegUses.erase(I);
    }
  }

  // Shortcut: Singly defined vregs do not have output/anti dependencies.
  if (MRI.hasOneDef(Reg))
    return;

  // Add output dependence to the next nearest defs of this vreg.
  //
  // Unless this definition is dead, the output dependence should be
  // transitively redundant with antidependencies from this definition's
  // uses. We're conservative for now until we have a way to guarantee the uses
  // are not eliminated sometime during scheduling. The output dependence edge
  // is also useful if output latency exceeds def-use latency.
  LaneBitmask LaneMask = DefLaneMask;
  for (VReg2SUnit &V2SU : make_range(CurrentVRegDefs.find(Reg),
                                     CurrentVRegDefs.end())) {
    // Ignore defs for other lanes.
    if ((V2SU.LaneMask & LaneMask).none())
      continue;
    // Add an output dependence.
    SUnit *DefSU = V2SU.SU;
    // Ignore additional defs of the same lanes in one instruction. This can
    // happen because lanemasks are shared for targets with too many
    // subregisters. We also use some representration tricks/hacks where we
    // add super-register defs/uses, to imply that although we only access parts
    // of the reg we care about the full one.
    if (DefSU == SU)
      continue;
    SDep Dep(SU, SDep::Output, Reg);
    Dep.setLatency(
      SchedModel.computeOutputLatency(MI, OperIdx, DefSU->getInstr()));
    DefSU->addPred(Dep);

    // Update current definition. This can get tricky if the def was about a
    // bigger lanemask before. We then have to shrink it and create a new
    // VReg2SUnit for the non-overlapping part.
    LaneBitmask OverlapMask = V2SU.LaneMask & LaneMask;
    LaneBitmask NonOverlapMask = V2SU.LaneMask & ~LaneMask;
    V2SU.SU = SU;
    V2SU.LaneMask = OverlapMask;
    if (NonOverlapMask.any())
      CurrentVRegDefs.insert(VReg2SUnit(Reg, NonOverlapMask, DefSU));
  }
  // If there was no CurrentVRegDefs entry for some lanes yet, create one.
  if (LaneMask.any())
    CurrentVRegDefs.insert(VReg2SUnit(Reg, LaneMask, SU));
}

/// Adds a register data dependency if the instruction that defines the
/// virtual register used at OperIdx is mapped to an SUnit. Add a register
/// antidependency from this SUnit to instructions that occur later in the same
/// scheduling region if they write the virtual register.
///
/// TODO: Handle ExitSU "uses" properly.
void ScheduleDAGInstrs::addVRegUseDeps(SUnit *SU, unsigned OperIdx) {
  const MachineInstr *MI = SU->getInstr();
  assert(!MI->isDebugOrPseudoInstr());

  const MachineOperand &MO = MI->getOperand(OperIdx);
  Register Reg = MO.getReg();

  // Remember the use. Data dependencies will be added when we find the def.
  LaneBitmask LaneMask = TrackLaneMasks ? getLaneMaskForMO(MO)
                                        : LaneBitmask::getAll();
  CurrentVRegUses.insert(VReg2SUnitOperIdx(Reg, LaneMask, OperIdx, SU));

  // Add antidependences to the following defs of the vreg.
  for (VReg2SUnit &V2SU : make_range(CurrentVRegDefs.find(Reg),
                                     CurrentVRegDefs.end())) {
    // Ignore defs for unrelated lanes.
    LaneBitmask PrevDefLaneMask = V2SU.LaneMask;
    if ((PrevDefLaneMask & LaneMask).none())
      continue;
    if (V2SU.SU == SU)
      continue;

    V2SU.SU->addPred(SDep(SU, SDep::Anti, Reg));
  }
}

/// Returns true if MI is an instruction we are unable to reason about
/// (like a call or something with unmodeled side effects).
static inline bool isGlobalMemoryObject(MachineInstr *MI) {
  return MI->isCall() || MI->hasUnmodeledSideEffects() ||
         (MI->hasOrderedMemoryRef() && !MI->isDereferenceableInvariantLoad());
}

void ScheduleDAGInstrs::addChainDependency (SUnit *SUa, SUnit *SUb,
                                            unsigned Latency) {
  if (SUa->getInstr()->mayAlias(AAForDep, *SUb->getInstr(), UseTBAA)) {
    SDep Dep(SUa, SDep::MayAliasMem);
    Dep.setLatency(Latency);
    SUb->addPred(Dep);
  }
}

/// Creates an SUnit for each real instruction, numbered in top-down
/// topological order. The instruction order A < B, implies that no edge exists
/// from B to A.
///
/// Map each real instruction to its SUnit.
///
/// After initSUnits, the SUnits vector cannot be resized and the scheduler may
/// hang onto SUnit pointers. We may relax this in the future by using SUnit IDs
/// instead of pointers.
///
/// MachineScheduler relies on initSUnits numbering the nodes by their order in
/// the original instruction list.
void ScheduleDAGInstrs::initSUnits() {
  // We'll be allocating one SUnit for each real instruction in the region,
  // which is contained within a basic block.
  SUnits.reserve(NumRegionInstrs);

  for (MachineInstr &MI : make_range(RegionBegin, RegionEnd)) {
    if (MI.isDebugOrPseudoInstr())
      continue;

    SUnit *SU = newSUnit(&MI);
    MISUnitMap[&MI] = SU;

    SU->isCall = MI.isCall();
    SU->isCommutable = MI.isCommutable();

    // Assign the Latency field of SU using target-provided information.
    SU->Latency = SchedModel.computeInstrLatency(SU->getInstr());

    // If this SUnit uses a reserved or unbuffered resource, mark it as such.
    //
    // Reserved resources block an instruction from issuing and stall the
    // entire pipeline. These are identified by BufferSize=0.
    //
    // Unbuffered resources prevent execution of subsequent instructions that
    // require the same resources. This is used for in-order execution pipelines
    // within an out-of-order core. These are identified by BufferSize=1.
    if (SchedModel.hasInstrSchedModel()) {
      const MCSchedClassDesc *SC = getSchedClass(SU);
      for (const MCWriteProcResEntry &PRE :
           make_range(SchedModel.getWriteProcResBegin(SC),
                      SchedModel.getWriteProcResEnd(SC))) {
        switch (SchedModel.getProcResource(PRE.ProcResourceIdx)->BufferSize) {
        case 0:
          SU->hasReservedResource = true;
          break;
        case 1:
          SU->isUnbuffered = true;
          break;
        default:
          break;
        }
      }
    }
  }
}

class ScheduleDAGInstrs::Value2SUsMap : public MapVector<ValueType, SUList> {
  /// Current total number of SUs in map.
  unsigned NumNodes = 0;

  /// 1 for loads, 0 for stores. (see comment in SUList)
  unsigned TrueMemOrderLatency;

public:
  Value2SUsMap(unsigned lat = 0) : TrueMemOrderLatency(lat) {}

  /// To keep NumNodes up to date, insert() is used instead of
  /// this operator w/ push_back().
  ValueType &operator[](const SUList &Key) {
    llvm_unreachable("Don't use. Use insert() instead."); };

  /// Adds SU to the SUList of V. If Map grows huge, reduce its size by calling
  /// reduce().
  void inline insert(SUnit *SU, ValueType V) {
    MapVector::operator[](V).push_back(SU);
    NumNodes++;
  }

  /// Clears the list of SUs mapped to V.
  void inline clearList(ValueType V) {
    iterator Itr = find(V);
    if (Itr != end()) {
      assert(NumNodes >= Itr->second.size());
      NumNodes -= Itr->second.size();

      Itr->second.clear();
    }
  }

  /// Clears map from all contents.
  void clear() {
    MapVector<ValueType, SUList>::clear();
    NumNodes = 0;
  }

  unsigned inline size() const { return NumNodes; }

  /// Counts the number of SUs in this map after a reduction.
  void reComputeSize() {
    NumNodes = 0;
    for (auto &I : *this)
      NumNodes += I.second.size();
  }

  unsigned inline getTrueMemOrderLatency() const {
    return TrueMemOrderLatency;
  }

  void dump();
};

void ScheduleDAGInstrs::addChainDependencies(SUnit *SU,
                                             Value2SUsMap &Val2SUsMap) {
  for (auto &I : Val2SUsMap)
    addChainDependencies(SU, I.second,
                         Val2SUsMap.getTrueMemOrderLatency());
}

void ScheduleDAGInstrs::addChainDependencies(SUnit *SU,
                                             Value2SUsMap &Val2SUsMap,
                                             ValueType V) {
  Value2SUsMap::iterator Itr = Val2SUsMap.find(V);
  if (Itr != Val2SUsMap.end())
    addChainDependencies(SU, Itr->second,
                         Val2SUsMap.getTrueMemOrderLatency());
}

void ScheduleDAGInstrs::addBarrierChain(Value2SUsMap &map) {
  assert(BarrierChain != nullptr);

  for (auto &[V, SUs] : map) {
    (void)V;
    for (auto *SU : SUs)
      SU->addPredBarrier(BarrierChain);
  }
  map.clear();
}

void ScheduleDAGInstrs::insertBarrierChain(Value2SUsMap &map) {
  assert(BarrierChain != nullptr);

  // Go through all lists of SUs.
  for (Value2SUsMap::iterator I = map.begin(), EE = map.end(); I != EE;) {
    Value2SUsMap::iterator CurrItr = I++;
    SUList &sus = CurrItr->second;
    SUList::iterator SUItr = sus.begin(), SUEE = sus.end();
    for (; SUItr != SUEE; ++SUItr) {
      // Stop on BarrierChain or any instruction above it.
      if ((*SUItr)->NodeNum <= BarrierChain->NodeNum)
        break;

      (*SUItr)->addPredBarrier(BarrierChain);
    }

    // Remove also the BarrierChain from list if present.
    if (SUItr != SUEE && *SUItr == BarrierChain)
      SUItr++;

    // Remove all SUs that are now successors of BarrierChain.
    if (SUItr != sus.begin())
      sus.erase(sus.begin(), SUItr);
  }

  // Remove all entries with empty su lists.
  map.remove_if([&](std::pair<ValueType, SUList> &mapEntry) {
      return (mapEntry.second.empty()); });

  // Recompute the size of the map (NumNodes).
  map.reComputeSize();
}

void ScheduleDAGInstrs::buildSchedGraph(AAResults *AA,
                                        RegPressureTracker *RPTracker,
                                        PressureDiffs *PDiffs,
                                        LiveIntervals *LIS,
                                        bool TrackLaneMasks) {
  const TargetSubtargetInfo &ST = MF.getSubtarget();
  bool UseAA = EnableAASchedMI.getNumOccurrences() > 0 ? EnableAASchedMI
                                                       : ST.useAA();
  AAForDep = UseAA ? AA : nullptr;

  BarrierChain = nullptr;

  this->TrackLaneMasks = TrackLaneMasks;
  MISUnitMap.clear();
  ScheduleDAG::clearDAG();

  // Create an SUnit for each real instruction.
  initSUnits();

  if (PDiffs)
    PDiffs->init(SUnits.size());

  // We build scheduling units by walking a block's instruction list
  // from bottom to top.

  // Each MIs' memory operand(s) is analyzed to a list of underlying
  // objects. The SU is then inserted in the SUList(s) mapped from the
  // Value(s). Each Value thus gets mapped to lists of SUs depending
  // on it, stores and loads kept separately. Two SUs are trivially
  // non-aliasing if they both depend on only identified Values and do
  // not share any common Value.
  Value2SUsMap Stores, Loads(1 /*TrueMemOrderLatency*/);

  // Certain memory accesses are known to not alias any SU in Stores
  // or Loads, and have therefore their own 'NonAlias'
  // domain. E.g. spill / reload instructions never alias LLVM I/R
  // Values. It would be nice to assume that this type of memory
  // accesses always have a proper memory operand modelling, and are
  // therefore never unanalyzable, but this is conservatively not
  // done.
  Value2SUsMap NonAliasStores, NonAliasLoads(1 /*TrueMemOrderLatency*/);

  // Track all instructions that may raise floating-point exceptions.
  // These do not depend on one other (or normal loads or stores), but
  // must not be rescheduled across global barriers.  Note that we don't
  // really need a "map" here since we don't track those MIs by value;
  // using the same Value2SUsMap data type here is simply a matter of
  // convenience.
  Value2SUsMap FPExceptions;

  // Remove any stale debug info; sometimes BuildSchedGraph is called again
  // without emitting the info from the previous call.
  DbgValues.clear();
  FirstDbgValue = nullptr;

  assert(Defs.empty() && Uses.empty() &&
         "Only BuildGraph should update Defs/Uses");
  Defs.setUniverse(TRI->getNumRegs());
  Uses.setUniverse(TRI->getNumRegs());

  assert(CurrentVRegDefs.empty() && "nobody else should use CurrentVRegDefs");
  assert(CurrentVRegUses.empty() && "nobody else should use CurrentVRegUses");
  unsigned NumVirtRegs = MRI.getNumVirtRegs();
  CurrentVRegDefs.setUniverse(NumVirtRegs);
  CurrentVRegUses.setUniverse(NumVirtRegs);

  // Model data dependencies between instructions being scheduled and the
  // ExitSU.
  addSchedBarrierDeps();

  // Walk the list of instructions, from bottom moving up.
  MachineInstr *DbgMI = nullptr;
  for (MachineBasicBlock::iterator MII = RegionEnd, MIE = RegionBegin;
       MII != MIE; --MII) {
    MachineInstr &MI = *std::prev(MII);
    if (DbgMI) {
      DbgValues.emplace_back(DbgMI, &MI);
      DbgMI = nullptr;
    }

    if (MI.isDebugValue() || MI.isDebugPHI()) {
      DbgMI = &MI;
      continue;
    }

    if (MI.isDebugLabel() || MI.isDebugRef() || MI.isPseudoProbe())
      continue;

    SUnit *SU = MISUnitMap[&MI];
    assert(SU && "No SUnit mapped to this MI");

    if (RPTracker) {
      RegisterOperands RegOpers;
      RegOpers.collect(MI, *TRI, MRI, TrackLaneMasks, false);
      if (TrackLaneMasks) {
        SlotIndex SlotIdx = LIS->getInstructionIndex(MI);
        RegOpers.adjustLaneLiveness(*LIS, MRI, SlotIdx);
      }
      if (PDiffs != nullptr)
        PDiffs->addInstruction(SU->NodeNum, RegOpers, MRI);

      if (RPTracker->getPos() == RegionEnd || &*RPTracker->getPos() != &MI)
        RPTracker->recedeSkipDebugValues();
      assert(&*RPTracker->getPos() == &MI && "RPTracker in sync");
      RPTracker->recede(RegOpers);
    }

    assert(
        (CanHandleTerminators || (!MI.isTerminator() && !MI.isPosition())) &&
        "Cannot schedule terminators or labels!");

    // Add register-based dependencies (data, anti, and output).
    // For some instructions (calls, returns, inline-asm, etc.) there can
    // be explicit uses and implicit defs, in which case the use will appear
    // on the operand list before the def. Do two passes over the operand
    // list to make sure that defs are processed before any uses.
    bool HasVRegDef = false;
    for (unsigned j = 0, n = MI.getNumOperands(); j != n; ++j) {
      const MachineOperand &MO = MI.getOperand(j);
      if (!MO.isReg() || !MO.isDef())
        continue;
      Register Reg = MO.getReg();
      if (Reg.isPhysical()) {
        addPhysRegDeps(SU, j);
      } else if (Reg.isVirtual()) {
        HasVRegDef = true;
        addVRegDefDeps(SU, j);
      }
    }
    // Now process all uses.
    for (unsigned j = 0, n = MI.getNumOperands(); j != n; ++j) {
      const MachineOperand &MO = MI.getOperand(j);
      // Only look at use operands.
      // We do not need to check for MO.readsReg() here because subsequent
      // subregister defs will get output dependence edges and need no
      // additional use dependencies.
      if (!MO.isReg() || !MO.isUse())
        continue;
      Register Reg = MO.getReg();
      if (Reg.isPhysical()) {
        addPhysRegDeps(SU, j);
      } else if (Reg.isVirtual() && MO.readsReg()) {
        addVRegUseDeps(SU, j);
      }
    }

    // If we haven't seen any uses in this scheduling region, create a
    // dependence edge to ExitSU to model the live-out latency. This is required
    // for vreg defs with no in-region use, and prefetches with no vreg def.
    //
    // FIXME: NumDataSuccs would be more precise than NumSuccs here. This
    // check currently relies on being called before adding chain deps.
    if (SU->NumSuccs == 0 && SU->Latency > 1 && (HasVRegDef || MI.mayLoad())) {
      SDep Dep(SU, SDep::Artificial);
      Dep.setLatency(SU->Latency - 1);
      ExitSU.addPred(Dep);
    }

    // Add memory dependencies (Note: isStoreToStackSlot and
    // isLoadFromStackSLot are not usable after stack slots are lowered to
    // actual addresses).

    // This is a barrier event that acts as a pivotal node in the DAG.
    if (isGlobalMemoryObject(&MI)) {

      // Become the barrier chain.
      if (BarrierChain)
        BarrierChain->addPredBarrier(SU);
      BarrierChain = SU;

      LLVM_DEBUG(dbgs() << "Global memory object and new barrier chain: SU("
                        << BarrierChain->NodeNum << ").\n";);

      // Add dependencies against everything below it and clear maps.
      addBarrierChain(Stores);
      addBarrierChain(Loads);
      addBarrierChain(NonAliasStores);
      addBarrierChain(NonAliasLoads);
      addBarrierChain(FPExceptions);

      continue;
    }

    // Instructions that may raise FP exceptions may not be moved
    // across any global barriers.
    if (MI.mayRaiseFPException()) {
      if (BarrierChain)
        BarrierChain->addPredBarrier(SU);

      FPExceptions.insert(SU, UnknownValue);

      if (FPExceptions.size() >= HugeRegion) {
        LLVM_DEBUG(dbgs() << "Reducing FPExceptions map.\n";);
        Value2SUsMap empty;
        reduceHugeMemNodeMaps(FPExceptions, empty, getReductionSize());
      }
    }

    // If it's not a store or a variant load, we're done.
    if (!MI.mayStore() &&
        !(MI.mayLoad() && !MI.isDereferenceableInvariantLoad()))
      continue;

    // Always add dependecy edge to BarrierChain if present.
    if (BarrierChain)
      BarrierChain->addPredBarrier(SU);

    // Find the underlying objects for MI. The Objs vector is either
    // empty, or filled with the Values of memory locations which this
    // SU depends on.
    UnderlyingObjectsVector Objs;
    bool ObjsFound = getUnderlyingObjectsForInstr(&MI, MFI, Objs,
                                                  MF.getDataLayout());

    if (MI.mayStore()) {
      if (!ObjsFound) {
        // An unknown store depends on all stores and loads.
        addChainDependencies(SU, Stores);
        addChainDependencies(SU, NonAliasStores);
        addChainDependencies(SU, Loads);
        addChainDependencies(SU, NonAliasLoads);

        // Map this store to 'UnknownValue'.
        Stores.insert(SU, UnknownValue);
      } else {
        // Add precise dependencies against all previously seen memory
        // accesses mapped to the same Value(s).
        for (const UnderlyingObject &UnderlObj : Objs) {
          ValueType V = UnderlObj.getValue();
          bool ThisMayAlias = UnderlObj.mayAlias();

          // Add dependencies to previous stores and loads mapped to V.
          addChainDependencies(SU, (ThisMayAlias ? Stores : NonAliasStores), V);
          addChainDependencies(SU, (ThisMayAlias ? Loads : NonAliasLoads), V);
        }
        // Update the store map after all chains have been added to avoid adding
        // self-loop edge if multiple underlying objects are present.
        for (const UnderlyingObject &UnderlObj : Objs) {
          ValueType V = UnderlObj.getValue();
          bool ThisMayAlias = UnderlObj.mayAlias();

          // Map this store to V.
          (ThisMayAlias ? Stores : NonAliasStores).insert(SU, V);
        }
        // The store may have dependencies to unanalyzable loads and
        // stores.
        addChainDependencies(SU, Loads, UnknownValue);
        addChainDependencies(SU, Stores, UnknownValue);
      }
    } else { // SU is a load.
      if (!ObjsFound) {
        // An unknown load depends on all stores.
        addChainDependencies(SU, Stores);
        addChainDependencies(SU, NonAliasStores);

        Loads.insert(SU, UnknownValue);
      } else {
        for (const UnderlyingObject &UnderlObj : Objs) {
          ValueType V = UnderlObj.getValue();
          bool ThisMayAlias = UnderlObj.mayAlias();

          // Add precise dependencies against all previously seen stores
          // mapping to the same Value(s).
          addChainDependencies(SU, (ThisMayAlias ? Stores : NonAliasStores), V);

          // Map this load to V.
          (ThisMayAlias ? Loads : NonAliasLoads).insert(SU, V);
        }
        // The load may have dependencies to unanalyzable stores.
        addChainDependencies(SU, Stores, UnknownValue);
      }
    }

    // Reduce maps if they grow huge.
    if (Stores.size() + Loads.size() >= HugeRegion) {
      LLVM_DEBUG(dbgs() << "Reducing Stores and Loads maps.\n";);
      reduceHugeMemNodeMaps(Stores, Loads, getReductionSize());
    }
    if (NonAliasStores.size() + NonAliasLoads.size() >= HugeRegion) {
      LLVM_DEBUG(
          dbgs() << "Reducing NonAliasStores and NonAliasLoads maps.\n";);
      reduceHugeMemNodeMaps(NonAliasStores, NonAliasLoads, getReductionSize());
    }
  }

  if (DbgMI)
    FirstDbgValue = DbgMI;

  Defs.clear();
  Uses.clear();
  CurrentVRegDefs.clear();
  CurrentVRegUses.clear();

  Topo.MarkDirty();
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const PseudoSourceValue* PSV) {
  PSV->printCustom(OS);
  return OS;
}

void ScheduleDAGInstrs::Value2SUsMap::dump() {
  for (const auto &[ValType, SUs] : *this) {
    if (isa<const Value *>(ValType)) {
      const Value *V = cast<const Value *>(ValType);
      if (isa<UndefValue>(V))
        dbgs() << "Unknown";
      else
        V->printAsOperand(dbgs());
    } else if (isa<const PseudoSourceValue *>(ValType))
      dbgs() << cast<const PseudoSourceValue *>(ValType);
    else
      llvm_unreachable("Unknown Value type.");

    dbgs() << " : ";
    dumpSUList(SUs);
  }
}

void ScheduleDAGInstrs::reduceHugeMemNodeMaps(Value2SUsMap &stores,
                                              Value2SUsMap &loads, unsigned N) {
  LLVM_DEBUG(dbgs() << "Before reduction:\nStoring SUnits:\n"; stores.dump();
             dbgs() << "Loading SUnits:\n"; loads.dump());

  // Insert all SU's NodeNums into a vector and sort it.
  std::vector<unsigned> NodeNums;
  NodeNums.reserve(stores.size() + loads.size());
  for (const auto &[V, SUs] : stores) {
    (void)V;
    for (const auto *SU : SUs)
      NodeNums.push_back(SU->NodeNum);
  }
  for (const auto &[V, SUs] : loads) {
    (void)V;
    for (const auto *SU : SUs)
      NodeNums.push_back(SU->NodeNum);
  }
  llvm::sort(NodeNums);

  // The N last elements in NodeNums will be removed, and the SU with
  // the lowest NodeNum of them will become the new BarrierChain to
  // let the not yet seen SUs have a dependency to the removed SUs.
  assert(N <= NodeNums.size());
  SUnit *newBarrierChain = &SUnits[*(NodeNums.end() - N)];
  if (BarrierChain) {
    // The aliasing and non-aliasing maps reduce independently of each
    // other, but share a common BarrierChain. Check if the
    // newBarrierChain is above the former one. If it is not, it may
    // introduce a loop to use newBarrierChain, so keep the old one.
    if (newBarrierChain->NodeNum < BarrierChain->NodeNum) {
      BarrierChain->addPredBarrier(newBarrierChain);
      BarrierChain = newBarrierChain;
      LLVM_DEBUG(dbgs() << "Inserting new barrier chain: SU("
                        << BarrierChain->NodeNum << ").\n";);
    }
    else
      LLVM_DEBUG(dbgs() << "Keeping old barrier chain: SU("
                        << BarrierChain->NodeNum << ").\n";);
  }
  else
    BarrierChain = newBarrierChain;

  insertBarrierChain(stores);
  insertBarrierChain(loads);

  LLVM_DEBUG(dbgs() << "After reduction:\nStoring SUnits:\n"; stores.dump();
             dbgs() << "Loading SUnits:\n"; loads.dump());
}

static void toggleKills(const MachineRegisterInfo &MRI, LiveRegUnits &LiveRegs,
                        MachineInstr &MI, bool addToLiveRegs) {
  for (MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.readsReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;

    // Things that are available after the instruction are killed by it.
    bool IsKill = LiveRegs.available(Reg);

    // Exception: Do not kill reserved registers
    MO.setIsKill(IsKill && !MRI.isReserved(Reg));
    if (addToLiveRegs)
      LiveRegs.addReg(Reg);
  }
}

void ScheduleDAGInstrs::fixupKills(MachineBasicBlock &MBB) {
  LLVM_DEBUG(dbgs() << "Fixup kills for " << printMBBReference(MBB) << '\n');

  LiveRegs.init(*TRI);
  LiveRegs.addLiveOuts(MBB);

  // Examine block from end to start...
  for (MachineInstr &MI : llvm::reverse(MBB)) {
    if (MI.isDebugOrPseudoInstr())
      continue;

    // Update liveness.  Registers that are defed but not used in this
    // instruction are now dead. Mark register and all subregs as they
    // are completely defined.
    for (ConstMIBundleOperands O(MI); O.isValid(); ++O) {
      const MachineOperand &MO = *O;
      if (MO.isReg()) {
        if (!MO.isDef())
          continue;
        Register Reg = MO.getReg();
        if (!Reg)
          continue;
        LiveRegs.removeReg(Reg);
      } else if (MO.isRegMask()) {
        LiveRegs.removeRegsNotPreserved(MO.getRegMask());
      }
    }

    // If there is a bundle header fix it up first.
    if (!MI.isBundled()) {
      toggleKills(MRI, LiveRegs, MI, true);
    } else {
      MachineBasicBlock::instr_iterator Bundle = MI.getIterator();
      if (MI.isBundle())
        toggleKills(MRI, LiveRegs, MI, false);

      // Some targets make the (questionable) assumtion that the instructions
      // inside the bundle are ordered and consequently only the last use of
      // a register inside the bundle can kill it.
      MachineBasicBlock::instr_iterator I = std::next(Bundle);
      while (I->isBundledWithSucc())
        ++I;
      do {
        if (!I->isDebugOrPseudoInstr())
          toggleKills(MRI, LiveRegs, *I, true);
        --I;
      } while (I != Bundle);
    }
  }
}

void ScheduleDAGInstrs::dumpNode(const SUnit &SU) const {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  dumpNodeName(SU);
  if (SchedPrintCycles)
    dbgs() << " [TopReadyCycle = " << SU.TopReadyCycle
           << ", BottomReadyCycle = " << SU.BotReadyCycle << "]";
  dbgs() << ": ";
  SU.getInstr()->dump();
#endif
}

void ScheduleDAGInstrs::dump() const {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  if (EntrySU.getInstr() != nullptr)
    dumpNodeAll(EntrySU);
  for (const SUnit &SU : SUnits)
    dumpNodeAll(SU);
  if (ExitSU.getInstr() != nullptr)
    dumpNodeAll(ExitSU);
#endif
}

std::string ScheduleDAGInstrs::getGraphNodeLabel(const SUnit *SU) const {
  std::string s;
  raw_string_ostream oss(s);
  if (SU == &EntrySU)
    oss << "<entry>";
  else if (SU == &ExitSU)
    oss << "<exit>";
  else
    SU->getInstr()->print(oss, /*IsStandalone=*/true);
  return s;
}

/// Return the basic block label. It is not necessarilly unique because a block
/// contains multiple scheduling regions. But it is fine for visualization.
std::string ScheduleDAGInstrs::getDAGName() const {
  return "dag." + BB->getFullName();
}

bool ScheduleDAGInstrs::canAddEdge(SUnit *SuccSU, SUnit *PredSU) {
  return SuccSU == &ExitSU || !Topo.IsReachable(PredSU, SuccSU);
}

bool ScheduleDAGInstrs::addEdge(SUnit *SuccSU, const SDep &PredDep) {
  if (SuccSU != &ExitSU) {
    // Do not use WillCreateCycle, it assumes SD scheduling.
    // If Pred is reachable from Succ, then the edge creates a cycle.
    if (Topo.IsReachable(PredDep.getSUnit(), SuccSU))
      return false;
    Topo.AddPredQueued(SuccSU, PredDep.getSUnit());
  }
  SuccSU->addPred(PredDep, /*Required=*/!PredDep.isArtificial());
  // Return true regardless of whether a new edge needed to be inserted.
  return true;
}

//===----------------------------------------------------------------------===//
// SchedDFSResult Implementation
//===----------------------------------------------------------------------===//

namespace llvm {

/// Internal state used to compute SchedDFSResult.
class SchedDFSImpl {
  SchedDFSResult &R;

  /// Join DAG nodes into equivalence classes by their subtree.
  IntEqClasses SubtreeClasses;
  /// List PredSU, SuccSU pairs that represent data edges between subtrees.
  std::vector<std::pair<const SUnit *, const SUnit*>> ConnectionPairs;

  struct RootData {
    unsigned NodeID;
    unsigned ParentNodeID;  ///< Parent node (member of the parent subtree).
    unsigned SubInstrCount = 0; ///< Instr count in this tree only, not
                                /// children.

    RootData(unsigned id): NodeID(id),
                           ParentNodeID(SchedDFSResult::InvalidSubtreeID) {}

    unsigned getSparseSetIndex() const { return NodeID; }
  };

  SparseSet<RootData> RootSet;

public:
  SchedDFSImpl(SchedDFSResult &r): R(r), SubtreeClasses(R.DFSNodeData.size()) {
    RootSet.setUniverse(R.DFSNodeData.size());
  }

  /// Returns true if this node been visited by the DFS traversal.
  ///
  /// During visitPostorderNode the Node's SubtreeID is assigned to the Node
  /// ID. Later, SubtreeID is updated but remains valid.
  bool isVisited(const SUnit *SU) const {
    return R.DFSNodeData[SU->NodeNum].SubtreeID
      != SchedDFSResult::InvalidSubtreeID;
  }

  /// Initializes this node's instruction count. We don't need to flag the node
  /// visited until visitPostorder because the DAG cannot have cycles.
  void visitPreorder(const SUnit *SU) {
    R.DFSNodeData[SU->NodeNum].InstrCount =
      SU->getInstr()->isTransient() ? 0 : 1;
  }

  /// Called once for each node after all predecessors are visited. Revisit this
  /// node's predecessors and potentially join them now that we know the ILP of
  /// the other predecessors.
  void visitPostorderNode(const SUnit *SU) {
    // Mark this node as the root of a subtree. It may be joined with its
    // successors later.
    R.DFSNodeData[SU->NodeNum].SubtreeID = SU->NodeNum;
    RootData RData(SU->NodeNum);
    RData.SubInstrCount = SU->getInstr()->isTransient() ? 0 : 1;

    // If any predecessors are still in their own subtree, they either cannot be
    // joined or are large enough to remain separate. If this parent node's
    // total instruction count is not greater than a child subtree by at least
    // the subtree limit, then try to join it now since splitting subtrees is
    // only useful if multiple high-pressure paths are possible.
    unsigned InstrCount = R.DFSNodeData[SU->NodeNum].InstrCount;
    for (const SDep &PredDep : SU->Preds) {
      if (PredDep.getKind() != SDep::Data)
        continue;
      unsigned PredNum = PredDep.getSUnit()->NodeNum;
      if ((InstrCount - R.DFSNodeData[PredNum].InstrCount) < R.SubtreeLimit)
        joinPredSubtree(PredDep, SU, /*CheckLimit=*/false);

      // Either link or merge the TreeData entry from the child to the parent.
      if (R.DFSNodeData[PredNum].SubtreeID == PredNum) {
        // If the predecessor's parent is invalid, this is a tree edge and the
        // current node is the parent.
        if (RootSet[PredNum].ParentNodeID == SchedDFSResult::InvalidSubtreeID)
          RootSet[PredNum].ParentNodeID = SU->NodeNum;
      }
      else if (RootSet.count(PredNum)) {
        // The predecessor is not a root, but is still in the root set. This
        // must be the new parent that it was just joined to. Note that
        // RootSet[PredNum].ParentNodeID may either be invalid or may still be
        // set to the original parent.
        RData.SubInstrCount += RootSet[PredNum].SubInstrCount;
        RootSet.erase(PredNum);
      }
    }
    RootSet[SU->NodeNum] = RData;
  }

  /// Called once for each tree edge after calling visitPostOrderNode on
  /// the predecessor. Increment the parent node's instruction count and
  /// preemptively join this subtree to its parent's if it is small enough.
  void visitPostorderEdge(const SDep &PredDep, const SUnit *Succ) {
    R.DFSNodeData[Succ->NodeNum].InstrCount
      += R.DFSNodeData[PredDep.getSUnit()->NodeNum].InstrCount;
    joinPredSubtree(PredDep, Succ);
  }

  /// Adds a connection for cross edges.
  void visitCrossEdge(const SDep &PredDep, const SUnit *Succ) {
    ConnectionPairs.emplace_back(PredDep.getSUnit(), Succ);
  }

  /// Sets each node's subtree ID to the representative ID and record
  /// connections between trees.
  void finalize() {
    SubtreeClasses.compress();
    R.DFSTreeData.resize(SubtreeClasses.getNumClasses());
    assert(SubtreeClasses.getNumClasses() == RootSet.size()
           && "number of roots should match trees");
    for (const RootData &Root : RootSet) {
      unsigned TreeID = SubtreeClasses[Root.NodeID];
      if (Root.ParentNodeID != SchedDFSResult::InvalidSubtreeID)
        R.DFSTreeData[TreeID].ParentTreeID = SubtreeClasses[Root.ParentNodeID];
      R.DFSTreeData[TreeID].SubInstrCount = Root.SubInstrCount;
      // Note that SubInstrCount may be greater than InstrCount if we joined
      // subtrees across a cross edge. InstrCount will be attributed to the
      // original parent, while SubInstrCount will be attributed to the joined
      // parent.
    }
    R.SubtreeConnections.resize(SubtreeClasses.getNumClasses());
    R.SubtreeConnectLevels.resize(SubtreeClasses.getNumClasses());
    LLVM_DEBUG(dbgs() << R.getNumSubtrees() << " subtrees:\n");
    for (unsigned Idx = 0, End = R.DFSNodeData.size(); Idx != End; ++Idx) {
      R.DFSNodeData[Idx].SubtreeID = SubtreeClasses[Idx];
      LLVM_DEBUG(dbgs() << "  SU(" << Idx << ") in tree "
                        << R.DFSNodeData[Idx].SubtreeID << '\n');
    }
    for (const auto &[Pred, Succ] : ConnectionPairs) {
      unsigned PredTree = SubtreeClasses[Pred->NodeNum];
      unsigned SuccTree = SubtreeClasses[Succ->NodeNum];
      if (PredTree == SuccTree)
        continue;
      unsigned Depth = Pred->getDepth();
      addConnection(PredTree, SuccTree, Depth);
      addConnection(SuccTree, PredTree, Depth);
    }
  }

protected:
  /// Joins the predecessor subtree with the successor that is its DFS parent.
  /// Applies some heuristics before joining.
  bool joinPredSubtree(const SDep &PredDep, const SUnit *Succ,
                       bool CheckLimit = true) {
    assert(PredDep.getKind() == SDep::Data && "Subtrees are for data edges");

    // Check if the predecessor is already joined.
    const SUnit *PredSU = PredDep.getSUnit();
    unsigned PredNum = PredSU->NodeNum;
    if (R.DFSNodeData[PredNum].SubtreeID != PredNum)
      return false;

    // Four is the magic number of successors before a node is considered a
    // pinch point.
    unsigned NumDataSucs = 0;
    for (const SDep &SuccDep : PredSU->Succs) {
      if (SuccDep.getKind() == SDep::Data) {
        if (++NumDataSucs >= 4)
          return false;
      }
    }
    if (CheckLimit && R.DFSNodeData[PredNum].InstrCount > R.SubtreeLimit)
      return false;
    R.DFSNodeData[PredNum].SubtreeID = Succ->NodeNum;
    SubtreeClasses.join(Succ->NodeNum, PredNum);
    return true;
  }

  /// Called by finalize() to record a connection between trees.
  void addConnection(unsigned FromTree, unsigned ToTree, unsigned Depth) {
    if (!Depth)
      return;

    do {
      SmallVectorImpl<SchedDFSResult::Connection> &Connections =
        R.SubtreeConnections[FromTree];
      for (SchedDFSResult::Connection &C : Connections) {
        if (C.TreeID == ToTree) {
          C.Level = std::max(C.Level, Depth);
          return;
        }
      }
      Connections.push_back(SchedDFSResult::Connection(ToTree, Depth));
      FromTree = R.DFSTreeData[FromTree].ParentTreeID;
    } while (FromTree != SchedDFSResult::InvalidSubtreeID);
  }
};

} // end namespace llvm

namespace {

/// Manage the stack used by a reverse depth-first search over the DAG.
class SchedDAGReverseDFS {
  std::vector<std::pair<const SUnit *, SUnit::const_pred_iterator>> DFSStack;

public:
  bool isComplete() const { return DFSStack.empty(); }

  void follow(const SUnit *SU) {
    DFSStack.emplace_back(SU, SU->Preds.begin());
  }
  void advance() { ++DFSStack.back().second; }

  const SDep *backtrack() {
    DFSStack.pop_back();
    return DFSStack.empty() ? nullptr : std::prev(DFSStack.back().second);
  }

  const SUnit *getCurr() const { return DFSStack.back().first; }

  SUnit::const_pred_iterator getPred() const { return DFSStack.back().second; }

  SUnit::const_pred_iterator getPredEnd() const {
    return getCurr()->Preds.end();
  }
};

} // end anonymous namespace

static bool hasDataSucc(const SUnit *SU) {
  for (const SDep &SuccDep : SU->Succs) {
    if (SuccDep.getKind() == SDep::Data &&
        !SuccDep.getSUnit()->isBoundaryNode())
      return true;
  }
  return false;
}

/// Computes an ILP metric for all nodes in the subDAG reachable via depth-first
/// search from this root.
void SchedDFSResult::compute(ArrayRef<SUnit> SUnits) {
  if (!IsBottomUp)
    llvm_unreachable("Top-down ILP metric is unimplemented");

  SchedDFSImpl Impl(*this);
  for (const SUnit &SU : SUnits) {
    if (Impl.isVisited(&SU) || hasDataSucc(&SU))
      continue;

    SchedDAGReverseDFS DFS;
    Impl.visitPreorder(&SU);
    DFS.follow(&SU);
    while (true) {
      // Traverse the leftmost path as far as possible.
      while (DFS.getPred() != DFS.getPredEnd()) {
        const SDep &PredDep = *DFS.getPred();
        DFS.advance();
        // Ignore non-data edges.
        if (PredDep.getKind() != SDep::Data
            || PredDep.getSUnit()->isBoundaryNode()) {
          continue;
        }
        // An already visited edge is a cross edge, assuming an acyclic DAG.
        if (Impl.isVisited(PredDep.getSUnit())) {
          Impl.visitCrossEdge(PredDep, DFS.getCurr());
          continue;
        }
        Impl.visitPreorder(PredDep.getSUnit());
        DFS.follow(PredDep.getSUnit());
      }
      // Visit the top of the stack in postorder and backtrack.
      const SUnit *Child = DFS.getCurr();
      const SDep *PredDep = DFS.backtrack();
      Impl.visitPostorderNode(Child);
      if (PredDep)
        Impl.visitPostorderEdge(*PredDep, DFS.getCurr());
      if (DFS.isComplete())
        break;
    }
  }
  Impl.finalize();
}

/// The root of the given SubtreeID was just scheduled. For all subtrees
/// connected to this tree, record the depth of the connection so that the
/// nearest connected subtrees can be prioritized.
void SchedDFSResult::scheduleTree(unsigned SubtreeID) {
  for (const Connection &C : SubtreeConnections[SubtreeID]) {
    SubtreeConnectLevels[C.TreeID] =
      std::max(SubtreeConnectLevels[C.TreeID], C.Level);
    LLVM_DEBUG(dbgs() << "  Tree: " << C.TreeID << " @"
                      << SubtreeConnectLevels[C.TreeID] << '\n');
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void ILPValue::print(raw_ostream &OS) const {
  OS << InstrCount << " / " << Length << " = ";
  if (!Length)
    OS << "BADILP";
  else
    OS << format("%g", ((double)InstrCount / Length));
}

LLVM_DUMP_METHOD void ILPValue::dump() const {
  dbgs() << *this << '\n';
}

namespace llvm {

LLVM_ATTRIBUTE_UNUSED
raw_ostream &operator<<(raw_ostream &OS, const ILPValue &Val) {
  Val.print(OS);
  return OS;
}

} // end namespace llvm

#endif
