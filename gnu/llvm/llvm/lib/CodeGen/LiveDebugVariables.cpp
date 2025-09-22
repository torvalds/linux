//===- LiveDebugVariables.cpp - Tracking debug info variables -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LiveDebugVariables analysis.
//
// Remove all DBG_VALUE instructions referencing virtual registers and replace
// them with a data structure tracking where live user variables are kept - in a
// virtual register or in a stack slot.
//
// Allow the data structure to be updated during register allocation when values
// are moved between registers and stack slots. Finally emit new DBG_VALUE
// instructions after register allocation is complete.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveDebugVariables.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "livedebugvars"

static cl::opt<bool>
EnableLDV("live-debug-variables", cl::init(true),
          cl::desc("Enable the live debug variables pass"), cl::Hidden);

STATISTIC(NumInsertedDebugValues, "Number of DBG_VALUEs inserted");
STATISTIC(NumInsertedDebugLabels, "Number of DBG_LABELs inserted");

char LiveDebugVariables::ID = 0;

INITIALIZE_PASS_BEGIN(LiveDebugVariables, DEBUG_TYPE,
                "Debug Variable Analysis", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_END(LiveDebugVariables, DEBUG_TYPE,
                "Debug Variable Analysis", false, false)

void LiveDebugVariables::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addRequiredTransitive<LiveIntervalsWrapperPass>();
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

LiveDebugVariables::LiveDebugVariables() : MachineFunctionPass(ID) {
  initializeLiveDebugVariablesPass(*PassRegistry::getPassRegistry());
}

enum : unsigned { UndefLocNo = ~0U };

namespace {
/// Describes a debug variable value by location number and expression along
/// with some flags about the original usage of the location.
class DbgVariableValue {
public:
  DbgVariableValue(ArrayRef<unsigned> NewLocs, bool WasIndirect, bool WasList,
                   const DIExpression &Expr)
      : WasIndirect(WasIndirect), WasList(WasList), Expression(&Expr) {
    assert(!(WasIndirect && WasList) &&
           "DBG_VALUE_LISTs should not be indirect.");
    SmallVector<unsigned> LocNoVec;
    for (unsigned LocNo : NewLocs) {
      auto It = find(LocNoVec, LocNo);
      if (It == LocNoVec.end())
        LocNoVec.push_back(LocNo);
      else {
        // Loc duplicates an element in LocNos; replace references to Op
        // with references to the duplicating element.
        unsigned OpIdx = LocNoVec.size();
        unsigned DuplicatingIdx = std::distance(LocNoVec.begin(), It);
        Expression =
            DIExpression::replaceArg(Expression, OpIdx, DuplicatingIdx);
      }
    }
    // FIXME: Debug values referencing 64+ unique machine locations are rare and
    // currently unsupported for performance reasons. If we can verify that
    // performance is acceptable for such debug values, we can increase the
    // bit-width of LocNoCount to 14 to enable up to 16384 unique machine
    // locations. We will also need to verify that this does not cause issues
    // with LiveDebugVariables' use of IntervalMap.
    if (LocNoVec.size() < 64) {
      LocNoCount = LocNoVec.size();
      if (LocNoCount > 0) {
        LocNos = std::make_unique<unsigned[]>(LocNoCount);
        std::copy(LocNoVec.begin(), LocNoVec.end(), loc_nos_begin());
      }
    } else {
      LLVM_DEBUG(dbgs() << "Found debug value with 64+ unique machine "
                           "locations, dropping...\n");
      LocNoCount = 1;
      // Turn this into an undef debug value list; right now, the simplest form
      // of this is an expression with one arg, and an undef debug operand.
      Expression =
          DIExpression::get(Expr.getContext(), {dwarf::DW_OP_LLVM_arg, 0});
      if (auto FragmentInfoOpt = Expr.getFragmentInfo())
        Expression = *DIExpression::createFragmentExpression(
            Expression, FragmentInfoOpt->OffsetInBits,
            FragmentInfoOpt->SizeInBits);
      LocNos = std::make_unique<unsigned[]>(LocNoCount);
      LocNos[0] = UndefLocNo;
    }
  }

  DbgVariableValue() : LocNoCount(0), WasIndirect(false), WasList(false) {}
  DbgVariableValue(const DbgVariableValue &Other)
      : LocNoCount(Other.LocNoCount), WasIndirect(Other.getWasIndirect()),
        WasList(Other.getWasList()), Expression(Other.getExpression()) {
    if (Other.getLocNoCount()) {
      LocNos.reset(new unsigned[Other.getLocNoCount()]);
      std::copy(Other.loc_nos_begin(), Other.loc_nos_end(), loc_nos_begin());
    }
  }

  DbgVariableValue &operator=(const DbgVariableValue &Other) {
    if (this == &Other)
      return *this;
    if (Other.getLocNoCount()) {
      LocNos.reset(new unsigned[Other.getLocNoCount()]);
      std::copy(Other.loc_nos_begin(), Other.loc_nos_end(), loc_nos_begin());
    } else {
      LocNos.release();
    }
    LocNoCount = Other.getLocNoCount();
    WasIndirect = Other.getWasIndirect();
    WasList = Other.getWasList();
    Expression = Other.getExpression();
    return *this;
  }

  const DIExpression *getExpression() const { return Expression; }
  uint8_t getLocNoCount() const { return LocNoCount; }
  bool containsLocNo(unsigned LocNo) const {
    return is_contained(loc_nos(), LocNo);
  }
  bool getWasIndirect() const { return WasIndirect; }
  bool getWasList() const { return WasList; }
  bool isUndef() const { return LocNoCount == 0 || containsLocNo(UndefLocNo); }

  DbgVariableValue decrementLocNosAfterPivot(unsigned Pivot) const {
    SmallVector<unsigned, 4> NewLocNos;
    for (unsigned LocNo : loc_nos())
      NewLocNos.push_back(LocNo != UndefLocNo && LocNo > Pivot ? LocNo - 1
                                                               : LocNo);
    return DbgVariableValue(NewLocNos, WasIndirect, WasList, *Expression);
  }

  DbgVariableValue remapLocNos(ArrayRef<unsigned> LocNoMap) const {
    SmallVector<unsigned> NewLocNos;
    for (unsigned LocNo : loc_nos())
      // Undef values don't exist in locations (and thus not in LocNoMap
      // either) so skip over them. See getLocationNo().
      NewLocNos.push_back(LocNo == UndefLocNo ? UndefLocNo : LocNoMap[LocNo]);
    return DbgVariableValue(NewLocNos, WasIndirect, WasList, *Expression);
  }

  DbgVariableValue changeLocNo(unsigned OldLocNo, unsigned NewLocNo) const {
    SmallVector<unsigned> NewLocNos;
    NewLocNos.assign(loc_nos_begin(), loc_nos_end());
    auto OldLocIt = find(NewLocNos, OldLocNo);
    assert(OldLocIt != NewLocNos.end() && "Old location must be present.");
    *OldLocIt = NewLocNo;
    return DbgVariableValue(NewLocNos, WasIndirect, WasList, *Expression);
  }

  bool hasLocNoGreaterThan(unsigned LocNo) const {
    return any_of(loc_nos(),
                  [LocNo](unsigned ThisLocNo) { return ThisLocNo > LocNo; });
  }

  void printLocNos(llvm::raw_ostream &OS) const {
    for (const unsigned &Loc : loc_nos())
      OS << (&Loc == loc_nos_begin() ? " " : ", ") << Loc;
  }

  friend inline bool operator==(const DbgVariableValue &LHS,
                                const DbgVariableValue &RHS) {
    if (std::tie(LHS.LocNoCount, LHS.WasIndirect, LHS.WasList,
                 LHS.Expression) !=
        std::tie(RHS.LocNoCount, RHS.WasIndirect, RHS.WasList, RHS.Expression))
      return false;
    return std::equal(LHS.loc_nos_begin(), LHS.loc_nos_end(),
                      RHS.loc_nos_begin());
  }

  friend inline bool operator!=(const DbgVariableValue &LHS,
                                const DbgVariableValue &RHS) {
    return !(LHS == RHS);
  }

  unsigned *loc_nos_begin() { return LocNos.get(); }
  const unsigned *loc_nos_begin() const { return LocNos.get(); }
  unsigned *loc_nos_end() { return LocNos.get() + LocNoCount; }
  const unsigned *loc_nos_end() const { return LocNos.get() + LocNoCount; }
  ArrayRef<unsigned> loc_nos() const {
    return ArrayRef<unsigned>(LocNos.get(), LocNoCount);
  }

private:
  // IntervalMap requires the value object to be very small, to the extent
  // that we do not have enough room for an std::vector. Using a C-style array
  // (with a unique_ptr wrapper for convenience) allows us to optimize for this
  // specific case by packing the array size into only 6 bits (it is highly
  // unlikely that any debug value will need 64+ locations).
  std::unique_ptr<unsigned[]> LocNos;
  uint8_t LocNoCount : 6;
  bool WasIndirect : 1;
  bool WasList : 1;
  const DIExpression *Expression = nullptr;
};
} // namespace

/// Map of where a user value is live to that value.
using LocMap = IntervalMap<SlotIndex, DbgVariableValue, 4>;

/// Map of stack slot offsets for spilled locations.
/// Non-spilled locations are not added to the map.
using SpillOffsetMap = DenseMap<unsigned, unsigned>;

/// Cache to save the location where it can be used as the starting
/// position as input for calling MachineBasicBlock::SkipPHIsLabelsAndDebug.
/// This is to prevent MachineBasicBlock::SkipPHIsLabelsAndDebug from
/// repeatedly searching the same set of PHIs/Labels/Debug instructions
/// if it is called many times for the same block.
using BlockSkipInstsMap =
    DenseMap<MachineBasicBlock *, MachineBasicBlock::iterator>;

namespace {

class LDVImpl;

/// A user value is a part of a debug info user variable.
///
/// A DBG_VALUE instruction notes that (a sub-register of) a virtual register
/// holds part of a user variable. The part is identified by a byte offset.
///
/// UserValues are grouped into equivalence classes for easier searching. Two
/// user values are related if they are held by the same virtual register. The
/// equivalence class is the transitive closure of that relation.
class UserValue {
  const DILocalVariable *Variable; ///< The debug info variable we are part of.
  /// The part of the variable we describe.
  const std::optional<DIExpression::FragmentInfo> Fragment;
  DebugLoc dl;            ///< The debug location for the variable. This is
                          ///< used by dwarf writer to find lexical scope.
  UserValue *leader;      ///< Equivalence class leader.
  UserValue *next = nullptr; ///< Next value in equivalence class, or null.

  /// Numbered locations referenced by locmap.
  SmallVector<MachineOperand, 4> locations;

  /// Map of slot indices where this value is live.
  LocMap locInts;

  /// Set of interval start indexes that have been trimmed to the
  /// lexical scope.
  SmallSet<SlotIndex, 2> trimmedDefs;

  /// Insert a DBG_VALUE into MBB at Idx for DbgValue.
  void insertDebugValue(MachineBasicBlock *MBB, SlotIndex StartIdx,
                        SlotIndex StopIdx, DbgVariableValue DbgValue,
                        ArrayRef<bool> LocSpills,
                        ArrayRef<unsigned> SpillOffsets, LiveIntervals &LIS,
                        const TargetInstrInfo &TII,
                        const TargetRegisterInfo &TRI,
                        BlockSkipInstsMap &BBSkipInstsMap);

  /// Replace OldLocNo ranges with NewRegs ranges where NewRegs
  /// is live. Returns true if any changes were made.
  bool splitLocation(unsigned OldLocNo, ArrayRef<Register> NewRegs,
                     LiveIntervals &LIS);

public:
  /// Create a new UserValue.
  UserValue(const DILocalVariable *var,
            std::optional<DIExpression::FragmentInfo> Fragment, DebugLoc L,
            LocMap::Allocator &alloc)
      : Variable(var), Fragment(Fragment), dl(std::move(L)), leader(this),
        locInts(alloc) {}

  /// Get the leader of this value's equivalence class.
  UserValue *getLeader() {
    UserValue *l = leader;
    while (l != l->leader)
      l = l->leader;
    return leader = l;
  }

  /// Return the next UserValue in the equivalence class.
  UserValue *getNext() const { return next; }

  /// Merge equivalence classes.
  static UserValue *merge(UserValue *L1, UserValue *L2) {
    L2 = L2->getLeader();
    if (!L1)
      return L2;
    L1 = L1->getLeader();
    if (L1 == L2)
      return L1;
    // Splice L2 before L1's members.
    UserValue *End = L2;
    while (End->next) {
      End->leader = L1;
      End = End->next;
    }
    End->leader = L1;
    End->next = L1->next;
    L1->next = L2;
    return L1;
  }

  /// Return the location number that matches Loc.
  ///
  /// For undef values we always return location number UndefLocNo without
  /// inserting anything in locations. Since locations is a vector and the
  /// location number is the position in the vector and UndefLocNo is ~0,
  /// we would need a very big vector to put the value at the right position.
  unsigned getLocationNo(const MachineOperand &LocMO) {
    if (LocMO.isReg()) {
      if (LocMO.getReg() == 0)
        return UndefLocNo;
      // For register locations we dont care about use/def and other flags.
      for (unsigned i = 0, e = locations.size(); i != e; ++i)
        if (locations[i].isReg() &&
            locations[i].getReg() == LocMO.getReg() &&
            locations[i].getSubReg() == LocMO.getSubReg())
          return i;
    } else
      for (unsigned i = 0, e = locations.size(); i != e; ++i)
        if (LocMO.isIdenticalTo(locations[i]))
          return i;
    locations.push_back(LocMO);
    // We are storing a MachineOperand outside a MachineInstr.
    locations.back().clearParent();
    // Don't store def operands.
    if (locations.back().isReg()) {
      if (locations.back().isDef())
        locations.back().setIsDead(false);
      locations.back().setIsUse();
    }
    return locations.size() - 1;
  }

  /// Remove (recycle) a location number. If \p LocNo still is used by the
  /// locInts nothing is done.
  void removeLocationIfUnused(unsigned LocNo) {
    // Bail out if LocNo still is used.
    for (LocMap::const_iterator I = locInts.begin(); I.valid(); ++I) {
      const DbgVariableValue &DbgValue = I.value();
      if (DbgValue.containsLocNo(LocNo))
        return;
    }
    // Remove the entry in the locations vector, and adjust all references to
    // location numbers above the removed entry.
    locations.erase(locations.begin() + LocNo);
    for (LocMap::iterator I = locInts.begin(); I.valid(); ++I) {
      const DbgVariableValue &DbgValue = I.value();
      if (DbgValue.hasLocNoGreaterThan(LocNo))
        I.setValueUnchecked(DbgValue.decrementLocNosAfterPivot(LocNo));
    }
  }

  /// Ensure that all virtual register locations are mapped.
  void mapVirtRegs(LDVImpl *LDV);

  /// Add a definition point to this user value.
  void addDef(SlotIndex Idx, ArrayRef<MachineOperand> LocMOs, bool IsIndirect,
              bool IsList, const DIExpression &Expr) {
    SmallVector<unsigned> Locs;
    for (const MachineOperand &Op : LocMOs)
      Locs.push_back(getLocationNo(Op));
    DbgVariableValue DbgValue(Locs, IsIndirect, IsList, Expr);
    // Add a singular (Idx,Idx) -> value mapping.
    LocMap::iterator I = locInts.find(Idx);
    if (!I.valid() || I.start() != Idx)
      I.insert(Idx, Idx.getNextSlot(), std::move(DbgValue));
    else
      // A later DBG_VALUE at the same SlotIndex overrides the old location.
      I.setValue(std::move(DbgValue));
  }

  /// Extend the current definition as far as possible down.
  ///
  /// Stop when meeting an existing def or when leaving the live
  /// range of VNI. End points where VNI is no longer live are added to Kills.
  ///
  /// We only propagate DBG_VALUES locally here. LiveDebugValues performs a
  /// data-flow analysis to propagate them beyond basic block boundaries.
  ///
  /// \param Idx Starting point for the definition.
  /// \param DbgValue value to propagate.
  /// \param LiveIntervalInfo For each location number key in this map,
  /// restricts liveness to where the LiveRange has the value equal to the\
  /// VNInfo.
  /// \param [out] Kills Append end points of VNI's live range to Kills.
  /// \param LIS Live intervals analysis.
  void
  extendDef(SlotIndex Idx, DbgVariableValue DbgValue,
            SmallDenseMap<unsigned, std::pair<LiveRange *, const VNInfo *>>
                &LiveIntervalInfo,
            std::optional<std::pair<SlotIndex, SmallVector<unsigned>>> &Kills,
            LiveIntervals &LIS);

  /// The value in LI may be copies to other registers. Determine if
  /// any of the copies are available at the kill points, and add defs if
  /// possible.
  ///
  /// \param DbgValue Location number of LI->reg, and DIExpression.
  /// \param LocIntervals Scan for copies of the value for each location in the
  /// corresponding LiveInterval->reg.
  /// \param KilledAt The point where the range of DbgValue could be extended.
  /// \param [in,out] NewDefs Append (Idx, DbgValue) of inserted defs here.
  void addDefsFromCopies(
      DbgVariableValue DbgValue,
      SmallVectorImpl<std::pair<unsigned, LiveInterval *>> &LocIntervals,
      SlotIndex KilledAt,
      SmallVectorImpl<std::pair<SlotIndex, DbgVariableValue>> &NewDefs,
      MachineRegisterInfo &MRI, LiveIntervals &LIS);

  /// Compute the live intervals of all locations after collecting all their
  /// def points.
  void computeIntervals(MachineRegisterInfo &MRI, const TargetRegisterInfo &TRI,
                        LiveIntervals &LIS, LexicalScopes &LS);

  /// Replace OldReg ranges with NewRegs ranges where NewRegs is
  /// live. Returns true if any changes were made.
  bool splitRegister(Register OldReg, ArrayRef<Register> NewRegs,
                     LiveIntervals &LIS);

  /// Rewrite virtual register locations according to the provided virtual
  /// register map. Record the stack slot offsets for the locations that
  /// were spilled.
  void rewriteLocations(VirtRegMap &VRM, const MachineFunction &MF,
                        const TargetInstrInfo &TII,
                        const TargetRegisterInfo &TRI,
                        SpillOffsetMap &SpillOffsets);

  /// Recreate DBG_VALUE instruction from data structures.
  void emitDebugValues(VirtRegMap *VRM, LiveIntervals &LIS,
                       const TargetInstrInfo &TII,
                       const TargetRegisterInfo &TRI,
                       const SpillOffsetMap &SpillOffsets,
                       BlockSkipInstsMap &BBSkipInstsMap);

  /// Return DebugLoc of this UserValue.
  const DebugLoc &getDebugLoc() { return dl; }

  void print(raw_ostream &, const TargetRegisterInfo *);
};

/// A user label is a part of a debug info user label.
class UserLabel {
  const DILabel *Label; ///< The debug info label we are part of.
  DebugLoc dl;          ///< The debug location for the label. This is
                        ///< used by dwarf writer to find lexical scope.
  SlotIndex loc;        ///< Slot used by the debug label.

  /// Insert a DBG_LABEL into MBB at Idx.
  void insertDebugLabel(MachineBasicBlock *MBB, SlotIndex Idx,
                        LiveIntervals &LIS, const TargetInstrInfo &TII,
                        BlockSkipInstsMap &BBSkipInstsMap);

public:
  /// Create a new UserLabel.
  UserLabel(const DILabel *label, DebugLoc L, SlotIndex Idx)
      : Label(label), dl(std::move(L)), loc(Idx) {}

  /// Does this UserLabel match the parameters?
  bool matches(const DILabel *L, const DILocation *IA,
             const SlotIndex Index) const {
    return Label == L && dl->getInlinedAt() == IA && loc == Index;
  }

  /// Recreate DBG_LABEL instruction from data structures.
  void emitDebugLabel(LiveIntervals &LIS, const TargetInstrInfo &TII,
                      BlockSkipInstsMap &BBSkipInstsMap);

  /// Return DebugLoc of this UserLabel.
  const DebugLoc &getDebugLoc() { return dl; }

  void print(raw_ostream &, const TargetRegisterInfo *);
};

/// Implementation of the LiveDebugVariables pass.
class LDVImpl {
  LiveDebugVariables &pass;
  LocMap::Allocator allocator;
  MachineFunction *MF = nullptr;
  LiveIntervals *LIS;
  const TargetRegisterInfo *TRI;

  /// Position and VReg of a PHI instruction during register allocation.
  struct PHIValPos {
    SlotIndex SI;    /// Slot where this PHI occurs.
    Register Reg;    /// VReg this PHI occurs in.
    unsigned SubReg; /// Qualifiying subregister for Reg.
  };

  /// Map from debug instruction number to PHI position during allocation.
  std::map<unsigned, PHIValPos> PHIValToPos;
  /// Index of, for each VReg, which debug instruction numbers and corresponding
  /// PHIs are sensitive to splitting. Each VReg may have multiple PHI defs,
  /// at different positions.
  DenseMap<Register, std::vector<unsigned>> RegToPHIIdx;

  /// Record for any debug instructions unlinked from their blocks during
  /// regalloc. Stores the instr and it's location, so that they can be
  /// re-inserted after regalloc is over.
  struct InstrPos {
    MachineInstr *MI;       ///< Debug instruction, unlinked from it's block.
    SlotIndex Idx;          ///< Slot position where MI should be re-inserted.
    MachineBasicBlock *MBB; ///< Block that MI was in.
  };

  /// Collection of stored debug instructions, preserved until after regalloc.
  SmallVector<InstrPos, 32> StashedDebugInstrs;

  /// Whether emitDebugValues is called.
  bool EmitDone = false;

  /// Whether the machine function is modified during the pass.
  bool ModifiedMF = false;

  /// All allocated UserValue instances.
  SmallVector<std::unique_ptr<UserValue>, 8> userValues;

  /// All allocated UserLabel instances.
  SmallVector<std::unique_ptr<UserLabel>, 2> userLabels;

  /// Map virtual register to eq class leader.
  using VRMap = DenseMap<unsigned, UserValue *>;
  VRMap virtRegToEqClass;

  /// Map to find existing UserValue instances.
  using UVMap = DenseMap<DebugVariable, UserValue *>;
  UVMap userVarMap;

  /// Find or create a UserValue.
  UserValue *getUserValue(const DILocalVariable *Var,
                          std::optional<DIExpression::FragmentInfo> Fragment,
                          const DebugLoc &DL);

  /// Find the EC leader for VirtReg or null.
  UserValue *lookupVirtReg(Register VirtReg);

  /// Add DBG_VALUE instruction to our maps.
  ///
  /// \param MI DBG_VALUE instruction
  /// \param Idx Last valid SLotIndex before instruction.
  ///
  /// \returns True if the DBG_VALUE instruction should be deleted.
  bool handleDebugValue(MachineInstr &MI, SlotIndex Idx);

  /// Track variable location debug instructions while using the instruction
  /// referencing implementation. Such debug instructions do not need to be
  /// updated during regalloc because they identify instructions rather than
  /// register locations. However, they needs to be removed from the
  /// MachineFunction during regalloc, then re-inserted later, to avoid
  /// disrupting the allocator.
  ///
  /// \param MI Any DBG_VALUE / DBG_INSTR_REF / DBG_PHI instruction
  /// \param Idx Last valid SlotIndex before instruction
  ///
  /// \returns Iterator to continue processing from after unlinking.
  MachineBasicBlock::iterator handleDebugInstr(MachineInstr &MI, SlotIndex Idx);

  /// Add DBG_LABEL instruction to UserLabel.
  ///
  /// \param MI DBG_LABEL instruction
  /// \param Idx Last valid SlotIndex before instruction.
  ///
  /// \returns True if the DBG_LABEL instruction should be deleted.
  bool handleDebugLabel(MachineInstr &MI, SlotIndex Idx);

  /// Collect and erase all DBG_VALUE instructions, adding a UserValue def
  /// for each instruction.
  ///
  /// \param mf MachineFunction to be scanned.
  /// \param InstrRef Whether to operate in instruction referencing mode. If
  ///        true, most of LiveDebugVariables doesn't run.
  ///
  /// \returns True if any debug values were found.
  bool collectDebugValues(MachineFunction &mf, bool InstrRef);

  /// Compute the live intervals of all user values after collecting all
  /// their def points.
  void computeIntervals();

public:
  LDVImpl(LiveDebugVariables *ps) : pass(*ps) {}

  bool runOnMachineFunction(MachineFunction &mf, bool InstrRef);

  /// Release all memory.
  void clear() {
    MF = nullptr;
    PHIValToPos.clear();
    RegToPHIIdx.clear();
    StashedDebugInstrs.clear();
    userValues.clear();
    userLabels.clear();
    virtRegToEqClass.clear();
    userVarMap.clear();
    // Make sure we call emitDebugValues if the machine function was modified.
    assert((!ModifiedMF || EmitDone) &&
           "Dbg values are not emitted in LDV");
    EmitDone = false;
    ModifiedMF = false;
  }

  /// Map virtual register to an equivalence class.
  void mapVirtReg(Register VirtReg, UserValue *EC);

  /// Replace any PHI referring to OldReg with its corresponding NewReg, if
  /// present.
  void splitPHIRegister(Register OldReg, ArrayRef<Register> NewRegs);

  /// Replace all references to OldReg with NewRegs.
  void splitRegister(Register OldReg, ArrayRef<Register> NewRegs);

  /// Recreate DBG_VALUE instruction from data structures.
  void emitDebugValues(VirtRegMap *VRM);

  void print(raw_ostream&);
};

} // end anonymous namespace

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
static void printDebugLoc(const DebugLoc &DL, raw_ostream &CommentOS,
                          const LLVMContext &Ctx) {
  if (!DL)
    return;

  auto *Scope = cast<DIScope>(DL.getScope());
  // Omit the directory, because it's likely to be long and uninteresting.
  CommentOS << Scope->getFilename();
  CommentOS << ':' << DL.getLine();
  if (DL.getCol() != 0)
    CommentOS << ':' << DL.getCol();

  DebugLoc InlinedAtDL = DL.getInlinedAt();
  if (!InlinedAtDL)
    return;

  CommentOS << " @[ ";
  printDebugLoc(InlinedAtDL, CommentOS, Ctx);
  CommentOS << " ]";
}

static void printExtendedName(raw_ostream &OS, const DINode *Node,
                              const DILocation *DL) {
  const LLVMContext &Ctx = Node->getContext();
  StringRef Res;
  unsigned Line = 0;
  if (const auto *V = dyn_cast<const DILocalVariable>(Node)) {
    Res = V->getName();
    Line = V->getLine();
  } else if (const auto *L = dyn_cast<const DILabel>(Node)) {
    Res = L->getName();
    Line = L->getLine();
  }

  if (!Res.empty())
    OS << Res << "," << Line;
  auto *InlinedAt = DL ? DL->getInlinedAt() : nullptr;
  if (InlinedAt) {
    if (DebugLoc InlinedAtDL = InlinedAt) {
      OS << " @[";
      printDebugLoc(InlinedAtDL, OS, Ctx);
      OS << "]";
    }
  }
}

void UserValue::print(raw_ostream &OS, const TargetRegisterInfo *TRI) {
  OS << "!\"";
  printExtendedName(OS, Variable, dl);

  OS << "\"\t";
  for (LocMap::const_iterator I = locInts.begin(); I.valid(); ++I) {
    OS << " [" << I.start() << ';' << I.stop() << "):";
    if (I.value().isUndef())
      OS << " undef";
    else {
      I.value().printLocNos(OS);
      if (I.value().getWasIndirect())
        OS << " ind";
      else if (I.value().getWasList())
        OS << " list";
    }
  }
  for (unsigned i = 0, e = locations.size(); i != e; ++i) {
    OS << " Loc" << i << '=';
    locations[i].print(OS, TRI);
  }
  OS << '\n';
}

void UserLabel::print(raw_ostream &OS, const TargetRegisterInfo *TRI) {
  OS << "!\"";
  printExtendedName(OS, Label, dl);

  OS << "\"\t";
  OS << loc;
  OS << '\n';
}

void LDVImpl::print(raw_ostream &OS) {
  OS << "********** DEBUG VARIABLES **********\n";
  for (auto &userValue : userValues)
    userValue->print(OS, TRI);
  OS << "********** DEBUG LABELS **********\n";
  for (auto &userLabel : userLabels)
    userLabel->print(OS, TRI);
}
#endif

void UserValue::mapVirtRegs(LDVImpl *LDV) {
  for (const MachineOperand &MO : locations)
    if (MO.isReg() && MO.getReg().isVirtual())
      LDV->mapVirtReg(MO.getReg(), this);
}

UserValue *
LDVImpl::getUserValue(const DILocalVariable *Var,
                      std::optional<DIExpression::FragmentInfo> Fragment,
                      const DebugLoc &DL) {
  // FIXME: Handle partially overlapping fragments. See
  // https://reviews.llvm.org/D70121#1849741.
  DebugVariable ID(Var, Fragment, DL->getInlinedAt());
  UserValue *&UV = userVarMap[ID];
  if (!UV) {
    userValues.push_back(
        std::make_unique<UserValue>(Var, Fragment, DL, allocator));
    UV = userValues.back().get();
  }
  return UV;
}

void LDVImpl::mapVirtReg(Register VirtReg, UserValue *EC) {
  assert(VirtReg.isVirtual() && "Only map VirtRegs");
  UserValue *&Leader = virtRegToEqClass[VirtReg];
  Leader = UserValue::merge(Leader, EC);
}

UserValue *LDVImpl::lookupVirtReg(Register VirtReg) {
  if (UserValue *UV = virtRegToEqClass.lookup(VirtReg))
    return UV->getLeader();
  return nullptr;
}

bool LDVImpl::handleDebugValue(MachineInstr &MI, SlotIndex Idx) {
  // DBG_VALUE loc, offset, variable, expr
  // DBG_VALUE_LIST variable, expr, locs...
  if (!MI.isDebugValue()) {
    LLVM_DEBUG(dbgs() << "Can't handle non-DBG_VALUE*: " << MI);
    return false;
  }
  if (!MI.getDebugVariableOp().isMetadata()) {
    LLVM_DEBUG(dbgs() << "Can't handle DBG_VALUE* with invalid variable: "
                      << MI);
    return false;
  }
  if (MI.isNonListDebugValue() &&
      (MI.getNumOperands() != 4 ||
       !(MI.getDebugOffset().isImm() || MI.getDebugOffset().isReg()))) {
    LLVM_DEBUG(dbgs() << "Can't handle malformed DBG_VALUE: " << MI);
    return false;
  }

  // Detect invalid DBG_VALUE instructions, with a debug-use of a virtual
  // register that hasn't been defined yet. If we do not remove those here, then
  // the re-insertion of the DBG_VALUE instruction after register allocation
  // will be incorrect.
  bool Discard = false;
  for (const MachineOperand &Op : MI.debug_operands()) {
    if (Op.isReg() && Op.getReg().isVirtual()) {
      const Register Reg = Op.getReg();
      if (!LIS->hasInterval(Reg)) {
        // The DBG_VALUE is described by a virtual register that does not have a
        // live interval. Discard the DBG_VALUE.
        Discard = true;
        LLVM_DEBUG(dbgs() << "Discarding debug info (no LIS interval): " << Idx
                          << " " << MI);
      } else {
        // The DBG_VALUE is only valid if either Reg is live out from Idx, or
        // Reg is defined dead at Idx (where Idx is the slot index for the
        // instruction preceding the DBG_VALUE).
        const LiveInterval &LI = LIS->getInterval(Reg);
        LiveQueryResult LRQ = LI.Query(Idx);
        if (!LRQ.valueOutOrDead()) {
          // We have found a DBG_VALUE with the value in a virtual register that
          // is not live. Discard the DBG_VALUE.
          Discard = true;
          LLVM_DEBUG(dbgs() << "Discarding debug info (reg not live): " << Idx
                            << " " << MI);
        }
      }
    }
  }

  // Get or create the UserValue for (variable,offset) here.
  bool IsIndirect = MI.isDebugOffsetImm();
  if (IsIndirect)
    assert(MI.getDebugOffset().getImm() == 0 &&
           "DBG_VALUE with nonzero offset");
  bool IsList = MI.isDebugValueList();
  const DILocalVariable *Var = MI.getDebugVariable();
  const DIExpression *Expr = MI.getDebugExpression();
  UserValue *UV = getUserValue(Var, Expr->getFragmentInfo(), MI.getDebugLoc());
  if (!Discard)
    UV->addDef(Idx,
               ArrayRef<MachineOperand>(MI.debug_operands().begin(),
                                        MI.debug_operands().end()),
               IsIndirect, IsList, *Expr);
  else {
    MachineOperand MO = MachineOperand::CreateReg(0U, false);
    MO.setIsDebug();
    // We should still pass a list the same size as MI.debug_operands() even if
    // all MOs are undef, so that DbgVariableValue can correctly adjust the
    // expression while removing the duplicated undefs.
    SmallVector<MachineOperand, 4> UndefMOs(MI.getNumDebugOperands(), MO);
    UV->addDef(Idx, UndefMOs, false, IsList, *Expr);
  }
  return true;
}

MachineBasicBlock::iterator LDVImpl::handleDebugInstr(MachineInstr &MI,
                                                      SlotIndex Idx) {
  assert(MI.isDebugValueLike() || MI.isDebugPHI());

  // In instruction referencing mode, there should be no DBG_VALUE instructions
  // that refer to virtual registers. They might still refer to constants.
  if (MI.isDebugValueLike())
    assert(none_of(MI.debug_operands(),
                   [](const MachineOperand &MO) {
                     return MO.isReg() && MO.getReg().isVirtual();
                   }) &&
           "MIs should not refer to Virtual Registers in InstrRef mode.");

  // Unlink the instruction, store it in the debug instructions collection.
  auto NextInst = std::next(MI.getIterator());
  auto *MBB = MI.getParent();
  MI.removeFromParent();
  StashedDebugInstrs.push_back({&MI, Idx, MBB});
  return NextInst;
}

bool LDVImpl::handleDebugLabel(MachineInstr &MI, SlotIndex Idx) {
  // DBG_LABEL label
  if (MI.getNumOperands() != 1 || !MI.getOperand(0).isMetadata()) {
    LLVM_DEBUG(dbgs() << "Can't handle " << MI);
    return false;
  }

  // Get or create the UserLabel for label here.
  const DILabel *Label = MI.getDebugLabel();
  const DebugLoc &DL = MI.getDebugLoc();
  bool Found = false;
  for (auto const &L : userLabels) {
    if (L->matches(Label, DL->getInlinedAt(), Idx)) {
      Found = true;
      break;
    }
  }
  if (!Found)
    userLabels.push_back(std::make_unique<UserLabel>(Label, DL, Idx));

  return true;
}

bool LDVImpl::collectDebugValues(MachineFunction &mf, bool InstrRef) {
  bool Changed = false;
  for (MachineBasicBlock &MBB : mf) {
    for (MachineBasicBlock::iterator MBBI = MBB.begin(), MBBE = MBB.end();
         MBBI != MBBE;) {
      // Use the first debug instruction in the sequence to get a SlotIndex
      // for following consecutive debug instructions.
      if (!MBBI->isDebugOrPseudoInstr()) {
        ++MBBI;
        continue;
      }
      // Debug instructions has no slot index. Use the previous
      // non-debug instruction's SlotIndex as its SlotIndex.
      SlotIndex Idx =
          MBBI == MBB.begin()
              ? LIS->getMBBStartIdx(&MBB)
              : LIS->getInstructionIndex(*std::prev(MBBI)).getRegSlot();
      // Handle consecutive debug instructions with the same slot index.
      do {
        // In instruction referencing mode, pass each instr to handleDebugInstr
        // to be unlinked. Ignore DBG_VALUE_LISTs -- they refer to vregs, and
        // need to go through the normal live interval splitting process.
        if (InstrRef && (MBBI->isNonListDebugValue() || MBBI->isDebugPHI() ||
                         MBBI->isDebugRef())) {
          MBBI = handleDebugInstr(*MBBI, Idx);
          Changed = true;
        // In normal debug mode, use the dedicated DBG_VALUE / DBG_LABEL handler
        // to track things through register allocation, and erase the instr.
        } else if ((MBBI->isDebugValue() && handleDebugValue(*MBBI, Idx)) ||
                   (MBBI->isDebugLabel() && handleDebugLabel(*MBBI, Idx))) {
          MBBI = MBB.erase(MBBI);
          Changed = true;
        } else
          ++MBBI;
      } while (MBBI != MBBE && MBBI->isDebugOrPseudoInstr());
    }
  }
  return Changed;
}

void UserValue::extendDef(
    SlotIndex Idx, DbgVariableValue DbgValue,
    SmallDenseMap<unsigned, std::pair<LiveRange *, const VNInfo *>>
        &LiveIntervalInfo,
    std::optional<std::pair<SlotIndex, SmallVector<unsigned>>> &Kills,
    LiveIntervals &LIS) {
  SlotIndex Start = Idx;
  MachineBasicBlock *MBB = LIS.getMBBFromIndex(Start);
  SlotIndex Stop = LIS.getMBBEndIdx(MBB);
  LocMap::iterator I = locInts.find(Start);

  // Limit to the intersection of the VNIs' live ranges.
  for (auto &LII : LiveIntervalInfo) {
    LiveRange *LR = LII.second.first;
    assert(LR && LII.second.second && "Missing range info for Idx.");
    LiveInterval::Segment *Segment = LR->getSegmentContaining(Start);
    assert(Segment && Segment->valno == LII.second.second &&
           "Invalid VNInfo for Idx given?");
    if (Segment->end < Stop) {
      Stop = Segment->end;
      Kills = {Stop, {LII.first}};
    } else if (Segment->end == Stop && Kills) {
      // If multiple locations end at the same place, track all of them in
      // Kills.
      Kills->second.push_back(LII.first);
    }
  }

  // There could already be a short def at Start.
  if (I.valid() && I.start() <= Start) {
    // Stop when meeting a different location or an already extended interval.
    Start = Start.getNextSlot();
    if (I.value() != DbgValue || I.stop() != Start) {
      // Clear `Kills`, as we have a new def available.
      Kills = std::nullopt;
      return;
    }
    // This is a one-slot placeholder. Just skip it.
    ++I;
  }

  // Limited by the next def.
  if (I.valid() && I.start() < Stop) {
    Stop = I.start();
    // Clear `Kills`, as we have a new def available.
    Kills = std::nullopt;
  }

  if (Start < Stop) {
    DbgVariableValue ExtDbgValue(DbgValue);
    I.insert(Start, Stop, std::move(ExtDbgValue));
  }
}

void UserValue::addDefsFromCopies(
    DbgVariableValue DbgValue,
    SmallVectorImpl<std::pair<unsigned, LiveInterval *>> &LocIntervals,
    SlotIndex KilledAt,
    SmallVectorImpl<std::pair<SlotIndex, DbgVariableValue>> &NewDefs,
    MachineRegisterInfo &MRI, LiveIntervals &LIS) {
  // Don't track copies from physregs, there are too many uses.
  if (any_of(LocIntervals,
             [](auto LocI) { return !LocI.second->reg().isVirtual(); }))
    return;

  // Collect all the (vreg, valno) pairs that are copies of LI.
  SmallDenseMap<unsigned,
                SmallVector<std::pair<LiveInterval *, const VNInfo *>, 4>>
      CopyValues;
  for (auto &LocInterval : LocIntervals) {
    unsigned LocNo = LocInterval.first;
    LiveInterval *LI = LocInterval.second;
    for (MachineOperand &MO : MRI.use_nodbg_operands(LI->reg())) {
      MachineInstr *MI = MO.getParent();
      // Copies of the full value.
      if (MO.getSubReg() || !MI->isCopy())
        continue;
      Register DstReg = MI->getOperand(0).getReg();

      // Don't follow copies to physregs. These are usually setting up call
      // arguments, and the argument registers are always call clobbered. We are
      // better off in the source register which could be a callee-saved
      // register, or it could be spilled.
      if (!DstReg.isVirtual())
        continue;

      // Is the value extended to reach this copy? If not, another def may be
      // blocking it, or we are looking at a wrong value of LI.
      SlotIndex Idx = LIS.getInstructionIndex(*MI);
      LocMap::iterator I = locInts.find(Idx.getRegSlot(true));
      if (!I.valid() || I.value() != DbgValue)
        continue;

      if (!LIS.hasInterval(DstReg))
        continue;
      LiveInterval *DstLI = &LIS.getInterval(DstReg);
      const VNInfo *DstVNI = DstLI->getVNInfoAt(Idx.getRegSlot());
      assert(DstVNI && DstVNI->def == Idx.getRegSlot() && "Bad copy value");
      CopyValues[LocNo].push_back(std::make_pair(DstLI, DstVNI));
    }
  }

  if (CopyValues.empty())
    return;

#if !defined(NDEBUG)
  for (auto &LocInterval : LocIntervals)
    LLVM_DEBUG(dbgs() << "Got " << CopyValues[LocInterval.first].size()
                      << " copies of " << *LocInterval.second << '\n');
#endif

  // Try to add defs of the copied values for the kill point. Check that there
  // isn't already a def at Idx.
  LocMap::iterator I = locInts.find(KilledAt);
  if (I.valid() && I.start() <= KilledAt)
    return;
  DbgVariableValue NewValue(DbgValue);
  for (auto &LocInterval : LocIntervals) {
    unsigned LocNo = LocInterval.first;
    bool FoundCopy = false;
    for (auto &LIAndVNI : CopyValues[LocNo]) {
      LiveInterval *DstLI = LIAndVNI.first;
      const VNInfo *DstVNI = LIAndVNI.second;
      if (DstLI->getVNInfoAt(KilledAt) != DstVNI)
        continue;
      LLVM_DEBUG(dbgs() << "Kill at " << KilledAt << " covered by valno #"
                        << DstVNI->id << " in " << *DstLI << '\n');
      MachineInstr *CopyMI = LIS.getInstructionFromIndex(DstVNI->def);
      assert(CopyMI && CopyMI->isCopy() && "Bad copy value");
      unsigned NewLocNo = getLocationNo(CopyMI->getOperand(0));
      NewValue = NewValue.changeLocNo(LocNo, NewLocNo);
      FoundCopy = true;
      break;
    }
    // If there are any killed locations we can't find a copy for, we can't
    // extend the variable value.
    if (!FoundCopy)
      return;
  }
  I.insert(KilledAt, KilledAt.getNextSlot(), NewValue);
  NewDefs.push_back(std::make_pair(KilledAt, NewValue));
}

void UserValue::computeIntervals(MachineRegisterInfo &MRI,
                                 const TargetRegisterInfo &TRI,
                                 LiveIntervals &LIS, LexicalScopes &LS) {
  SmallVector<std::pair<SlotIndex, DbgVariableValue>, 16> Defs;

  // Collect all defs to be extended (Skipping undefs).
  for (LocMap::const_iterator I = locInts.begin(); I.valid(); ++I)
    if (!I.value().isUndef())
      Defs.push_back(std::make_pair(I.start(), I.value()));

  // Extend all defs, and possibly add new ones along the way.
  for (unsigned i = 0; i != Defs.size(); ++i) {
    SlotIndex Idx = Defs[i].first;
    DbgVariableValue DbgValue = Defs[i].second;
    SmallDenseMap<unsigned, std::pair<LiveRange *, const VNInfo *>> LIs;
    SmallVector<const VNInfo *, 4> VNIs;
    bool ShouldExtendDef = false;
    for (unsigned LocNo : DbgValue.loc_nos()) {
      const MachineOperand &LocMO = locations[LocNo];
      if (!LocMO.isReg() || !LocMO.getReg().isVirtual()) {
        ShouldExtendDef |= !LocMO.isReg();
        continue;
      }
      ShouldExtendDef = true;
      LiveInterval *LI = nullptr;
      const VNInfo *VNI = nullptr;
      if (LIS.hasInterval(LocMO.getReg())) {
        LI = &LIS.getInterval(LocMO.getReg());
        VNI = LI->getVNInfoAt(Idx);
      }
      if (LI && VNI)
        LIs[LocNo] = {LI, VNI};
    }
    if (ShouldExtendDef) {
      std::optional<std::pair<SlotIndex, SmallVector<unsigned>>> Kills;
      extendDef(Idx, DbgValue, LIs, Kills, LIS);

      if (Kills) {
        SmallVector<std::pair<unsigned, LiveInterval *>, 2> KilledLocIntervals;
        bool AnySubreg = false;
        for (unsigned LocNo : Kills->second) {
          const MachineOperand &LocMO = this->locations[LocNo];
          if (LocMO.getSubReg()) {
            AnySubreg = true;
            break;
          }
          LiveInterval *LI = &LIS.getInterval(LocMO.getReg());
          KilledLocIntervals.push_back({LocNo, LI});
        }

        // FIXME: Handle sub-registers in addDefsFromCopies. The problem is that
        // if the original location for example is %vreg0:sub_hi, and we find a
        // full register copy in addDefsFromCopies (at the moment it only
        // handles full register copies), then we must add the sub1 sub-register
        // index to the new location. However, that is only possible if the new
        // virtual register is of the same regclass (or if there is an
        // equivalent sub-register in that regclass). For now, simply skip
        // handling copies if a sub-register is involved.
        if (!AnySubreg)
          addDefsFromCopies(DbgValue, KilledLocIntervals, Kills->first, Defs,
                            MRI, LIS);
      }
    }

    // For physregs, we only mark the start slot idx. DwarfDebug will see it
    // as if the DBG_VALUE is valid up until the end of the basic block, or
    // the next def of the physical register. So we do not need to extend the
    // range. It might actually happen that the DBG_VALUE is the last use of
    // the physical register (e.g. if this is an unused input argument to a
    // function).
  }

  // The computed intervals may extend beyond the range of the debug
  // location's lexical scope. In this case, splitting of an interval
  // can result in an interval outside of the scope being created,
  // causing extra unnecessary DBG_VALUEs to be emitted. To prevent
  // this, trim the intervals to the lexical scope in the case of inlined
  // variables, since heavy inlining may cause production of dramatically big
  // number of DBG_VALUEs to be generated.
  if (!dl.getInlinedAt())
    return;

  LexicalScope *Scope = LS.findLexicalScope(dl);
  if (!Scope)
    return;

  SlotIndex PrevEnd;
  LocMap::iterator I = locInts.begin();

  // Iterate over the lexical scope ranges. Each time round the loop
  // we check the intervals for overlap with the end of the previous
  // range and the start of the next. The first range is handled as
  // a special case where there is no PrevEnd.
  for (const InsnRange &Range : Scope->getRanges()) {
    SlotIndex RStart = LIS.getInstructionIndex(*Range.first);
    SlotIndex REnd = LIS.getInstructionIndex(*Range.second);

    // Variable locations at the first instruction of a block should be
    // based on the block's SlotIndex, not the first instruction's index.
    if (Range.first == Range.first->getParent()->begin())
      RStart = LIS.getSlotIndexes()->getIndexBefore(*Range.first);

    // At the start of each iteration I has been advanced so that
    // I.stop() >= PrevEnd. Check for overlap.
    if (PrevEnd && I.start() < PrevEnd) {
      SlotIndex IStop = I.stop();
      DbgVariableValue DbgValue = I.value();

      // Stop overlaps previous end - trim the end of the interval to
      // the scope range.
      I.setStopUnchecked(PrevEnd);
      ++I;

      // If the interval also overlaps the start of the "next" (i.e.
      // current) range create a new interval for the remainder (which
      // may be further trimmed).
      if (RStart < IStop)
        I.insert(RStart, IStop, DbgValue);
    }

    // Advance I so that I.stop() >= RStart, and check for overlap.
    I.advanceTo(RStart);
    if (!I.valid())
      return;

    if (I.start() < RStart) {
      // Interval start overlaps range - trim to the scope range.
      I.setStartUnchecked(RStart);
      // Remember that this interval was trimmed.
      trimmedDefs.insert(RStart);
    }

    // The end of a lexical scope range is the last instruction in the
    // range. To convert to an interval we need the index of the
    // instruction after it.
    REnd = REnd.getNextIndex();

    // Advance I to first interval outside current range.
    I.advanceTo(REnd);
    if (!I.valid())
      return;

    PrevEnd = REnd;
  }

  // Check for overlap with end of final range.
  if (PrevEnd && I.start() < PrevEnd)
    I.setStopUnchecked(PrevEnd);
}

void LDVImpl::computeIntervals() {
  LexicalScopes LS;
  LS.initialize(*MF);

  for (const auto &UV : userValues) {
    UV->computeIntervals(MF->getRegInfo(), *TRI, *LIS, LS);
    UV->mapVirtRegs(this);
  }
}

bool LDVImpl::runOnMachineFunction(MachineFunction &mf, bool InstrRef) {
  clear();
  MF = &mf;
  LIS = &pass.getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  TRI = mf.getSubtarget().getRegisterInfo();
  LLVM_DEBUG(dbgs() << "********** COMPUTING LIVE DEBUG VARIABLES: "
                    << mf.getName() << " **********\n");

  bool Changed = collectDebugValues(mf, InstrRef);
  computeIntervals();
  LLVM_DEBUG(print(dbgs()));

  // Collect the set of VReg / SlotIndexs where PHIs occur; index the sensitive
  // VRegs too, for when we're notified of a range split.
  SlotIndexes *Slots = LIS->getSlotIndexes();
  for (const auto &PHIIt : MF->DebugPHIPositions) {
    const MachineFunction::DebugPHIRegallocPos &Position = PHIIt.second;
    MachineBasicBlock *MBB = Position.MBB;
    Register Reg = Position.Reg;
    unsigned SubReg = Position.SubReg;
    SlotIndex SI = Slots->getMBBStartIdx(MBB);
    PHIValPos VP = {SI, Reg, SubReg};
    PHIValToPos.insert(std::make_pair(PHIIt.first, VP));
    RegToPHIIdx[Reg].push_back(PHIIt.first);
  }

  ModifiedMF = Changed;
  return Changed;
}

static void removeDebugInstrs(MachineFunction &mf) {
  for (MachineBasicBlock &MBB : mf) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB))
      if (MI.isDebugInstr())
        MBB.erase(&MI);
  }
}

