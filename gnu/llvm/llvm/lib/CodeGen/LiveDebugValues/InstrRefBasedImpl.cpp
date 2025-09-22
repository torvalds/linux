//===- InstrRefBasedImpl.cpp - Tracking Debug Value MIs -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file InstrRefBasedImpl.cpp
///
/// This is a separate implementation of LiveDebugValues, see
/// LiveDebugValues.cpp and VarLocBasedImpl.cpp for more information.
///
/// This pass propagates variable locations between basic blocks, resolving
/// control flow conflicts between them. The problem is SSA construction, where
/// each debug instruction assigns the *value* that a variable has, and every
/// instruction where the variable is in scope uses that variable. The resulting
/// map of instruction-to-value is then translated into a register (or spill)
/// location for each variable over each instruction.
///
/// The primary difference from normal SSA construction is that we cannot
/// _create_ PHI values that contain variable values. CodeGen has already
/// completed, and we can't alter it just to make debug-info complete. Thus:
/// we can identify function positions where we would like a PHI value for a
/// variable, but must search the MachineFunction to see whether such a PHI is
/// available. If no such PHI exists, the variable location must be dropped.
///
/// To achieve this, we perform two kinds of analysis. First, we identify
/// every value defined by every instruction (ignoring those that only move
/// another value), then re-compute an SSA-form representation of the
/// MachineFunction, using value propagation to eliminate any un-necessary
/// PHI values. This gives us a map of every value computed in the function,
/// and its location within the register file / stack.
///
/// Secondly, for each variable we perform the same analysis, where each debug
/// instruction is considered a def, and every instruction where the variable
/// is in lexical scope as a use. Value propagation is used again to eliminate
/// any un-necessary PHIs. This gives us a map of each variable to the value
/// it should have in a block.
///
/// Once both are complete, we have two maps for each block:
///  * Variables to the values they should have,
///  * Values to the register / spill slot they are located in.
/// After which we can marry-up variable values with a location, and emit
/// DBG_VALUE instructions specifying those locations. Variable locations may
/// be dropped in this process due to the desired variable value not being
/// resident in any machine location, or because there is no PHI value in any
/// location that accurately represents the desired value.  The building of
/// location lists for each block is left to DbgEntityHistoryCalculator.
///
/// This pass is kept efficient because the size of the first SSA problem
/// is proportional to the working-set size of the function, which the compiler
/// tries to keep small. (It's also proportional to the number of blocks).
/// Additionally, we repeatedly perform the second SSA problem analysis with
/// only the variables and blocks in a single lexical scope, exploiting their
/// locality.
///
/// ### Terminology
///
/// A machine location is a register or spill slot, a value is something that's
/// defined by an instruction or PHI node, while a variable value is the value
/// assigned to a variable. A variable location is a machine location, that must
/// contain the appropriate variable value. A value that is a PHI node is
/// occasionally called an mphi.
///
/// The first SSA problem is the "machine value location" problem,
/// because we're determining which machine locations contain which values.
/// The "locations" are constant: what's unknown is what value they contain.
///
/// The second SSA problem (the one for variables) is the "variable value
/// problem", because it's determining what values a variable has, rather than
/// what location those values are placed in.
///
/// TODO:
///   Overlapping fragments
///   Entry values
///   Add back DEBUG statements for debugging this
///   Collect statistics
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GenericIteratedDominanceFrontier.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/SSAUpdaterImpl.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <functional>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

#include "InstrRefBasedImpl.h"
#include "LiveDebugValues.h"
#include <optional>

using namespace llvm;
using namespace LiveDebugValues;

// SSAUpdaterImple sets DEBUG_TYPE, change it.
#undef DEBUG_TYPE
#define DEBUG_TYPE "livedebugvalues"

// Act more like the VarLoc implementation, by propagating some locations too
// far and ignoring some transfers.
static cl::opt<bool> EmulateOldLDV("emulate-old-livedebugvalues", cl::Hidden,
                                   cl::desc("Act like old LiveDebugValues did"),
                                   cl::init(false));

// Limit for the maximum number of stack slots we should track, past which we
// will ignore any spills. InstrRefBasedLDV gathers detailed information on all
// stack slots which leads to high memory consumption, and in some scenarios
// (such as asan with very many locals) the working set of the function can be
// very large, causing many spills. In these scenarios, it is very unlikely that
// the developer has hundreds of variables live at the same time that they're
// carefully thinking about -- instead, they probably autogenerated the code.
// When this happens, gracefully stop tracking excess spill slots, rather than
// consuming all the developer's memory.
static cl::opt<unsigned>
    StackWorkingSetLimit("livedebugvalues-max-stack-slots", cl::Hidden,
                         cl::desc("livedebugvalues-stack-ws-limit"),
                         cl::init(250));

DbgOpID DbgOpID::UndefID = DbgOpID(0xffffffff);

/// Tracker for converting machine value locations and variable values into
/// variable locations (the output of LiveDebugValues), recorded as DBG_VALUEs
/// specifying block live-in locations and transfers within blocks.
///
/// Operating on a per-block basis, this class takes a (pre-loaded) MLocTracker
/// and must be initialized with the set of variable values that are live-in to
/// the block. The caller then repeatedly calls process(). TransferTracker picks
/// out variable locations for the live-in variable values (if there _is_ a
/// location) and creates the corresponding DBG_VALUEs. Then, as the block is
/// stepped through, transfers of values between machine locations are
/// identified and if profitable, a DBG_VALUE created.
///
/// This is where debug use-before-defs would be resolved: a variable with an
/// unavailable value could materialize in the middle of a block, when the
/// value becomes available. Or, we could detect clobbers and re-specify the
/// variable in a backup location. (XXX these are unimplemented).
class TransferTracker {
public:
  const TargetInstrInfo *TII;
  const TargetLowering *TLI;
  /// This machine location tracker is assumed to always contain the up-to-date
  /// value mapping for all machine locations. TransferTracker only reads
  /// information from it. (XXX make it const?)
  MLocTracker *MTracker;
  MachineFunction &MF;
  const DebugVariableMap &DVMap;
  bool ShouldEmitDebugEntryValues;

  /// Record of all changes in variable locations at a block position. Awkwardly
  /// we allow inserting either before or after the point: MBB != nullptr
  /// indicates it's before, otherwise after.
  struct Transfer {
    MachineBasicBlock::instr_iterator Pos; /// Position to insert DBG_VALUes
    MachineBasicBlock *MBB; /// non-null if we should insert after.
    /// Vector of DBG_VALUEs to insert. Store with their DebugVariableID so that
    /// they can be sorted into a stable order for emission at a later time.
    SmallVector<std::pair<DebugVariableID, MachineInstr *>, 4> Insts;
  };

  /// Stores the resolved operands (machine locations and constants) and
  /// qualifying meta-information needed to construct a concrete DBG_VALUE-like
  /// instruction.
  struct ResolvedDbgValue {
    SmallVector<ResolvedDbgOp> Ops;
    DbgValueProperties Properties;

    ResolvedDbgValue(SmallVectorImpl<ResolvedDbgOp> &Ops,
                     DbgValueProperties Properties)
        : Ops(Ops.begin(), Ops.end()), Properties(Properties) {}

    /// Returns all the LocIdx values used in this struct, in the order in which
    /// they appear as operands in the debug value; may contain duplicates.
    auto loc_indices() const {
      return map_range(
          make_filter_range(
              Ops, [](const ResolvedDbgOp &Op) { return !Op.IsConst; }),
          [](const ResolvedDbgOp &Op) { return Op.Loc; });
    }
  };

  /// Collection of transfers (DBG_VALUEs) to be inserted.
  SmallVector<Transfer, 32> Transfers;

  /// Local cache of what-value-is-in-what-LocIdx. Used to identify differences
  /// between TransferTrackers view of variable locations and MLocTrackers. For
  /// example, MLocTracker observes all clobbers, but TransferTracker lazily
  /// does not.
  SmallVector<ValueIDNum, 32> VarLocs;

  /// Map from LocIdxes to which DebugVariables are based that location.
  /// Mantained while stepping through the block. Not accurate if
  /// VarLocs[Idx] != MTracker->LocIdxToIDNum[Idx].
  DenseMap<LocIdx, SmallSet<DebugVariableID, 4>> ActiveMLocs;

  /// Map from DebugVariable to it's current location and qualifying meta
  /// information. To be used in conjunction with ActiveMLocs to construct
  /// enough information for the DBG_VALUEs for a particular LocIdx.
  DenseMap<DebugVariableID, ResolvedDbgValue> ActiveVLocs;

  /// Temporary cache of DBG_VALUEs to be entered into the Transfers collection.
  SmallVector<std::pair<DebugVariableID, MachineInstr *>, 4> PendingDbgValues;

  /// Record of a use-before-def: created when a value that's live-in to the
  /// current block isn't available in any machine location, but it will be
  /// defined in this block.
  struct UseBeforeDef {
    /// Value of this variable, def'd in block.
    SmallVector<DbgOp> Values;
    /// Identity of this variable.
    DebugVariableID VarID;
    /// Additional variable properties.
    DbgValueProperties Properties;
    UseBeforeDef(ArrayRef<DbgOp> Values, DebugVariableID VarID,
                 const DbgValueProperties &Properties)
        : Values(Values.begin(), Values.end()), VarID(VarID),
          Properties(Properties) {}
  };

  /// Map from instruction index (within the block) to the set of UseBeforeDefs
  /// that become defined at that instruction.
  DenseMap<unsigned, SmallVector<UseBeforeDef, 1>> UseBeforeDefs;

  /// The set of variables that are in UseBeforeDefs and can become a location
  /// once the relevant value is defined. An element being erased from this
  /// collection prevents the use-before-def materializing.
  DenseSet<DebugVariableID> UseBeforeDefVariables;

  const TargetRegisterInfo &TRI;
  const BitVector &CalleeSavedRegs;

  TransferTracker(const TargetInstrInfo *TII, MLocTracker *MTracker,
                  MachineFunction &MF, const DebugVariableMap &DVMap,
                  const TargetRegisterInfo &TRI,
                  const BitVector &CalleeSavedRegs, const TargetPassConfig &TPC)
      : TII(TII), MTracker(MTracker), MF(MF), DVMap(DVMap), TRI(TRI),
        CalleeSavedRegs(CalleeSavedRegs) {
    TLI = MF.getSubtarget().getTargetLowering();
    auto &TM = TPC.getTM<TargetMachine>();
    ShouldEmitDebugEntryValues = TM.Options.ShouldEmitDebugEntryValues();
  }

  bool isCalleeSaved(LocIdx L) const {
    unsigned Reg = MTracker->LocIdxToLocID[L];
    if (Reg >= MTracker->NumRegs)
      return false;
    for (MCRegAliasIterator RAI(Reg, &TRI, true); RAI.isValid(); ++RAI)
      if (CalleeSavedRegs.test(*RAI))
        return true;
    return false;
  };

  // An estimate of the expected lifespan of values at a machine location, with
  // a greater value corresponding to a longer expected lifespan, i.e. spill
  // slots generally live longer than callee-saved registers which generally
  // live longer than non-callee-saved registers. The minimum value of 0
  // corresponds to an illegal location that cannot have a "lifespan" at all.
  enum class LocationQuality : unsigned char {
    Illegal = 0,
    Register,
    CalleeSavedRegister,
    SpillSlot,
    Best = SpillSlot
  };

  class LocationAndQuality {
    unsigned Location : 24;
    unsigned Quality : 8;

  public:
    LocationAndQuality() : Location(0), Quality(0) {}
    LocationAndQuality(LocIdx L, LocationQuality Q)
        : Location(L.asU64()), Quality(static_cast<unsigned>(Q)) {}
    LocIdx getLoc() const {
      if (!Quality)
        return LocIdx::MakeIllegalLoc();
      return LocIdx(Location);
    }
    LocationQuality getQuality() const { return LocationQuality(Quality); }
    bool isIllegal() const { return !Quality; }
    bool isBest() const { return getQuality() == LocationQuality::Best; }
  };

  using ValueLocPair = std::pair<ValueIDNum, LocationAndQuality>;

  static inline bool ValueToLocSort(const ValueLocPair &A,
                                    const ValueLocPair &B) {
    return A.first < B.first;
  };

  // Returns the LocationQuality for the location L iff the quality of L is
  // is strictly greater than the provided minimum quality.
  std::optional<LocationQuality>
  getLocQualityIfBetter(LocIdx L, LocationQuality Min) const {
    if (L.isIllegal())
      return std::nullopt;
    if (Min >= LocationQuality::SpillSlot)
      return std::nullopt;
    if (MTracker->isSpill(L))
      return LocationQuality::SpillSlot;
    if (Min >= LocationQuality::CalleeSavedRegister)
      return std::nullopt;
    if (isCalleeSaved(L))
      return LocationQuality::CalleeSavedRegister;
    if (Min >= LocationQuality::Register)
      return std::nullopt;
    return LocationQuality::Register;
  }

  /// For a variable \p Var with the live-in value \p Value, attempts to resolve
  /// the DbgValue to a concrete DBG_VALUE, emitting that value and loading the
  /// tracking information to track Var throughout the block.
  /// \p ValueToLoc is a map containing the best known location for every
  ///    ValueIDNum that Value may use.
  /// \p MBB is the basic block that we are loading the live-in value for.
  /// \p DbgOpStore is the map containing the DbgOpID->DbgOp mapping needed to
  ///    determine the values used by Value.
  void loadVarInloc(MachineBasicBlock &MBB, DbgOpIDMap &DbgOpStore,
                    const SmallVectorImpl<ValueLocPair> &ValueToLoc,
                    DebugVariableID VarID, DbgValue Value) {
    SmallVector<DbgOp> DbgOps;
    SmallVector<ResolvedDbgOp> ResolvedDbgOps;
    bool IsValueValid = true;
    unsigned LastUseBeforeDef = 0;

    // If every value used by the incoming DbgValue is available at block
    // entry, ResolvedDbgOps will contain the machine locations/constants for
    // those values and will be used to emit a debug location.
    // If one or more values are not yet available, but will all be defined in
    // this block, then LastUseBeforeDef will track the instruction index in
    // this BB at which the last of those values is defined, DbgOps will
    // contain the values that we will emit when we reach that instruction.
    // If one or more values are undef or not available throughout this block,
    // and we can't recover as an entry value, we set IsValueValid=false and
    // skip this variable.
    for (DbgOpID ID : Value.getDbgOpIDs()) {
      DbgOp Op = DbgOpStore.find(ID);
      DbgOps.push_back(Op);
      if (ID.isUndef()) {
        IsValueValid = false;
        break;
      }
      if (ID.isConst()) {
        ResolvedDbgOps.push_back(Op.MO);
        continue;
      }

      // Search for the desired ValueIDNum, to examine the best location found
      // for it. Use an empty ValueLocPair to search for an entry in ValueToLoc.
      const ValueIDNum &Num = Op.ID;
      ValueLocPair Probe(Num, LocationAndQuality());
      auto ValuesPreferredLoc = std::lower_bound(
          ValueToLoc.begin(), ValueToLoc.end(), Probe, ValueToLocSort);

      // There must be a legitimate entry found for Num.
      assert(ValuesPreferredLoc != ValueToLoc.end() &&
             ValuesPreferredLoc->first == Num);

      if (ValuesPreferredLoc->second.isIllegal()) {
        // If it's a def that occurs in this block, register it as a
        // use-before-def to be resolved as we step through the block.
        // Continue processing values so that we add any other UseBeforeDef
        // entries needed for later.
        if (Num.getBlock() == (unsigned)MBB.getNumber() && !Num.isPHI()) {
          LastUseBeforeDef = std::max(LastUseBeforeDef,
                                      static_cast<unsigned>(Num.getInst()));
          continue;
        }
        recoverAsEntryValue(VarID, Value.Properties, Num);
        IsValueValid = false;
        break;
      }

      // Defer modifying ActiveVLocs until after we've confirmed we have a
      // live range.
      LocIdx M = ValuesPreferredLoc->second.getLoc();
      ResolvedDbgOps.push_back(M);
    }

    // If we cannot produce a valid value for the LiveIn value within this
    // block, skip this variable.
    if (!IsValueValid)
      return;

    // Add UseBeforeDef entry for the last value to be defined in this block.
    if (LastUseBeforeDef) {
      addUseBeforeDef(VarID, Value.Properties, DbgOps, LastUseBeforeDef);
      return;
    }

    // The LiveIn value is available at block entry, begin tracking and record
    // the transfer.
    for (const ResolvedDbgOp &Op : ResolvedDbgOps)
      if (!Op.IsConst)
        ActiveMLocs[Op.Loc].insert(VarID);
    auto NewValue = ResolvedDbgValue{ResolvedDbgOps, Value.Properties};
    auto Result = ActiveVLocs.insert(std::make_pair(VarID, NewValue));
    if (!Result.second)
      Result.first->second = NewValue;
    auto &[Var, DILoc] = DVMap.lookupDVID(VarID);
    PendingDbgValues.push_back(
        std::make_pair(VarID, &*MTracker->emitLoc(ResolvedDbgOps, Var, DILoc,
                                                  Value.Properties)));
  }

  /// Load object with live-in variable values. \p mlocs contains the live-in
  /// values in each machine location, while \p vlocs the live-in variable
  /// values. This method picks variable locations for the live-in variables,
  /// creates DBG_VALUEs and puts them in #Transfers, then prepares the other
  /// object fields to track variable locations as we step through the block.
  /// FIXME: could just examine mloctracker instead of passing in \p mlocs?
  void
  loadInlocs(MachineBasicBlock &MBB, ValueTable &MLocs, DbgOpIDMap &DbgOpStore,
             const SmallVectorImpl<std::pair<DebugVariableID, DbgValue>> &VLocs,
             unsigned NumLocs) {
    ActiveMLocs.clear();
    ActiveVLocs.clear();
    VarLocs.clear();
    VarLocs.reserve(NumLocs);
    UseBeforeDefs.clear();
    UseBeforeDefVariables.clear();

    // Mapping of the preferred locations for each value. Collected into this
    // vector then sorted for easy searching.
    SmallVector<ValueLocPair, 16> ValueToLoc;

    // Initialized the preferred-location map with illegal locations, to be
    // filled in later.
    for (const auto &VLoc : VLocs)
      if (VLoc.second.Kind == DbgValue::Def)
        for (DbgOpID OpID : VLoc.second.getDbgOpIDs())
          if (!OpID.ID.IsConst)
            ValueToLoc.push_back(
                {DbgOpStore.find(OpID).ID, LocationAndQuality()});

    llvm::sort(ValueToLoc, ValueToLocSort);
    ActiveMLocs.reserve(VLocs.size());
    ActiveVLocs.reserve(VLocs.size());

    // Produce a map of value numbers to the current machine locs they live
    // in. When emulating VarLocBasedImpl, there should only be one
    // location; when not, we get to pick.
    for (auto Location : MTracker->locations()) {
      LocIdx Idx = Location.Idx;
      ValueIDNum &VNum = MLocs[Idx.asU64()];
      if (VNum == ValueIDNum::EmptyValue)
        continue;
      VarLocs.push_back(VNum);

      // Is there a variable that wants a location for this value? If not, skip.
      ValueLocPair Probe(VNum, LocationAndQuality());
      auto VIt = std::lower_bound(ValueToLoc.begin(), ValueToLoc.end(), Probe,
                                  ValueToLocSort);
      if (VIt == ValueToLoc.end() || VIt->first != VNum)
        continue;

      auto &Previous = VIt->second;
      // If this is the first location with that value, pick it. Otherwise,
      // consider whether it's a "longer term" location.
      std::optional<LocationQuality> ReplacementQuality =
          getLocQualityIfBetter(Idx, Previous.getQuality());
      if (ReplacementQuality)
        Previous = LocationAndQuality(Idx, *ReplacementQuality);
    }

    // Now map variables to their picked LocIdxes.
    for (const auto &Var : VLocs) {
      loadVarInloc(MBB, DbgOpStore, ValueToLoc, Var.first, Var.second);
    }
    flushDbgValues(MBB.begin(), &MBB);
  }

  /// Record that \p Var has value \p ID, a value that becomes available
  /// later in the function.
  void addUseBeforeDef(DebugVariableID VarID,
                       const DbgValueProperties &Properties,
                       const SmallVectorImpl<DbgOp> &DbgOps, unsigned Inst) {
    UseBeforeDefs[Inst].emplace_back(DbgOps, VarID, Properties);
    UseBeforeDefVariables.insert(VarID);
  }

