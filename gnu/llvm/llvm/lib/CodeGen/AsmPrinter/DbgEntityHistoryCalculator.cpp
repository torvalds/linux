//===- llvm/CodeGen/AsmPrinter/DbgEntityHistoryCalculator.cpp -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/DbgEntityHistoryCalculator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <map>
#include <optional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "dwarfdebug"

namespace {
using EntryIndex = DbgValueHistoryMap::EntryIndex;
}

void InstructionOrdering::initialize(const MachineFunction &MF) {
  // We give meta instructions the same ordinal as the preceding instruction
  // because this class is written for the task of comparing positions of
  // variable location ranges against scope ranges. To reflect what we'll see
  // in the binary, when we look at location ranges we must consider all
  // DBG_VALUEs between two real instructions at the same position. And a
  // scope range which ends on a meta instruction should be considered to end
  // at the last seen real instruction. E.g.
  //
  //  1 instruction p      Both the variable location for x and for y start
  //  1 DBG_VALUE for "x"  after instruction p so we give them all the same
  //  1 DBG_VALUE for "y"  number. If a scope range ends at DBG_VALUE for "y",
  //  2 instruction q      we should treat it as ending after instruction p
  //                       because it will be the last real instruction in the
  //                       range. DBG_VALUEs at or after this position for
  //                       variables declared in the scope will have no effect.
  clear();
  unsigned Position = 0;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      InstNumberMap[&MI] = MI.isMetaInstruction() ? Position : ++Position;
}

bool InstructionOrdering::isBefore(const MachineInstr *A,
                                   const MachineInstr *B) const {
  assert(A->getParent() && B->getParent() && "Operands must have a parent");
  assert(A->getMF() == B->getMF() &&
         "Operands must be in the same MachineFunction");
  return InstNumberMap.lookup(A) < InstNumberMap.lookup(B);
}

bool DbgValueHistoryMap::startDbgValue(InlinedEntity Var,
                                       const MachineInstr &MI,
                                       EntryIndex &NewIndex) {
  // Instruction range should start with a DBG_VALUE instruction for the
  // variable.
  assert(MI.isDebugValue() && "not a DBG_VALUE");
  auto &Entries = VarEntries[Var];
  if (!Entries.empty() && Entries.back().isDbgValue() &&
      !Entries.back().isClosed() &&
      Entries.back().getInstr()->isEquivalentDbgInstr(MI)) {
    LLVM_DEBUG(dbgs() << "Coalescing identical DBG_VALUE entries:\n"
                      << "\t" << Entries.back().getInstr() << "\t" << MI
                      << "\n");
    return false;
  }
  Entries.emplace_back(&MI, Entry::DbgValue);
  NewIndex = Entries.size() - 1;
  return true;
}

EntryIndex DbgValueHistoryMap::startClobber(InlinedEntity Var,
                                            const MachineInstr &MI) {
  auto &Entries = VarEntries[Var];
  // If an instruction clobbers multiple registers that the variable is
  // described by, then we may have already created a clobbering instruction.
  if (Entries.back().isClobber() && Entries.back().getInstr() == &MI)
    return Entries.size() - 1;
  Entries.emplace_back(&MI, Entry::Clobber);
  return Entries.size() - 1;
}

void DbgValueHistoryMap::Entry::endEntry(EntryIndex Index) {
  // For now, instruction ranges are not allowed to cross basic block
  // boundaries.
  assert(isDbgValue() && "Setting end index for non-debug value");
  assert(!isClosed() && "End index has already been set");
  EndIndex = Index;
}

/// Check if the instruction range [StartMI, EndMI] intersects any instruction
/// range in Ranges. EndMI can be nullptr to indicate that the range is
/// unbounded. Assumes Ranges is ordered and disjoint. Returns true and points
/// to the first intersecting scope range if one exists.
static std::optional<ArrayRef<InsnRange>::iterator>
intersects(const MachineInstr *StartMI, const MachineInstr *EndMI,
           const ArrayRef<InsnRange> &Ranges,
           const InstructionOrdering &Ordering) {
  for (auto RangesI = Ranges.begin(), RangesE = Ranges.end();
       RangesI != RangesE; ++RangesI) {
    if (EndMI && Ordering.isBefore(EndMI, RangesI->first))
      return std::nullopt;
    if (EndMI && !Ordering.isBefore(RangesI->second, EndMI))
      return RangesI;
    if (Ordering.isBefore(StartMI, RangesI->second))
      return RangesI;
  }
  return std::nullopt;
}