bool LiveDebugVariables::runOnMachineFunction(MachineFunction &mf) {
  if (!EnableLDV)
    return false;
  if (!mf.getFunction().getSubprogram()) {
    removeDebugInstrs(mf);
    return false;
  }

  // Have we been asked to track variable locations using instruction
  // referencing?
  bool InstrRef = mf.useDebugInstrRef();

  if (!pImpl)
    pImpl = new LDVImpl(this);
  return static_cast<LDVImpl *>(pImpl)->runOnMachineFunction(mf, InstrRef);
}

void LiveDebugVariables::releaseMemory() {
  if (pImpl)
    static_cast<LDVImpl*>(pImpl)->clear();
}

LiveDebugVariables::~LiveDebugVariables() {
  if (pImpl)
    delete static_cast<LDVImpl*>(pImpl);
}

//===----------------------------------------------------------------------===//
//                           Live Range Splitting
//===----------------------------------------------------------------------===//

bool
UserValue::splitLocation(unsigned OldLocNo, ArrayRef<Register> NewRegs,
                         LiveIntervals& LIS) {
  LLVM_DEBUG({
    dbgs() << "Splitting Loc" << OldLocNo << '\t';
    print(dbgs(), nullptr);
  });
  bool DidChange = false;
  LocMap::iterator LocMapI;
  LocMapI.setMap(locInts);
  for (Register NewReg : NewRegs) {
    LiveInterval *LI = &LIS.getInterval(NewReg);
    if (LI->empty())
      continue;

    // Don't allocate the new LocNo until it is needed.
    unsigned NewLocNo = UndefLocNo;

    // Iterate over the overlaps between locInts and LI.
    LocMapI.find(LI->beginIndex());
    if (!LocMapI.valid())
      continue;
    LiveInterval::iterator LII = LI->advanceTo(LI->begin(), LocMapI.start());
    LiveInterval::iterator LIE = LI->end();
    while (LocMapI.valid() && LII != LIE) {
      // At this point, we know that LocMapI.stop() > LII->start.
      LII = LI->advanceTo(LII, LocMapI.start());
      if (LII == LIE)
        break;

      // Now LII->end > LocMapI.start(). Do we have an overlap?
      if (LocMapI.value().containsLocNo(OldLocNo) &&
          LII->start < LocMapI.stop()) {
        // Overlapping correct location. Allocate NewLocNo now.
        if (NewLocNo == UndefLocNo) {
          MachineOperand MO = MachineOperand::CreateReg(LI->reg(), false);
          MO.setSubReg(locations[OldLocNo].getSubReg());
          NewLocNo = getLocationNo(MO);
          DidChange = true;
        }

        SlotIndex LStart = LocMapI.start();
        SlotIndex LStop = LocMapI.stop();
        DbgVariableValue OldDbgValue = LocMapI.value();

        // Trim LocMapI down to the LII overlap.
        if (LStart < LII->start)
          LocMapI.setStartUnchecked(LII->start);
        if (LStop > LII->end)
          LocMapI.setStopUnchecked(LII->end);

        // Change the value in the overlap. This may trigger coalescing.
        LocMapI.setValue(OldDbgValue.changeLocNo(OldLocNo, NewLocNo));

        // Re-insert any removed OldDbgValue ranges.
        if (LStart < LocMapI.start()) {
          LocMapI.insert(LStart, LocMapI.start(), OldDbgValue);
          ++LocMapI;
          assert(LocMapI.valid() && "Unexpected coalescing");
        }
        if (LStop > LocMapI.stop()) {
          ++LocMapI;
          LocMapI.insert(LII->end, LStop, OldDbgValue);
          --LocMapI;
        }
      }

      // Advance to the next overlap.
      if (LII->end < LocMapI.stop()) {
        if (++LII == LIE)
          break;
        LocMapI.advanceTo(LII->start);
      } else {
        ++LocMapI;
        if (!LocMapI.valid())
          break;
        LII = LI->advanceTo(LII, LocMapI.start());
      }
    }
  }

  // Finally, remove OldLocNo unless it is still used by some interval in the
  // locInts map. One case when OldLocNo still is in use is when the register
  // has been spilled. In such situations the spilled register is kept as a
  // location until rewriteLocations is called (VirtRegMap is mapping the old
  // register to the spill slot). So for a while we can have locations that map
  // to virtual registers that have been removed from both the MachineFunction
  // and from LiveIntervals.
  //
  // We may also just be using the location for a value with a different
  // expression.
  removeLocationIfUnused(OldLocNo);

  LLVM_DEBUG({
    dbgs() << "Split result: \t";
    print(dbgs(), nullptr);
  });
  return DidChange;
}