  /// After the instruction at index \p Inst and position \p pos has been
  /// processed, check whether it defines a variable value in a use-before-def.
  /// If so, and the variable value hasn't changed since the start of the
  /// block, create a DBG_VALUE.
  void checkInstForNewValues(unsigned Inst, MachineBasicBlock::iterator pos) {
    auto MIt = UseBeforeDefs.find(Inst);
    if (MIt == UseBeforeDefs.end())
      return;

    // Map of values to the locations that store them for every value used by
    // the variables that may have become available.
    SmallDenseMap<ValueIDNum, LocationAndQuality> ValueToLoc;

    // Populate ValueToLoc with illegal default mappings for every value used by
    // any UseBeforeDef variables for this instruction.
    for (auto &Use : MIt->second) {
      if (!UseBeforeDefVariables.count(Use.VarID))
        continue;

      for (DbgOp &Op : Use.Values) {
        assert(!Op.isUndef() && "UseBeforeDef erroneously created for a "
                                "DbgValue with undef values.");
        if (Op.IsConst)
          continue;

        ValueToLoc.insert({Op.ID, LocationAndQuality()});
      }
    }

    // Exit early if we have no DbgValues to produce.
    if (ValueToLoc.empty())
      return;

    // Determine the best location for each desired value.
    for (auto Location : MTracker->locations()) {
      LocIdx Idx = Location.Idx;
      ValueIDNum &LocValueID = Location.Value;

      // Is there a variable that wants a location for this value? If not, skip.
      auto VIt = ValueToLoc.find(LocValueID);
      if (VIt == ValueToLoc.end())
        continue;

      auto &Previous = VIt->second;
      // If this is the first location with that value, pick it. Otherwise,
      // consider whether it's a "longer term" location.
      std::optional<LocationQuality> ReplacementQuality =
          getLocQualityIfBetter(Idx, Previous.getQuality());
      if (ReplacementQuality)
        Previous = LocationAndQuality(Idx, *ReplacementQuality);
    }

    // Using the map of values to locations, produce a final set of values for
    // this variable.
    for (auto &Use : MIt->second) {
      if (!UseBeforeDefVariables.count(Use.VarID))
        continue;

      SmallVector<ResolvedDbgOp> DbgOps;

      for (DbgOp &Op : Use.Values) {
        if (Op.IsConst) {
          DbgOps.push_back(Op.MO);
          continue;
        }
        LocIdx NewLoc = ValueToLoc.find(Op.ID)->second.getLoc();
        if (NewLoc.isIllegal())
          break;
        DbgOps.push_back(NewLoc);
      }

      // If at least one value used by this debug value is no longer available,
      // i.e. one of the values was killed before we finished defining all of
      // the values used by this variable, discard.
      if (DbgOps.size() != Use.Values.size())
        continue;

      // Otherwise, we're good to go.
      auto &[Var, DILoc] = DVMap.lookupDVID(Use.VarID);
      PendingDbgValues.push_back(std::make_pair(
          Use.VarID, MTracker->emitLoc(DbgOps, Var, DILoc, Use.Properties)));
    }
    flushDbgValues(pos, nullptr);
  }

  /// Helper to move created DBG_VALUEs into Transfers collection.
  void flushDbgValues(MachineBasicBlock::iterator Pos, MachineBasicBlock *MBB) {
    if (PendingDbgValues.size() == 0)
      return;

    // Pick out the instruction start position.
    MachineBasicBlock::instr_iterator BundleStart;
    if (MBB && Pos == MBB->begin())
      BundleStart = MBB->instr_begin();
    else
      BundleStart = getBundleStart(Pos->getIterator());

    Transfers.push_back({BundleStart, MBB, PendingDbgValues});
    PendingDbgValues.clear();
  }

  bool isEntryValueVariable(const DebugVariable &Var,
                            const DIExpression *Expr) const {
    if (!Var.getVariable()->isParameter())
      return false;

    if (Var.getInlinedAt())
      return false;

    if (Expr->getNumElements() > 0 && !Expr->isDeref())
      return false;

    return true;
  }

  bool isEntryValueValue(const ValueIDNum &Val) const {
    // Must be in entry block (block number zero), and be a PHI / live-in value.
    if (Val.getBlock() || !Val.isPHI())
      return false;

    // Entry values must enter in a register.
    if (MTracker->isSpill(Val.getLoc()))
      return false;

    Register SP = TLI->getStackPointerRegisterToSaveRestore();
    Register FP = TRI.getFrameRegister(MF);
    Register Reg = MTracker->LocIdxToLocID[Val.getLoc()];
    return Reg != SP && Reg != FP;
  }

  bool recoverAsEntryValue(DebugVariableID VarID,
                           const DbgValueProperties &Prop,
                           const ValueIDNum &Num) {
    // Is this variable location a candidate to be an entry value. First,
    // should we be trying this at all?
    if (!ShouldEmitDebugEntryValues)
      return false;

    const DIExpression *DIExpr = Prop.DIExpr;

    // We don't currently emit entry values for DBG_VALUE_LISTs.
    if (Prop.IsVariadic) {
      // If this debug value can be converted to be non-variadic, then do so;
      // otherwise give up.
      auto NonVariadicExpression =
          DIExpression::convertToNonVariadicExpression(DIExpr);
      if (!NonVariadicExpression)
        return false;
      DIExpr = *NonVariadicExpression;
    }

    auto &[Var, DILoc] = DVMap.lookupDVID(VarID);

    // Is the variable appropriate for entry values (i.e., is a parameter).
    if (!isEntryValueVariable(Var, DIExpr))
      return false;

    // Is the value assigned to this variable still the entry value?
    if (!isEntryValueValue(Num))
      return false;

    // Emit a variable location using an entry value expression.
    DIExpression *NewExpr =
        DIExpression::prepend(DIExpr, DIExpression::EntryValue);
    Register Reg = MTracker->LocIdxToLocID[Num.getLoc()];
    MachineOperand MO = MachineOperand::CreateReg(Reg, false);
    PendingDbgValues.push_back(std::make_pair(
        VarID, &*emitMOLoc(MO, Var, {NewExpr, Prop.Indirect, false})));
    return true;
  }

  /// Change a variable value after encountering a DBG_VALUE inside a block.
  void redefVar(const MachineInstr &MI) {
    DebugVariable Var(MI.getDebugVariable(), MI.getDebugExpression(),
                      MI.getDebugLoc()->getInlinedAt());
    DbgValueProperties Properties(MI);
    DebugVariableID VarID = DVMap.getDVID(Var);

    // Ignore non-register locations, we don't transfer those.
    if (MI.isUndefDebugValue() ||
        all_of(MI.debug_operands(),
               [](const MachineOperand &MO) { return !MO.isReg(); })) {
      auto It = ActiveVLocs.find(VarID);
      if (It != ActiveVLocs.end()) {
        for (LocIdx Loc : It->second.loc_indices())
          ActiveMLocs[Loc].erase(VarID);
        ActiveVLocs.erase(It);
      }
      // Any use-before-defs no longer apply.
      UseBeforeDefVariables.erase(VarID);
      return;
    }

    SmallVector<ResolvedDbgOp> NewLocs;
    for (const MachineOperand &MO : MI.debug_operands()) {
      if (MO.isReg()) {
        // Any undef regs have already been filtered out above.
        Register Reg = MO.getReg();
        LocIdx NewLoc = MTracker->getRegMLoc(Reg);
        NewLocs.push_back(NewLoc);
      } else {
        NewLocs.push_back(MO);
      }
    }

    redefVar(MI, Properties, NewLocs);
  }

  /// Handle a change in variable location within a block. Terminate the
  /// variables current location, and record the value it now refers to, so
  /// that we can detect location transfers later on.
  void redefVar(const MachineInstr &MI, const DbgValueProperties &Properties,
                SmallVectorImpl<ResolvedDbgOp> &NewLocs) {
    DebugVariable Var(MI.getDebugVariable(), MI.getDebugExpression(),
                      MI.getDebugLoc()->getInlinedAt());
    DebugVariableID VarID = DVMap.getDVID(Var);
    // Any use-before-defs no longer apply.
    UseBeforeDefVariables.erase(VarID);

    // Erase any previous location.
    auto It = ActiveVLocs.find(VarID);
    if (It != ActiveVLocs.end()) {
      for (LocIdx Loc : It->second.loc_indices())
        ActiveMLocs[Loc].erase(VarID);
    }

    // If there _is_ no new location, all we had to do was erase.
    if (NewLocs.empty()) {
      if (It != ActiveVLocs.end())
        ActiveVLocs.erase(It);
      return;
    }

    SmallVector<std::pair<LocIdx, DebugVariableID>> LostMLocs;
    for (ResolvedDbgOp &Op : NewLocs) {
      if (Op.IsConst)
        continue;

      LocIdx NewLoc = Op.Loc;

      // Check whether our local copy of values-by-location in #VarLocs is out
      // of date. Wipe old tracking data for the location if it's been clobbered
      // in the meantime.
      if (MTracker->readMLoc(NewLoc) != VarLocs[NewLoc.asU64()]) {
        for (const auto &P : ActiveMLocs[NewLoc]) {
          auto LostVLocIt = ActiveVLocs.find(P);
          if (LostVLocIt != ActiveVLocs.end()) {
            for (LocIdx Loc : LostVLocIt->second.loc_indices()) {
              // Every active variable mapping for NewLoc will be cleared, no
              // need to track individual variables.
              if (Loc == NewLoc)
                continue;
              LostMLocs.emplace_back(Loc, P);
            }
          }
          ActiveVLocs.erase(P);
        }
        for (const auto &LostMLoc : LostMLocs)
          ActiveMLocs[LostMLoc.first].erase(LostMLoc.second);
        LostMLocs.clear();
        It = ActiveVLocs.find(VarID);
        ActiveMLocs[NewLoc.asU64()].clear();
        VarLocs[NewLoc.asU64()] = MTracker->readMLoc(NewLoc);
      }

      ActiveMLocs[NewLoc].insert(VarID);
    }

    if (It == ActiveVLocs.end()) {
      ActiveVLocs.insert(
          std::make_pair(VarID, ResolvedDbgValue(NewLocs, Properties)));
    } else {
      It->second.Ops.assign(NewLocs);
      It->second.Properties = Properties;
    }
  }

  /// Account for a location \p mloc being clobbered. Examine the variable
  /// locations that will be terminated: and try to recover them by using
  /// another location. Optionally, given \p MakeUndef, emit a DBG_VALUE to
  /// explicitly terminate a location if it can't be recovered.
  void clobberMloc(LocIdx MLoc, MachineBasicBlock::iterator Pos,
                   bool MakeUndef = true) {
    auto ActiveMLocIt = ActiveMLocs.find(MLoc);
    if (ActiveMLocIt == ActiveMLocs.end())
      return;

    // What was the old variable value?
    ValueIDNum OldValue = VarLocs[MLoc.asU64()];
    clobberMloc(MLoc, OldValue, Pos, MakeUndef);
  }
  /// Overload that takes an explicit value \p OldValue for when the value in
  /// \p MLoc has changed and the TransferTracker's locations have not been
  /// updated yet.
  void clobberMloc(LocIdx MLoc, ValueIDNum OldValue,
                   MachineBasicBlock::iterator Pos, bool MakeUndef = true) {
    auto ActiveMLocIt = ActiveMLocs.find(MLoc);
    if (ActiveMLocIt == ActiveMLocs.end())
      return;

    VarLocs[MLoc.asU64()] = ValueIDNum::EmptyValue;

    // Examine the remaining variable locations: if we can find the same value
    // again, we can recover the location.
    std::optional<LocIdx> NewLoc;
    for (auto Loc : MTracker->locations())
      if (Loc.Value == OldValue)
        NewLoc = Loc.Idx;

    // If there is no location, and we weren't asked to make the variable
    // explicitly undef, then stop here.
    if (!NewLoc && !MakeUndef) {
      // Try and recover a few more locations with entry values.
      for (DebugVariableID VarID : ActiveMLocIt->second) {
        auto &Prop = ActiveVLocs.find(VarID)->second.Properties;
        recoverAsEntryValue(VarID, Prop, OldValue);
      }
      flushDbgValues(Pos, nullptr);
      return;
    }

    // Examine all the variables based on this location.
    DenseSet<DebugVariableID> NewMLocs;
    // If no new location has been found, every variable that depends on this
    // MLoc is dead, so end their existing MLoc->Var mappings as well.
    SmallVector<std::pair<LocIdx, DebugVariableID>> LostMLocs;
    for (DebugVariableID VarID : ActiveMLocIt->second) {
      auto ActiveVLocIt = ActiveVLocs.find(VarID);
      // Re-state the variable location: if there's no replacement then NewLoc
      // is std::nullopt and a $noreg DBG_VALUE will be created. Otherwise, a
      // DBG_VALUE identifying the alternative location will be emitted.
      const DbgValueProperties &Properties = ActiveVLocIt->second.Properties;

      // Produce the new list of debug ops - an empty list if no new location
      // was found, or the existing list with the substitution MLoc -> NewLoc
      // otherwise.
      SmallVector<ResolvedDbgOp> DbgOps;
      if (NewLoc) {
        ResolvedDbgOp OldOp(MLoc);
        ResolvedDbgOp NewOp(*NewLoc);
        // Insert illegal ops to overwrite afterwards.
        DbgOps.insert(DbgOps.begin(), ActiveVLocIt->second.Ops.size(),
                      ResolvedDbgOp(LocIdx::MakeIllegalLoc()));
        replace_copy(ActiveVLocIt->second.Ops, DbgOps.begin(), OldOp, NewOp);
      }

      auto &[Var, DILoc] = DVMap.lookupDVID(VarID);
      PendingDbgValues.push_back(std::make_pair(
          VarID, &*MTracker->emitLoc(DbgOps, Var, DILoc, Properties)));

      // Update machine locations <=> variable locations maps. Defer updating
      // ActiveMLocs to avoid invalidating the ActiveMLocIt iterator.
      if (!NewLoc) {
        for (LocIdx Loc : ActiveVLocIt->second.loc_indices()) {
          if (Loc != MLoc)
            LostMLocs.emplace_back(Loc, VarID);
        }
        ActiveVLocs.erase(ActiveVLocIt);
      } else {
        ActiveVLocIt->second.Ops = DbgOps;
        NewMLocs.insert(VarID);
      }
    }

    // Remove variables from ActiveMLocs if they no longer use any other MLocs
    // due to being killed by this clobber.
    for (auto &LocVarIt : LostMLocs) {
      auto LostMLocIt = ActiveMLocs.find(LocVarIt.first);
      assert(LostMLocIt != ActiveMLocs.end() &&
             "Variable was using this MLoc, but ActiveMLocs[MLoc] has no "
             "entries?");
      LostMLocIt->second.erase(LocVarIt.second);
    }

    // We lazily track what locations have which values; if we've found a new
    // location for the clobbered value, remember it.
    if (NewLoc)
      VarLocs[NewLoc->asU64()] = OldValue;

    flushDbgValues(Pos, nullptr);

    // Commit ActiveMLoc changes.
    ActiveMLocIt->second.clear();
    if (!NewMLocs.empty())
      for (DebugVariableID VarID : NewMLocs)
        ActiveMLocs[*NewLoc].insert(VarID);
  }

  /// Transfer variables based on \p Src to be based on \p Dst. This handles
  /// both register copies as well as spills and restores. Creates DBG_VALUEs
  /// describing the movement.
  void transferMlocs(LocIdx Src, LocIdx Dst, MachineBasicBlock::iterator Pos) {
    // Does Src still contain the value num we expect? If not, it's been
    // clobbered in the meantime, and our variable locations are stale.
    if (VarLocs[Src.asU64()] != MTracker->readMLoc(Src))
      return;

    // assert(ActiveMLocs[Dst].size() == 0);
    //^^^ Legitimate scenario on account of un-clobbered slot being assigned to?

    // Move set of active variables from one location to another.
    auto MovingVars = ActiveMLocs[Src];
    ActiveMLocs[Dst].insert(MovingVars.begin(), MovingVars.end());
    VarLocs[Dst.asU64()] = VarLocs[Src.asU64()];

    // For each variable based on Src; create a location at Dst.
    ResolvedDbgOp SrcOp(Src);
    ResolvedDbgOp DstOp(Dst);
    for (DebugVariableID VarID : MovingVars) {
      auto ActiveVLocIt = ActiveVLocs.find(VarID);
      assert(ActiveVLocIt != ActiveVLocs.end());

      // Update all instances of Src in the variable's tracked values to Dst.
      std::replace(ActiveVLocIt->second.Ops.begin(),
                   ActiveVLocIt->second.Ops.end(), SrcOp, DstOp);

      auto &[Var, DILoc] = DVMap.lookupDVID(VarID);
      MachineInstr *MI = MTracker->emitLoc(ActiveVLocIt->second.Ops, Var, DILoc,
                                           ActiveVLocIt->second.Properties);
      PendingDbgValues.push_back(std::make_pair(VarID, MI));
    }
    ActiveMLocs[Src].clear();
    flushDbgValues(Pos, nullptr);

    // XXX XXX XXX "pretend to be old LDV" means dropping all tracking data
    // about the old location.
    if (EmulateOldLDV)
      VarLocs[Src.asU64()] = ValueIDNum::EmptyValue;
  }

  MachineInstrBuilder emitMOLoc(const MachineOperand &MO,
                                const DebugVariable &Var,
                                const DbgValueProperties &Properties) {
    DebugLoc DL = DILocation::get(Var.getVariable()->getContext(), 0, 0,
                                  Var.getVariable()->getScope(),
                                  const_cast<DILocation *>(Var.getInlinedAt()));
    auto MIB = BuildMI(MF, DL, TII->get(TargetOpcode::DBG_VALUE));
    MIB.add(MO);
    if (Properties.Indirect)
      MIB.addImm(0);
    else
      MIB.addReg(0);
    MIB.addMetadata(Var.getVariable());
    MIB.addMetadata(Properties.DIExpr);
    return MIB;
  }
};

//===----------------------------------------------------------------------===//
//            Implementation
//===----------------------------------------------------------------------===//

ValueIDNum ValueIDNum::EmptyValue = {UINT_MAX, UINT_MAX, UINT_MAX};
ValueIDNum ValueIDNum::TombstoneValue = {UINT_MAX, UINT_MAX, UINT_MAX - 1};

#ifndef NDEBUG
void ResolvedDbgOp::dump(const MLocTracker *MTrack) const {
  if (IsConst) {
    dbgs() << MO;
  } else {
    dbgs() << MTrack->LocIdxToName(Loc);
  }
}
void DbgOp::dump(const MLocTracker *MTrack) const {
  if (IsConst) {
    dbgs() << MO;
  } else if (!isUndef()) {
    dbgs() << MTrack->IDAsString(ID);
  }
}
void DbgOpID::dump(const MLocTracker *MTrack, const DbgOpIDMap *OpStore) const {
  if (!OpStore) {
    dbgs() << "ID(" << asU32() << ")";
  } else {
    OpStore->find(*this).dump(MTrack);
  }
}
void DbgValue::dump(const MLocTracker *MTrack,
                    const DbgOpIDMap *OpStore) const {
  if (Kind == NoVal) {
    dbgs() << "NoVal(" << BlockNo << ")";
  } else if (Kind == VPHI || Kind == Def) {
    if (Kind == VPHI)
      dbgs() << "VPHI(" << BlockNo << ",";
    else
      dbgs() << "Def(";
    for (unsigned Idx = 0; Idx < getDbgOpIDs().size(); ++Idx) {
      getDbgOpID(Idx).dump(MTrack, OpStore);
      if (Idx != 0)
        dbgs() << ",";
    }
    dbgs() << ")";
  }
  if (Properties.Indirect)
    dbgs() << " indir";
  if (Properties.DIExpr)
    dbgs() << " " << *Properties.DIExpr;
}
#endif

MLocTracker::MLocTracker(MachineFunction &MF, const TargetInstrInfo &TII,
                         const TargetRegisterInfo &TRI,
                         const TargetLowering &TLI)
    : MF(MF), TII(TII), TRI(TRI), TLI(TLI),
      LocIdxToIDNum(ValueIDNum::EmptyValue), LocIdxToLocID(0) {
  NumRegs = TRI.getNumRegs();
  reset();
  LocIDToLocIdx.resize(NumRegs, LocIdx::MakeIllegalLoc());
  assert(NumRegs < (1u << NUM_LOC_BITS)); // Detect bit packing failure

  // Always track SP. This avoids the implicit clobbering caused by regmasks
  // from affectings its values. (LiveDebugValues disbelieves calls and
  // regmasks that claim to clobber SP).
  Register SP = TLI.getStackPointerRegisterToSaveRestore();
  if (SP) {
    unsigned ID = getLocID(SP);
    (void)lookupOrTrackRegister(ID);

    for (MCRegAliasIterator RAI(SP, &TRI, true); RAI.isValid(); ++RAI)
      SPAliases.insert(*RAI);
  }

  // Build some common stack positions -- full registers being spilt to the
  // stack.
  StackSlotIdxes.insert({{8, 0}, 0});
  StackSlotIdxes.insert({{16, 0}, 1});
  StackSlotIdxes.insert({{32, 0}, 2});
  StackSlotIdxes.insert({{64, 0}, 3});
  StackSlotIdxes.insert({{128, 0}, 4});
  StackSlotIdxes.insert({{256, 0}, 5});
  StackSlotIdxes.insert({{512, 0}, 6});

  // Traverse all the subregister idxes, and ensure there's an index for them.
  // Duplicates are no problem: we're interested in their position in the
  // stack slot, we don't want to type the slot.
  for (unsigned int I = 1; I < TRI.getNumSubRegIndices(); ++I) {
    unsigned Size = TRI.getSubRegIdxSize(I);
    unsigned Offs = TRI.getSubRegIdxOffset(I);
    unsigned Idx = StackSlotIdxes.size();

    // Some subregs have -1, -2 and so forth fed into their fields, to mean
    // special backend things. Ignore those.
    if (Size > 60000 || Offs > 60000)
      continue;

    StackSlotIdxes.insert({{Size, Offs}, Idx});
  }

  // There may also be strange register class sizes (think x86 fp80s).
  for (const TargetRegisterClass *RC : TRI.regclasses()) {
    unsigned Size = TRI.getRegSizeInBits(*RC);

    // We might see special reserved values as sizes, and classes for other
    // stuff the machine tries to model. If it's more than 512 bits, then it
    // is very unlikely to be a register than can be spilt.
    if (Size > 512)
      continue;

    unsigned Idx = StackSlotIdxes.size();
    StackSlotIdxes.insert({{Size, 0}, Idx});
  }

  for (auto &Idx : StackSlotIdxes)
    StackIdxesToPos[Idx.second] = Idx.first;

  NumSlotIdxes = StackSlotIdxes.size();
}

LocIdx MLocTracker::trackRegister(unsigned ID) {
  assert(ID != 0);
  LocIdx NewIdx = LocIdx(LocIdxToIDNum.size());
  LocIdxToIDNum.grow(NewIdx);
  LocIdxToLocID.grow(NewIdx);

  // Default: it's an mphi.
  ValueIDNum ValNum = {CurBB, 0, NewIdx};
  // Was this reg ever touched by a regmask?
  for (const auto &MaskPair : reverse(Masks)) {
    if (MaskPair.first->clobbersPhysReg(ID)) {
      // There was an earlier def we skipped.
      ValNum = {CurBB, MaskPair.second, NewIdx};
      break;
    }
  }

  LocIdxToIDNum[NewIdx] = ValNum;
  LocIdxToLocID[NewIdx] = ID;
  return NewIdx;
}