void DbgValueHistoryMap::trimLocationRanges(
    const MachineFunction &MF, LexicalScopes &LScopes,
    const InstructionOrdering &Ordering) {
  // The indices of the entries we're going to remove for each variable.
  SmallVector<EntryIndex, 4> ToRemove;
  // Entry reference count for each variable. Clobbers left with no references
  // will be removed.
  SmallVector<int, 4> ReferenceCount;
  // Entries reference other entries by index. Offsets is used to remap these
  // references if any entries are removed.
  SmallVector<size_t, 4> Offsets;

  LLVM_DEBUG(dbgs() << "Trimming location ranges for function '" << MF.getName()
                    << "'\n");

  for (auto &Record : VarEntries) {
    auto &HistoryMapEntries = Record.second;
    if (HistoryMapEntries.empty())
      continue;

    InlinedEntity Entity = Record.first;
    const DILocalVariable *LocalVar = cast<DILocalVariable>(Entity.first);

    LexicalScope *Scope = nullptr;
    if (const DILocation *InlinedAt = Entity.second) {
      Scope = LScopes.findInlinedScope(LocalVar->getScope(), InlinedAt);
    } else {
      Scope = LScopes.findLexicalScope(LocalVar->getScope());
      // Ignore variables for non-inlined function level scopes. The scope
      // ranges (from scope->getRanges()) will not include any instructions
      // before the first one with a debug-location, which could cause us to
      // incorrectly drop a location. We could introduce special casing for
      // these variables, but it doesn't seem worth it because no out-of-scope
      // locations have been observed for variables declared in function level
      // scopes.
      if (Scope &&
          (Scope->getScopeNode() == Scope->getScopeNode()->getSubprogram()) &&
          (Scope->getScopeNode() == LocalVar->getScope()))
        continue;
    }

    // If there is no scope for the variable then something has probably gone
    // wrong.
    if (!Scope)
      continue;

    ToRemove.clear();
    // Zero the reference counts.
    ReferenceCount.assign(HistoryMapEntries.size(), 0);
    // Index of the DBG_VALUE which marks the start of the current location
    // range.
    EntryIndex StartIndex = 0;
    ArrayRef<InsnRange> ScopeRanges(Scope->getRanges());
    for (auto EI = HistoryMapEntries.begin(), EE = HistoryMapEntries.end();
         EI != EE; ++EI, ++StartIndex) {
      // Only DBG_VALUEs can open location ranges so skip anything else.
      if (!EI->isDbgValue())
        continue;

      // Index of the entry which closes this range.
      EntryIndex EndIndex = EI->getEndIndex();
      // If this range is closed bump the reference count of the closing entry.
      if (EndIndex != NoEntry)
        ReferenceCount[EndIndex] += 1;
      // Skip this location range if the opening entry is still referenced. It
      // may close a location range which intersects a scope range.
      // TODO: We could be 'smarter' and trim these kinds of ranges such that
      // they do not leak out of the scope ranges if they partially overlap.
      if (ReferenceCount[StartIndex] > 0)
        continue;

      const MachineInstr *StartMI = EI->getInstr();
      const MachineInstr *EndMI = EndIndex != NoEntry
                                      ? HistoryMapEntries[EndIndex].getInstr()
                                      : nullptr;
      // Check if the location range [StartMI, EndMI] intersects with any scope
      // range for the variable.
      if (auto R = intersects(StartMI, EndMI, ScopeRanges, Ordering)) {
        // Adjust ScopeRanges to exclude ranges which subsequent location ranges
        // cannot possibly intersect.
        ScopeRanges = ArrayRef<InsnRange>(*R, ScopeRanges.end());
      } else {
        // If the location range does not intersect any scope range then the
        // DBG_VALUE which opened this location range is usless, mark it for
        // removal.
        ToRemove.push_back(StartIndex);
        // Because we'll be removing this entry we need to update the reference
        // count of the closing entry, if one exists.
        if (EndIndex != NoEntry)
          ReferenceCount[EndIndex] -= 1;
        LLVM_DEBUG(dbgs() << "Dropping value outside scope range of variable: ";
                   StartMI->print(llvm::dbgs()););
      }
    }

    // If there is nothing to remove then jump to next variable.
    if (ToRemove.empty())
      continue;

    // Mark clobbers that will no longer close any location ranges for removal.
    for (size_t i = 0; i < HistoryMapEntries.size(); ++i)
      if (ReferenceCount[i] <= 0 && HistoryMapEntries[i].isClobber())
        ToRemove.push_back(i);

    llvm::sort(ToRemove);

    // Build an offset map so we can update the EndIndex of the remaining
    // entries.
    // Zero the offsets.
    Offsets.assign(HistoryMapEntries.size(), 0);
    size_t CurOffset = 0;
    auto ToRemoveItr = ToRemove.begin();
    for (size_t EntryIdx = *ToRemoveItr; EntryIdx < HistoryMapEntries.size();
         ++EntryIdx) {
      // Check if this is an entry which will be removed.
      if (ToRemoveItr != ToRemove.end() && *ToRemoveItr == EntryIdx) {
        ++ToRemoveItr;
        ++CurOffset;
      }
      Offsets[EntryIdx] = CurOffset;
    }

    // Update the EndIndex of the entries to account for those which will be
    // removed.
    for (auto &Entry : HistoryMapEntries)
      if (Entry.isClosed())
        Entry.EndIndex -= Offsets[Entry.EndIndex];

    // Now actually remove the entries. Iterate backwards so that our remaining
    // ToRemove indices are valid after each erase.
    for (EntryIndex Idx : llvm::reverse(ToRemove))
      HistoryMapEntries.erase(HistoryMapEntries.begin() + Idx);
    LLVM_DEBUG(llvm::dbgs() << "New HistoryMap('" << LocalVar->getName()
                            << "') size: " << HistoryMapEntries.size() << "\n");
  }
}