bool
UserValue::splitRegister(Register OldReg, ArrayRef<Register> NewRegs,
                         LiveIntervals &LIS) {
  bool DidChange = false;
  // Split locations referring to OldReg. Iterate backwards so splitLocation can
  // safely erase unused locations.
  for (unsigned i = locations.size(); i ; --i) {
    unsigned LocNo = i-1;
    const MachineOperand *Loc = &locations[LocNo];
    if (!Loc->isReg() || Loc->getReg() != OldReg)
      continue;
    DidChange |= splitLocation(LocNo, NewRegs, LIS);
  }
  return DidChange;
}

void LDVImpl::splitPHIRegister(Register OldReg, ArrayRef<Register> NewRegs) {
  auto RegIt = RegToPHIIdx.find(OldReg);
  if (RegIt == RegToPHIIdx.end())
    return;

  std::vector<std::pair<Register, unsigned>> NewRegIdxes;
  // Iterate over all the debug instruction numbers affected by this split.
  for (unsigned InstrID : RegIt->second) {
    auto PHIIt = PHIValToPos.find(InstrID);
    assert(PHIIt != PHIValToPos.end());
    const SlotIndex &Slot = PHIIt->second.SI;
    assert(OldReg == PHIIt->second.Reg);

    // Find the new register that covers this position.
    for (auto NewReg : NewRegs) {
      const LiveInterval &LI = LIS->getInterval(NewReg);
      auto LII = LI.find(Slot);
      if (LII != LI.end() && LII->start <= Slot) {
        // This new register covers this PHI position, record this for indexing.
        NewRegIdxes.push_back(std::make_pair(NewReg, InstrID));
        // Record that this value lives in a different VReg now.
        PHIIt->second.Reg = NewReg;
        break;
      }
    }

    // If we do not find a new register covering this PHI, then register
    // allocation has dropped its location, for example because it's not live.
    // The old VReg will not be mapped to a physreg, and the instruction
    // number will have been optimized out.
  }

  // Re-create register index using the new register numbers.
  RegToPHIIdx.erase(RegIt);
  for (auto &RegAndInstr : NewRegIdxes)
    RegToPHIIdx[RegAndInstr.first].push_back(RegAndInstr.second);
}