void MLocTracker::writeRegMask(const MachineOperand *MO, unsigned CurBB,
                               unsigned InstID) {
  // Def any register we track have that isn't preserved. The regmask
  // terminates the liveness of a register, meaning its value can't be
  // relied upon -- we represent this by giving it a new value.
  for (auto Location : locations()) {
    unsigned ID = LocIdxToLocID[Location.Idx];
    // Don't clobber SP, even if the mask says it's clobbered.
    if (ID < NumRegs && !SPAliases.count(ID) && MO->clobbersPhysReg(ID))
      defReg(ID, CurBB, InstID);
  }
  Masks.push_back(std::make_pair(MO, InstID));
}

std::optional<SpillLocationNo> MLocTracker::getOrTrackSpillLoc(SpillLoc L) {
  SpillLocationNo SpillID(SpillLocs.idFor(L));

  if (SpillID.id() == 0) {
    // If there is no location, and we have reached the limit of how many stack
    // slots to track, then don't track this one.
    if (SpillLocs.size() >= StackWorkingSetLimit)
      return std::nullopt;

    // Spill location is untracked: create record for this one, and all
    // subregister slots too.
    SpillID = SpillLocationNo(SpillLocs.insert(L));
    for (unsigned StackIdx = 0; StackIdx < NumSlotIdxes; ++StackIdx) {
      unsigned L = getSpillIDWithIdx(SpillID, StackIdx);
      LocIdx Idx = LocIdx(LocIdxToIDNum.size()); // New idx
      LocIdxToIDNum.grow(Idx);
      LocIdxToLocID.grow(Idx);
      LocIDToLocIdx.push_back(Idx);
      LocIdxToLocID[Idx] = L;
      // Initialize to PHI value; corresponds to the location's live-in value
      // during transfer function construction.
      LocIdxToIDNum[Idx] = ValueIDNum(CurBB, 0, Idx);
    }
  }
  return SpillID;
}

std::string MLocTracker::LocIdxToName(LocIdx Idx) const {
  unsigned ID = LocIdxToLocID[Idx];
  if (ID >= NumRegs) {
    StackSlotPos Pos = locIDToSpillIdx(ID);
    ID -= NumRegs;
    unsigned Slot = ID / NumSlotIdxes;
    return Twine("slot ")
        .concat(Twine(Slot).concat(Twine(" sz ").concat(Twine(Pos.first)
        .concat(Twine(" offs ").concat(Twine(Pos.second))))))
        .str();
  } else {
    return TRI.getRegAsmName(ID).str();
  }
}