bool DbgValueHistoryMap::hasNonEmptyLocation(const Entries &Entries) const {
  for (const auto &Entry : Entries) {
    if (!Entry.isDbgValue())
      continue;

    const MachineInstr *MI = Entry.getInstr();
    assert(MI->isDebugValue());
    // A DBG_VALUE $noreg is an empty variable location
    if (MI->isUndefDebugValue())
      continue;

    return true;
  }

  return false;
}

void DbgLabelInstrMap::addInstr(InlinedEntity Label, const MachineInstr &MI) {
  assert(MI.isDebugLabel() && "not a DBG_LABEL");
  LabelInstr[Label] = &MI;
}

namespace {

// Maps physreg numbers to the variables they describe.
using InlinedEntity = DbgValueHistoryMap::InlinedEntity;
using RegDescribedVarsMap = std::map<unsigned, SmallVector<InlinedEntity, 1>>;

// Keeps track of the debug value entries that are currently live for each
// inlined entity. As the history map entries are stored in a SmallVector, they
// may be moved at insertion of new entries, so store indices rather than
// pointers.
using DbgValueEntriesMap = std::map<InlinedEntity, SmallSet<EntryIndex, 1>>;

} // end anonymous namespace

// Claim that @Var is not described by @RegNo anymore.
static void dropRegDescribedVar(RegDescribedVarsMap &RegVars, unsigned RegNo,
                                InlinedEntity Var) {
  const auto &I = RegVars.find(RegNo);
  assert(RegNo != 0U && I != RegVars.end());
  auto &VarSet = I->second;
  const auto &VarPos = llvm::find(VarSet, Var);
  assert(VarPos != VarSet.end());
  VarSet.erase(VarPos);
  // Don't keep empty sets in a map to keep it as small as possible.
  if (VarSet.empty())
    RegVars.erase(I);
}