void LDVImpl::splitRegister(Register OldReg, ArrayRef<Register> NewRegs) {
  // Consider whether this split range affects any PHI locations.
  splitPHIRegister(OldReg, NewRegs);

  // Check whether any intervals mapped by a DBG_VALUE were split and need
  // updating.
  bool DidChange = false;
  for (UserValue *UV = lookupVirtReg(OldReg); UV; UV = UV->getNext())
    DidChange |= UV->splitRegister(OldReg, NewRegs, *LIS);

  if (!DidChange)
    return;

  // Map all of the new virtual registers.
  UserValue *UV = lookupVirtReg(OldReg);
  for (Register NewReg : NewRegs)
    mapVirtReg(NewReg, UV);
}

void LiveDebugVariables::
splitRegister(Register OldReg, ArrayRef<Register> NewRegs, LiveIntervals &LIS) {
  if (pImpl)
    static_cast<LDVImpl*>(pImpl)->splitRegister(OldReg, NewRegs);
}

void UserValue::rewriteLocations(VirtRegMap &VRM, const MachineFunction &MF,
                                 const TargetInstrInfo &TII,
                                 const TargetRegisterInfo &TRI,
                                 SpillOffsetMap &SpillOffsets) {
  // Build a set of new locations with new numbers so we can coalesce our
  // IntervalMap if two vreg intervals collapse to the same physical location.
  // Use MapVector instead of SetVector because MapVector::insert returns the
  // position of the previously or newly inserted element. The boolean value
  // tracks if the location was produced by a spill.
  // FIXME: This will be problematic if we ever support direct and indirect
  // frame index locations, i.e. expressing both variables in memory and
  // 'int x, *px = &x'. The "spilled" bit must become part of the location.
  MapVector<MachineOperand, std::pair<bool, unsigned>> NewLocations;
  SmallVector<unsigned, 4> LocNoMap(locations.size());
  for (unsigned I = 0, E = locations.size(); I != E; ++I) {
    bool Spilled = false;
    unsigned SpillOffset = 0;
    MachineOperand Loc = locations[I];
    // Only virtual registers are rewritten.
    if (Loc.isReg() && Loc.getReg() && Loc.getReg().isVirtual()) {
      Register VirtReg = Loc.getReg();
      if (VRM.isAssignedReg(VirtReg) &&
          Register::isPhysicalRegister(VRM.getPhys(VirtReg))) {
        // This can create a %noreg operand in rare cases when the sub-register
        // index is no longer available. That means the user value is in a
        // non-existent sub-register, and %noreg is exactly what we want.
        Loc.substPhysReg(VRM.getPhys(VirtReg), TRI);
      } else if (VRM.getStackSlot(VirtReg) != VirtRegMap::NO_STACK_SLOT) {
        // Retrieve the stack slot offset.
        unsigned SpillSize;
        const MachineRegisterInfo &MRI = MF.getRegInfo();
        const TargetRegisterClass *TRC = MRI.getRegClass(VirtReg);
        bool Success = TII.getStackSlotRange(TRC, Loc.getSubReg(), SpillSize,
                                             SpillOffset, MF);

        // FIXME: Invalidate the location if the offset couldn't be calculated.
        (void)Success;

        Loc = MachineOperand::CreateFI(VRM.getStackSlot(VirtReg));
        Spilled = true;
      } else {
        Loc.setReg(0);
        Loc.setSubReg(0);
      }
    }

    // Insert this location if it doesn't already exist and record a mapping
    // from the old number to the new number.
    auto InsertResult = NewLocations.insert({Loc, {Spilled, SpillOffset}});
    unsigned NewLocNo = std::distance(NewLocations.begin(), InsertResult.first);
    LocNoMap[I] = NewLocNo;
  }

  // Rewrite the locations and record the stack slot offsets for spills.
  locations.clear();
  SpillOffsets.clear();
  for (auto &Pair : NewLocations) {
    bool Spilled;
    unsigned SpillOffset;
    std::tie(Spilled, SpillOffset) = Pair.second;
    locations.push_back(Pair.first);
    if (Spilled) {
      unsigned NewLocNo = std::distance(&*NewLocations.begin(), &Pair);
      SpillOffsets[NewLocNo] = SpillOffset;
    }
  }

  // Update the interval map, but only coalesce left, since intervals to the
  // right use the old location numbers. This should merge two contiguous
  // DBG_VALUE intervals with different vregs that were allocated to the same
  // physical register.
  for (LocMap::iterator I = locInts.begin(); I.valid(); ++I) {
    I.setValueUnchecked(I.value().remapLocNos(LocNoMap));
    I.setStart(I.start());
  }
}