std::string MLocTracker::IDAsString(const ValueIDNum &Num) const {
  std::string DefName = LocIdxToName(Num.getLoc());
  return Num.asString(DefName);
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void MLocTracker::dump() {
  for (auto Location : locations()) {
    std::string MLocName = LocIdxToName(Location.Value.getLoc());
    std::string DefName = Location.Value.asString(MLocName);
    dbgs() << LocIdxToName(Location.Idx) << " --> " << DefName << "\n";
  }
}

LLVM_DUMP_METHOD void MLocTracker::dump_mloc_map() {
  for (auto Location : locations()) {
    std::string foo = LocIdxToName(Location.Idx);
    dbgs() << "Idx " << Location.Idx.asU64() << " " << foo << "\n";
  }
}
#endif

MachineInstrBuilder
MLocTracker::emitLoc(const SmallVectorImpl<ResolvedDbgOp> &DbgOps,
                     const DebugVariable &Var, const DILocation *DILoc,
                     const DbgValueProperties &Properties) {
  DebugLoc DL = DebugLoc(DILoc);

  const MCInstrDesc &Desc = Properties.IsVariadic
                                ? TII.get(TargetOpcode::DBG_VALUE_LIST)
                                : TII.get(TargetOpcode::DBG_VALUE);

#ifdef EXPENSIVE_CHECKS
  assert(all_of(DbgOps,
                [](const ResolvedDbgOp &Op) {
                  return Op.IsConst || !Op.Loc.isIllegal();
                }) &&
         "Did not expect illegal ops in DbgOps.");
  assert((DbgOps.size() == 0 ||
          DbgOps.size() == Properties.getLocationOpCount()) &&
         "Expected to have either one DbgOp per MI LocationOp, or none.");
#endif

  auto GetRegOp = [](unsigned Reg) -> MachineOperand {
    return MachineOperand::CreateReg(
        /* Reg */ Reg, /* isDef */ false, /* isImp */ false,
        /* isKill */ false, /* isDead */ false,
        /* isUndef */ false, /* isEarlyClobber */ false,
        /* SubReg */ 0, /* isDebug */ true);
  };

  SmallVector<MachineOperand> MOs;

  auto EmitUndef = [&]() {
    MOs.clear();
    MOs.assign(Properties.getLocationOpCount(), GetRegOp(0));
    return BuildMI(MF, DL, Desc, false, MOs, Var.getVariable(),
                   Properties.DIExpr);
  };

  // Don't bother passing any real operands to BuildMI if any of them would be
  // $noreg.
  if (DbgOps.empty())
    return EmitUndef();

  bool Indirect = Properties.Indirect;

  const DIExpression *Expr = Properties.DIExpr;

  assert(DbgOps.size() == Properties.getLocationOpCount());

  // If all locations are valid, accumulate them into our list of
  // MachineOperands. For any spilled locations, either update the indirectness
  // register or apply the appropriate transformations in the DIExpression.
  for (size_t Idx = 0; Idx < Properties.getLocationOpCount(); ++Idx) {
    const ResolvedDbgOp &Op = DbgOps[Idx];

    if (Op.IsConst) {
      MOs.push_back(Op.MO);
      continue;
    }

    LocIdx MLoc = Op.Loc;
    unsigned LocID = LocIdxToLocID[MLoc];
    if (LocID >= NumRegs) {
      SpillLocationNo SpillID = locIDToSpill(LocID);
      StackSlotPos StackIdx = locIDToSpillIdx(LocID);
      unsigned short Offset = StackIdx.second;

      // TODO: support variables that are located in spill slots, with non-zero
      // offsets from the start of the spill slot. It would require some more
      // complex DIExpression calculations. This doesn't seem to be produced by
      // LLVM right now, so don't try and support it.
      // Accept no-subregister slots and subregisters where the offset is zero.
      // The consumer should already have type information to work out how large
      // the variable is.
      if (Offset == 0) {
        const SpillLoc &Spill = SpillLocs[SpillID.id()];
        unsigned Base = Spill.SpillBase;

        // There are several ways we can dereference things, and several inputs
        // to consider:
        // * NRVO variables will appear with IsIndirect set, but should have
        //   nothing else in their DIExpressions,
        // * Variables with DW_OP_stack_value in their expr already need an
        //   explicit dereference of the stack location,
        // * Values that don't match the variable size need DW_OP_deref_size,
        // * Everything else can just become a simple location expression.

        // We need to use deref_size whenever there's a mismatch between the
        // size of value and the size of variable portion being read.
        // Additionally, we should use it whenever dealing with stack_value
        // fragments, to avoid the consumer having to determine the deref size
        // from DW_OP_piece.
        bool UseDerefSize = false;
        unsigned ValueSizeInBits = getLocSizeInBits(MLoc);
        unsigned DerefSizeInBytes = ValueSizeInBits / 8;
        if (auto Fragment = Var.getFragment()) {
          unsigned VariableSizeInBits = Fragment->SizeInBits;
          if (VariableSizeInBits != ValueSizeInBits || Expr->isComplex())
            UseDerefSize = true;
        } else if (auto Size = Var.getVariable()->getSizeInBits()) {
          if (*Size != ValueSizeInBits) {
            UseDerefSize = true;
          }
        }

        SmallVector<uint64_t, 5> OffsetOps;
        TRI.getOffsetOpcodes(Spill.SpillOffset, OffsetOps);
        bool StackValue = false;

        if (Properties.Indirect) {
          // This is something like an NRVO variable, where the pointer has been
          // spilt to the stack. It should end up being a memory location, with
          // the pointer to the variable loaded off the stack with a deref:
          assert(!Expr->isImplicit());
          OffsetOps.push_back(dwarf::DW_OP_deref);
        } else if (UseDerefSize && Expr->isSingleLocationExpression()) {
          // TODO: Figure out how to handle deref size issues for variadic
          // values.
          // We're loading a value off the stack that's not the same size as the
          // variable. Add / subtract stack offset, explicitly deref with a
          // size, and add DW_OP_stack_value if not already present.
          OffsetOps.push_back(dwarf::DW_OP_deref_size);
          OffsetOps.push_back(DerefSizeInBytes);
          StackValue = true;
        } else if (Expr->isComplex() || Properties.IsVariadic) {
          // A variable with no size ambiguity, but with extra elements in it's
          // expression. Manually dereference the stack location.
          OffsetOps.push_back(dwarf::DW_OP_deref);
        } else {
          // A plain value that has been spilt to the stack, with no further
          // context. Request a location expression, marking the DBG_VALUE as
          // IsIndirect.
          Indirect = true;
        }

        Expr = DIExpression::appendOpsToArg(Expr, OffsetOps, Idx, StackValue);
        MOs.push_back(GetRegOp(Base));
      } else {
        // This is a stack location with a weird subregister offset: emit an
        // undef DBG_VALUE instead.
        return EmitUndef();
      }
    } else {
      // Non-empty, non-stack slot, must be a plain register.
      MOs.push_back(GetRegOp(LocID));
    }
  }

  return BuildMI(MF, DL, Desc, Indirect, MOs, Var.getVariable(), Expr);
}

/// Default construct and initialize the pass.
InstrRefBasedLDV::InstrRefBasedLDV() = default;

bool InstrRefBasedLDV::isCalleeSaved(LocIdx L) const {
  unsigned Reg = MTracker->LocIdxToLocID[L];
  return isCalleeSavedReg(Reg);
}
bool InstrRefBasedLDV::isCalleeSavedReg(Register R) const {
  for (MCRegAliasIterator RAI(R, TRI, true); RAI.isValid(); ++RAI)
    if (CalleeSavedRegs.test(*RAI))
      return true;
  return false;
}

//===----------------------------------------------------------------------===//
//            Debug Range Extension Implementation
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
// Something to restore in the future.
// void InstrRefBasedLDV::printVarLocInMBB(..)
#endif

std::optional<SpillLocationNo>
InstrRefBasedLDV::extractSpillBaseRegAndOffset(const MachineInstr &MI) {
  assert(MI.hasOneMemOperand() &&
         "Spill instruction does not have exactly one memory operand?");
  auto MMOI = MI.memoperands_begin();
  const PseudoSourceValue *PVal = (*MMOI)->getPseudoValue();
  assert(PVal->kind() == PseudoSourceValue::FixedStack &&
         "Inconsistent memory operand in spill instruction");
  int FI = cast<FixedStackPseudoSourceValue>(PVal)->getFrameIndex();
  const MachineBasicBlock *MBB = MI.getParent();
  Register Reg;
  StackOffset Offset = TFI->getFrameIndexReference(*MBB->getParent(), FI, Reg);
  return MTracker->getOrTrackSpillLoc({Reg, Offset});
}

std::optional<LocIdx>
InstrRefBasedLDV::findLocationForMemOperand(const MachineInstr &MI) {
  std::optional<SpillLocationNo> SpillLoc = extractSpillBaseRegAndOffset(MI);
  if (!SpillLoc)
    return std::nullopt;

  // Where in the stack slot is this value defined -- i.e., what size of value
  // is this? An important question, because it could be loaded into a register
  // from the stack at some point. Happily the memory operand will tell us
  // the size written to the stack.
  auto *MemOperand = *MI.memoperands_begin();
  LocationSize SizeInBits = MemOperand->getSizeInBits();
  assert(SizeInBits.hasValue() && "Expected to find a valid size!");

  // Find that position in the stack indexes we're tracking.
  auto IdxIt = MTracker->StackSlotIdxes.find({SizeInBits.getValue(), 0});
  if (IdxIt == MTracker->StackSlotIdxes.end())
    // That index is not tracked. This is suprising, and unlikely to ever
    // occur, but the safe action is to indicate the variable is optimised out.
    return std::nullopt;

  unsigned SpillID = MTracker->getSpillIDWithIdx(*SpillLoc, IdxIt->second);
  return MTracker->getSpillMLoc(SpillID);
}

/// End all previous ranges related to @MI and start a new range from @MI
/// if it is a DBG_VALUE instr.
bool InstrRefBasedLDV::transferDebugValue(const MachineInstr &MI) {
  if (!MI.isDebugValue())
    return false;

  assert(MI.getDebugVariable()->isValidLocationForIntrinsic(MI.getDebugLoc()) &&
         "Expected inlined-at fields to agree");

  // If there are no instructions in this lexical scope, do no location tracking
  // at all, this variable shouldn't get a legitimate location range.
  auto *Scope = LS.findLexicalScope(MI.getDebugLoc().get());
  if (Scope == nullptr)
    return true; // handled it; by doing nothing

  // MLocTracker needs to know that this register is read, even if it's only
  // read by a debug inst.
  for (const MachineOperand &MO : MI.debug_operands())
    if (MO.isReg() && MO.getReg() != 0)
      (void)MTracker->readReg(MO.getReg());

  // If we're preparing for the second analysis (variables), the machine value
  // locations are already solved, and we report this DBG_VALUE and the value
  // it refers to to VLocTracker.
  if (VTracker) {
    SmallVector<DbgOpID> DebugOps;
    // Feed defVar the new variable location, or if this is a DBG_VALUE $noreg,
    // feed defVar None.
    if (!MI.isUndefDebugValue()) {
      for (const MachineOperand &MO : MI.debug_operands()) {
        // There should be no undef registers here, as we've screened for undef
        // debug values.
        if (MO.isReg()) {
          DebugOps.push_back(DbgOpStore.insert(MTracker->readReg(MO.getReg())));
        } else if (MO.isImm() || MO.isFPImm() || MO.isCImm()) {
          DebugOps.push_back(DbgOpStore.insert(MO));
        } else {
          llvm_unreachable("Unexpected debug operand type.");
        }
      }
    }
    VTracker->defVar(MI, DbgValueProperties(MI), DebugOps);
  }

  // If performing final tracking of transfers, report this variable definition
  // to the TransferTracker too.
  if (TTracker)
    TTracker->redefVar(MI);
  return true;
}

std::optional<ValueIDNum> InstrRefBasedLDV::getValueForInstrRef(
    unsigned InstNo, unsigned OpNo, MachineInstr &MI,
    const FuncValueTable *MLiveOuts, const FuncValueTable *MLiveIns) {
  // Various optimizations may have happened to the value during codegen,
  // recorded in the value substitution table. Apply any substitutions to
  // the instruction / operand number in this DBG_INSTR_REF, and collect
  // any subregister extractions performed during optimization.
  const MachineFunction &MF = *MI.getParent()->getParent();

  // Create dummy substitution with Src set, for lookup.
  auto SoughtSub =
      MachineFunction::DebugSubstitution({InstNo, OpNo}, {0, 0}, 0);

  SmallVector<unsigned, 4> SeenSubregs;
  auto LowerBoundIt = llvm::lower_bound(MF.DebugValueSubstitutions, SoughtSub);
  while (LowerBoundIt != MF.DebugValueSubstitutions.end() &&
         LowerBoundIt->Src == SoughtSub.Src) {
    std::tie(InstNo, OpNo) = LowerBoundIt->Dest;
    SoughtSub.Src = LowerBoundIt->Dest;
    if (unsigned Subreg = LowerBoundIt->Subreg)
      SeenSubregs.push_back(Subreg);
    LowerBoundIt = llvm::lower_bound(MF.DebugValueSubstitutions, SoughtSub);
  }

  // Default machine value number is <None> -- if no instruction defines
  // the corresponding value, it must have been optimized out.
  std::optional<ValueIDNum> NewID;

  // Try to lookup the instruction number, and find the machine value number
  // that it defines. It could be an instruction, or a PHI.
  auto InstrIt = DebugInstrNumToInstr.find(InstNo);
  auto PHIIt = llvm::lower_bound(DebugPHINumToValue, InstNo);
  if (InstrIt != DebugInstrNumToInstr.end()) {
    const MachineInstr &TargetInstr = *InstrIt->second.first;
    uint64_t BlockNo = TargetInstr.getParent()->getNumber();

    // Pick out the designated operand. It might be a memory reference, if
    // a register def was folded into a stack store.
    if (OpNo == MachineFunction::DebugOperandMemNumber &&
        TargetInstr.hasOneMemOperand()) {
      std::optional<LocIdx> L = findLocationForMemOperand(TargetInstr);
      if (L)
        NewID = ValueIDNum(BlockNo, InstrIt->second.second, *L);
    } else if (OpNo != MachineFunction::DebugOperandMemNumber) {
      // Permit the debug-info to be completely wrong: identifying a nonexistant
      // operand, or one that is not a register definition, means something
      // unexpected happened during optimisation. Broken debug-info, however,
      // shouldn't crash the compiler -- instead leave the variable value as
      // None, which will make it appear "optimised out".
      if (OpNo < TargetInstr.getNumOperands()) {
        const MachineOperand &MO = TargetInstr.getOperand(OpNo);

        if (MO.isReg() && MO.isDef() && MO.getReg()) {
          unsigned LocID = MTracker->getLocID(MO.getReg());
          LocIdx L = MTracker->LocIDToLocIdx[LocID];
          NewID = ValueIDNum(BlockNo, InstrIt->second.second, L);
        }
      }

      if (!NewID) {
        LLVM_DEBUG(
            { dbgs() << "Seen instruction reference to illegal operand\n"; });
      }
    }
    // else: NewID is left as None.
  } else if (PHIIt != DebugPHINumToValue.end() && PHIIt->InstrNum == InstNo) {
    // It's actually a PHI value. Which value it is might not be obvious, use
    // the resolver helper to find out.
    assert(MLiveOuts && MLiveIns);
    NewID = resolveDbgPHIs(*MI.getParent()->getParent(), *MLiveOuts, *MLiveIns,
                           MI, InstNo);
  }

  // Apply any subregister extractions, in reverse. We might have seen code
  // like this:
  //    CALL64 @foo, implicit-def $rax
  //    %0:gr64 = COPY $rax
  //    %1:gr32 = COPY %0.sub_32bit
  //    %2:gr16 = COPY %1.sub_16bit
  //    %3:gr8  = COPY %2.sub_8bit
  // In which case each copy would have been recorded as a substitution with
  // a subregister qualifier. Apply those qualifiers now.
  if (NewID && !SeenSubregs.empty()) {
    unsigned Offset = 0;
    unsigned Size = 0;

    // Look at each subregister that we passed through, and progressively
    // narrow in, accumulating any offsets that occur. Substitutions should
    // only ever be the same or narrower width than what they read from;
    // iterate in reverse order so that we go from wide to small.
    for (unsigned Subreg : reverse(SeenSubregs)) {
      unsigned ThisSize = TRI->getSubRegIdxSize(Subreg);
      unsigned ThisOffset = TRI->getSubRegIdxOffset(Subreg);
      Offset += ThisOffset;
      Size = (Size == 0) ? ThisSize : std::min(Size, ThisSize);
    }

    // If that worked, look for an appropriate subregister with the register
    // where the define happens. Don't look at values that were defined during
    // a stack write: we can't currently express register locations within
    // spills.
    LocIdx L = NewID->getLoc();
    if (NewID && !MTracker->isSpill(L)) {
      // Find the register class for the register where this def happened.
      // FIXME: no index for this?
      Register Reg = MTracker->LocIdxToLocID[L];
      const TargetRegisterClass *TRC = nullptr;
      for (const auto *TRCI : TRI->regclasses())
        if (TRCI->contains(Reg))
          TRC = TRCI;
      assert(TRC && "Couldn't find target register class?");

      // If the register we have isn't the right size or in the right place,
      // Try to find a subregister inside it.
      unsigned MainRegSize = TRI->getRegSizeInBits(*TRC);
      if (Size != MainRegSize || Offset) {
        // Enumerate all subregisters, searching.
        Register NewReg = 0;
        for (MCPhysReg SR : TRI->subregs(Reg)) {
          unsigned Subreg = TRI->getSubRegIndex(Reg, SR);
          unsigned SubregSize = TRI->getSubRegIdxSize(Subreg);
          unsigned SubregOffset = TRI->getSubRegIdxOffset(Subreg);
          if (SubregSize == Size && SubregOffset == Offset) {
            NewReg = SR;
            break;
          }
        }

        // If we didn't find anything: there's no way to express our value.
        if (!NewReg) {
          NewID = std::nullopt;
        } else {
          // Re-state the value as being defined within the subregister
          // that we found.
          LocIdx NewLoc = MTracker->lookupOrTrackRegister(NewReg);
          NewID = ValueIDNum(NewID->getBlock(), NewID->getInst(), NewLoc);
        }
      }
    } else {
      // If we can't handle subregisters, unset the new value.
      NewID = std::nullopt;
    }
  }

  return NewID;
}

bool InstrRefBasedLDV::transferDebugInstrRef(MachineInstr &MI,
                                             const FuncValueTable *MLiveOuts,
                                             const FuncValueTable *MLiveIns) {
  if (!MI.isDebugRef())
    return false;

  // Only handle this instruction when we are building the variable value
  // transfer function.
  if (!VTracker && !TTracker)
    return false;

  const DILocalVariable *Var = MI.getDebugVariable();
  const DIExpression *Expr = MI.getDebugExpression();
  const DILocation *DebugLoc = MI.getDebugLoc();
  const DILocation *InlinedAt = DebugLoc->getInlinedAt();
  assert(Var->isValidLocationForIntrinsic(DebugLoc) &&
         "Expected inlined-at fields to agree");

  DebugVariable V(Var, Expr, InlinedAt);

  auto *Scope = LS.findLexicalScope(MI.getDebugLoc().get());
  if (Scope == nullptr)
    return true; // Handled by doing nothing. This variable is never in scope.

  SmallVector<DbgOpID> DbgOpIDs;
  for (const MachineOperand &MO : MI.debug_operands()) {
    if (!MO.isDbgInstrRef()) {
      assert(!MO.isReg() && "DBG_INSTR_REF should not contain registers");
      DbgOpID ConstOpID = DbgOpStore.insert(DbgOp(MO));
      DbgOpIDs.push_back(ConstOpID);
      continue;
    }

    unsigned InstNo = MO.getInstrRefInstrIndex();
    unsigned OpNo = MO.getInstrRefOpIndex();

    // Default machine value number is <None> -- if no instruction defines
    // the corresponding value, it must have been optimized out.
    std::optional<ValueIDNum> NewID =
        getValueForInstrRef(InstNo, OpNo, MI, MLiveOuts, MLiveIns);
    // We have a value number or std::nullopt. If the latter, then kill the
    // entire debug value.
    if (NewID) {
      DbgOpIDs.push_back(DbgOpStore.insert(*NewID));
    } else {
      DbgOpIDs.clear();
      break;
    }
  }

  // We have a DbgOpID for every value or for none. Tell the variable value
  // tracker about it. The rest of this LiveDebugValues implementation acts
  // exactly the same for DBG_INSTR_REFs as DBG_VALUEs (just, the former can
  // refer to values that aren't immediately available).
  DbgValueProperties Properties(Expr, false, true);
  if (VTracker)
    VTracker->defVar(MI, Properties, DbgOpIDs);

  // If we're on the final pass through the function, decompose this INSTR_REF
  // into a plain DBG_VALUE.
  if (!TTracker)
    return true;

  // Fetch the concrete DbgOps now, as we will need them later.
  SmallVector<DbgOp> DbgOps;
  for (DbgOpID OpID : DbgOpIDs) {
    DbgOps.push_back(DbgOpStore.find(OpID));
  }

  // Pick a location for the machine value number, if such a location exists.
  // (This information could be stored in TransferTracker to make it faster).
  SmallDenseMap<ValueIDNum, TransferTracker::LocationAndQuality> FoundLocs;
  SmallVector<ValueIDNum> ValuesToFind;
  // Initialized the preferred-location map with illegal locations, to be
  // filled in later.
  for (const DbgOp &Op : DbgOps) {
    if (!Op.IsConst)
      if (FoundLocs.insert({Op.ID, TransferTracker::LocationAndQuality()})
              .second)
        ValuesToFind.push_back(Op.ID);
  }

  for (auto Location : MTracker->locations()) {
    LocIdx CurL = Location.Idx;
    ValueIDNum ID = MTracker->readMLoc(CurL);
    auto ValueToFindIt = find(ValuesToFind, ID);
    if (ValueToFindIt == ValuesToFind.end())
      continue;
    auto &Previous = FoundLocs.find(ID)->second;
    // If this is the first location with that value, pick it. Otherwise,
    // consider whether it's a "longer term" location.
    std::optional<TransferTracker::LocationQuality> ReplacementQuality =
        TTracker->getLocQualityIfBetter(CurL, Previous.getQuality());
    if (ReplacementQuality) {
      Previous = TransferTracker::LocationAndQuality(CurL, *ReplacementQuality);
      if (Previous.isBest()) {
        ValuesToFind.erase(ValueToFindIt);
        if (ValuesToFind.empty())
          break;
      }
    }
  }

  SmallVector<ResolvedDbgOp> NewLocs;
  for (const DbgOp &DbgOp : DbgOps) {
    if (DbgOp.IsConst) {
      NewLocs.push_back(DbgOp.MO);
      continue;
    }
    LocIdx FoundLoc = FoundLocs.find(DbgOp.ID)->second.getLoc();
    if (FoundLoc.isIllegal()) {
      NewLocs.clear();
      break;
    }
    NewLocs.push_back(FoundLoc);
  }
  // Tell transfer tracker that the variable value has changed.
  TTracker->redefVar(MI, Properties, NewLocs);

  // If there were values with no location, but all such values are defined in
  // later instructions in this block, this is a block-local use-before-def.
  if (!DbgOps.empty() && NewLocs.empty()) {
    bool IsValidUseBeforeDef = true;
    uint64_t LastUseBeforeDef = 0;
    for (auto ValueLoc : FoundLocs) {
      ValueIDNum NewID = ValueLoc.first;
      LocIdx FoundLoc = ValueLoc.second.getLoc();
      if (!FoundLoc.isIllegal())
        continue;
      // If we have an value with no location that is not defined in this block,
      // then it has no location in this block, leaving this value undefined.
      if (NewID.getBlock() != CurBB || NewID.getInst() <= CurInst) {
        IsValidUseBeforeDef = false;
        break;
      }
      LastUseBeforeDef = std::max(LastUseBeforeDef, NewID.getInst());
    }
    if (IsValidUseBeforeDef) {
      DebugVariableID VID = DVMap.insertDVID(V, MI.getDebugLoc().get());
      TTracker->addUseBeforeDef(VID, {MI.getDebugExpression(), false, true},
                                DbgOps, LastUseBeforeDef);
    }
  }

  // Produce a DBG_VALUE representing what this DBG_INSTR_REF meant.
  // This DBG_VALUE is potentially a $noreg / undefined location, if
  // FoundLoc is illegal.
  // (XXX -- could morph the DBG_INSTR_REF in the future).
  MachineInstr *DbgMI =
      MTracker->emitLoc(NewLocs, V, MI.getDebugLoc().get(), Properties);
  DebugVariableID ID = DVMap.getDVID(V);

  TTracker->PendingDbgValues.push_back(std::make_pair(ID, DbgMI));
  TTracker->flushDbgValues(MI.getIterator(), nullptr);
  return true;
}

bool InstrRefBasedLDV::transferDebugPHI(MachineInstr &MI) {
  if (!MI.isDebugPHI())
    return false;

  // Analyse these only when solving the machine value location problem.
  if (VTracker || TTracker)
    return true;

  // First operand is the value location, either a stack slot or register.
  // Second is the debug instruction number of the original PHI.
  const MachineOperand &MO = MI.getOperand(0);
  unsigned InstrNum = MI.getOperand(1).getImm();

  auto EmitBadPHI = [this, &MI, InstrNum]() -> bool {
    // Helper lambda to do any accounting when we fail to find a location for
    // a DBG_PHI. This can happen if DBG_PHIs are malformed, or refer to a
    // dead stack slot, for example.
    // Record a DebugPHIRecord with an empty value + location.
    DebugPHINumToValue.push_back(
        {InstrNum, MI.getParent(), std::nullopt, std::nullopt});
    return true;
  };

  if (MO.isReg() && MO.getReg()) {
    // The value is whatever's currently in the register. Read and record it,
    // to be analysed later.
    Register Reg = MO.getReg();
    ValueIDNum Num = MTracker->readReg(Reg);
    auto PHIRec = DebugPHIRecord(
        {InstrNum, MI.getParent(), Num, MTracker->lookupOrTrackRegister(Reg)});
    DebugPHINumToValue.push_back(PHIRec);

    // Ensure this register is tracked.
    for (MCRegAliasIterator RAI(MO.getReg(), TRI, true); RAI.isValid(); ++RAI)
      MTracker->lookupOrTrackRegister(*RAI);
  } else if (MO.isFI()) {
    // The value is whatever's in this stack slot.
    unsigned FI = MO.getIndex();

    // If the stack slot is dead, then this was optimized away.
    // FIXME: stack slot colouring should account for slots that get merged.
    if (MFI->isDeadObjectIndex(FI))
      return EmitBadPHI();

    // Identify this spill slot, ensure it's tracked.
    Register Base;
    StackOffset Offs = TFI->getFrameIndexReference(*MI.getMF(), FI, Base);
    SpillLoc SL = {Base, Offs};
    std::optional<SpillLocationNo> SpillNo = MTracker->getOrTrackSpillLoc(SL);

    // We might be able to find a value, but have chosen not to, to avoid
    // tracking too much stack information.
    if (!SpillNo)
      return EmitBadPHI();

    // Any stack location DBG_PHI should have an associate bit-size.
    assert(MI.getNumOperands() == 3 && "Stack DBG_PHI with no size?");
    unsigned slotBitSize = MI.getOperand(2).getImm();

    unsigned SpillID = MTracker->getLocID(*SpillNo, {slotBitSize, 0});
    LocIdx SpillLoc = MTracker->getSpillMLoc(SpillID);
    ValueIDNum Result = MTracker->readMLoc(SpillLoc);

    // Record this DBG_PHI for later analysis.
    auto DbgPHI = DebugPHIRecord({InstrNum, MI.getParent(), Result, SpillLoc});
    DebugPHINumToValue.push_back(DbgPHI);
  } else {
    // Else: if the operand is neither a legal register or a stack slot, then
    // we're being fed illegal debug-info. Record an empty PHI, so that any
    // debug users trying to read this number will be put off trying to
    // interpret the value.
    LLVM_DEBUG(
        { dbgs() << "Seen DBG_PHI with unrecognised operand format\n"; });
    return EmitBadPHI();
  }

  return true;
}

void InstrRefBasedLDV::transferRegisterDef(MachineInstr &MI) {
  // Meta Instructions do not affect the debug liveness of any register they
  // define.
  if (MI.isImplicitDef()) {
    // Except when there's an implicit def, and the location it's defining has
    // no value number. The whole point of an implicit def is to announce that
    // the register is live, without be specific about it's value. So define
    // a value if there isn't one already.
    ValueIDNum Num = MTracker->readReg(MI.getOperand(0).getReg());
    // Has a legitimate value -> ignore the implicit def.
    if (Num.getLoc() != 0)
      return;
    // Otherwise, def it here.
  } else if (MI.isMetaInstruction())
    return;

  // We always ignore SP defines on call instructions, they don't actually
  // change the value of the stack pointer... except for win32's _chkstk. This
  // is rare: filter quickly for the common case (no stack adjustments, not a
  // call, etc). If it is a call that modifies SP, recognise the SP register
  // defs.
  bool CallChangesSP = false;
  if (AdjustsStackInCalls && MI.isCall() && MI.getOperand(0).isSymbol() &&
      !strcmp(MI.getOperand(0).getSymbolName(), StackProbeSymbolName.data()))
    CallChangesSP = true;

  // Test whether we should ignore a def of this register due to it being part
  // of the stack pointer.
  auto IgnoreSPAlias = [this, &MI, CallChangesSP](Register R) -> bool {
    if (CallChangesSP)
      return false;
    return MI.isCall() && MTracker->SPAliases.count(R);
  };

  // Find the regs killed by MI, and find regmasks of preserved regs.
  // Max out the number of statically allocated elements in `DeadRegs`, as this
  // prevents fallback to std::set::count() operations.
  SmallSet<uint32_t, 32> DeadRegs;
  SmallVector<const uint32_t *, 4> RegMasks;
  SmallVector<const MachineOperand *, 4> RegMaskPtrs;
  for (const MachineOperand &MO : MI.operands()) {
    // Determine whether the operand is a register def.
    if (MO.isReg() && MO.isDef() && MO.getReg() && MO.getReg().isPhysical() &&
        !IgnoreSPAlias(MO.getReg())) {
      // Remove ranges of all aliased registers.
      for (MCRegAliasIterator RAI(MO.getReg(), TRI, true); RAI.isValid(); ++RAI)
        // FIXME: Can we break out of this loop early if no insertion occurs?
        DeadRegs.insert(*RAI);
    } else if (MO.isRegMask()) {
      RegMasks.push_back(MO.getRegMask());
      RegMaskPtrs.push_back(&MO);
    }
  }

  // Tell MLocTracker about all definitions, of regmasks and otherwise.
  for (uint32_t DeadReg : DeadRegs)
    MTracker->defReg(DeadReg, CurBB, CurInst);

  for (const auto *MO : RegMaskPtrs)
    MTracker->writeRegMask(MO, CurBB, CurInst);

  // If this instruction writes to a spill slot, def that slot.
  if (hasFoldedStackStore(MI)) {
    if (std::optional<SpillLocationNo> SpillNo =
            extractSpillBaseRegAndOffset(MI)) {
      for (unsigned int I = 0; I < MTracker->NumSlotIdxes; ++I) {
        unsigned SpillID = MTracker->getSpillIDWithIdx(*SpillNo, I);
        LocIdx L = MTracker->getSpillMLoc(SpillID);
        MTracker->setMLoc(L, ValueIDNum(CurBB, CurInst, L));
      }
    }
  }

  if (!TTracker)
    return;

  // When committing variable values to locations: tell transfer tracker that
  // we've clobbered things. It may be able to recover the variable from a
  // different location.

  // Inform TTracker about any direct clobbers.
  for (uint32_t DeadReg : DeadRegs) {
    LocIdx Loc = MTracker->lookupOrTrackRegister(DeadReg);
    TTracker->clobberMloc(Loc, MI.getIterator(), false);
  }

  // Look for any clobbers performed by a register mask. Only test locations
  // that are actually being tracked.
  if (!RegMaskPtrs.empty()) {
    for (auto L : MTracker->locations()) {
      // Stack locations can't be clobbered by regmasks.
      if (MTracker->isSpill(L.Idx))
        continue;

      Register Reg = MTracker->LocIdxToLocID[L.Idx];
      if (IgnoreSPAlias(Reg))
        continue;

      for (const auto *MO : RegMaskPtrs)
        if (MO->clobbersPhysReg(Reg))
          TTracker->clobberMloc(L.Idx, MI.getIterator(), false);
    }
  }

  // Tell TTracker about any folded stack store.
  if (hasFoldedStackStore(MI)) {
    if (std::optional<SpillLocationNo> SpillNo =
            extractSpillBaseRegAndOffset(MI)) {
      for (unsigned int I = 0; I < MTracker->NumSlotIdxes; ++I) {
        unsigned SpillID = MTracker->getSpillIDWithIdx(*SpillNo, I);
        LocIdx L = MTracker->getSpillMLoc(SpillID);
        TTracker->clobberMloc(L, MI.getIterator(), true);
      }
    }
  }
}

void InstrRefBasedLDV::performCopy(Register SrcRegNum, Register DstRegNum) {
  // In all circumstances, re-def all aliases. It's definitely a new value now.
  for (MCRegAliasIterator RAI(DstRegNum, TRI, true); RAI.isValid(); ++RAI)
    MTracker->defReg(*RAI, CurBB, CurInst);

  ValueIDNum SrcValue = MTracker->readReg(SrcRegNum);
  MTracker->setReg(DstRegNum, SrcValue);

  // Copy subregisters from one location to another.
  for (MCSubRegIndexIterator SRI(SrcRegNum, TRI); SRI.isValid(); ++SRI) {
    unsigned SrcSubReg = SRI.getSubReg();
    unsigned SubRegIdx = SRI.getSubRegIndex();
    unsigned DstSubReg = TRI->getSubReg(DstRegNum, SubRegIdx);
    if (!DstSubReg)
      continue;

    // Do copy. There are two matching subregisters, the source value should
    // have been def'd when the super-reg was, the latter might not be tracked
    // yet.
    // This will force SrcSubReg to be tracked, if it isn't yet. Will read
    // mphi values if it wasn't tracked.
    LocIdx SrcL = MTracker->lookupOrTrackRegister(SrcSubReg);
    LocIdx DstL = MTracker->lookupOrTrackRegister(DstSubReg);
    (void)SrcL;
    (void)DstL;
    ValueIDNum CpyValue = MTracker->readReg(SrcSubReg);

    MTracker->setReg(DstSubReg, CpyValue);
  }
}

std::optional<SpillLocationNo>
InstrRefBasedLDV::isSpillInstruction(const MachineInstr &MI,
                                     MachineFunction *MF) {
  // TODO: Handle multiple stores folded into one.
  if (!MI.hasOneMemOperand())
    return std::nullopt;

  // Reject any memory operand that's aliased -- we can't guarantee its value.
  auto MMOI = MI.memoperands_begin();
  const PseudoSourceValue *PVal = (*MMOI)->getPseudoValue();
  if (PVal->isAliased(MFI))
    return std::nullopt;

  if (!MI.getSpillSize(TII) && !MI.getFoldedSpillSize(TII))
    return std::nullopt; // This is not a spill instruction, since no valid size
                         // was returned from either function.

  return extractSpillBaseRegAndOffset(MI);
}

bool InstrRefBasedLDV::isLocationSpill(const MachineInstr &MI,
                                       MachineFunction *MF, unsigned &Reg) {
  if (!isSpillInstruction(MI, MF))
    return false;

  int FI;
  Reg = TII->isStoreToStackSlotPostFE(MI, FI);
  return Reg != 0;
}

std::optional<SpillLocationNo>
InstrRefBasedLDV::isRestoreInstruction(const MachineInstr &MI,
                                       MachineFunction *MF, unsigned &Reg) {
  if (!MI.hasOneMemOperand())
    return std::nullopt;

  // FIXME: Handle folded restore instructions with more than one memory
  // operand.
  if (MI.getRestoreSize(TII)) {
    Reg = MI.getOperand(0).getReg();
    return extractSpillBaseRegAndOffset(MI);
  }
  return std::nullopt;
}

bool InstrRefBasedLDV::transferSpillOrRestoreInst(MachineInstr &MI) {
  // XXX -- it's too difficult to implement VarLocBasedImpl's  stack location
  // limitations under the new model. Therefore, when comparing them, compare
  // versions that don't attempt spills or restores at all.
  if (EmulateOldLDV)
    return false;

  // Strictly limit ourselves to plain loads and stores, not all instructions
  // that can access the stack.
  int DummyFI = -1;
  if (!TII->isStoreToStackSlotPostFE(MI, DummyFI) &&
      !TII->isLoadFromStackSlotPostFE(MI, DummyFI))
    return false;

  MachineFunction *MF = MI.getMF();
  unsigned Reg;

  LLVM_DEBUG(dbgs() << "Examining instruction: "; MI.dump(););

  // Strictly limit ourselves to plain loads and stores, not all instructions
  // that can access the stack.
  int FIDummy;
  if (!TII->isStoreToStackSlotPostFE(MI, FIDummy) &&
      !TII->isLoadFromStackSlotPostFE(MI, FIDummy))
    return false;

  // First, if there are any DBG_VALUEs pointing at a spill slot that is
  // written to, terminate that variable location. The value in memory
  // will have changed. DbgEntityHistoryCalculator doesn't try to detect this.
  if (std::optional<SpillLocationNo> Loc = isSpillInstruction(MI, MF)) {
    // Un-set this location and clobber, so that earlier locations don't
    // continue past this store.
    for (unsigned SlotIdx = 0; SlotIdx < MTracker->NumSlotIdxes; ++SlotIdx) {
      unsigned SpillID = MTracker->getSpillIDWithIdx(*Loc, SlotIdx);
      std::optional<LocIdx> MLoc = MTracker->getSpillMLoc(SpillID);
      if (!MLoc)
        continue;

      // We need to over-write the stack slot with something (here, a def at
      // this instruction) to ensure no values are preserved in this stack slot
      // after the spill. It also prevents TTracker from trying to recover the
      // location and re-installing it in the same place.
      ValueIDNum Def(CurBB, CurInst, *MLoc);
      MTracker->setMLoc(*MLoc, Def);
      if (TTracker)
        TTracker->clobberMloc(*MLoc, MI.getIterator());
    }
  }

  // Try to recognise spill and restore instructions that may transfer a value.
  if (isLocationSpill(MI, MF, Reg)) {
    // isLocationSpill returning true should guarantee we can extract a
    // location.
    SpillLocationNo Loc = *extractSpillBaseRegAndOffset(MI);

    auto DoTransfer = [&](Register SrcReg, unsigned SpillID) {
      auto ReadValue = MTracker->readReg(SrcReg);
      LocIdx DstLoc = MTracker->getSpillMLoc(SpillID);
      MTracker->setMLoc(DstLoc, ReadValue);

      if (TTracker) {
        LocIdx SrcLoc = MTracker->getRegMLoc(SrcReg);
        TTracker->transferMlocs(SrcLoc, DstLoc, MI.getIterator());
      }
    };

    // Then, transfer subreg bits.
    for (MCPhysReg SR : TRI->subregs(Reg)) {
      // Ensure this reg is tracked,
      (void)MTracker->lookupOrTrackRegister(SR);
      unsigned SubregIdx = TRI->getSubRegIndex(Reg, SR);
      unsigned SpillID = MTracker->getLocID(Loc, SubregIdx);
      DoTransfer(SR, SpillID);
    }

    // Directly lookup size of main source reg, and transfer.
    unsigned Size = TRI->getRegSizeInBits(Reg, *MRI);
    unsigned SpillID = MTracker->getLocID(Loc, {Size, 0});
    DoTransfer(Reg, SpillID);
  } else {
    std::optional<SpillLocationNo> Loc = isRestoreInstruction(MI, MF, Reg);
    if (!Loc)
      return false;

    // Assumption: we're reading from the base of the stack slot, not some
    // offset into it. It seems very unlikely LLVM would ever generate
    // restores where this wasn't true. This then becomes a question of what
    // subregisters in the destination register line up with positions in the
    // stack slot.

    // Def all registers that alias the destination.
    for (MCRegAliasIterator RAI(Reg, TRI, true); RAI.isValid(); ++RAI)
      MTracker->defReg(*RAI, CurBB, CurInst);

    // Now find subregisters within the destination register, and load values
    // from stack slot positions.
    auto DoTransfer = [&](Register DestReg, unsigned SpillID) {
      LocIdx SrcIdx = MTracker->getSpillMLoc(SpillID);
      auto ReadValue = MTracker->readMLoc(SrcIdx);
      MTracker->setReg(DestReg, ReadValue);
    };

    for (MCPhysReg SR : TRI->subregs(Reg)) {
      unsigned Subreg = TRI->getSubRegIndex(Reg, SR);
      unsigned SpillID = MTracker->getLocID(*Loc, Subreg);
      DoTransfer(SR, SpillID);
    }

    // Directly look up this registers slot idx by size, and transfer.
    unsigned Size = TRI->getRegSizeInBits(Reg, *MRI);
    unsigned SpillID = MTracker->getLocID(*Loc, {Size, 0});
    DoTransfer(Reg, SpillID);
  }
  return true;
}

bool InstrRefBasedLDV::transferRegisterCopy(MachineInstr &MI) {
  auto DestSrc = TII->isCopyLikeInstr(MI);
  if (!DestSrc)
    return false;

  const MachineOperand *DestRegOp = DestSrc->Destination;
  const MachineOperand *SrcRegOp = DestSrc->Source;

  Register SrcReg = SrcRegOp->getReg();
  Register DestReg = DestRegOp->getReg();

  // Ignore identity copies. Yep, these make it as far as LiveDebugValues.
  if (SrcReg == DestReg)
    return true;

  // For emulating VarLocBasedImpl:
  // We want to recognize instructions where destination register is callee
  // saved register. If register that could be clobbered by the call is
  // included, there would be a great chance that it is going to be clobbered
  // soon. It is more likely that previous register, which is callee saved, is
  // going to stay unclobbered longer, even if it is killed.
  //
  // For InstrRefBasedImpl, we can track multiple locations per value, so
  // ignore this condition.
  if (EmulateOldLDV && !isCalleeSavedReg(DestReg))
    return false;

  // InstrRefBasedImpl only followed killing copies.
  if (EmulateOldLDV && !SrcRegOp->isKill())
    return false;

  // Before we update MTracker, remember which values were present in each of
  // the locations about to be overwritten, so that we can recover any
  // potentially clobbered variables.
  DenseMap<LocIdx, ValueIDNum> ClobberedLocs;
  if (TTracker) {
    for (MCRegAliasIterator RAI(DestReg, TRI, true); RAI.isValid(); ++RAI) {
      LocIdx ClobberedLoc = MTracker->getRegMLoc(*RAI);
      auto MLocIt = TTracker->ActiveMLocs.find(ClobberedLoc);
      // If ActiveMLocs isn't tracking this location or there are no variables
      // using it, don't bother remembering.
      if (MLocIt == TTracker->ActiveMLocs.end() || MLocIt->second.empty())
        continue;
      ValueIDNum Value = MTracker->readReg(*RAI);
      ClobberedLocs[ClobberedLoc] = Value;
    }
  }

  // Copy MTracker info, including subregs if available.
  InstrRefBasedLDV::performCopy(SrcReg, DestReg);

  // The copy might have clobbered variables based on the destination register.
  // Tell TTracker about it, passing the old ValueIDNum to search for
  // alternative locations (or else terminating those variables).
  if (TTracker) {
    for (auto LocVal : ClobberedLocs) {
      TTracker->clobberMloc(LocVal.first, LocVal.second, MI.getIterator(), false);
    }
  }

  // Only produce a transfer of DBG_VALUE within a block where old LDV
  // would have. We might make use of the additional value tracking in some
  // other way, later.
  if (TTracker && isCalleeSavedReg(DestReg) && SrcRegOp->isKill())
    TTracker->transferMlocs(MTracker->getRegMLoc(SrcReg),
                            MTracker->getRegMLoc(DestReg), MI.getIterator());

  // VarLocBasedImpl would quit tracking the old location after copying.
  if (EmulateOldLDV && SrcReg != DestReg)
    MTracker->defReg(SrcReg, CurBB, CurInst);

  return true;
}

/// Accumulate a mapping between each DILocalVariable fragment and other
/// fragments of that DILocalVariable which overlap. This reduces work during
/// the data-flow stage from "Find any overlapping fragments" to "Check if the
/// known-to-overlap fragments are present".
/// \param MI A previously unprocessed debug instruction to analyze for
///           fragment usage.
void InstrRefBasedLDV::accumulateFragmentMap(MachineInstr &MI) {
  assert(MI.isDebugValueLike());
  DebugVariable MIVar(MI.getDebugVariable(), MI.getDebugExpression(),
                      MI.getDebugLoc()->getInlinedAt());
  FragmentInfo ThisFragment = MIVar.getFragmentOrDefault();

  // If this is the first sighting of this variable, then we are guaranteed
  // there are currently no overlapping fragments either. Initialize the set
  // of seen fragments, record no overlaps for the current one, and return.
  auto SeenIt = SeenFragments.find(MIVar.getVariable());
  if (SeenIt == SeenFragments.end()) {
    SmallSet<FragmentInfo, 4> OneFragment;
    OneFragment.insert(ThisFragment);
    SeenFragments.insert({MIVar.getVariable(), OneFragment});

    OverlapFragments.insert({{MIVar.getVariable(), ThisFragment}, {}});
    return;
  }

  // If this particular Variable/Fragment pair already exists in the overlap
  // map, it has already been accounted for.
  auto IsInOLapMap =
      OverlapFragments.insert({{MIVar.getVariable(), ThisFragment}, {}});
  if (!IsInOLapMap.second)
    return;

  auto &ThisFragmentsOverlaps = IsInOLapMap.first->second;
  auto &AllSeenFragments = SeenIt->second;

  // Otherwise, examine all other seen fragments for this variable, with "this"
  // fragment being a previously unseen fragment. Record any pair of
  // overlapping fragments.
  for (const auto &ASeenFragment : AllSeenFragments) {
    // Does this previously seen fragment overlap?
    if (DIExpression::fragmentsOverlap(ThisFragment, ASeenFragment)) {
      // Yes: Mark the current fragment as being overlapped.
      ThisFragmentsOverlaps.push_back(ASeenFragment);
      // Mark the previously seen fragment as being overlapped by the current
      // one.
      auto ASeenFragmentsOverlaps =
          OverlapFragments.find({MIVar.getVariable(), ASeenFragment});
      assert(ASeenFragmentsOverlaps != OverlapFragments.end() &&
             "Previously seen var fragment has no vector of overlaps");
      ASeenFragmentsOverlaps->second.push_back(ThisFragment);
    }
  }

  AllSeenFragments.insert(ThisFragment);
}

void InstrRefBasedLDV::process(MachineInstr &MI,
                               const FuncValueTable *MLiveOuts,
                               const FuncValueTable *MLiveIns) {
  // Try to interpret an MI as a debug or transfer instruction. Only if it's
  // none of these should we interpret it's register defs as new value
  // definitions.
  if (transferDebugValue(MI))
    return;
  if (transferDebugInstrRef(MI, MLiveOuts, MLiveIns))
    return;
  if (transferDebugPHI(MI))
    return;
  if (transferRegisterCopy(MI))
    return;
  if (transferSpillOrRestoreInst(MI))
    return;
  transferRegisterDef(MI);
}

void InstrRefBasedLDV::produceMLocTransferFunction(
    MachineFunction &MF, SmallVectorImpl<MLocTransferMap> &MLocTransfer,
    unsigned MaxNumBlocks) {
  // Because we try to optimize around register mask operands by ignoring regs
  // that aren't currently tracked, we set up something ugly for later: RegMask
  // operands that are seen earlier than the first use of a register, still need
  // to clobber that register in the transfer function. But this information
  // isn't actively recorded. Instead, we track each RegMask used in each block,
  // and accumulated the clobbered but untracked registers in each block into
  // the following bitvector. Later, if new values are tracked, we can add
  // appropriate clobbers.
  SmallVector<BitVector, 32> BlockMasks;
  BlockMasks.resize(MaxNumBlocks);

  // Reserve one bit per register for the masks described above.
  unsigned BVWords = MachineOperand::getRegMaskSize(TRI->getNumRegs());
  for (auto &BV : BlockMasks)
    BV.resize(TRI->getNumRegs(), true);

  // Step through all instructions and inhale the transfer function.
  for (auto &MBB : MF) {
    // Object fields that are read by trackers to know where we are in the
    // function.
    CurBB = MBB.getNumber();
    CurInst = 1;

    // Set all machine locations to a PHI value. For transfer function
    // production only, this signifies the live-in value to the block.
    MTracker->reset();
    MTracker->setMPhis(CurBB);

    // Step through each instruction in this block.
    for (auto &MI : MBB) {
      // Pass in an empty unique_ptr for the value tables when accumulating the
      // machine transfer function.
      process(MI, nullptr, nullptr);

      // Also accumulate fragment map.
      if (MI.isDebugValueLike())
        accumulateFragmentMap(MI);

      // Create a map from the instruction number (if present) to the
      // MachineInstr and its position.
      if (uint64_t InstrNo = MI.peekDebugInstrNum()) {
        auto InstrAndPos = std::make_pair(&MI, CurInst);
        auto InsertResult =
            DebugInstrNumToInstr.insert(std::make_pair(InstrNo, InstrAndPos));

        // There should never be duplicate instruction numbers.
        assert(InsertResult.second);
        (void)InsertResult;
      }

      ++CurInst;
    }

    // Produce the transfer function, a map of machine location to new value. If
    // any machine location has the live-in phi value from the start of the
    // block, it's live-through and doesn't need recording in the transfer
    // function.
    for (auto Location : MTracker->locations()) {
      LocIdx Idx = Location.Idx;
      ValueIDNum &P = Location.Value;
      if (P.isPHI() && P.getLoc() == Idx.asU64())
        continue;

      // Insert-or-update.
      auto &TransferMap = MLocTransfer[CurBB];
      auto Result = TransferMap.insert(std::make_pair(Idx.asU64(), P));
      if (!Result.second)
        Result.first->second = P;
    }

    // Accumulate any bitmask operands into the clobbered reg mask for this
    // block.
    for (auto &P : MTracker->Masks) {
      BlockMasks[CurBB].clearBitsNotInMask(P.first->getRegMask(), BVWords);
    }
  }

  // Compute a bitvector of all the registers that are tracked in this block.
  BitVector UsedRegs(TRI->getNumRegs());
  for (auto Location : MTracker->locations()) {
    unsigned ID = MTracker->LocIdxToLocID[Location.Idx];
    // Ignore stack slots, and aliases of the stack pointer.
    if (ID >= TRI->getNumRegs() || MTracker->SPAliases.count(ID))
      continue;
    UsedRegs.set(ID);
  }

  // Check that any regmask-clobber of a register that gets tracked, is not
  // live-through in the transfer function. It needs to be clobbered at the
  // very least.
  for (unsigned int I = 0; I < MaxNumBlocks; ++I) {
    BitVector &BV = BlockMasks[I];
    BV.flip();
    BV &= UsedRegs;
    // This produces all the bits that we clobber, but also use. Check that
    // they're all clobbered or at least set in the designated transfer
    // elem.
    for (unsigned Bit : BV.set_bits()) {
      unsigned ID = MTracker->getLocID(Bit);
      LocIdx Idx = MTracker->LocIDToLocIdx[ID];
      auto &TransferMap = MLocTransfer[I];

      // Install a value representing the fact that this location is effectively
      // written to in this block. As there's no reserved value, instead use
      // a value number that is never generated. Pick the value number for the
      // first instruction in the block, def'ing this location, which we know
      // this block never used anyway.
      ValueIDNum NotGeneratedNum = ValueIDNum(I, 1, Idx);
      auto Result =
        TransferMap.insert(std::make_pair(Idx.asU64(), NotGeneratedNum));
      if (!Result.second) {
        ValueIDNum &ValueID = Result.first->second;
        if (ValueID.getBlock() == I && ValueID.isPHI())
          // It was left as live-through. Set it to clobbered.
          ValueID = NotGeneratedNum;
      }
    }
  }
}

bool InstrRefBasedLDV::mlocJoin(
    MachineBasicBlock &MBB, SmallPtrSet<const MachineBasicBlock *, 16> &Visited,
    FuncValueTable &OutLocs, ValueTable &InLocs) {
  LLVM_DEBUG(dbgs() << "join MBB: " << MBB.getNumber() << "\n");
  bool Changed = false;

  // Handle value-propagation when control flow merges on entry to a block. For
  // any location without a PHI already placed, the location has the same value
  // as its predecessors. If a PHI is placed, test to see whether it's now a
  // redundant PHI that we can eliminate.

  SmallVector<const MachineBasicBlock *, 8> BlockOrders;
  for (auto *Pred : MBB.predecessors())
    BlockOrders.push_back(Pred);

  // Visit predecessors in RPOT order.
  auto Cmp = [&](const MachineBasicBlock *A, const MachineBasicBlock *B) {
    return BBToOrder.find(A)->second < BBToOrder.find(B)->second;
  };
  llvm::sort(BlockOrders, Cmp);

  // Skip entry block.
  if (BlockOrders.size() == 0) {
    // FIXME: We don't use assert here to prevent instr-ref-unreachable.mir
    // failing.
    LLVM_DEBUG(if (!MBB.isEntryBlock()) dbgs()
               << "Found not reachable block " << MBB.getFullName()
               << " from entry which may lead out of "
                  "bound access to VarLocs\n");
    return false;
  }

  // Step through all machine locations, look at each predecessor and test
  // whether we can eliminate redundant PHIs.
  for (auto Location : MTracker->locations()) {
    LocIdx Idx = Location.Idx;

    // Pick out the first predecessors live-out value for this location. It's
    // guaranteed to not be a backedge, as we order by RPO.
    ValueIDNum FirstVal = OutLocs[*BlockOrders[0]][Idx.asU64()];

    // If we've already eliminated a PHI here, do no further checking, just
    // propagate the first live-in value into this block.
    if (InLocs[Idx.asU64()] != ValueIDNum(MBB.getNumber(), 0, Idx)) {
      if (InLocs[Idx.asU64()] != FirstVal) {
        InLocs[Idx.asU64()] = FirstVal;
        Changed |= true;
      }
      continue;
    }

    // We're now examining a PHI to see whether it's un-necessary. Loop around
    // the other live-in values and test whether they're all the same.
    bool Disagree = false;
    for (unsigned int I = 1; I < BlockOrders.size(); ++I) {
      const MachineBasicBlock *PredMBB = BlockOrders[I];
      const ValueIDNum &PredLiveOut = OutLocs[*PredMBB][Idx.asU64()];

      // Incoming values agree, continue trying to eliminate this PHI.
      if (FirstVal == PredLiveOut)
        continue;

      // We can also accept a PHI value that feeds back into itself.
      if (PredLiveOut == ValueIDNum(MBB.getNumber(), 0, Idx))
        continue;

      // Live-out of a predecessor disagrees with the first predecessor.
      Disagree = true;
    }

    // No disagreement? No PHI. Otherwise, leave the PHI in live-ins.
    if (!Disagree) {
      InLocs[Idx.asU64()] = FirstVal;
      Changed |= true;
    }
  }

  // TODO: Reimplement NumInserted and NumRemoved.
  return Changed;
}

void InstrRefBasedLDV::findStackIndexInterference(
    SmallVectorImpl<unsigned> &Slots) {
  // We could spend a bit of time finding the exact, minimal, set of stack
  // indexes that interfere with each other, much like reg units. Or, we can
  // rely on the fact that:
  //  * The smallest / lowest index will interfere with everything at zero
  //    offset, which will be the largest set of registers,
  //  * Most indexes with non-zero offset will end up being interference units
  //    anyway.
  // So just pick those out and return them.

  // We can rely on a single-byte stack index existing already, because we
  // initialize them in MLocTracker.
  auto It = MTracker->StackSlotIdxes.find({8, 0});
  assert(It != MTracker->StackSlotIdxes.end());
  Slots.push_back(It->second);

  // Find anything that has a non-zero offset and add that too.
  for (auto &Pair : MTracker->StackSlotIdxes) {
    // Is offset zero? If so, ignore.
    if (!Pair.first.second)
      continue;
    Slots.push_back(Pair.second);
  }
}

void InstrRefBasedLDV::placeMLocPHIs(
    MachineFunction &MF, SmallPtrSetImpl<MachineBasicBlock *> &AllBlocks,
    FuncValueTable &MInLocs, SmallVectorImpl<MLocTransferMap> &MLocTransfer) {
  SmallVector<unsigned, 4> StackUnits;
  findStackIndexInterference(StackUnits);

  // To avoid repeatedly running the PHI placement algorithm, leverage the
  // fact that a def of register MUST also def its register units. Find the
  // units for registers, place PHIs for them, and then replicate them for
  // aliasing registers. Some inputs that are never def'd (DBG_PHIs of
  // arguments) don't lead to register units being tracked, just place PHIs for
  // those registers directly. Stack slots have their own form of "unit",
  // store them to one side.
  SmallSet<Register, 32> RegUnitsToPHIUp;
  SmallSet<LocIdx, 32> NormalLocsToPHI;
  SmallSet<SpillLocationNo, 32> StackSlots;
  for (auto Location : MTracker->locations()) {
    LocIdx L = Location.Idx;
    if (MTracker->isSpill(L)) {
      StackSlots.insert(MTracker->locIDToSpill(MTracker->LocIdxToLocID[L]));
      continue;
    }

    Register R = MTracker->LocIdxToLocID[L];
    SmallSet<Register, 8> FoundRegUnits;
    bool AnyIllegal = false;
    for (MCRegUnit Unit : TRI->regunits(R.asMCReg())) {
      for (MCRegUnitRootIterator URoot(Unit, TRI); URoot.isValid(); ++URoot) {
        if (!MTracker->isRegisterTracked(*URoot)) {
          // Not all roots were loaded into the tracking map: this register
          // isn't actually def'd anywhere, we only read from it. Generate PHIs
          // for this reg, but don't iterate units.
          AnyIllegal = true;
        } else {
          FoundRegUnits.insert(*URoot);
        }
      }
    }

    if (AnyIllegal) {
      NormalLocsToPHI.insert(L);
      continue;
    }

    RegUnitsToPHIUp.insert(FoundRegUnits.begin(), FoundRegUnits.end());
  }

  // Lambda to fetch PHIs for a given location, and write into the PHIBlocks
  // collection.
  SmallVector<MachineBasicBlock *, 32> PHIBlocks;
  auto CollectPHIsForLoc = [&](LocIdx L) {
    // Collect the set of defs.
    SmallPtrSet<MachineBasicBlock *, 32> DefBlocks;
    for (unsigned int I = 0; I < OrderToBB.size(); ++I) {
      MachineBasicBlock *MBB = OrderToBB[I];
      const auto &TransferFunc = MLocTransfer[MBB->getNumber()];
      if (TransferFunc.contains(L))
        DefBlocks.insert(MBB);
    }

    // The entry block defs the location too: it's the live-in / argument value.
    // Only insert if there are other defs though; everything is trivially live
    // through otherwise.
    if (!DefBlocks.empty())
      DefBlocks.insert(&*MF.begin());

    // Ask the SSA construction algorithm where we should put PHIs. Clear
    // anything that might have been hanging around from earlier.
    PHIBlocks.clear();
    BlockPHIPlacement(AllBlocks, DefBlocks, PHIBlocks);
  };

  auto InstallPHIsAtLoc = [&PHIBlocks, &MInLocs](LocIdx L) {
    for (const MachineBasicBlock *MBB : PHIBlocks)
      MInLocs[*MBB][L.asU64()] = ValueIDNum(MBB->getNumber(), 0, L);
  };

  // For locations with no reg units, just place PHIs.
  for (LocIdx L : NormalLocsToPHI) {
    CollectPHIsForLoc(L);
    // Install those PHI values into the live-in value array.
    InstallPHIsAtLoc(L);
  }

  // For stack slots, calculate PHIs for the equivalent of the units, then
  // install for each index.
  for (SpillLocationNo Slot : StackSlots) {
    for (unsigned Idx : StackUnits) {
      unsigned SpillID = MTracker->getSpillIDWithIdx(Slot, Idx);
      LocIdx L = MTracker->getSpillMLoc(SpillID);
      CollectPHIsForLoc(L);
      InstallPHIsAtLoc(L);

      // Find anything that aliases this stack index, install PHIs for it too.
      unsigned Size, Offset;
      std::tie(Size, Offset) = MTracker->StackIdxesToPos[Idx];
      for (auto &Pair : MTracker->StackSlotIdxes) {
        unsigned ThisSize, ThisOffset;
        std::tie(ThisSize, ThisOffset) = Pair.first;
        if (ThisSize + ThisOffset <= Offset || Size + Offset <= ThisOffset)
          continue;

        unsigned ThisID = MTracker->getSpillIDWithIdx(Slot, Pair.second);
        LocIdx ThisL = MTracker->getSpillMLoc(ThisID);
        InstallPHIsAtLoc(ThisL);
      }
    }
  }

  // For reg units, place PHIs, and then place them for any aliasing registers.
  for (Register R : RegUnitsToPHIUp) {
    LocIdx L = MTracker->lookupOrTrackRegister(R);
    CollectPHIsForLoc(L);

    // Install those PHI values into the live-in value array.
    InstallPHIsAtLoc(L);

    // Now find aliases and install PHIs for those.
    for (MCRegAliasIterator RAI(R, TRI, true); RAI.isValid(); ++RAI) {
      // Super-registers that are "above" the largest register read/written by
      // the function will alias, but will not be tracked.
      if (!MTracker->isRegisterTracked(*RAI))
        continue;

      LocIdx AliasLoc = MTracker->lookupOrTrackRegister(*RAI);
      InstallPHIsAtLoc(AliasLoc);
    }
  }
}

void InstrRefBasedLDV::buildMLocValueMap(
    MachineFunction &MF, FuncValueTable &MInLocs, FuncValueTable &MOutLocs,
    SmallVectorImpl<MLocTransferMap> &MLocTransfer) {
  std::priority_queue<unsigned int, std::vector<unsigned int>,
                      std::greater<unsigned int>>
      Worklist, Pending;

  // We track what is on the current and pending worklist to avoid inserting
  // the same thing twice. We could avoid this with a custom priority queue,
  // but this is probably not worth it.
  SmallPtrSet<MachineBasicBlock *, 16> OnPending, OnWorklist;

  // Initialize worklist with every block to be visited. Also produce list of
  // all blocks.
  SmallPtrSet<MachineBasicBlock *, 32> AllBlocks;
  for (unsigned int I = 0; I < BBToOrder.size(); ++I) {
    Worklist.push(I);
    OnWorklist.insert(OrderToBB[I]);
    AllBlocks.insert(OrderToBB[I]);
  }

  // Initialize entry block to PHIs. These represent arguments.
  for (auto Location : MTracker->locations())
    MInLocs.tableForEntryMBB()[Location.Idx.asU64()] =
        ValueIDNum(0, 0, Location.Idx);

  MTracker->reset();

  // Start by placing PHIs, using the usual SSA constructor algorithm. Consider
  // any machine-location that isn't live-through a block to be def'd in that
  // block.
  placeMLocPHIs(MF, AllBlocks, MInLocs, MLocTransfer);

  // Propagate values to eliminate redundant PHIs. At the same time, this
  // produces the table of Block x Location => Value for the entry to each
  // block.
  // The kind of PHIs we can eliminate are, for example, where one path in a
  // conditional spills and restores a register, and the register still has
  // the same value once control flow joins, unbeknowns to the PHI placement
  // code. Propagating values allows us to identify such un-necessary PHIs and
  // remove them.
  SmallPtrSet<const MachineBasicBlock *, 16> Visited;
  while (!Worklist.empty() || !Pending.empty()) {
    // Vector for storing the evaluated block transfer function.
    SmallVector<std::pair<LocIdx, ValueIDNum>, 32> ToRemap;

    while (!Worklist.empty()) {
      MachineBasicBlock *MBB = OrderToBB[Worklist.top()];
      CurBB = MBB->getNumber();
      Worklist.pop();

      // Join the values in all predecessor blocks.
      bool InLocsChanged;
      InLocsChanged = mlocJoin(*MBB, Visited, MOutLocs, MInLocs[*MBB]);
      InLocsChanged |= Visited.insert(MBB).second;

      // Don't examine transfer function if we've visited this loc at least
      // once, and inlocs haven't changed.
      if (!InLocsChanged)
        continue;

      // Load the current set of live-ins into MLocTracker.
      MTracker->loadFromArray(MInLocs[*MBB], CurBB);

      // Each element of the transfer function can be a new def, or a read of
      // a live-in value. Evaluate each element, and store to "ToRemap".
      ToRemap.clear();
      for (auto &P : MLocTransfer[CurBB]) {
        if (P.second.getBlock() == CurBB && P.second.isPHI()) {
          // This is a movement of whatever was live in. Read it.
          ValueIDNum NewID = MTracker->readMLoc(P.second.getLoc());
          ToRemap.push_back(std::make_pair(P.first, NewID));
        } else {
          // It's a def. Just set it.
          assert(P.second.getBlock() == CurBB);
          ToRemap.push_back(std::make_pair(P.first, P.second));
        }
      }

      // Commit the transfer function changes into mloc tracker, which
      // transforms the contents of the MLocTracker into the live-outs.
      for (auto &P : ToRemap)
        MTracker->setMLoc(P.first, P.second);

      // Now copy out-locs from mloc tracker into out-loc vector, checking
      // whether changes have occurred. These changes can have come from both
      // the transfer function, and mlocJoin.
      bool OLChanged = false;
      for (auto Location : MTracker->locations()) {
        OLChanged |= MOutLocs[*MBB][Location.Idx.asU64()] != Location.Value;
        MOutLocs[*MBB][Location.Idx.asU64()] = Location.Value;
      }

      MTracker->reset();

      // No need to examine successors again if out-locs didn't change.
      if (!OLChanged)
        continue;

      // All successors should be visited: put any back-edges on the pending
      // list for the next pass-through, and any other successors to be
      // visited this pass, if they're not going to be already.
      for (auto *s : MBB->successors()) {
        // Does branching to this successor represent a back-edge?
        if (BBToOrder[s] > BBToOrder[MBB]) {
          // No: visit it during this dataflow iteration.
          if (OnWorklist.insert(s).second)
            Worklist.push(BBToOrder[s]);
        } else {
          // Yes: visit it on the next iteration.
          if (OnPending.insert(s).second)
            Pending.push(BBToOrder[s]);
        }
      }
    }

    Worklist.swap(Pending);
    std::swap(OnPending, OnWorklist);
    OnPending.clear();
    // At this point, pending must be empty, since it was just the empty
    // worklist
    assert(Pending.empty() && "Pending should be empty");
  }

  // Once all the live-ins don't change on mlocJoin(), we've eliminated all
  // redundant PHIs.
}

void InstrRefBasedLDV::BlockPHIPlacement(
    const SmallPtrSetImpl<MachineBasicBlock *> &AllBlocks,
    const SmallPtrSetImpl<MachineBasicBlock *> &DefBlocks,
    SmallVectorImpl<MachineBasicBlock *> &PHIBlocks) {
  // Apply IDF calculator to the designated set of location defs, storing
  // required PHIs into PHIBlocks. Uses the dominator tree stored in the
  // InstrRefBasedLDV object.
  IDFCalculatorBase<MachineBasicBlock, false> IDF(DomTree->getBase());

  IDF.setLiveInBlocks(AllBlocks);
  IDF.setDefiningBlocks(DefBlocks);
  IDF.calculate(PHIBlocks);
}

bool InstrRefBasedLDV::pickVPHILoc(
    SmallVectorImpl<DbgOpID> &OutValues, const MachineBasicBlock &MBB,
    const LiveIdxT &LiveOuts, FuncValueTable &MOutLocs,
    const SmallVectorImpl<const MachineBasicBlock *> &BlockOrders) {

  // No predecessors means no PHIs.
  if (BlockOrders.empty())
    return false;

  // All the location operands that do not already agree need to be joined,
  // track the indices of each such location operand here.
  SmallDenseSet<unsigned> LocOpsToJoin;

  auto FirstValueIt = LiveOuts.find(BlockOrders[0]);
  if (FirstValueIt == LiveOuts.end())
    return false;
  const DbgValue &FirstValue = *FirstValueIt->second;

  for (const auto p : BlockOrders) {
    auto OutValIt = LiveOuts.find(p);
    if (OutValIt == LiveOuts.end())
      // If we have a predecessor not in scope, we'll never find a PHI position.
      return false;
    const DbgValue &OutVal = *OutValIt->second;

    // No-values cannot have locations we can join on.
    if (OutVal.Kind == DbgValue::NoVal)
      return false;

    // For unjoined VPHIs where we don't know the location, we definitely
    // can't find a join loc unless the VPHI is a backedge.
    if (OutVal.isUnjoinedPHI() && OutVal.BlockNo != MBB.getNumber())
      return false;

    if (!FirstValue.Properties.isJoinable(OutVal.Properties))
      return false;

    for (unsigned Idx = 0; Idx < FirstValue.getLocationOpCount(); ++Idx) {
      // An unjoined PHI has no defined locations, and so a shared location must
      // be found for every operand.
      if (OutVal.isUnjoinedPHI()) {
        LocOpsToJoin.insert(Idx);
        continue;
      }
      DbgOpID FirstValOp = FirstValue.getDbgOpID(Idx);
      DbgOpID OutValOp = OutVal.getDbgOpID(Idx);
      if (FirstValOp != OutValOp) {
        // We can never join constant ops - the ops must either both be equal
        // constant ops or non-const ops.
        if (FirstValOp.isConst() || OutValOp.isConst())
          return false;
        else
          LocOpsToJoin.insert(Idx);
      }
    }
  }

  SmallVector<DbgOpID> NewDbgOps;

  for (unsigned Idx = 0; Idx < FirstValue.getLocationOpCount(); ++Idx) {
    // If this op doesn't need to be joined because the values agree, use that
    // already-agreed value.
    if (!LocOpsToJoin.contains(Idx)) {
      NewDbgOps.push_back(FirstValue.getDbgOpID(Idx));
      continue;
    }

    std::optional<ValueIDNum> JoinedOpLoc =
        pickOperandPHILoc(Idx, MBB, LiveOuts, MOutLocs, BlockOrders);

    if (!JoinedOpLoc)
      return false;

    NewDbgOps.push_back(DbgOpStore.insert(*JoinedOpLoc));
  }

  OutValues.append(NewDbgOps);
  return true;
}

std::optional<ValueIDNum> InstrRefBasedLDV::pickOperandPHILoc(
    unsigned DbgOpIdx, const MachineBasicBlock &MBB, const LiveIdxT &LiveOuts,
    FuncValueTable &MOutLocs,
    const SmallVectorImpl<const MachineBasicBlock *> &BlockOrders) {

  // Collect a set of locations from predecessor where its live-out value can
  // be found.
  SmallVector<SmallVector<LocIdx, 4>, 8> Locs;
  unsigned NumLocs = MTracker->getNumLocs();

  for (const auto p : BlockOrders) {
    auto OutValIt = LiveOuts.find(p);
    assert(OutValIt != LiveOuts.end());
    const DbgValue &OutVal = *OutValIt->second;
    DbgOpID OutValOpID = OutVal.getDbgOpID(DbgOpIdx);
    DbgOp OutValOp = DbgOpStore.find(OutValOpID);
    assert(!OutValOp.IsConst);

    // Create new empty vector of locations.
    Locs.resize(Locs.size() + 1);

    // If the live-in value is a def, find the locations where that value is
    // present. Do the same for VPHIs where we know the VPHI value.
    if (OutVal.Kind == DbgValue::Def ||
        (OutVal.Kind == DbgValue::VPHI && OutVal.BlockNo != MBB.getNumber() &&
         !OutValOp.isUndef())) {
      ValueIDNum ValToLookFor = OutValOp.ID;
      // Search the live-outs of the predecessor for the specified value.
      for (unsigned int I = 0; I < NumLocs; ++I) {
        if (MOutLocs[*p][I] == ValToLookFor)
          Locs.back().push_back(LocIdx(I));
      }
    } else {
      assert(OutVal.Kind == DbgValue::VPHI);
      // Otherwise: this is a VPHI on a backedge feeding back into itself, i.e.
      // a value that's live-through the whole loop. (It has to be a backedge,
      // because a block can't dominate itself). We can accept as a PHI location
      // any location where the other predecessors agree, _and_ the machine
      // locations feed back into themselves. Therefore, add all self-looping
      // machine-value PHI locations.
      for (unsigned int I = 0; I < NumLocs; ++I) {
        ValueIDNum MPHI(MBB.getNumber(), 0, LocIdx(I));
        if (MOutLocs[*p][I] == MPHI)
          Locs.back().push_back(LocIdx(I));
      }
    }
  }
  // We should have found locations for all predecessors, or returned.
  assert(Locs.size() == BlockOrders.size());

  // Starting with the first set of locations, take the intersection with
  // subsequent sets.
  SmallVector<LocIdx, 4> CandidateLocs = Locs[0];
  for (unsigned int I = 1; I < Locs.size(); ++I) {
    auto &LocVec = Locs[I];
    SmallVector<LocIdx, 4> NewCandidates;
    std::set_intersection(CandidateLocs.begin(), CandidateLocs.end(),
                          LocVec.begin(), LocVec.end(), std::inserter(NewCandidates, NewCandidates.begin()));
    CandidateLocs = NewCandidates;
  }
  if (CandidateLocs.empty())
    return std::nullopt;

  // We now have a set of LocIdxes that contain the right output value in
  // each of the predecessors. Pick the lowest; if there's a register loc,
  // that'll be it.
  LocIdx L = *CandidateLocs.begin();

  // Return a PHI-value-number for the found location.
  ValueIDNum PHIVal = {(unsigned)MBB.getNumber(), 0, L};
  return PHIVal;
}

bool InstrRefBasedLDV::vlocJoin(
    MachineBasicBlock &MBB, LiveIdxT &VLOCOutLocs,
    SmallPtrSet<const MachineBasicBlock *, 8> &BlocksToExplore,
    DbgValue &LiveIn) {
  LLVM_DEBUG(dbgs() << "join MBB: " << MBB.getNumber() << "\n");
  bool Changed = false;

  // Order predecessors by RPOT order, for exploring them in that order.
  SmallVector<MachineBasicBlock *, 8> BlockOrders(MBB.predecessors());

  auto Cmp = [&](MachineBasicBlock *A, MachineBasicBlock *B) {
    return BBToOrder[A] < BBToOrder[B];
  };

  llvm::sort(BlockOrders, Cmp);

  unsigned CurBlockRPONum = BBToOrder[&MBB];

  // Collect all the incoming DbgValues for this variable, from predecessor
  // live-out values.
  SmallVector<InValueT, 8> Values;
  bool Bail = false;
  int BackEdgesStart = 0;
  for (auto *p : BlockOrders) {
    // If the predecessor isn't in scope / to be explored, we'll never be
    // able to join any locations.
    if (!BlocksToExplore.contains(p)) {
      Bail = true;
      break;
    }

    // All Live-outs will have been initialized.
    DbgValue &OutLoc = *VLOCOutLocs.find(p)->second;

    // Keep track of where back-edges begin in the Values vector. Relies on
    // BlockOrders being sorted by RPO.
    unsigned ThisBBRPONum = BBToOrder[p];
    if (ThisBBRPONum < CurBlockRPONum)
      ++BackEdgesStart;

    Values.push_back(std::make_pair(p, &OutLoc));
  }

  // If there were no values, or one of the predecessors couldn't have a
  // value, then give up immediately. It's not safe to produce a live-in
  // value. Leave as whatever it was before.
  if (Bail || Values.size() == 0)
    return false;

  // All (non-entry) blocks have at least one non-backedge predecessor.
  // Pick the variable value from the first of these, to compare against
  // all others.
  const DbgValue &FirstVal = *Values[0].second;

  // If the old live-in value is not a PHI then either a) no PHI is needed
  // here, or b) we eliminated the PHI that was here. If so, we can just
  // propagate in the first parent's incoming value.
  if (LiveIn.Kind != DbgValue::VPHI || LiveIn.BlockNo != MBB.getNumber()) {
    Changed = LiveIn != FirstVal;
    if (Changed)
      LiveIn = FirstVal;
    return Changed;
  }

  // Scan for variable values that can never be resolved: if they have
  // different DIExpressions, different indirectness, or are mixed constants /
  // non-constants.
  for (const auto &V : Values) {
    if (!V.second->Properties.isJoinable(FirstVal.Properties))
      return false;
    if (V.second->Kind == DbgValue::NoVal)
      return false;
    if (!V.second->hasJoinableLocOps(FirstVal))
      return false;
  }

  // Try to eliminate this PHI. Do the incoming values all agree?
  bool Disagree = false;
  for (auto &V : Values) {
    if (*V.second == FirstVal)
      continue; // No disagreement.

    // If both values are not equal but have equal non-empty IDs then they refer
    // to the same value from different sources (e.g. one is VPHI and the other
    // is Def), which does not cause disagreement.
    if (V.second->hasIdenticalValidLocOps(FirstVal))
      continue;

    // Eliminate if a backedge feeds a VPHI back into itself.
    if (V.second->Kind == DbgValue::VPHI &&
        V.second->BlockNo == MBB.getNumber() &&
        // Is this a backedge?
        std::distance(Values.begin(), &V) >= BackEdgesStart)
      continue;

    Disagree = true;
  }

  // No disagreement -> live-through value.
  if (!Disagree) {
    Changed = LiveIn != FirstVal;
    if (Changed)
      LiveIn = FirstVal;
    return Changed;
  } else {
    // Otherwise use a VPHI.
    DbgValue VPHI(MBB.getNumber(), FirstVal.Properties, DbgValue::VPHI);
    Changed = LiveIn != VPHI;
    if (Changed)
      LiveIn = VPHI;
    return Changed;
  }
}

void InstrRefBasedLDV::getBlocksForScope(
    const DILocation *DILoc,
    SmallPtrSetImpl<const MachineBasicBlock *> &BlocksToExplore,
    const SmallPtrSetImpl<MachineBasicBlock *> &AssignBlocks) {
  // Get the set of "normal" in-lexical-scope blocks.
  LS.getMachineBasicBlocks(DILoc, BlocksToExplore);

  // VarLoc LiveDebugValues tracks variable locations that are defined in
  // blocks not in scope. This is something we could legitimately ignore, but
  // lets allow it for now for the sake of coverage.
  BlocksToExplore.insert(AssignBlocks.begin(), AssignBlocks.end());

  // Storage for artificial blocks we intend to add to BlocksToExplore.
  DenseSet<const MachineBasicBlock *> ToAdd;

  // To avoid needlessly dropping large volumes of variable locations, propagate
  // variables through aritifical blocks, i.e. those that don't have any
  // instructions in scope at all. To accurately replicate VarLoc
  // LiveDebugValues, this means exploring all artificial successors too.
  // Perform a depth-first-search to enumerate those blocks.
  for (const auto *MBB : BlocksToExplore) {
    // Depth-first-search state: each node is a block and which successor
    // we're currently exploring.
    SmallVector<std::pair<const MachineBasicBlock *,
                          MachineBasicBlock::const_succ_iterator>,
                8>
        DFS;

    // Find any artificial successors not already tracked.
    for (auto *succ : MBB->successors()) {
      if (BlocksToExplore.count(succ))
        continue;
      if (!ArtificialBlocks.count(succ))
        continue;
      ToAdd.insert(succ);
      DFS.push_back({succ, succ->succ_begin()});
    }

    // Search all those blocks, depth first.
    while (!DFS.empty()) {
      const MachineBasicBlock *CurBB = DFS.back().first;
      MachineBasicBlock::const_succ_iterator &CurSucc = DFS.back().second;
      // Walk back if we've explored this blocks successors to the end.
      if (CurSucc == CurBB->succ_end()) {
        DFS.pop_back();
        continue;
      }

      // If the current successor is artificial and unexplored, descend into
      // it.
      if (!ToAdd.count(*CurSucc) && ArtificialBlocks.count(*CurSucc)) {
        ToAdd.insert(*CurSucc);
        DFS.push_back({*CurSucc, (*CurSucc)->succ_begin()});
        continue;
      }

      ++CurSucc;
    }
  };

  BlocksToExplore.insert(ToAdd.begin(), ToAdd.end());
}

void InstrRefBasedLDV::buildVLocValueMap(
    const DILocation *DILoc,
    const SmallSet<DebugVariableID, 4> &VarsWeCareAbout,
    SmallPtrSetImpl<MachineBasicBlock *> &AssignBlocks, LiveInsT &Output,
    FuncValueTable &MOutLocs, FuncValueTable &MInLocs,
    SmallVectorImpl<VLocTracker> &AllTheVLocs) {
  // This method is much like buildMLocValueMap: but focuses on a single
  // LexicalScope at a time. Pick out a set of blocks and variables that are
  // to have their value assignments solved, then run our dataflow algorithm
  // until a fixedpoint is reached.
  std::priority_queue<unsigned int, std::vector<unsigned int>,
                      std::greater<unsigned int>>
      Worklist, Pending;
  SmallPtrSet<MachineBasicBlock *, 16> OnWorklist, OnPending;

  // The set of blocks we'll be examining.
  SmallPtrSet<const MachineBasicBlock *, 8> BlocksToExplore;

  // The order in which to examine them (RPO).
  SmallVector<MachineBasicBlock *, 16> BlockOrders;
  SmallVector<unsigned, 32> BlockOrderNums;

  getBlocksForScope(DILoc, BlocksToExplore, AssignBlocks);

  // Single block scope: not interesting! No propagation at all. Note that
  // this could probably go above ArtificialBlocks without damage, but
  // that then produces output differences from original-live-debug-values,
  // which propagates from a single block into many artificial ones.
  if (BlocksToExplore.size() == 1)
    return;

  // Convert a const set to a non-const set. LexicalScopes
  // getMachineBasicBlocks returns const MBB pointers, IDF wants mutable ones.
  // (Neither of them mutate anything).
  SmallPtrSet<MachineBasicBlock *, 8> MutBlocksToExplore;
  for (const auto *MBB : BlocksToExplore)
    MutBlocksToExplore.insert(const_cast<MachineBasicBlock *>(MBB));

  // Picks out relevants blocks RPO order and sort them. Sort their
  // order-numbers and map back to MBB pointers later, to avoid repeated
  // DenseMap queries during comparisons.
  for (const auto *MBB : BlocksToExplore)
    BlockOrderNums.push_back(BBToOrder[MBB]);

  llvm::sort(BlockOrderNums);
  for (unsigned int I : BlockOrderNums)
    BlockOrders.push_back(OrderToBB[I]);
  BlockOrderNums.clear();
  unsigned NumBlocks = BlockOrders.size();

  // Allocate some vectors for storing the live ins and live outs. Large.
  SmallVector<DbgValue, 32> LiveIns, LiveOuts;
  LiveIns.reserve(NumBlocks);
  LiveOuts.reserve(NumBlocks);

  // Initialize all values to start as NoVals. This signifies "it's live
  // through, but we don't know what it is".
  DbgValueProperties EmptyProperties(EmptyExpr, false, false);
  for (unsigned int I = 0; I < NumBlocks; ++I) {
    DbgValue EmptyDbgValue(I, EmptyProperties, DbgValue::NoVal);
    LiveIns.push_back(EmptyDbgValue);
    LiveOuts.push_back(EmptyDbgValue);
  }

  // Produce by-MBB indexes of live-in/live-outs, to ease lookup within
  // vlocJoin.
  LiveIdxT LiveOutIdx, LiveInIdx;
  LiveOutIdx.reserve(NumBlocks);
  LiveInIdx.reserve(NumBlocks);
  for (unsigned I = 0; I < NumBlocks; ++I) {
    LiveOutIdx[BlockOrders[I]] = &LiveOuts[I];
    LiveInIdx[BlockOrders[I]] = &LiveIns[I];
  }

  // Loop over each variable and place PHIs for it, then propagate values
  // between blocks. This keeps the locality of working on one lexical scope at
  // at time, but avoids re-processing variable values because some other
  // variable has been assigned.
  for (DebugVariableID VarID : VarsWeCareAbout) {
    // Re-initialize live-ins and live-outs, to clear the remains of previous
    // variables live-ins / live-outs.
    for (unsigned int I = 0; I < NumBlocks; ++I) {
      DbgValue EmptyDbgValue(I, EmptyProperties, DbgValue::NoVal);
      LiveIns[I] = EmptyDbgValue;
      LiveOuts[I] = EmptyDbgValue;
    }

    // Place PHIs for variable values, using the LLVM IDF calculator.
    // Collect the set of blocks where variables are def'd.
    SmallPtrSet<MachineBasicBlock *, 32> DefBlocks;
    for (const MachineBasicBlock *ExpMBB : BlocksToExplore) {
      auto &TransferFunc = AllTheVLocs[ExpMBB->getNumber()].Vars;
      if (TransferFunc.contains(VarID))
        DefBlocks.insert(const_cast<MachineBasicBlock *>(ExpMBB));
    }

    SmallVector<MachineBasicBlock *, 32> PHIBlocks;

    // Request the set of PHIs we should insert for this variable. If there's
    // only one value definition, things are very simple.
    if (DefBlocks.size() == 1) {
      placePHIsForSingleVarDefinition(MutBlocksToExplore, *DefBlocks.begin(),
                                      AllTheVLocs, VarID, Output);
      continue;
    }

    // Otherwise: we need to place PHIs through SSA and propagate values.
    BlockPHIPlacement(MutBlocksToExplore, DefBlocks, PHIBlocks);

    // Insert PHIs into the per-block live-in tables for this variable.
    for (MachineBasicBlock *PHIMBB : PHIBlocks) {
      unsigned BlockNo = PHIMBB->getNumber();
      DbgValue *LiveIn = LiveInIdx[PHIMBB];
      *LiveIn = DbgValue(BlockNo, EmptyProperties, DbgValue::VPHI);
    }

    for (auto *MBB : BlockOrders) {
      Worklist.push(BBToOrder[MBB]);
      OnWorklist.insert(MBB);
    }

    // Iterate over all the blocks we selected, propagating the variables value.
    // This loop does two things:
    //  * Eliminates un-necessary VPHIs in vlocJoin,
    //  * Evaluates the blocks transfer function (i.e. variable assignments) and
    //    stores the result to the blocks live-outs.
    // Always evaluate the transfer function on the first iteration, and when
    // the live-ins change thereafter.
    bool FirstTrip = true;
    while (!Worklist.empty() || !Pending.empty()) {
      while (!Worklist.empty()) {
        auto *MBB = OrderToBB[Worklist.top()];
        CurBB = MBB->getNumber();
        Worklist.pop();

        auto LiveInsIt = LiveInIdx.find(MBB);
        assert(LiveInsIt != LiveInIdx.end());
        DbgValue *LiveIn = LiveInsIt->second;

        // Join values from predecessors. Updates LiveInIdx, and writes output
        // into JoinedInLocs.
        bool InLocsChanged =
            vlocJoin(*MBB, LiveOutIdx, BlocksToExplore, *LiveIn);

        SmallVector<const MachineBasicBlock *, 8> Preds;
        for (const auto *Pred : MBB->predecessors())
          Preds.push_back(Pred);

        // If this block's live-in value is a VPHI, try to pick a machine-value
        // for it. This makes the machine-value available and propagated
        // through all blocks by the time value propagation finishes. We can't
        // do this any earlier as it needs to read the block live-outs.
        if (LiveIn->Kind == DbgValue::VPHI && LiveIn->BlockNo == (int)CurBB) {
          // There's a small possibility that on a preceeding path, a VPHI is
          // eliminated and transitions from VPHI-with-location to
          // live-through-value. As a result, the selected location of any VPHI
          // might change, so we need to re-compute it on each iteration.
          SmallVector<DbgOpID> JoinedOps;

          if (pickVPHILoc(JoinedOps, *MBB, LiveOutIdx, MOutLocs, Preds)) {
            bool NewLocPicked = !equal(LiveIn->getDbgOpIDs(), JoinedOps);
            InLocsChanged |= NewLocPicked;
            if (NewLocPicked)
              LiveIn->setDbgOpIDs(JoinedOps);
          }
        }

        if (!InLocsChanged && !FirstTrip)
          continue;

        DbgValue *LiveOut = LiveOutIdx[MBB];
        bool OLChanged = false;

        // Do transfer function.
        auto &VTracker = AllTheVLocs[MBB->getNumber()];
        auto TransferIt = VTracker.Vars.find(VarID);
        if (TransferIt != VTracker.Vars.end()) {
          // Erase on empty transfer (DBG_VALUE $noreg).
          if (TransferIt->second.Kind == DbgValue::Undef) {
            DbgValue NewVal(MBB->getNumber(), EmptyProperties, DbgValue::NoVal);
            if (*LiveOut != NewVal) {
              *LiveOut = NewVal;
              OLChanged = true;
            }
          } else {
            // Insert new variable value; or overwrite.
            if (*LiveOut != TransferIt->second) {
              *LiveOut = TransferIt->second;
              OLChanged = true;
            }
          }
        } else {
          // Just copy live-ins to live-outs, for anything not transferred.
          if (*LiveOut != *LiveIn) {
            *LiveOut = *LiveIn;
            OLChanged = true;
          }
        }

        // If no live-out value changed, there's no need to explore further.
        if (!OLChanged)
          continue;

        // We should visit all successors. Ensure we'll visit any non-backedge
        // successors during this dataflow iteration; book backedge successors
        // to be visited next time around.
        for (auto *s : MBB->successors()) {
          // Ignore out of scope / not-to-be-explored successors.
          if (!LiveInIdx.contains(s))
            continue;

          if (BBToOrder[s] > BBToOrder[MBB]) {
            if (OnWorklist.insert(s).second)
              Worklist.push(BBToOrder[s]);
          } else if (OnPending.insert(s).second && (FirstTrip || OLChanged)) {
            Pending.push(BBToOrder[s]);
          }
        }
      }
      Worklist.swap(Pending);
      std::swap(OnWorklist, OnPending);
      OnPending.clear();
      assert(Pending.empty());
      FirstTrip = false;
    }

    // Save live-ins to output vector. Ignore any that are still marked as being
    // VPHIs with no location -- those are variables that we know the value of,
    // but are not actually available in the register file.
    for (auto *MBB : BlockOrders) {
      DbgValue *BlockLiveIn = LiveInIdx[MBB];
      if (BlockLiveIn->Kind == DbgValue::NoVal)
        continue;
      if (BlockLiveIn->isUnjoinedPHI())
        continue;
      if (BlockLiveIn->Kind == DbgValue::VPHI)
        BlockLiveIn->Kind = DbgValue::Def;
      [[maybe_unused]] auto &[Var, DILoc] = DVMap.lookupDVID(VarID);
      assert(BlockLiveIn->Properties.DIExpr->getFragmentInfo() ==
                 Var.getFragment() &&
             "Fragment info missing during value prop");
      Output[MBB->getNumber()].push_back(std::make_pair(VarID, *BlockLiveIn));
    }
  } // Per-variable loop.

  BlockOrders.clear();
  BlocksToExplore.clear();
}

void InstrRefBasedLDV::placePHIsForSingleVarDefinition(
    const SmallPtrSetImpl<MachineBasicBlock *> &InScopeBlocks,
    MachineBasicBlock *AssignMBB, SmallVectorImpl<VLocTracker> &AllTheVLocs,
    DebugVariableID VarID, LiveInsT &Output) {
  // If there is a single definition of the variable, then working out it's
  // value everywhere is very simple: it's every block dominated by the
  // definition. At the dominance frontier, the usual algorithm would:
  //  * Place PHIs,
  //  * Propagate values into them,
  //  * Find there's no incoming variable value from the other incoming branches
  //    of the dominance frontier,
  //  * Specify there's no variable value in blocks past the frontier.
  // This is a common case, hence it's worth special-casing it.

  // Pick out the variables value from the block transfer function.
  VLocTracker &VLocs = AllTheVLocs[AssignMBB->getNumber()];
  auto ValueIt = VLocs.Vars.find(VarID);
  const DbgValue &Value = ValueIt->second;

  // If it's an explicit assignment of "undef", that means there is no location
  // anyway, anywhere.
  if (Value.Kind == DbgValue::Undef)
    return;

  // Assign the variable value to entry to each dominated block that's in scope.
  // Skip the definition block -- it's assigned the variable value in the middle
  // of the block somewhere.
  for (auto *ScopeBlock : InScopeBlocks) {
    if (!DomTree->properlyDominates(AssignMBB, ScopeBlock))
      continue;

    Output[ScopeBlock->getNumber()].push_back({VarID, Value});
  }

  // All blocks that aren't dominated have no live-in value, thus no variable
  // value will be given to them.
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void InstrRefBasedLDV::dump_mloc_transfer(
    const MLocTransferMap &mloc_transfer) const {
  for (const auto &P : mloc_transfer) {
    std::string foo = MTracker->LocIdxToName(P.first);
    std::string bar = MTracker->IDAsString(P.second);
    dbgs() << "Loc " << foo << " --> " << bar << "\n";
  }
}
#endif

void InstrRefBasedLDV::initialSetup(MachineFunction &MF) {
  // Build some useful data structures.

  LLVMContext &Context = MF.getFunction().getContext();
  EmptyExpr = DIExpression::get(Context, {});

  auto hasNonArtificialLocation = [](const MachineInstr &MI) -> bool {
    if (const DebugLoc &DL = MI.getDebugLoc())
      return DL.getLine() != 0;
    return false;
  };

  // Collect a set of all the artificial blocks. Collect the size too, ilist
  // size calls are O(n).
  unsigned int Size = 0;
  for (auto &MBB : MF) {
    ++Size;
    if (none_of(MBB.instrs(), hasNonArtificialLocation))
      ArtificialBlocks.insert(&MBB);
  }

  // Compute mappings of block <=> RPO order.
  ReversePostOrderTraversal<MachineFunction *> RPOT(&MF);
  unsigned int RPONumber = 0;
  OrderToBB.reserve(Size);
  BBToOrder.reserve(Size);
  BBNumToRPO.reserve(Size);
  auto processMBB = [&](MachineBasicBlock *MBB) {
    OrderToBB.push_back(MBB);
    BBToOrder[MBB] = RPONumber;
    BBNumToRPO[MBB->getNumber()] = RPONumber;
    ++RPONumber;
  };
  for (MachineBasicBlock *MBB : RPOT)
    processMBB(MBB);
  for (MachineBasicBlock &MBB : MF)
    if (!BBToOrder.contains(&MBB))
      processMBB(&MBB);

  // Order value substitutions by their "source" operand pair, for quick lookup.
  llvm::sort(MF.DebugValueSubstitutions);

#ifdef EXPENSIVE_CHECKS
  // As an expensive check, test whether there are any duplicate substitution
  // sources in the collection.
  if (MF.DebugValueSubstitutions.size() > 2) {
    for (auto It = MF.DebugValueSubstitutions.begin();
         It != std::prev(MF.DebugValueSubstitutions.end()); ++It) {
      assert(It->Src != std::next(It)->Src && "Duplicate variable location "
                                              "substitution seen");
    }
  }
#endif
}

// Produce an "ejection map" for blocks, i.e., what's the highest-numbered
// lexical scope it's used in. When exploring in DFS order and we pass that
// scope, the block can be processed and any tracking information freed.
void InstrRefBasedLDV::makeDepthFirstEjectionMap(
    SmallVectorImpl<unsigned> &EjectionMap,
    const ScopeToDILocT &ScopeToDILocation,
    ScopeToAssignBlocksT &ScopeToAssignBlocks) {
  SmallPtrSet<const MachineBasicBlock *, 8> BlocksToExplore;
  SmallVector<std::pair<LexicalScope *, ssize_t>, 4> WorkStack;
  auto *TopScope = LS.getCurrentFunctionScope();

  // Unlike lexical scope explorers, we explore in reverse order, to find the
  // "last" lexical scope used for each block early.
  WorkStack.push_back({TopScope, TopScope->getChildren().size() - 1});

  while (!WorkStack.empty()) {
    auto &ScopePosition = WorkStack.back();
    LexicalScope *WS = ScopePosition.first;
    ssize_t ChildNum = ScopePosition.second--;

    const SmallVectorImpl<LexicalScope *> &Children = WS->getChildren();
    if (ChildNum >= 0) {
      // If ChildNum is positive, there are remaining children to explore.
      // Push the child and its children-count onto the stack.
      auto &ChildScope = Children[ChildNum];
      WorkStack.push_back(
          std::make_pair(ChildScope, ChildScope->getChildren().size() - 1));
    } else {
      WorkStack.pop_back();

      // We've explored all children and any later blocks: examine all blocks
      // in our scope. If they haven't yet had an ejection number set, then
      // this scope will be the last to use that block.
      auto DILocationIt = ScopeToDILocation.find(WS);
      if (DILocationIt != ScopeToDILocation.end()) {
        getBlocksForScope(DILocationIt->second, BlocksToExplore,
                          ScopeToAssignBlocks.find(WS)->second);
        for (const auto *MBB : BlocksToExplore) {
          unsigned BBNum = MBB->getNumber();
          if (EjectionMap[BBNum] == 0)
            EjectionMap[BBNum] = WS->getDFSOut();
        }

        BlocksToExplore.clear();
      }
    }
  }
}

bool InstrRefBasedLDV::depthFirstVLocAndEmit(
    unsigned MaxNumBlocks, const ScopeToDILocT &ScopeToDILocation,
    const ScopeToVarsT &ScopeToVars, ScopeToAssignBlocksT &ScopeToAssignBlocks,
    LiveInsT &Output, FuncValueTable &MOutLocs, FuncValueTable &MInLocs,
    SmallVectorImpl<VLocTracker> &AllTheVLocs, MachineFunction &MF,
    const TargetPassConfig &TPC) {
  TTracker =
      new TransferTracker(TII, MTracker, MF, DVMap, *TRI, CalleeSavedRegs, TPC);
  unsigned NumLocs = MTracker->getNumLocs();
  VTracker = nullptr;

  // No scopes? No variable locations.
  if (!LS.getCurrentFunctionScope())
    return false;

  // Build map from block number to the last scope that uses the block.
  SmallVector<unsigned, 16> EjectionMap;
  EjectionMap.resize(MaxNumBlocks, 0);
  makeDepthFirstEjectionMap(EjectionMap, ScopeToDILocation,
                            ScopeToAssignBlocks);

  // Helper lambda for ejecting a block -- if nothing is going to use the block,
  // we can translate the variable location information into DBG_VALUEs and then
  // free all of InstrRefBasedLDV's data structures.
  auto EjectBlock = [&](MachineBasicBlock &MBB) -> void {
    unsigned BBNum = MBB.getNumber();
    AllTheVLocs[BBNum].clear();

    // Prime the transfer-tracker, and then step through all the block
    // instructions, installing transfers.
    MTracker->reset();
    MTracker->loadFromArray(MInLocs[MBB], BBNum);
    TTracker->loadInlocs(MBB, MInLocs[MBB], DbgOpStore, Output[BBNum], NumLocs);

    CurBB = BBNum;
    CurInst = 1;
    for (auto &MI : MBB) {
      process(MI, &MOutLocs, &MInLocs);
      TTracker->checkInstForNewValues(CurInst, MI.getIterator());
      ++CurInst;
    }

    // Free machine-location tables for this block.
    MInLocs.ejectTableForBlock(MBB);
    MOutLocs.ejectTableForBlock(MBB);
    // We don't need live-in variable values for this block either.
    Output[BBNum].clear();
    AllTheVLocs[BBNum].clear();
  };

  SmallPtrSet<const MachineBasicBlock *, 8> BlocksToExplore;
  SmallVector<std::pair<LexicalScope *, ssize_t>, 4> WorkStack;
  WorkStack.push_back({LS.getCurrentFunctionScope(), 0});
  unsigned HighestDFSIn = 0;

  // Proceed to explore in depth first order.
  while (!WorkStack.empty()) {
    auto &ScopePosition = WorkStack.back();
    LexicalScope *WS = ScopePosition.first;
    ssize_t ChildNum = ScopePosition.second++;

    // We obesrve scopes with children twice here, once descending in, once
    // ascending out of the scope nest. Use HighestDFSIn as a ratchet to ensure
    // we don't process a scope twice. Additionally, ignore scopes that don't
    // have a DILocation -- by proxy, this means we never tracked any variable
    // assignments in that scope.
    auto DILocIt = ScopeToDILocation.find(WS);
    if (HighestDFSIn <= WS->getDFSIn() && DILocIt != ScopeToDILocation.end()) {
      const DILocation *DILoc = DILocIt->second;
      auto &VarsWeCareAbout = ScopeToVars.find(WS)->second;
      auto &BlocksInScope = ScopeToAssignBlocks.find(WS)->second;

      buildVLocValueMap(DILoc, VarsWeCareAbout, BlocksInScope, Output, MOutLocs,
                        MInLocs, AllTheVLocs);
    }

    HighestDFSIn = std::max(HighestDFSIn, WS->getDFSIn());

    // Descend into any scope nests.
    const SmallVectorImpl<LexicalScope *> &Children = WS->getChildren();
    if (ChildNum < (ssize_t)Children.size()) {
      // There are children to explore -- push onto stack and continue.
      auto &ChildScope = Children[ChildNum];
      WorkStack.push_back(std::make_pair(ChildScope, 0));
    } else {
      WorkStack.pop_back();

      // We've explored a leaf, or have explored all the children of a scope.
      // Try to eject any blocks where this is the last scope it's relevant to.
      auto DILocationIt = ScopeToDILocation.find(WS);
      if (DILocationIt == ScopeToDILocation.end())
        continue;

      getBlocksForScope(DILocationIt->second, BlocksToExplore,
                        ScopeToAssignBlocks.find(WS)->second);
      for (const auto *MBB : BlocksToExplore)
        if (WS->getDFSOut() == EjectionMap[MBB->getNumber()])
          EjectBlock(const_cast<MachineBasicBlock &>(*MBB));

      BlocksToExplore.clear();
    }
  }

  // Some artificial blocks may not have been ejected, meaning they're not
  // connected to an actual legitimate scope. This can technically happen
  // with things like the entry block. In theory, we shouldn't need to do
  // anything for such out-of-scope blocks, but for the sake of being similar
  // to VarLocBasedLDV, eject these too.
  for (auto *MBB : ArtificialBlocks)
    if (MInLocs.hasTableFor(*MBB))
      EjectBlock(*MBB);

  return emitTransfers();
}

bool InstrRefBasedLDV::emitTransfers() {
  // Go through all the transfers recorded in the TransferTracker -- this is
  // both the live-ins to a block, and any movements of values that happen
  // in the middle.
  for (auto &P : TTracker->Transfers) {
    // We have to insert DBG_VALUEs in a consistent order, otherwise they
    // appear in DWARF in different orders. Use the order that they appear
    // when walking through each block / each instruction, stored in
    // DVMap.
    llvm::sort(P.Insts, llvm::less_first());

    // Insert either before or after the designated point...
    if (P.MBB) {
      MachineBasicBlock &MBB = *P.MBB;
      for (const auto &Pair : P.Insts)
        MBB.insert(P.Pos, Pair.second);
    } else {
      // Terminators, like tail calls, can clobber things. Don't try and place
      // transfers after them.
      if (P.Pos->isTerminator())
        continue;

      MachineBasicBlock &MBB = *P.Pos->getParent();
      for (const auto &Pair : P.Insts)
        MBB.insertAfterBundle(P.Pos, Pair.second);
    }
  }

  return TTracker->Transfers.size() != 0;
}

/// Calculate the liveness information for the given machine function and
/// extend ranges across basic blocks.
bool InstrRefBasedLDV::ExtendRanges(MachineFunction &MF,
                                    MachineDominatorTree *DomTree,
                                    TargetPassConfig *TPC,
                                    unsigned InputBBLimit,
                                    unsigned InputDbgValLimit) {
  // No subprogram means this function contains no debuginfo.
  if (!MF.getFunction().getSubprogram())
    return false;

  LLVM_DEBUG(dbgs() << "\nDebug Range Extension\n");
  this->TPC = TPC;

  this->DomTree = DomTree;
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();
  TII = MF.getSubtarget().getInstrInfo();
  TFI = MF.getSubtarget().getFrameLowering();
  TFI->getCalleeSaves(MF, CalleeSavedRegs);
  MFI = &MF.getFrameInfo();
  LS.initialize(MF);

  const auto &STI = MF.getSubtarget();
  AdjustsStackInCalls = MFI->adjustsStack() &&
                        STI.getFrameLowering()->stackProbeFunctionModifiesSP();
  if (AdjustsStackInCalls)
    StackProbeSymbolName = STI.getTargetLowering()->getStackProbeSymbolName(MF);

  MTracker =
      new MLocTracker(MF, *TII, *TRI, *MF.getSubtarget().getTargetLowering());
  VTracker = nullptr;
  TTracker = nullptr;

  SmallVector<MLocTransferMap, 32> MLocTransfer;
  SmallVector<VLocTracker, 8> vlocs;
  LiveInsT SavedLiveIns;

  int MaxNumBlocks = -1;
  for (auto &MBB : MF)
    MaxNumBlocks = std::max(MBB.getNumber(), MaxNumBlocks);
  assert(MaxNumBlocks >= 0);
  ++MaxNumBlocks;

  initialSetup(MF);

  MLocTransfer.resize(MaxNumBlocks);
  vlocs.resize(MaxNumBlocks, VLocTracker(DVMap, OverlapFragments, EmptyExpr));
  SavedLiveIns.resize(MaxNumBlocks);

  produceMLocTransferFunction(MF, MLocTransfer, MaxNumBlocks);

  // Allocate and initialize two array-of-arrays for the live-in and live-out
  // machine values. The outer dimension is the block number; while the inner
  // dimension is a LocIdx from MLocTracker.
  unsigned NumLocs = MTracker->getNumLocs();
  FuncValueTable MOutLocs(MaxNumBlocks, NumLocs);
  FuncValueTable MInLocs(MaxNumBlocks, NumLocs);

  // Solve the machine value dataflow problem using the MLocTransfer function,
  // storing the computed live-ins / live-outs into the array-of-arrays. We use
  // both live-ins and live-outs for decision making in the variable value
  // dataflow problem.
  buildMLocValueMap(MF, MInLocs, MOutLocs, MLocTransfer);

  // Patch up debug phi numbers, turning unknown block-live-in values into
  // either live-through machine values, or PHIs.
  for (auto &DBG_PHI : DebugPHINumToValue) {
    // Identify unresolved block-live-ins.
    if (!DBG_PHI.ValueRead)
      continue;

    ValueIDNum &Num = *DBG_PHI.ValueRead;
    if (!Num.isPHI())
      continue;

    unsigned BlockNo = Num.getBlock();
    LocIdx LocNo = Num.getLoc();
    ValueIDNum ResolvedValue = MInLocs[BlockNo][LocNo.asU64()];
    // If there is no resolved value for this live-in then it is not directly
    // reachable from the entry block -- model it as a PHI on entry to this
    // block, which means we leave the ValueIDNum unchanged.
    if (ResolvedValue != ValueIDNum::EmptyValue)
      Num = ResolvedValue;
  }
  // Later, we'll be looking up ranges of instruction numbers.
  llvm::sort(DebugPHINumToValue);

  // Walk back through each block / instruction, collecting DBG_VALUE
  // instructions and recording what machine value their operands refer to.
  for (MachineBasicBlock *MBB : OrderToBB) {
    CurBB = MBB->getNumber();
    VTracker = &vlocs[CurBB];
    VTracker->MBB = MBB;
    MTracker->loadFromArray(MInLocs[*MBB], CurBB);
    CurInst = 1;
    for (auto &MI : *MBB) {
      process(MI, &MOutLocs, &MInLocs);
      ++CurInst;
    }
    MTracker->reset();
  }

  // Map from one LexicalScope to all the variables in that scope.
  ScopeToVarsT ScopeToVars;

  // Map from One lexical scope to all blocks where assignments happen for
  // that scope.
  ScopeToAssignBlocksT ScopeToAssignBlocks;

  // Store map of DILocations that describes scopes.
  ScopeToDILocT ScopeToDILocation;

  // To mirror old LiveDebugValues, enumerate variables in RPOT order. Otherwise
  // the order is unimportant, it just has to be stable.
  unsigned VarAssignCount = 0;
  for (unsigned int I = 0; I < OrderToBB.size(); ++I) {
    auto *MBB = OrderToBB[I];
    auto *VTracker = &vlocs[MBB->getNumber()];
    // Collect each variable with a DBG_VALUE in this block.
    for (auto &idx : VTracker->Vars) {
      DebugVariableID VarID = idx.first;
      const DILocation *ScopeLoc = VTracker->Scopes[VarID];
      assert(ScopeLoc != nullptr);
      auto *Scope = LS.findLexicalScope(ScopeLoc);

      // No insts in scope -> shouldn't have been recorded.
      assert(Scope != nullptr);

      ScopeToVars[Scope].insert(VarID);
      ScopeToAssignBlocks[Scope].insert(VTracker->MBB);
      ScopeToDILocation[Scope] = ScopeLoc;
      ++VarAssignCount;
    }
  }

  bool Changed = false;

  // If we have an extremely large number of variable assignments and blocks,
  // bail out at this point. We've burnt some time doing analysis already,
  // however we should cut our losses.
  if ((unsigned)MaxNumBlocks > InputBBLimit &&
      VarAssignCount > InputDbgValLimit) {
    LLVM_DEBUG(dbgs() << "Disabling InstrRefBasedLDV: " << MF.getName()
                      << " has " << MaxNumBlocks << " basic blocks and "
                      << VarAssignCount
                      << " variable assignments, exceeding limits.\n");
  } else {
    // Optionally, solve the variable value problem and emit to blocks by using
    // a lexical-scope-depth search. It should be functionally identical to
    // the "else" block of this condition.
    Changed = depthFirstVLocAndEmit(
        MaxNumBlocks, ScopeToDILocation, ScopeToVars, ScopeToAssignBlocks,
        SavedLiveIns, MOutLocs, MInLocs, vlocs, MF, *TPC);
  }

  delete MTracker;
  delete TTracker;
  MTracker = nullptr;
  VTracker = nullptr;
  TTracker = nullptr;

  ArtificialBlocks.clear();
  OrderToBB.clear();
  BBToOrder.clear();
  BBNumToRPO.clear();
  DebugInstrNumToInstr.clear();
  DebugPHINumToValue.clear();
  OverlapFragments.clear();
  SeenFragments.clear();
  SeenDbgPHIs.clear();
  DbgOpStore.clear();
  DVMap.clear();

  return Changed;
}

LDVImpl *llvm::makeInstrRefBasedLiveDebugValues() {
  return new InstrRefBasedLDV();
}

namespace {
class LDVSSABlock;
class LDVSSAUpdater;

// Pick a type to identify incoming block values as we construct SSA. We
// can't use anything more robust than an integer unfortunately, as SSAUpdater
// expects to zero-initialize the type.
typedef uint64_t BlockValueNum;

/// Represents an SSA PHI node for the SSA updater class. Contains the block
/// this PHI is in, the value number it would have, and the expected incoming
/// values from parent blocks.
class LDVSSAPhi {
public:
  SmallVector<std::pair<LDVSSABlock *, BlockValueNum>, 4> IncomingValues;
  LDVSSABlock *ParentBlock;
  BlockValueNum PHIValNum;
  LDVSSAPhi(BlockValueNum PHIValNum, LDVSSABlock *ParentBlock)
      : ParentBlock(ParentBlock), PHIValNum(PHIValNum) {}

  LDVSSABlock *getParent() { return ParentBlock; }
};

/// Thin wrapper around a block predecessor iterator. Only difference from a
/// normal block iterator is that it dereferences to an LDVSSABlock.
class LDVSSABlockIterator {
public:
  MachineBasicBlock::pred_iterator PredIt;
  LDVSSAUpdater &Updater;

  LDVSSABlockIterator(MachineBasicBlock::pred_iterator PredIt,
                      LDVSSAUpdater &Updater)
      : PredIt(PredIt), Updater(Updater) {}

  bool operator!=(const LDVSSABlockIterator &OtherIt) const {
    return OtherIt.PredIt != PredIt;
  }

  LDVSSABlockIterator &operator++() {
    ++PredIt;
    return *this;
  }

  LDVSSABlock *operator*();
};

/// Thin wrapper around a block for SSA Updater interface. Necessary because
/// we need to track the PHI value(s) that we may have observed as necessary
/// in this block.
class LDVSSABlock {
public:
  MachineBasicBlock &BB;
  LDVSSAUpdater &Updater;
  using PHIListT = SmallVector<LDVSSAPhi, 1>;
  /// List of PHIs in this block. There should only ever be one.
  PHIListT PHIList;

  LDVSSABlock(MachineBasicBlock &BB, LDVSSAUpdater &Updater)
      : BB(BB), Updater(Updater) {}

  LDVSSABlockIterator succ_begin() {
    return LDVSSABlockIterator(BB.succ_begin(), Updater);
  }

  LDVSSABlockIterator succ_end() {
    return LDVSSABlockIterator(BB.succ_end(), Updater);
  }

  /// SSAUpdater has requested a PHI: create that within this block record.
  LDVSSAPhi *newPHI(BlockValueNum Value) {
    PHIList.emplace_back(Value, this);
    return &PHIList.back();
  }

  /// SSAUpdater wishes to know what PHIs already exist in this block.
  PHIListT &phis() { return PHIList; }
};

/// Utility class for the SSAUpdater interface: tracks blocks, PHIs and values
/// while SSAUpdater is exploring the CFG. It's passed as a handle / baton to
// SSAUpdaterTraits<LDVSSAUpdater>.
class LDVSSAUpdater {
public:
  /// Map of value numbers to PHI records.
  DenseMap<BlockValueNum, LDVSSAPhi *> PHIs;
  /// Map of which blocks generate Undef values -- blocks that are not
  /// dominated by any Def.
  DenseMap<MachineBasicBlock *, BlockValueNum> PoisonMap;
  /// Map of machine blocks to our own records of them.
  DenseMap<MachineBasicBlock *, LDVSSABlock *> BlockMap;
  /// Machine location where any PHI must occur.
  LocIdx Loc;
  /// Table of live-in machine value numbers for blocks / locations.
  const FuncValueTable &MLiveIns;

  LDVSSAUpdater(LocIdx L, const FuncValueTable &MLiveIns)
      : Loc(L), MLiveIns(MLiveIns) {}

  void reset() {
    for (auto &Block : BlockMap)
      delete Block.second;

    PHIs.clear();
    PoisonMap.clear();
    BlockMap.clear();
  }

  ~LDVSSAUpdater() { reset(); }

  /// For a given MBB, create a wrapper block for it. Stores it in the
  /// LDVSSAUpdater block map.
  LDVSSABlock *getSSALDVBlock(MachineBasicBlock *BB) {
    auto it = BlockMap.find(BB);
    if (it == BlockMap.end()) {
      BlockMap[BB] = new LDVSSABlock(*BB, *this);
      it = BlockMap.find(BB);
    }
    return it->second;
  }

  /// Find the live-in value number for the given block. Looks up the value at
  /// the PHI location on entry.
  BlockValueNum getValue(LDVSSABlock *LDVBB) {
    return MLiveIns[LDVBB->BB][Loc.asU64()].asU64();
  }
};

LDVSSABlock *LDVSSABlockIterator::operator*() {
  return Updater.getSSALDVBlock(*PredIt);
}

#ifndef NDEBUG

raw_ostream &operator<<(raw_ostream &out, const LDVSSAPhi &PHI) {
  out << "SSALDVPHI " << PHI.PHIValNum;
  return out;
}

#endif

} // namespace

namespace llvm {

/// Template specialization to give SSAUpdater access to CFG and value
/// information. SSAUpdater calls methods in these traits, passing in the
/// LDVSSAUpdater object, to learn about blocks and the values they define.
/// It also provides methods to create PHI nodes and track them.
template <> class SSAUpdaterTraits<LDVSSAUpdater> {
public:
  using BlkT = LDVSSABlock;
  using ValT = BlockValueNum;
  using PhiT = LDVSSAPhi;
  using BlkSucc_iterator = LDVSSABlockIterator;

  // Methods to access block successors -- dereferencing to our wrapper class.
  static BlkSucc_iterator BlkSucc_begin(BlkT *BB) { return BB->succ_begin(); }
  static BlkSucc_iterator BlkSucc_end(BlkT *BB) { return BB->succ_end(); }

  /// Iterator for PHI operands.
  class PHI_iterator {
  private:
    LDVSSAPhi *PHI;
    unsigned Idx;

  public:
    explicit PHI_iterator(LDVSSAPhi *P) // begin iterator
        : PHI(P), Idx(0) {}
    PHI_iterator(LDVSSAPhi *P, bool) // end iterator
        : PHI(P), Idx(PHI->IncomingValues.size()) {}

    PHI_iterator &operator++() {
      Idx++;
      return *this;
    }
    bool operator==(const PHI_iterator &X) const { return Idx == X.Idx; }
    bool operator!=(const PHI_iterator &X) const { return !operator==(X); }

    BlockValueNum getIncomingValue() { return PHI->IncomingValues[Idx].second; }

    LDVSSABlock *getIncomingBlock() { return PHI->IncomingValues[Idx].first; }
  };

  static inline PHI_iterator PHI_begin(PhiT *PHI) { return PHI_iterator(PHI); }

  static inline PHI_iterator PHI_end(PhiT *PHI) {
    return PHI_iterator(PHI, true);
  }

  /// FindPredecessorBlocks - Put the predecessors of BB into the Preds
  /// vector.
  static void FindPredecessorBlocks(LDVSSABlock *BB,
                                    SmallVectorImpl<LDVSSABlock *> *Preds) {
    for (MachineBasicBlock *Pred : BB->BB.predecessors())
      Preds->push_back(BB->Updater.getSSALDVBlock(Pred));
  }

  /// GetPoisonVal - Normally creates an IMPLICIT_DEF instruction with a new
  /// register. For LiveDebugValues, represents a block identified as not having
  /// any DBG_PHI predecessors.
  static BlockValueNum GetPoisonVal(LDVSSABlock *BB, LDVSSAUpdater *Updater) {
    // Create a value number for this block -- it needs to be unique and in the
    // "poison" collection, so that we know it's not real. Use a number
    // representing a PHI into this block.
    BlockValueNum Num = ValueIDNum(BB->BB.getNumber(), 0, Updater->Loc).asU64();
    Updater->PoisonMap[&BB->BB] = Num;
    return Num;
  }

  /// CreateEmptyPHI - Create a (representation of a) PHI in the given block.
  /// SSAUpdater will populate it with information about incoming values. The
  /// value number of this PHI is whatever the  machine value number problem
  /// solution determined it to be. This includes non-phi values if SSAUpdater
  /// tries to create a PHI where the incoming values are identical.
  static BlockValueNum CreateEmptyPHI(LDVSSABlock *BB, unsigned NumPreds,
                                   LDVSSAUpdater *Updater) {
    BlockValueNum PHIValNum = Updater->getValue(BB);
    LDVSSAPhi *PHI = BB->newPHI(PHIValNum);
    Updater->PHIs[PHIValNum] = PHI;
    return PHIValNum;
  }

  /// AddPHIOperand - Add the specified value as an operand of the PHI for
  /// the specified predecessor block.
  static void AddPHIOperand(LDVSSAPhi *PHI, BlockValueNum Val, LDVSSABlock *Pred) {
    PHI->IncomingValues.push_back(std::make_pair(Pred, Val));
  }

  /// ValueIsPHI - Check if the instruction that defines the specified value
  /// is a PHI instruction.
  static LDVSSAPhi *ValueIsPHI(BlockValueNum Val, LDVSSAUpdater *Updater) {
    return Updater->PHIs.lookup(Val);
  }

  /// ValueIsNewPHI - Like ValueIsPHI but also check if the PHI has no source
  /// operands, i.e., it was just added.
  static LDVSSAPhi *ValueIsNewPHI(BlockValueNum Val, LDVSSAUpdater *Updater) {
    LDVSSAPhi *PHI = ValueIsPHI(Val, Updater);
    if (PHI && PHI->IncomingValues.size() == 0)
      return PHI;
    return nullptr;
  }

  /// GetPHIValue - For the specified PHI instruction, return the value
  /// that it defines.
  static BlockValueNum GetPHIValue(LDVSSAPhi *PHI) { return PHI->PHIValNum; }
};

} // end namespace llvm

std::optional<ValueIDNum> InstrRefBasedLDV::resolveDbgPHIs(
    MachineFunction &MF, const FuncValueTable &MLiveOuts,
    const FuncValueTable &MLiveIns, MachineInstr &Here, uint64_t InstrNum) {
  // This function will be called twice per DBG_INSTR_REF, and might end up
  // computing lots of SSA information: memoize it.
  auto SeenDbgPHIIt = SeenDbgPHIs.find(std::make_pair(&Here, InstrNum));
  if (SeenDbgPHIIt != SeenDbgPHIs.end())
    return SeenDbgPHIIt->second;

  std::optional<ValueIDNum> Result =
      resolveDbgPHIsImpl(MF, MLiveOuts, MLiveIns, Here, InstrNum);
  SeenDbgPHIs.insert({std::make_pair(&Here, InstrNum), Result});
  return Result;
}

std::optional<ValueIDNum> InstrRefBasedLDV::resolveDbgPHIsImpl(
    MachineFunction &MF, const FuncValueTable &MLiveOuts,
    const FuncValueTable &MLiveIns, MachineInstr &Here, uint64_t InstrNum) {
  // Pick out records of DBG_PHI instructions that have been observed. If there
  // are none, then we cannot compute a value number.
  auto RangePair = std::equal_range(DebugPHINumToValue.begin(),
                                    DebugPHINumToValue.end(), InstrNum);
  auto LowerIt = RangePair.first;
  auto UpperIt = RangePair.second;

  // No DBG_PHI means there can be no location.
  if (LowerIt == UpperIt)
    return std::nullopt;

  // If any DBG_PHIs referred to a location we didn't understand, don't try to
  // compute a value. There might be scenarios where we could recover a value
  // for some range of DBG_INSTR_REFs, but at this point we can have high
  // confidence that we've seen a bug.
  auto DBGPHIRange = make_range(LowerIt, UpperIt);
  for (const DebugPHIRecord &DBG_PHI : DBGPHIRange)
    if (!DBG_PHI.ValueRead)
      return std::nullopt;

  // If there's only one DBG_PHI, then that is our value number.
  if (std::distance(LowerIt, UpperIt) == 1)
    return *LowerIt->ValueRead;

  // Pick out the location (physreg, slot) where any PHIs must occur. It's
  // technically possible for us to merge values in different registers in each
  // block, but highly unlikely that LLVM will generate such code after register
  // allocation.
  LocIdx Loc = *LowerIt->ReadLoc;

  // We have several DBG_PHIs, and a use position (the Here inst). All each
  // DBG_PHI does is identify a value at a program position. We can treat each
  // DBG_PHI like it's a Def of a value, and the use position is a Use of a
  // value, just like SSA. We use the bulk-standard LLVM SSA updater class to
  // determine which Def is used at the Use, and any PHIs that happen along
  // the way.
  // Adapted LLVM SSA Updater:
  LDVSSAUpdater Updater(Loc, MLiveIns);
  // Map of which Def or PHI is the current value in each block.
  DenseMap<LDVSSABlock *, BlockValueNum> AvailableValues;
  // Set of PHIs that we have created along the way.
  SmallVector<LDVSSAPhi *, 8> CreatedPHIs;

  // Each existing DBG_PHI is a Def'd value under this model. Record these Defs
  // for the SSAUpdater.
  for (const auto &DBG_PHI : DBGPHIRange) {
    LDVSSABlock *Block = Updater.getSSALDVBlock(DBG_PHI.MBB);
    const ValueIDNum &Num = *DBG_PHI.ValueRead;
    AvailableValues.insert(std::make_pair(Block, Num.asU64()));
  }

  LDVSSABlock *HereBlock = Updater.getSSALDVBlock(Here.getParent());
  const auto &AvailIt = AvailableValues.find(HereBlock);
  if (AvailIt != AvailableValues.end()) {
    // Actually, we already know what the value is -- the Use is in the same
    // block as the Def.
    return ValueIDNum::fromU64(AvailIt->second);
  }

  // Otherwise, we must use the SSA Updater. It will identify the value number
  // that we are to use, and the PHIs that must happen along the way.
  SSAUpdaterImpl<LDVSSAUpdater> Impl(&Updater, &AvailableValues, &CreatedPHIs);
  BlockValueNum ResultInt = Impl.GetValue(Updater.getSSALDVBlock(Here.getParent()));
  ValueIDNum Result = ValueIDNum::fromU64(ResultInt);

  // We have the number for a PHI, or possibly live-through value, to be used
  // at this Use. There are a number of things we have to check about it though:
  //  * Does any PHI use an 'Undef' (like an IMPLICIT_DEF) value? If so, this
  //    Use was not completely dominated by DBG_PHIs and we should abort.
  //  * Are the Defs or PHIs clobbered in a block? SSAUpdater isn't aware that
  //    we've left SSA form. Validate that the inputs to each PHI are the
  //    expected values.
  //  * Is a PHI we've created actually a merging of values, or are all the
  //    predecessor values the same, leading to a non-PHI machine value number?
  //    (SSAUpdater doesn't know that either). Remap validated PHIs into the
  //    the ValidatedValues collection below to sort this out.
  DenseMap<LDVSSABlock *, ValueIDNum> ValidatedValues;

  // Define all the input DBG_PHI values in ValidatedValues.
  for (const auto &DBG_PHI : DBGPHIRange) {
    LDVSSABlock *Block = Updater.getSSALDVBlock(DBG_PHI.MBB);
    const ValueIDNum &Num = *DBG_PHI.ValueRead;
    ValidatedValues.insert(std::make_pair(Block, Num));
  }

  // Sort PHIs to validate into RPO-order.
  SmallVector<LDVSSAPhi *, 8> SortedPHIs;
  for (auto &PHI : CreatedPHIs)
    SortedPHIs.push_back(PHI);

  llvm::sort(SortedPHIs, [&](LDVSSAPhi *A, LDVSSAPhi *B) {
    return BBToOrder[&A->getParent()->BB] < BBToOrder[&B->getParent()->BB];
  });

  for (auto &PHI : SortedPHIs) {
    ValueIDNum ThisBlockValueNum = MLiveIns[PHI->ParentBlock->BB][Loc.asU64()];

    // Are all these things actually defined?
    for (auto &PHIIt : PHI->IncomingValues) {
      // Any undef input means DBG_PHIs didn't dominate the use point.
      if (Updater.PoisonMap.contains(&PHIIt.first->BB))
        return std::nullopt;

      ValueIDNum ValueToCheck;
      const ValueTable &BlockLiveOuts = MLiveOuts[PHIIt.first->BB];

      auto VVal = ValidatedValues.find(PHIIt.first);
      if (VVal == ValidatedValues.end()) {
        // We cross a loop, and this is a backedge. LLVMs tail duplication
        // happens so late that DBG_PHI instructions should not be able to
        // migrate into loops -- meaning we can only be live-through this
        // loop.
        ValueToCheck = ThisBlockValueNum;
      } else {
        // Does the block have as a live-out, in the location we're examining,
        // the value that we expect? If not, it's been moved or clobbered.
        ValueToCheck = VVal->second;
      }

      if (BlockLiveOuts[Loc.asU64()] != ValueToCheck)
        return std::nullopt;
    }

    // Record this value as validated.
    ValidatedValues.insert({PHI->ParentBlock, ThisBlockValueNum});
  }

  // All the PHIs are valid: we can return what the SSAUpdater said our value
  // number was.
  return Result;
}
