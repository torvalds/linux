//===- GCNIterativeScheduler.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the class GCNIterativeScheduler.
///
//===----------------------------------------------------------------------===//

#include "GCNIterativeScheduler.h"
#include "GCNSchedStrategy.h"
#include "SIMachineFunctionInfo.h"

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

namespace llvm {

std::vector<const SUnit *> makeMinRegSchedule(ArrayRef<const SUnit *> TopRoots,
                                              const ScheduleDAG &DAG);

std::vector<const SUnit *> makeGCNILPScheduler(ArrayRef<const SUnit *> BotRoots,
                                               const ScheduleDAG &DAG);
} // namespace llvm

// shim accessors for different order containers
static inline MachineInstr *getMachineInstr(MachineInstr *MI) {
  return MI;
}
static inline MachineInstr *getMachineInstr(const SUnit *SU) {
  return SU->getInstr();
}
static inline MachineInstr *getMachineInstr(const SUnit &SU) {
  return SU.getInstr();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD
static void printRegion(raw_ostream &OS,
                        MachineBasicBlock::iterator Begin,
                        MachineBasicBlock::iterator End,
                        const LiveIntervals *LIS,
                        unsigned MaxInstNum =
                          std::numeric_limits<unsigned>::max()) {
  auto BB = Begin->getParent();
  OS << BB->getParent()->getName() << ":" << printMBBReference(*BB) << ' '
     << BB->getName() << ":\n";
  auto I = Begin;
  MaxInstNum = std::max(MaxInstNum, 1u);
  for (; I != End && MaxInstNum; ++I, --MaxInstNum) {
    if (!I->isDebugInstr() && LIS)
      OS << LIS->getInstructionIndex(*I);
    OS << '\t' << *I;
  }
  if (I != End) {
    OS << "\t...\n";
    I = std::prev(End);
    if (!I->isDebugInstr() && LIS)
      OS << LIS->getInstructionIndex(*I);
    OS << '\t' << *I;
  }
  if (End != BB->end()) { // print boundary inst if present
    OS << "----\n";
    if (LIS) OS << LIS->getInstructionIndex(*End) << '\t';
    OS << *End;
  }
}

LLVM_DUMP_METHOD
static void printLivenessInfo(raw_ostream &OS,
                              MachineBasicBlock::iterator Begin,
                              MachineBasicBlock::iterator End,
                              const LiveIntervals *LIS) {
  const auto BB = Begin->getParent();
  const auto &MRI = BB->getParent()->getRegInfo();

  const auto LiveIns = getLiveRegsBefore(*Begin, *LIS);
  OS << "LIn RP: " << print(getRegPressure(MRI, LiveIns));

  const auto BottomMI = End == BB->end() ? std::prev(End) : End;
  const auto LiveOuts = getLiveRegsAfter(*BottomMI, *LIS);
  OS << "LOt RP: " << print(getRegPressure(MRI, LiveOuts));
}

LLVM_DUMP_METHOD
void GCNIterativeScheduler::printRegions(raw_ostream &OS) const {
  const auto &ST = MF.getSubtarget<GCNSubtarget>();
  for (const auto R : Regions) {
    OS << "Region to schedule ";
    printRegion(OS, R->Begin, R->End, LIS, 1);
    printLivenessInfo(OS, R->Begin, R->End, LIS);
    OS << "Max RP: " << print(R->MaxPressure, &ST);
  }
}

LLVM_DUMP_METHOD
void GCNIterativeScheduler::printSchedResult(raw_ostream &OS,
                                             const Region *R,
                                             const GCNRegPressure &RP) const {
  OS << "\nAfter scheduling ";
  printRegion(OS, R->Begin, R->End, LIS);
  printSchedRP(OS, R->MaxPressure, RP);
  OS << '\n';
}

LLVM_DUMP_METHOD
void GCNIterativeScheduler::printSchedRP(raw_ostream &OS,
                                         const GCNRegPressure &Before,
                                         const GCNRegPressure &After) const {
  const auto &ST = MF.getSubtarget<GCNSubtarget>();
  OS << "RP before: " << print(Before, &ST)
     << "RP after:  " << print(After, &ST);
}
#endif

// DAG builder helper
class GCNIterativeScheduler::BuildDAG {
  GCNIterativeScheduler &Sch;
  SmallVector<SUnit *, 8> TopRoots;

  SmallVector<SUnit*, 8> BotRoots;
public:
  BuildDAG(const Region &R, GCNIterativeScheduler &_Sch)
    : Sch(_Sch) {
    auto BB = R.Begin->getParent();
    Sch.BaseClass::startBlock(BB);
    Sch.BaseClass::enterRegion(BB, R.Begin, R.End, R.NumRegionInstrs);

    Sch.buildSchedGraph(Sch.AA, nullptr, nullptr, nullptr,
                        /*TrackLaneMask*/true);
    Sch.Topo.InitDAGTopologicalSorting();
    Sch.findRootsAndBiasEdges(TopRoots, BotRoots);
  }

  ~BuildDAG() {
    Sch.BaseClass::exitRegion();
    Sch.BaseClass::finishBlock();
  }

  ArrayRef<const SUnit *> getTopRoots() const {
    return TopRoots;
  }
  ArrayRef<SUnit*> getBottomRoots() const {
    return BotRoots;
  }
};

class GCNIterativeScheduler::OverrideLegacyStrategy {
  GCNIterativeScheduler &Sch;
  Region &Rgn;
  std::unique_ptr<MachineSchedStrategy> SaveSchedImpl;
  GCNRegPressure SaveMaxRP;

public:
  OverrideLegacyStrategy(Region &R,
                         MachineSchedStrategy &OverrideStrategy,
                         GCNIterativeScheduler &_Sch)
    : Sch(_Sch)
    , Rgn(R)
    , SaveSchedImpl(std::move(_Sch.SchedImpl))
    , SaveMaxRP(R.MaxPressure) {
    Sch.SchedImpl.reset(&OverrideStrategy);
    auto BB = R.Begin->getParent();
    Sch.BaseClass::startBlock(BB);
    Sch.BaseClass::enterRegion(BB, R.Begin, R.End, R.NumRegionInstrs);
  }

  ~OverrideLegacyStrategy() {
    Sch.BaseClass::exitRegion();
    Sch.BaseClass::finishBlock();
    Sch.SchedImpl.release();
    Sch.SchedImpl = std::move(SaveSchedImpl);
  }

  void schedule() {
    assert(Sch.RegionBegin == Rgn.Begin && Sch.RegionEnd == Rgn.End);
    LLVM_DEBUG(dbgs() << "\nScheduling ";
               printRegion(dbgs(), Rgn.Begin, Rgn.End, Sch.LIS, 2));
    Sch.BaseClass::schedule();

    // Unfortunately placeDebugValues incorrectly modifies RegionEnd, restore
    Sch.RegionEnd = Rgn.End;
    //assert(Rgn.End == Sch.RegionEnd);
    Rgn.Begin = Sch.RegionBegin;
    Rgn.MaxPressure.clear();
  }

  void restoreOrder() {
    assert(Sch.RegionBegin == Rgn.Begin && Sch.RegionEnd == Rgn.End);
    // DAG SUnits are stored using original region's order
    // so just use SUnits as the restoring schedule
    Sch.scheduleRegion(Rgn, Sch.SUnits, SaveMaxRP);
  }
};

namespace {

// just a stub to make base class happy
class SchedStrategyStub : public MachineSchedStrategy {
public:
  bool shouldTrackPressure() const override { return false; }
  bool shouldTrackLaneMasks() const override { return false; }
  void initialize(ScheduleDAGMI *DAG) override {}
  SUnit *pickNode(bool &IsTopNode) override { return nullptr; }
  void schedNode(SUnit *SU, bool IsTopNode) override {}
  void releaseTopNode(SUnit *SU) override {}
  void releaseBottomNode(SUnit *SU) override {}
};

} // end anonymous namespace

GCNIterativeScheduler::GCNIterativeScheduler(MachineSchedContext *C,
                                             StrategyKind S)
  : BaseClass(C, std::make_unique<SchedStrategyStub>())
  , Context(C)
  , Strategy(S)
  , UPTracker(*LIS) {
}

// returns max pressure for a region
GCNRegPressure
GCNIterativeScheduler::getRegionPressure(MachineBasicBlock::iterator Begin,
                                         MachineBasicBlock::iterator End)
  const {
  // For the purpose of pressure tracking bottom inst of the region should
  // be also processed. End is either BB end, BB terminator inst or sched
  // boundary inst.
  auto const BBEnd = Begin->getParent()->end();
  auto const BottomMI = End == BBEnd ? std::prev(End) : End;

  // scheduleRegions walks bottom to top, so its likely we just get next
  // instruction to track
  auto AfterBottomMI = std::next(BottomMI);
  if (AfterBottomMI == BBEnd ||
      &*AfterBottomMI != UPTracker.getLastTrackedMI()) {
    UPTracker.reset(*BottomMI);
  } else {
    assert(UPTracker.isValid());
  }

  for (auto I = BottomMI; I != Begin; --I)
    UPTracker.recede(*I);

  UPTracker.recede(*Begin);

  assert(UPTracker.isValid() ||
         (dbgs() << "Tracked region ",
          printRegion(dbgs(), Begin, End, LIS), false));
  return UPTracker.getMaxPressureAndReset();
}

// returns max pressure for a tentative schedule
template <typename Range> GCNRegPressure
GCNIterativeScheduler::getSchedulePressure(const Region &R,
                                           Range &&Schedule) const {
  auto const BBEnd = R.Begin->getParent()->end();
  GCNUpwardRPTracker RPTracker(*LIS);
  if (R.End != BBEnd) {
    // R.End points to the boundary instruction but the
    // schedule doesn't include it
    RPTracker.reset(*R.End);
    RPTracker.recede(*R.End);
  } else {
    // R.End doesn't point to the boundary instruction
    RPTracker.reset(*std::prev(BBEnd));
  }
  for (auto I = Schedule.end(), B = Schedule.begin(); I != B;) {
    RPTracker.recede(*getMachineInstr(*--I));
  }
  return RPTracker.getMaxPressureAndReset();
}

void GCNIterativeScheduler::enterRegion(MachineBasicBlock *BB, // overridden
                                        MachineBasicBlock::iterator Begin,
                                        MachineBasicBlock::iterator End,
                                        unsigned NumRegionInstrs) {
  BaseClass::enterRegion(BB, Begin, End, NumRegionInstrs);
  if (NumRegionInstrs > 2) {
    Regions.push_back(
      new (Alloc.Allocate())
      Region { Begin, End, NumRegionInstrs,
               getRegionPressure(Begin, End), nullptr });
  }
}

void GCNIterativeScheduler::schedule() { // overridden
  // do nothing
  LLVM_DEBUG(printLivenessInfo(dbgs(), RegionBegin, RegionEnd, LIS);
             if (!Regions.empty() && Regions.back()->Begin == RegionBegin) {
               dbgs() << "Max RP: "
                      << print(Regions.back()->MaxPressure,
                               &MF.getSubtarget<GCNSubtarget>());
             } dbgs()
             << '\n';);
}

void GCNIterativeScheduler::finalizeSchedule() { // overridden
  if (Regions.empty())
    return;
  switch (Strategy) {
  case SCHEDULE_MINREGONLY: scheduleMinReg(); break;
  case SCHEDULE_MINREGFORCED: scheduleMinReg(true); break;
  case SCHEDULE_LEGACYMAXOCCUPANCY: scheduleLegacyMaxOccupancy(); break;
  case SCHEDULE_ILP: scheduleILP(false); break;
  }
}

// Detach schedule from SUnits and interleave it with debug values.
// Returned schedule becomes independent of DAG state.
std::vector<MachineInstr*>
GCNIterativeScheduler::detachSchedule(ScheduleRef Schedule) const {
  std::vector<MachineInstr*> Res;
  Res.reserve(Schedule.size() * 2);

  if (FirstDbgValue)
    Res.push_back(FirstDbgValue);

  const auto DbgB = DbgValues.begin(), DbgE = DbgValues.end();
  for (const auto *SU : Schedule) {
    Res.push_back(SU->getInstr());
    const auto &D = std::find_if(DbgB, DbgE, [SU](decltype(*DbgB) &P) {
      return P.second == SU->getInstr();
    });
    if (D != DbgE)
      Res.push_back(D->first);
  }
  return Res;
}

void GCNIterativeScheduler::setBestSchedule(Region &R,
                                            ScheduleRef Schedule,
                                            const GCNRegPressure &MaxRP) {
  R.BestSchedule.reset(
    new TentativeSchedule{ detachSchedule(Schedule), MaxRP });
}

void GCNIterativeScheduler::scheduleBest(Region &R) {
  assert(R.BestSchedule.get() && "No schedule specified");
  scheduleRegion(R, R.BestSchedule->Schedule, R.BestSchedule->MaxPressure);
  R.BestSchedule.reset();
}

// minimal required region scheduler, works for ranges of SUnits*,
// SUnits or MachineIntrs*
template <typename Range>
void GCNIterativeScheduler::scheduleRegion(Region &R, Range &&Schedule,
                                           const GCNRegPressure &MaxRP) {
  assert(RegionBegin == R.Begin && RegionEnd == R.End);
  assert(LIS != nullptr);
#ifndef NDEBUG
  const auto SchedMaxRP = getSchedulePressure(R, Schedule);
#endif
  auto BB = R.Begin->getParent();
  auto Top = R.Begin;
  for (const auto &I : Schedule) {
    auto MI = getMachineInstr(I);
    if (MI != &*Top) {
      BB->remove(MI);
      BB->insert(Top, MI);
      if (!MI->isDebugInstr())
        LIS->handleMove(*MI, true);
    }
    if (!MI->isDebugInstr()) {
      // Reset read - undef flags and update them later.
      for (auto &Op : MI->all_defs())
        Op.setIsUndef(false);

      RegisterOperands RegOpers;
      RegOpers.collect(*MI, *TRI, MRI, /*ShouldTrackLaneMasks*/true,
                                       /*IgnoreDead*/false);
      // Adjust liveness and add missing dead+read-undef flags.
      auto SlotIdx = LIS->getInstructionIndex(*MI).getRegSlot();
      RegOpers.adjustLaneLiveness(*LIS, MRI, SlotIdx, MI);
    }
    Top = std::next(MI->getIterator());
  }
  RegionBegin = getMachineInstr(Schedule.front());

  // Schedule consisting of MachineInstr* is considered 'detached'
  // and already interleaved with debug values
  if (!std::is_same_v<decltype(*Schedule.begin()), MachineInstr*>) {
    placeDebugValues();
    // Unfortunately placeDebugValues incorrectly modifies RegionEnd, restore
    // assert(R.End == RegionEnd);
    RegionEnd = R.End;
  }

  R.Begin = RegionBegin;
  R.MaxPressure = MaxRP;

#ifndef NDEBUG
  const auto RegionMaxRP = getRegionPressure(R);
  const auto &ST = MF.getSubtarget<GCNSubtarget>();
#endif
  assert(
      (SchedMaxRP == RegionMaxRP && (MaxRP.empty() || SchedMaxRP == MaxRP)) ||
      (dbgs() << "Max RP mismatch!!!\n"
                 "RP for schedule (calculated): "
              << print(SchedMaxRP, &ST)
              << "RP for schedule (reported): " << print(MaxRP, &ST)
              << "RP after scheduling: " << print(RegionMaxRP, &ST),
       false));
}

// Sort recorded regions by pressure - highest at the front
void GCNIterativeScheduler::sortRegionsByPressure(unsigned TargetOcc) {
  llvm::sort(Regions, [this, TargetOcc](const Region *R1, const Region *R2) {
    return R2->MaxPressure.less(MF, R1->MaxPressure, TargetOcc);
  });
}

///////////////////////////////////////////////////////////////////////////////
// Legacy MaxOccupancy Strategy

// Tries to increase occupancy applying minreg scheduler for a sequence of
// most demanding regions. Obtained schedules are saved as BestSchedule for a
// region.
// TargetOcc is the best achievable occupancy for a kernel.
// Returns better occupancy on success or current occupancy on fail.
// BestSchedules aren't deleted on fail.
unsigned GCNIterativeScheduler::tryMaximizeOccupancy(unsigned TargetOcc) {
  // TODO: assert Regions are sorted descending by pressure
  const auto &ST = MF.getSubtarget<GCNSubtarget>();
  const auto Occ = Regions.front()->MaxPressure.getOccupancy(ST);
  LLVM_DEBUG(dbgs() << "Trying to improve occupancy, target = " << TargetOcc
                    << ", current = " << Occ << '\n');

  auto NewOcc = TargetOcc;
  for (auto *R : Regions) {
    if (R->MaxPressure.getOccupancy(ST) >= NewOcc)
      break;

    LLVM_DEBUG(printRegion(dbgs(), R->Begin, R->End, LIS, 3);
               printLivenessInfo(dbgs(), R->Begin, R->End, LIS));

    BuildDAG DAG(*R, *this);
    const auto MinSchedule = makeMinRegSchedule(DAG.getTopRoots(), *this);
    const auto MaxRP = getSchedulePressure(*R, MinSchedule);
    LLVM_DEBUG(dbgs() << "Occupancy improvement attempt:\n";
               printSchedRP(dbgs(), R->MaxPressure, MaxRP));

    NewOcc = std::min(NewOcc, MaxRP.getOccupancy(ST));
    if (NewOcc <= Occ)
      break;

    setBestSchedule(*R, MinSchedule, MaxRP);
  }
  LLVM_DEBUG(dbgs() << "New occupancy = " << NewOcc
                    << ", prev occupancy = " << Occ << '\n');
  if (NewOcc > Occ) {
    SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
    MFI->increaseOccupancy(MF, NewOcc);
  }

  return std::max(NewOcc, Occ);
}

void GCNIterativeScheduler::scheduleLegacyMaxOccupancy(
  bool TryMaximizeOccupancy) {
  const auto &ST = MF.getSubtarget<GCNSubtarget>();
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  auto TgtOcc = MFI->getMinAllowedOccupancy();

  sortRegionsByPressure(TgtOcc);
  auto Occ = Regions.front()->MaxPressure.getOccupancy(ST);

  if (TryMaximizeOccupancy && Occ < TgtOcc)
    Occ = tryMaximizeOccupancy(TgtOcc);

  // This is really weird but for some magic scheduling regions twice
  // gives performance improvement
  const int NumPasses = Occ < TgtOcc ? 2 : 1;

  TgtOcc = std::min(Occ, TgtOcc);
  LLVM_DEBUG(dbgs() << "Scheduling using default scheduler, "
                       "target occupancy = "
                    << TgtOcc << '\n');
  GCNMaxOccupancySchedStrategy LStrgy(Context);
  unsigned FinalOccupancy = std::min(Occ, MFI->getOccupancy());

  for (int I = 0; I < NumPasses; ++I) {
    // running first pass with TargetOccupancy = 0 mimics previous scheduling
    // approach and is a performance magic
    LStrgy.setTargetOccupancy(I == 0 ? 0 : TgtOcc);
    for (auto *R : Regions) {
      OverrideLegacyStrategy Ovr(*R, LStrgy, *this);

      Ovr.schedule();
      const auto RP = getRegionPressure(*R);
      LLVM_DEBUG(printSchedRP(dbgs(), R->MaxPressure, RP));

      if (RP.getOccupancy(ST) < TgtOcc) {
        LLVM_DEBUG(dbgs() << "Didn't fit into target occupancy O" << TgtOcc);
        if (R->BestSchedule.get() &&
            R->BestSchedule->MaxPressure.getOccupancy(ST) >= TgtOcc) {
          LLVM_DEBUG(dbgs() << ", scheduling minimal register\n");
          scheduleBest(*R);
        } else {
          LLVM_DEBUG(dbgs() << ", restoring\n");
          Ovr.restoreOrder();
          assert(R->MaxPressure.getOccupancy(ST) >= TgtOcc);
        }
      }
      FinalOccupancy = std::min(FinalOccupancy, RP.getOccupancy(ST));
    }
  }
  MFI->limitOccupancy(FinalOccupancy);
}

///////////////////////////////////////////////////////////////////////////////
// Minimal Register Strategy

void GCNIterativeScheduler::scheduleMinReg(bool force) {
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const auto TgtOcc = MFI->getOccupancy();
  sortRegionsByPressure(TgtOcc);

  auto MaxPressure = Regions.front()->MaxPressure;
  for (auto *R : Regions) {
    if (!force && R->MaxPressure.less(MF, MaxPressure, TgtOcc))
      break;

    BuildDAG DAG(*R, *this);
    const auto MinSchedule = makeMinRegSchedule(DAG.getTopRoots(), *this);

    const auto RP = getSchedulePressure(*R, MinSchedule);
    LLVM_DEBUG(if (R->MaxPressure.less(MF, RP, TgtOcc)) {
      dbgs() << "\nWarning: Pressure becomes worse after minreg!";
      printSchedRP(dbgs(), R->MaxPressure, RP);
    });

    if (!force && MaxPressure.less(MF, RP, TgtOcc))
      break;

    scheduleRegion(*R, MinSchedule, RP);
    LLVM_DEBUG(printSchedResult(dbgs(), R, RP));

    MaxPressure = RP;
  }
}

///////////////////////////////////////////////////////////////////////////////
// ILP scheduler port

void GCNIterativeScheduler::scheduleILP(
  bool TryMaximizeOccupancy) {
  const auto &ST = MF.getSubtarget<GCNSubtarget>();
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  auto TgtOcc = MFI->getMinAllowedOccupancy();

  sortRegionsByPressure(TgtOcc);
  auto Occ = Regions.front()->MaxPressure.getOccupancy(ST);

  if (TryMaximizeOccupancy && Occ < TgtOcc)
    Occ = tryMaximizeOccupancy(TgtOcc);

  TgtOcc = std::min(Occ, TgtOcc);
  LLVM_DEBUG(dbgs() << "Scheduling using default scheduler, "
                       "target occupancy = "
                    << TgtOcc << '\n');

  unsigned FinalOccupancy = std::min(Occ, MFI->getOccupancy());
  for (auto *R : Regions) {
    BuildDAG DAG(*R, *this);
    const auto ILPSchedule = makeGCNILPScheduler(DAG.getBottomRoots(), *this);

    const auto RP = getSchedulePressure(*R, ILPSchedule);
    LLVM_DEBUG(printSchedRP(dbgs(), R->MaxPressure, RP));

    if (RP.getOccupancy(ST) < TgtOcc) {
      LLVM_DEBUG(dbgs() << "Didn't fit into target occupancy O" << TgtOcc);
      if (R->BestSchedule.get() &&
        R->BestSchedule->MaxPressure.getOccupancy(ST) >= TgtOcc) {
        LLVM_DEBUG(dbgs() << ", scheduling minimal register\n");
        scheduleBest(*R);
      }
    } else {
      scheduleRegion(*R, ILPSchedule, RP);
      LLVM_DEBUG(printSchedResult(dbgs(), R, RP));
      FinalOccupancy = std::min(FinalOccupancy, RP.getOccupancy(ST));
    }
  }
  MFI->limitOccupancy(FinalOccupancy);
}