/// Find an iterator for inserting a DBG_VALUE instruction.
static MachineBasicBlock::iterator
findInsertLocation(MachineBasicBlock *MBB, SlotIndex Idx, LiveIntervals &LIS,
                   BlockSkipInstsMap &BBSkipInstsMap) {
  SlotIndex Start = LIS.getMBBStartIdx(MBB);
  Idx = Idx.getBaseIndex();

  // Try to find an insert location by going backwards from Idx.
  MachineInstr *MI;
  while (!(MI = LIS.getInstructionFromIndex(Idx))) {
    // We've reached the beginning of MBB.
    if (Idx == Start) {
      // Retrieve the last PHI/Label/Debug location found when calling
      // SkipPHIsLabelsAndDebug last time. Start searching from there.
      //
      // Note the iterator kept in BBSkipInstsMap is one step back based
      // on the iterator returned by SkipPHIsLabelsAndDebug last time.
      // One exception is when SkipPHIsLabelsAndDebug returns MBB->begin(),
      // BBSkipInstsMap won't save it. This is to consider the case that
      // new instructions may be inserted at the beginning of MBB after
      // last call of SkipPHIsLabelsAndDebug. If we save MBB->begin() in
      // BBSkipInstsMap, after new non-phi/non-label/non-debug instructions
      // are inserted at the beginning of the MBB, the iterator in
      // BBSkipInstsMap won't point to the beginning of the MBB anymore.
      // Therefore The next search in SkipPHIsLabelsAndDebug will skip those
      // newly added instructions and that is unwanted.
      MachineBasicBlock::iterator BeginIt;
      auto MapIt = BBSkipInstsMap.find(MBB);
      if (MapIt == BBSkipInstsMap.end())
        BeginIt = MBB->begin();
      else
        BeginIt = std::next(MapIt->second);
      auto I = MBB->SkipPHIsLabelsAndDebug(BeginIt);
      if (I != BeginIt)
        BBSkipInstsMap[MBB] = std::prev(I);
      return I;
    }
    Idx = Idx.getPrevIndex();
  }

  // Don't insert anything after the first terminator, though.
  return MI->isTerminator() ? MBB->getFirstTerminator() :
                              std::next(MachineBasicBlock::iterator(MI));
}