// Claim that @Var is now described by @RegNo.
static void addRegDescribedVar(RegDescribedVarsMap &RegVars, unsigned RegNo,
                               InlinedEntity Var) {
  assert(RegNo != 0U);
  auto &VarSet = RegVars[RegNo];
  assert(!is_contained(VarSet, Var));
  VarSet.push_back(Var);
}

/// Create a clobbering entry and end all open debug value entries
/// for \p Var that are described by \p RegNo using that entry. Inserts into \p
/// FellowRegisters the set of Registers that were also used to describe \p Var
/// alongside \p RegNo.
static void clobberRegEntries(InlinedEntity Var, unsigned RegNo,
                              const MachineInstr &ClobberingInstr,
                              DbgValueEntriesMap &LiveEntries,
                              DbgValueHistoryMap &HistMap,
                              SmallVectorImpl<Register> &FellowRegisters) {
  EntryIndex ClobberIndex = HistMap.startClobber(Var, ClobberingInstr);
  // Close all entries whose values are described by the register.
  SmallVector<EntryIndex, 4> IndicesToErase;
  // If a given register appears in a live DBG_VALUE_LIST for Var alongside the
  // clobbered register, and never appears in a live DBG_VALUE* for Var without
  // the clobbered register, then it is no longer linked to the variable.
  SmallSet<Register, 4> MaybeRemovedRegisters;
  SmallSet<Register, 4> KeepRegisters;
  for (auto Index : LiveEntries[Var]) {
    auto &Entry = HistMap.getEntry(Var, Index);
    assert(Entry.isDbgValue() && "Not a DBG_VALUE in LiveEntries");
    if (Entry.getInstr()->isDebugEntryValue())
      continue;
    if (Entry.getInstr()->hasDebugOperandForReg(RegNo)) {
      IndicesToErase.push_back(Index);
      Entry.endEntry(ClobberIndex);
      for (const auto &MO : Entry.getInstr()->debug_operands())
        if (MO.isReg() && MO.getReg() && MO.getReg() != RegNo)
          MaybeRemovedRegisters.insert(MO.getReg());
    } else {
      for (const auto &MO : Entry.getInstr()->debug_operands())
        if (MO.isReg() && MO.getReg())
          KeepRegisters.insert(MO.getReg());
    }
  }

  for (Register Reg : MaybeRemovedRegisters)
    if (!KeepRegisters.contains(Reg))
      FellowRegisters.push_back(Reg);

  // Drop all entries that have ended.
  for (auto Index : IndicesToErase)
    LiveEntries[Var].erase(Index);
}

/// Add a new debug value for \p Var. Closes all overlapping debug values.
static void handleNewDebugValue(InlinedEntity Var, const MachineInstr &DV,
                                RegDescribedVarsMap &RegVars,
                                DbgValueEntriesMap &LiveEntries,
                                DbgValueHistoryMap &HistMap) {
  EntryIndex NewIndex;
  if (HistMap.startDbgValue(Var, DV, NewIndex)) {
    SmallDenseMap<unsigned, bool, 4> TrackedRegs;

    // If we have created a new debug value entry, close all preceding
    // live entries that overlap.
    SmallVector<EntryIndex, 4> IndicesToErase;
    const DIExpression *DIExpr = DV.getDebugExpression();
    for (auto Index : LiveEntries[Var]) {
      auto &Entry = HistMap.getEntry(Var, Index);
      assert(Entry.isDbgValue() && "Not a DBG_VALUE in LiveEntries");
      const MachineInstr &DV = *Entry.getInstr();
      bool Overlaps = DIExpr->fragmentsOverlap(DV.getDebugExpression());
      if (Overlaps) {
        IndicesToErase.push_back(Index);
        Entry.endEntry(NewIndex);
      }
      if (!DV.isDebugEntryValue())
        for (const MachineOperand &Op : DV.debug_operands())
          if (Op.isReg() && Op.getReg())
            TrackedRegs[Op.getReg()] |= !Overlaps;
    }

    // If the new debug value is described by a register, add tracking of
    // that register if it is not already tracked.
    if (!DV.isDebugEntryValue()) {
      for (const MachineOperand &Op : DV.debug_operands()) {
        if (Op.isReg() && Op.getReg()) {
          Register NewReg = Op.getReg();
          if (!TrackedRegs.count(NewReg))
            addRegDescribedVar(RegVars, NewReg, Var);
          LiveEntries[Var].insert(NewIndex);
          TrackedRegs[NewReg] = true;
        }
      }
    }

    // Drop tracking of registers that are no longer used.
    for (auto I : TrackedRegs)
      if (!I.second)
        dropRegDescribedVar(RegVars, I.first, Var);

    // Drop all entries that have ended, and mark the new entry as live.
    for (auto Index : IndicesToErase)
      LiveEntries[Var].erase(Index);
    LiveEntries[Var].insert(NewIndex);
  }
}