/// Find an iterator for inserting the next DBG_VALUE instruction
/// (or end if no more insert locations found).
static MachineBasicBlock::iterator
findNextInsertLocation(MachineBasicBlock *MBB, MachineBasicBlock::iterator I,
                       SlotIndex StopIdx, ArrayRef<MachineOperand> LocMOs,
                       LiveIntervals &LIS, const TargetRegisterInfo &TRI) {
  SmallVector<Register, 4> Regs;
  for (const MachineOperand &LocMO : LocMOs)
    if (LocMO.isReg())
      Regs.push_back(LocMO.getReg());
  if (Regs.empty())
    return MBB->instr_end();

  // Find the next instruction in the MBB that define the register Reg.
  while (I != MBB->end() && !I->isTerminator()) {
    if (!LIS.isNotInMIMap(*I) &&
        SlotIndex::isEarlierEqualInstr(StopIdx, LIS.getInstructionIndex(*I)))
      break;
    if (any_of(Regs, [&I, &TRI](Register &Reg) {
          return I->definesRegister(Reg, &TRI);
        }))
      // The insert location is directly after the instruction/bundle.
      return std::next(I);
    ++I;
  }
  return MBB->end();
}

void UserValue::insertDebugValue(MachineBasicBlock *MBB, SlotIndex StartIdx,
                                 SlotIndex StopIdx, DbgVariableValue DbgValue,
                                 ArrayRef<bool> LocSpills,
                                 ArrayRef<unsigned> SpillOffsets,
                                 LiveIntervals &LIS, const TargetInstrInfo &TII,
                                 const TargetRegisterInfo &TRI,
                                 BlockSkipInstsMap &BBSkipInstsMap) {
  SlotIndex MBBEndIdx = LIS.getMBBEndIdx(&*MBB);
  // Only search within the current MBB.
  StopIdx = (MBBEndIdx < StopIdx) ? MBBEndIdx : StopIdx;
  MachineBasicBlock::iterator I =
      findInsertLocation(MBB, StartIdx, LIS, BBSkipInstsMap);
  // Undef values don't exist in locations so create new "noreg" register MOs
  // for them. See getLocationNo().
  SmallVector<MachineOperand, 8> MOs;
  if (DbgValue.isUndef()) {
    MOs.assign(DbgValue.loc_nos().size(),
               MachineOperand::CreateReg(
                   /* Reg */ 0, /* isDef */ false, /* isImp */ false,
                   /* isKill */ false, /* isDead */ false,
                   /* isUndef */ false, /* isEarlyClobber */ false,
                   /* SubReg */ 0, /* isDebug */ true));
  } else {
    for (unsigned LocNo : DbgValue.loc_nos())
      MOs.push_back(locations[LocNo]);
  }

  ++NumInsertedDebugValues;

  assert(cast<DILocalVariable>(Variable)
             ->isValidLocationForIntrinsic(getDebugLoc()) &&
         "Expected inlined-at fields to agree");

  // If the location was spilled, the new DBG_VALUE will be indirect. If the
  // original DBG_VALUE was indirect, we need to add DW_OP_deref to indicate
  // that the original virtual register was a pointer. Also, add the stack slot
  // offset for the spilled register to the expression.
  const DIExpression *Expr = DbgValue.getExpression();
  bool IsIndirect = DbgValue.getWasIndirect();
  bool IsList = DbgValue.getWasList();
  for (unsigned I = 0, E = LocSpills.size(); I != E; ++I) {
    if (LocSpills[I]) {
      if (!IsList) {
        uint8_t DIExprFlags = DIExpression::ApplyOffset;
        if (IsIndirect)
          DIExprFlags |= DIExpression::DerefAfter;
        Expr = DIExpression::prepend(Expr, DIExprFlags, SpillOffsets[I]);
        IsIndirect = true;
      } else {
        SmallVector<uint64_t, 4> Ops;
        DIExpression::appendOffset(Ops, SpillOffsets[I]);
        Ops.push_back(dwarf::DW_OP_deref);
        Expr = DIExpression::appendOpsToArg(Expr, Ops, I);
      }
    }

    assert((!LocSpills[I] || MOs[I].isFI()) &&
           "a spilled location must be a frame index");
  }

  unsigned DbgValueOpcode =
      IsList ? TargetOpcode::DBG_VALUE_LIST : TargetOpcode::DBG_VALUE;
  do {
    BuildMI(*MBB, I, getDebugLoc(), TII.get(DbgValueOpcode), IsIndirect, MOs,
            Variable, Expr);

    // Continue and insert DBG_VALUES after every redefinition of a register
    // associated with the debug value within the range
    I = findNextInsertLocation(MBB, I, StopIdx, MOs, LIS, TRI);
  } while (I != MBB->end());
}

void UserLabel::insertDebugLabel(MachineBasicBlock *MBB, SlotIndex Idx,
                                 LiveIntervals &LIS, const TargetInstrInfo &TII,
                                 BlockSkipInstsMap &BBSkipInstsMap) {
  MachineBasicBlock::iterator I =
      findInsertLocation(MBB, Idx, LIS, BBSkipInstsMap);
  ++NumInsertedDebugLabels;
  BuildMI(*MBB, I, getDebugLoc(), TII.get(TargetOpcode::DBG_LABEL))
      .addMetadata(Label);
}

void UserValue::emitDebugValues(VirtRegMap *VRM, LiveIntervals &LIS,
                                const TargetInstrInfo &TII,
                                const TargetRegisterInfo &TRI,
                                const SpillOffsetMap &SpillOffsets,
                                BlockSkipInstsMap &BBSkipInstsMap) {
  MachineFunction::iterator MFEnd = VRM->getMachineFunction().end();

  for (LocMap::const_iterator I = locInts.begin(); I.valid();) {
    SlotIndex Start = I.start();
    SlotIndex Stop = I.stop();
    DbgVariableValue DbgValue = I.value();

    SmallVector<bool> SpilledLocs;
    SmallVector<unsigned> LocSpillOffsets;
    for (unsigned LocNo : DbgValue.loc_nos()) {
      auto SpillIt =
          !DbgValue.isUndef() ? SpillOffsets.find(LocNo) : SpillOffsets.end();
      bool Spilled = SpillIt != SpillOffsets.end();
      SpilledLocs.push_back(Spilled);
      LocSpillOffsets.push_back(Spilled ? SpillIt->second : 0);
    }

    // If the interval start was trimmed to the lexical scope insert the
    // DBG_VALUE at the previous index (otherwise it appears after the
    // first instruction in the range).
    if (trimmedDefs.count(Start))
      Start = Start.getPrevIndex();

    LLVM_DEBUG(auto &dbg = dbgs(); dbg << "\t[" << Start << ';' << Stop << "):";
               DbgValue.printLocNos(dbg));
    MachineFunction::iterator MBB = LIS.getMBBFromIndex(Start)->getIterator();
    SlotIndex MBBEnd = LIS.getMBBEndIdx(&*MBB);

    LLVM_DEBUG(dbgs() << ' ' << printMBBReference(*MBB) << '-' << MBBEnd);
    insertDebugValue(&*MBB, Start, Stop, DbgValue, SpilledLocs, LocSpillOffsets,
                     LIS, TII, TRI, BBSkipInstsMap);
    // This interval may span multiple basic blocks.
    // Insert a DBG_VALUE into each one.
    while (Stop > MBBEnd) {
      // Move to the next block.
      Start = MBBEnd;
      if (++MBB == MFEnd)
        break;
      MBBEnd = LIS.getMBBEndIdx(&*MBB);
      LLVM_DEBUG(dbgs() << ' ' << printMBBReference(*MBB) << '-' << MBBEnd);
      insertDebugValue(&*MBB, Start, Stop, DbgValue, SpilledLocs,
                       LocSpillOffsets, LIS, TII, TRI, BBSkipInstsMap);
    }
    LLVM_DEBUG(dbgs() << '\n');
    if (MBB == MFEnd)
      break;

    ++I;
  }
}