// Terminate the location range for variables described by register at
// @I by inserting @ClobberingInstr to their history.
static void clobberRegisterUses(RegDescribedVarsMap &RegVars,
                                RegDescribedVarsMap::iterator I,
                                DbgValueHistoryMap &HistMap,
                                DbgValueEntriesMap &LiveEntries,
                                const MachineInstr &ClobberingInstr) {
  // Iterate over all variables described by this register and add this
  // instruction to their history, clobbering it. All registers that also
  // describe the clobbered variables (i.e. in variadic debug values) will have
  // those Variables removed from their DescribedVars.
  for (const auto &Var : I->second) {
    SmallVector<Register, 4> FellowRegisters;
    clobberRegEntries(Var, I->first, ClobberingInstr, LiveEntries, HistMap,
                      FellowRegisters);
    for (Register RegNo : FellowRegisters)
      dropRegDescribedVar(RegVars, RegNo, Var);
  }
  RegVars.erase(I);
}

// Terminate the location range for variables described by register
// @RegNo by inserting @ClobberingInstr to their history.
static void clobberRegisterUses(RegDescribedVarsMap &RegVars, unsigned RegNo,
                                DbgValueHistoryMap &HistMap,
                                DbgValueEntriesMap &LiveEntries,
                                const MachineInstr &ClobberingInstr) {
  const auto &I = RegVars.find(RegNo);
  if (I == RegVars.end())
    return;
  clobberRegisterUses(RegVars, I, HistMap, LiveEntries, ClobberingInstr);
}