void UserLabel::emitDebugLabel(LiveIntervals &LIS, const TargetInstrInfo &TII,
                               BlockSkipInstsMap &BBSkipInstsMap) {
  LLVM_DEBUG(dbgs() << "\t" << loc);
  MachineFunction::iterator MBB = LIS.getMBBFromIndex(loc)->getIterator();

  LLVM_DEBUG(dbgs() << ' ' << printMBBReference(*MBB));
  insertDebugLabel(&*MBB, loc, LIS, TII, BBSkipInstsMap);

  LLVM_DEBUG(dbgs() << '\n');
}

void LDVImpl::emitDebugValues(VirtRegMap *VRM) {
  LLVM_DEBUG(dbgs() << "********** EMITTING LIVE DEBUG VARIABLES **********\n");
  if (!MF)
    return;

  BlockSkipInstsMap BBSkipInstsMap;
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  SpillOffsetMap SpillOffsets;
  for (auto &userValue : userValues) {
    LLVM_DEBUG(userValue->print(dbgs(), TRI));
    userValue->rewriteLocations(*VRM, *MF, *TII, *TRI, SpillOffsets);
    userValue->emitDebugValues(VRM, *LIS, *TII, *TRI, SpillOffsets,
                               BBSkipInstsMap);
  }
  LLVM_DEBUG(dbgs() << "********** EMITTING LIVE DEBUG LABELS **********\n");
  for (auto &userLabel : userLabels) {
    LLVM_DEBUG(userLabel->print(dbgs(), TRI));
    userLabel->emitDebugLabel(*LIS, *TII, BBSkipInstsMap);
  }

  LLVM_DEBUG(dbgs() << "********** EMITTING DEBUG PHIS **********\n");

  auto Slots = LIS->getSlotIndexes();
  for (auto &It : PHIValToPos) {
    // For each ex-PHI, identify its physreg location or stack slot, and emit
    // a DBG_PHI for it.
    unsigned InstNum = It.first;
    auto Slot = It.second.SI;
    Register Reg = It.second.Reg;
    unsigned SubReg = It.second.SubReg;

    MachineBasicBlock *OrigMBB = Slots->getMBBFromIndex(Slot);
    if (VRM->isAssignedReg(Reg) &&
        Register::isPhysicalRegister(VRM->getPhys(Reg))) {
      unsigned PhysReg = VRM->getPhys(Reg);
      if (SubReg != 0)
        PhysReg = TRI->getSubReg(PhysReg, SubReg);

      auto Builder = BuildMI(*OrigMBB, OrigMBB->begin(), DebugLoc(),
                             TII->get(TargetOpcode::DBG_PHI));
      Builder.addReg(PhysReg);
      Builder.addImm(InstNum);
    } else if (VRM->getStackSlot(Reg) != VirtRegMap::NO_STACK_SLOT) {
      const MachineRegisterInfo &MRI = MF->getRegInfo();
      const TargetRegisterClass *TRC = MRI.getRegClass(Reg);
      unsigned SpillSize, SpillOffset;

      unsigned regSizeInBits = TRI->getRegSizeInBits(*TRC);
      if (SubReg)
        regSizeInBits = TRI->getSubRegIdxSize(SubReg);

      // Test whether this location is legal with the given subreg. If the
      // subregister has a nonzero offset, drop this location, it's too complex
      // to describe. (TODO: future work).
      bool Success =
          TII->getStackSlotRange(TRC, SubReg, SpillSize, SpillOffset, *MF);

      if (Success && SpillOffset == 0) {
        auto Builder = BuildMI(*OrigMBB, OrigMBB->begin(), DebugLoc(),
                               TII->get(TargetOpcode::DBG_PHI));
        Builder.addFrameIndex(VRM->getStackSlot(Reg));
        Builder.addImm(InstNum);
        // Record how large the original value is. The stack slot might be
        // merged and altered during optimisation, but we will want to know how
        // large the value is, at this DBG_PHI.
        Builder.addImm(regSizeInBits);
      }

      LLVM_DEBUG(
      if (SpillOffset != 0) {
        dbgs() << "DBG_PHI for Vreg " << Reg << " subreg " << SubReg <<
                  " has nonzero offset\n";
      }
      );
    }
    // If there was no mapping for a value ID, it's optimized out. Create no
    // DBG_PHI, and any variables using this value will become optimized out.
  }
  MF->DebugPHIPositions.clear();

  LLVM_DEBUG(dbgs() << "********** EMITTING INSTR REFERENCES **********\n");

  // Re-insert any debug instrs back in the position they were. We must
  // re-insert in the same order to ensure that debug instructions don't swap,
  // which could re-order assignments. Do so in a batch -- once we find the
  // insert position, insert all instructions at the same SlotIdx. They are
  // guaranteed to appear in-sequence in StashedDebugInstrs because we insert
  // them in order.
  for (auto *StashIt = StashedDebugInstrs.begin();
       StashIt != StashedDebugInstrs.end(); ++StashIt) {
    SlotIndex Idx = StashIt->Idx;
    MachineBasicBlock *MBB = StashIt->MBB;
    MachineInstr *MI = StashIt->MI;

    auto EmitInstsHere = [this, &StashIt, MBB, Idx,
                          MI](MachineBasicBlock::iterator InsertPos) {
      // Insert this debug instruction.
      MBB->insert(InsertPos, MI);

      // Look at subsequent stashed debug instructions: if they're at the same
      // index, insert those too.
      auto NextItem = std::next(StashIt);
      while (NextItem != StashedDebugInstrs.end() && NextItem->Idx == Idx) {
        assert(NextItem->MBB == MBB && "Instrs with same slot index should be"
               "in the same block");
        MBB->insert(InsertPos, NextItem->MI);
        StashIt = NextItem;
        NextItem = std::next(StashIt);
      };
    };

    // Start block index: find the first non-debug instr in the block, and
    // insert before it.
    if (Idx == Slots->getMBBStartIdx(MBB)) {
      MachineBasicBlock::iterator InsertPos =
          findInsertLocation(MBB, Idx, *LIS, BBSkipInstsMap);
      EmitInstsHere(InsertPos);
      continue;
    }

    if (MachineInstr *Pos = Slots->getInstructionFromIndex(Idx)) {
      // Insert at the end of any debug instructions.
      auto PostDebug = std::next(Pos->getIterator());
      PostDebug = skipDebugInstructionsForward(PostDebug, MBB->instr_end());
      EmitInstsHere(PostDebug);
    } else {
      // Insert position disappeared; walk forwards through slots until we
      // find a new one.
      SlotIndex End = Slots->getMBBEndIdx(MBB);
      for (; Idx < End; Idx = Slots->getNextNonNullIndex(Idx)) {
        Pos = Slots->getInstructionFromIndex(Idx);
        if (Pos) {
          EmitInstsHere(Pos->getIterator());
          break;
        }
      }

      // We have reached the end of the block and didn't find anywhere to
      // insert! It's not safe to discard any debug instructions; place them
      // in front of the first terminator, or in front of end().
      if (Idx >= End) {
        auto TermIt = MBB->getFirstTerminator();
        EmitInstsHere(TermIt);
      }
    }
  }

  EmitDone = true;
  BBSkipInstsMap.clear();
}

void LiveDebugVariables::emitDebugValues(VirtRegMap *VRM) {
  if (pImpl)
    static_cast<LDVImpl*>(pImpl)->emitDebugValues(VRM);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LiveDebugVariables::dump() const {
  if (pImpl)
    static_cast<LDVImpl*>(pImpl)->print(dbgs());
}
#endif