void llvm::calculateDbgEntityHistory(const MachineFunction *MF,
                                     const TargetRegisterInfo *TRI,
                                     DbgValueHistoryMap &DbgValues,
                                     DbgLabelInstrMap &DbgLabels) {
  const TargetLowering *TLI = MF->getSubtarget().getTargetLowering();
  Register SP = TLI->getStackPointerRegisterToSaveRestore();
  Register FrameReg = TRI->getFrameRegister(*MF);
  RegDescribedVarsMap RegVars;
  DbgValueEntriesMap LiveEntries;
  for (const auto &MBB : *MF) {
    for (const auto &MI : MBB) {
      if (MI.isDebugValue()) {
        assert(MI.getNumOperands() > 1 && "Invalid DBG_VALUE instruction!");
        // Use the base variable (without any DW_OP_piece expressions)
        // as index into History. The full variables including the
        // piece expressions are attached to the MI.
        const DILocalVariable *RawVar = MI.getDebugVariable();
        assert(RawVar->isValidLocationForIntrinsic(MI.getDebugLoc()) &&
               "Expected inlined-at fields to agree");
        InlinedEntity Var(RawVar, MI.getDebugLoc()->getInlinedAt());

        handleNewDebugValue(Var, MI, RegVars, LiveEntries, DbgValues);
      } else if (MI.isDebugLabel()) {
        assert(MI.getNumOperands() == 1 && "Invalid DBG_LABEL instruction!");
        const DILabel *RawLabel = MI.getDebugLabel();
        assert(RawLabel->isValidLocationForIntrinsic(MI.getDebugLoc()) &&
            "Expected inlined-at fields to agree");
        // When collecting debug information for labels, there is no MCSymbol
        // generated for it. So, we keep MachineInstr in DbgLabels in order
        // to query MCSymbol afterward.
        InlinedEntity L(RawLabel, MI.getDebugLoc()->getInlinedAt());
        DbgLabels.addInstr(L, MI);
      }

      // Meta Instructions have no output and do not change any values and so
      // can be safely ignored.
      if (MI.isMetaInstruction())
        continue;

      // Not a DBG_VALUE instruction. It may clobber registers which describe
      // some variables.
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.isDef() && MO.getReg()) {
          // Ignore call instructions that claim to clobber SP. The AArch64
          // backend does this for aggregate function arguments.
          if (MI.isCall() && MO.getReg() == SP)
            continue;
          // If this is a virtual register, only clobber it since it doesn't
          // have aliases.
          if (MO.getReg().isVirtual())
            clobberRegisterUses(RegVars, MO.getReg(), DbgValues, LiveEntries,
                                MI);
          // If this is a register def operand, it may end a debug value
          // range. Ignore frame-register defs in the epilogue and prologue,
          // we expect debuggers to understand that stack-locations are
          // invalid outside of the function body.
          else if (MO.getReg() != FrameReg ||
                   (!MI.getFlag(MachineInstr::FrameDestroy) &&
                   !MI.getFlag(MachineInstr::FrameSetup))) {
            for (MCRegAliasIterator AI(MO.getReg(), TRI, true); AI.isValid();
                 ++AI)
              clobberRegisterUses(RegVars, *AI, DbgValues, LiveEntries, MI);
          }
        } else if (MO.isRegMask()) {
          // If this is a register mask operand, clobber all debug values in
          // non-CSRs.
          SmallVector<unsigned, 32> RegsToClobber;
          // Don't consider SP to be clobbered by register masks.
          for (auto It : RegVars) {
            unsigned int Reg = It.first;
            if (Reg != SP && Register::isPhysicalRegister(Reg) &&
                MO.clobbersPhysReg(Reg))
              RegsToClobber.push_back(Reg);
          }

          for (unsigned Reg : RegsToClobber) {
            clobberRegisterUses(RegVars, Reg, DbgValues, LiveEntries, MI);
          }
        }
      } // End MO loop.
    }   // End instr loop.

    // Make sure locations for all variables are valid only until the end of
    // the basic block (unless it's the last basic block, in which case let
    // their liveness run off to the end of the function).
    if (!MBB.empty() && &MBB != &MF->back()) {
      // Iterate over all variables that have open debug values.
      for (auto &Pair : LiveEntries) {
        if (Pair.second.empty())
          continue;

        // Create a clobbering entry.
        EntryIndex ClobIdx = DbgValues.startClobber(Pair.first, MBB.back());

        // End all entries.
        for (EntryIndex Idx : Pair.second) {
          DbgValueHistoryMap::Entry &Ent = DbgValues.getEntry(Pair.first, Idx);
          assert(Ent.isDbgValue() && !Ent.isClosed());
          Ent.endEntry(ClobIdx);
        }
      }

      LiveEntries.clear();
      RegVars.clear();
    }
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DbgValueHistoryMap::dump(StringRef FuncName) const {
  dbgs() << "DbgValueHistoryMap('" << FuncName << "'):\n";
  for (const auto &VarRangePair : *this) {
    const InlinedEntity &Var = VarRangePair.first;
    const Entries &Entries = VarRangePair.second;

    const DILocalVariable *LocalVar = cast<DILocalVariable>(Var.first);
    const DILocation *Location = Var.second;

    dbgs() << " - " << LocalVar->getName() << " at ";

    if (Location)
      dbgs() << Location->getFilename() << ":" << Location->getLine() << ":"
             << Location->getColumn();
    else
      dbgs() << "<unknown location>";

    dbgs() << " --\n";

    for (const auto &E : enumerate(Entries)) {
      const auto &Entry = E.value();
      dbgs() << "  Entry[" << E.index() << "]: ";
      if (Entry.isDbgValue())
        dbgs() << "Debug value\n";
      else
        dbgs() << "Clobber\n";
      dbgs() << "   Instr: " << *Entry.getInstr();
      if (Entry.isDbgValue()) {
        if (Entry.getEndIndex() == NoEntry)
          dbgs() << "   - Valid until end of function\n";
        else
          dbgs() << "   - Closed by Entry[" << Entry.getEndIndex() << "]\n";
      }
      dbgs() << "\n";
    }
  }
}
#endif
