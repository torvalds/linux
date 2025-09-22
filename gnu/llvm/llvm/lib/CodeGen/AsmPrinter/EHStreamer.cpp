//===- CodeGen/AsmPrinter/EHStreamer.cpp - Exception Directive Streamer ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing exception info into assembly files.
//
//===----------------------------------------------------------------------===//

#include "EHStreamer.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

using namespace llvm;

EHStreamer::EHStreamer(AsmPrinter *A) : Asm(A), MMI(Asm->MMI) {}

EHStreamer::~EHStreamer() = default;

/// How many leading type ids two landing pads have in common.
unsigned EHStreamer::sharedTypeIDs(const LandingPadInfo *L,
                                   const LandingPadInfo *R) {
  const std::vector<int> &LIds = L->TypeIds, &RIds = R->TypeIds;
  return std::mismatch(LIds.begin(), LIds.end(), RIds.begin(), RIds.end())
             .first -
         LIds.begin();
}

/// Compute the actions table and gather the first action index for each landing
/// pad site.
void EHStreamer::computeActionsTable(
    const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
    SmallVectorImpl<ActionEntry> &Actions,
    SmallVectorImpl<unsigned> &FirstActions) {
  // The action table follows the call-site table in the LSDA. The individual
  // records are of two types:
  //
  //   * Catch clause
  //   * Exception specification
  //
  // The two record kinds have the same format, with only small differences.
  // They are distinguished by the "switch value" field: Catch clauses
  // (TypeInfos) have strictly positive switch values, and exception
  // specifications (FilterIds) have strictly negative switch values. Value 0
  // indicates a catch-all clause.
  //
  // Negative type IDs index into FilterIds. Positive type IDs index into
  // TypeInfos.  The value written for a positive type ID is just the type ID
  // itself.  For a negative type ID, however, the value written is the
  // (negative) byte offset of the corresponding FilterIds entry.  The byte
  // offset is usually equal to the type ID (because the FilterIds entries are
  // written using a variable width encoding, which outputs one byte per entry
  // as long as the value written is not too large) but can differ.  This kind
  // of complication does not occur for positive type IDs because type infos are
  // output using a fixed width encoding.  FilterOffsets[i] holds the byte
  // offset corresponding to FilterIds[i].

  const std::vector<unsigned> &FilterIds = Asm->MF->getFilterIds();
  SmallVector<int, 16> FilterOffsets;
  FilterOffsets.reserve(FilterIds.size());
  int Offset = -1;

  for (unsigned FilterId : FilterIds) {
    FilterOffsets.push_back(Offset);
    Offset -= getULEB128Size(FilterId);
  }

  FirstActions.reserve(LandingPads.size());

  int FirstAction = 0;
  unsigned SizeActions = 0; // Total size of all action entries for a function
  const LandingPadInfo *PrevLPI = nullptr;

  for (const LandingPadInfo *LPI : LandingPads) {
    const std::vector<int> &TypeIds = LPI->TypeIds;
    unsigned NumShared = PrevLPI ? sharedTypeIDs(LPI, PrevLPI) : 0;
    unsigned SizeSiteActions = 0; // Total size of all entries for a landingpad

    if (NumShared < TypeIds.size()) {
      // Size of one action entry (typeid + next action)
      unsigned SizeActionEntry = 0;
      unsigned PrevAction = (unsigned)-1;

      if (NumShared) {
        unsigned SizePrevIds = PrevLPI->TypeIds.size();
        assert(Actions.size());
        PrevAction = Actions.size() - 1;
        SizeActionEntry = getSLEB128Size(Actions[PrevAction].NextAction) +
                          getSLEB128Size(Actions[PrevAction].ValueForTypeID);

        for (unsigned j = NumShared; j != SizePrevIds; ++j) {
          assert(PrevAction != (unsigned)-1 && "PrevAction is invalid!");
          SizeActionEntry -= getSLEB128Size(Actions[PrevAction].ValueForTypeID);
          SizeActionEntry += -Actions[PrevAction].NextAction;
          PrevAction = Actions[PrevAction].Previous;
        }
      }

      // Compute the actions.
      for (unsigned J = NumShared, M = TypeIds.size(); J != M; ++J) {
        int TypeID = TypeIds[J];
        assert(-1 - TypeID < (int)FilterOffsets.size() && "Unknown filter id!");
        int ValueForTypeID =
            isFilterEHSelector(TypeID) ? FilterOffsets[-1 - TypeID] : TypeID;
        unsigned SizeTypeID = getSLEB128Size(ValueForTypeID);

        int NextAction = SizeActionEntry ? -(SizeActionEntry + SizeTypeID) : 0;
        SizeActionEntry = SizeTypeID + getSLEB128Size(NextAction);
        SizeSiteActions += SizeActionEntry;

        ActionEntry Action = { ValueForTypeID, NextAction, PrevAction };
        Actions.push_back(Action);
        PrevAction = Actions.size() - 1;
      }

      // Record the first action of the landing pad site.
      FirstAction = SizeActions + SizeSiteActions - SizeActionEntry + 1;
    } // else identical - re-use previous FirstAction

    // Information used when creating the call-site table. The action record
    // field of the call site record is the offset of the first associated
    // action record, relative to the start of the actions table. This value is
    // biased by 1 (1 indicating the start of the actions table), and 0
    // indicates that there are no actions.
    FirstActions.push_back(FirstAction);

    // Compute this sites contribution to size.
    SizeActions += SizeSiteActions;

    PrevLPI = LPI;
  }
}

/// Return `true' if this is a call to a function marked `nounwind'. Return
/// `false' otherwise.
bool EHStreamer::callToNoUnwindFunction(const MachineInstr *MI) {
  assert(MI->isCall() && "This should be a call instruction!");

  bool MarkedNoUnwind = false;
  bool SawFunc = false;

  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isGlobal()) continue;

    const Function *F = dyn_cast<Function>(MO.getGlobal());
    if (!F) continue;

    if (SawFunc) {
      // Be conservative. If we have more than one function operand for this
      // call, then we can't make the assumption that it's the callee and
      // not a parameter to the call.
      //
      // FIXME: Determine if there's a way to say that `F' is the callee or
      // parameter.
      MarkedNoUnwind = false;
      break;
    }

    MarkedNoUnwind = F->doesNotThrow();
    SawFunc = true;
  }

  return MarkedNoUnwind;
}

void EHStreamer::computePadMap(
    const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
    RangeMapType &PadMap) {
  // Invokes and nounwind calls have entries in PadMap (due to being bracketed
  // by try-range labels when lowered).  Ordinary calls do not, so appropriate
  // try-ranges for them need be deduced so we can put them in the LSDA.
  for (unsigned i = 0, N = LandingPads.size(); i != N; ++i) {
    const LandingPadInfo *LandingPad = LandingPads[i];
    for (unsigned j = 0, E = LandingPad->BeginLabels.size(); j != E; ++j) {
      MCSymbol *BeginLabel = LandingPad->BeginLabels[j];
      MCSymbol *EndLabel = LandingPad->BeginLabels[j];
      // If we have deleted the code for a given invoke after registering it in
      // the LandingPad label list, the associated symbols will not have been
      // emitted. In that case, ignore this callsite entry.
      if (!BeginLabel->isDefined() || !EndLabel->isDefined())
        continue;
      assert(!PadMap.count(BeginLabel) && "Duplicate landing pad labels!");
      PadRange P = { i, j };
      PadMap[BeginLabel] = P;
    }
  }
}

/// Compute the call-site table.  The entry for an invoke has a try-range
/// containing the call, a non-zero landing pad, and an appropriate action.  The
/// entry for an ordinary call has a try-range containing the call and zero for
/// the landing pad and the action.  Calls marked 'nounwind' have no entry and
/// must not be contained in the try-range of any entry - they form gaps in the
/// table.  Entries must be ordered by try-range address.
///
/// Call-sites are split into one or more call-site ranges associated with
/// different sections of the function.
///
///   - Without -basic-block-sections, all call-sites are grouped into one
///     call-site-range corresponding to the function section.
///
///   - With -basic-block-sections, one call-site range is created for each
///     section, with its FragmentBeginLabel and FragmentEndLabel respectively
//      set to the beginning and ending of the corresponding section and its
//      ExceptionLabel set to the exception symbol dedicated for this section.
//      Later, one LSDA header will be emitted for each call-site range with its
//      call-sites following. The action table and type info table will be
//      shared across all ranges.
void EHStreamer::computeCallSiteTable(
    SmallVectorImpl<CallSiteEntry> &CallSites,
    SmallVectorImpl<CallSiteRange> &CallSiteRanges,
    const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
    const SmallVectorImpl<unsigned> &FirstActions) {
  RangeMapType PadMap;
  computePadMap(LandingPads, PadMap);

  // The end label of the previous invoke or nounwind try-range.
  MCSymbol *LastLabel = Asm->getFunctionBegin();

  // Whether there is a potentially throwing instruction (currently this means
  // an ordinary call) between the end of the previous try-range and now.
  bool SawPotentiallyThrowing = false;

  // Whether the last CallSite entry was for an invoke.
  bool PreviousIsInvoke = false;

  bool IsSJLJ = Asm->MAI->getExceptionHandlingType() == ExceptionHandling::SjLj;

  // Visit all instructions in order of address.
  for (const auto &MBB : *Asm->MF) {
    if (&MBB == &Asm->MF->front() || MBB.isBeginSection()) {
      // We start a call-site range upon function entry and at the beginning of
      // every basic block section.
      CallSiteRanges.push_back(
          {Asm->MBBSectionRanges[MBB.getSectionID()].BeginLabel,
           Asm->MBBSectionRanges[MBB.getSectionID()].EndLabel,
           Asm->getMBBExceptionSym(MBB), CallSites.size()});
      PreviousIsInvoke = false;
      SawPotentiallyThrowing = false;
      LastLabel = nullptr;
    }

    if (MBB.isEHPad())
      CallSiteRanges.back().IsLPRange = true;

    for (const auto &MI : MBB) {
      if (!MI.isEHLabel()) {
        if (MI.isCall())
          SawPotentiallyThrowing |= !callToNoUnwindFunction(&MI);
        continue;
      }

      // End of the previous try-range?
      MCSymbol *BeginLabel = MI.getOperand(0).getMCSymbol();
      if (BeginLabel == LastLabel)
        SawPotentiallyThrowing = false;

      // Beginning of a new try-range?
      RangeMapType::const_iterator L = PadMap.find(BeginLabel);
      if (L == PadMap.end())
        // Nope, it was just some random label.
        continue;

      const PadRange &P = L->second;
      const LandingPadInfo *LandingPad = LandingPads[P.PadIndex];
      assert(BeginLabel == LandingPad->BeginLabels[P.RangeIndex] &&
             "Inconsistent landing pad map!");

      // For Dwarf and AIX exception handling (SjLj handling doesn't use this).
      // If some instruction between the previous try-range and this one may
      // throw, create a call-site entry with no landing pad for the region
      // between the try-ranges.
      if (SawPotentiallyThrowing &&
          (Asm->MAI->usesCFIForEH() ||
           Asm->MAI->getExceptionHandlingType() == ExceptionHandling::AIX)) {
        CallSites.push_back({LastLabel, BeginLabel, nullptr, 0});
        PreviousIsInvoke = false;
      }

      LastLabel = LandingPad->EndLabels[P.RangeIndex];
      assert(BeginLabel && LastLabel && "Invalid landing pad!");

      if (!LandingPad->LandingPadLabel) {
        // Create a gap.
        PreviousIsInvoke = false;
      } else {
        // This try-range is for an invoke.
        CallSiteEntry Site = {
          BeginLabel,
          LastLabel,
          LandingPad,
          FirstActions[P.PadIndex]
        };

        // Try to merge with the previous call-site. SJLJ doesn't do this
        if (PreviousIsInvoke && !IsSJLJ) {
          CallSiteEntry &Prev = CallSites.back();
          if (Site.LPad == Prev.LPad && Site.Action == Prev.Action) {
            // Extend the range of the previous entry.
            Prev.EndLabel = Site.EndLabel;
            continue;
          }
        }

        // Otherwise, create a new call-site.
        if (!IsSJLJ)
          CallSites.push_back(Site);
        else {
          // SjLj EH must maintain the call sites in the order assigned
          // to them by the SjLjPrepare pass.
          unsigned SiteNo = Asm->MF->getCallSiteBeginLabel(BeginLabel);
          if (CallSites.size() < SiteNo)
            CallSites.resize(SiteNo);
          CallSites[SiteNo - 1] = Site;
        }
        PreviousIsInvoke = true;
      }
    }

    // We end the call-site range upon function exit and at the end of every
    // basic block section.
    if (&MBB == &Asm->MF->back() || MBB.isEndSection()) {
      // If some instruction between the previous try-range and the end of the
      // function may throw, create a call-site entry with no landing pad for
      // the region following the try-range.
      if (SawPotentiallyThrowing && !IsSJLJ) {
        CallSiteEntry Site = {LastLabel, CallSiteRanges.back().FragmentEndLabel,
                              nullptr, 0};
        CallSites.push_back(Site);
        SawPotentiallyThrowing = false;
      }
      CallSiteRanges.back().CallSiteEndIdx = CallSites.size();
    }
  }
}

/// Emit landing pads and actions.
///
/// The general organization of the table is complex, but the basic concepts are
/// easy.  First there is a header which describes the location and organization
/// of the three components that follow.
///
///  1. The landing pad site information describes the range of code covered by
///     the try.  In our case it's an accumulation of the ranges covered by the
///     invokes in the try.  There is also a reference to the landing pad that
///     handles the exception once processed.  Finally an index into the actions
///     table.
///  2. The action table, in our case, is composed of pairs of type IDs and next
///     action offset.  Starting with the action index from the landing pad
///     site, each type ID is checked for a match to the current exception.  If
///     it matches then the exception and type id are passed on to the landing
///     pad.  Otherwise the next action is looked up.  This chain is terminated
///     with a next action of zero.  If no type id is found then the frame is
///     unwound and handling continues.
///  3. Type ID table contains references to all the C++ typeinfo for all
///     catches in the function.  This tables is reverse indexed base 1.
///
/// Returns the starting symbol of an exception table.
MCSymbol *EHStreamer::emitExceptionTable() {
  const MachineFunction *MF = Asm->MF;
  const std::vector<const GlobalValue *> &TypeInfos = MF->getTypeInfos();
  const std::vector<unsigned> &FilterIds = MF->getFilterIds();
  const std::vector<LandingPadInfo> &PadInfos = MF->getLandingPads();

  // Sort the landing pads in order of their type ids.  This is used to fold
  // duplicate actions.
  SmallVector<const LandingPadInfo *, 64> LandingPads;
  LandingPads.reserve(PadInfos.size());

  for (const LandingPadInfo &LPI : PadInfos) {
    // If a landing-pad has an associated label, but the label wasn't ever
    // emitted, then skip it.  (This can occur if the landingpad's MBB was
    // deleted).
    if (LPI.LandingPadLabel && !LPI.LandingPadLabel->isDefined())
      continue;
    LandingPads.push_back(&LPI);
  }

  // Order landing pads lexicographically by type id.
  llvm::sort(LandingPads, [](const LandingPadInfo *L, const LandingPadInfo *R) {
    return L->TypeIds < R->TypeIds;
  });

  // Compute the actions table and gather the first action index for each
  // landing pad site.
  SmallVector<ActionEntry, 32> Actions;
  SmallVector<unsigned, 64> FirstActions;
  computeActionsTable(LandingPads, Actions, FirstActions);

  // Compute the call-site table and call-site ranges. Normally, there is only
  // one call-site-range which covers the whole function. With
  // -basic-block-sections, there is one call-site-range per basic block
  // section.
  SmallVector<CallSiteEntry, 64> CallSites;
  SmallVector<CallSiteRange, 4> CallSiteRanges;
  computeCallSiteTable(CallSites, CallSiteRanges, LandingPads, FirstActions);

  bool IsSJLJ = Asm->MAI->getExceptionHandlingType() == ExceptionHandling::SjLj;
  bool IsWasm = Asm->MAI->getExceptionHandlingType() == ExceptionHandling::Wasm;
  bool HasLEB128Directives = Asm->MAI->hasLEB128Directives();
  unsigned CallSiteEncoding =
      IsSJLJ ? static_cast<unsigned>(dwarf::DW_EH_PE_udata4) :
               Asm->getObjFileLowering().getCallSiteEncoding();
  bool HaveTTData = !TypeInfos.empty() || !FilterIds.empty();

  // Type infos.
  MCSection *LSDASection = Asm->getObjFileLowering().getSectionForLSDA(
      MF->getFunction(), *Asm->CurrentFnSym, Asm->TM);
  unsigned TTypeEncoding;

  if (!HaveTTData) {
    // If there is no TypeInfo, then we just explicitly say that we're omitting
    // that bit.
    TTypeEncoding = dwarf::DW_EH_PE_omit;
  } else {
    // Okay, we have actual filters or typeinfos to emit.  As such, we need to
    // pick a type encoding for them.  We're about to emit a list of pointers to
    // typeinfo objects at the end of the LSDA.  However, unless we're in static
    // mode, this reference will require a relocation by the dynamic linker.
    //
    // Because of this, we have a couple of options:
    //
    //   1) If we are in -static mode, we can always use an absolute reference
    //      from the LSDA, because the static linker will resolve it.
    //
    //   2) Otherwise, if the LSDA section is writable, we can output the direct
    //      reference to the typeinfo and allow the dynamic linker to relocate
    //      it.  Since it is in a writable section, the dynamic linker won't
    //      have a problem.
    //
    //   3) Finally, if we're in PIC mode and the LDSA section isn't writable,
    //      we need to use some form of indirection.  For example, on Darwin,
    //      we can output a statically-relocatable reference to a dyld stub. The
    //      offset to the stub is constant, but the contents are in a section
    //      that is updated by the dynamic linker.  This is easy enough, but we
    //      need to tell the personality function of the unwinder to indirect
    //      through the dyld stub.
    //
    // FIXME: When (3) is actually implemented, we'll have to emit the stubs
    // somewhere.  This predicate should be moved to a shared location that is
    // in target-independent code.
    //
    TTypeEncoding = Asm->getObjFileLowering().getTTypeEncoding();
  }

  // Begin the exception table.
  // Sometimes we want not to emit the data into separate section (e.g. ARM
  // EHABI). In this case LSDASection will be NULL.
  if (LSDASection)
    Asm->OutStreamer->switchSection(LSDASection);
  Asm->emitAlignment(Align(4));

  // Emit the LSDA.
  MCSymbol *GCCETSym =
    Asm->OutContext.getOrCreateSymbol(Twine("GCC_except_table")+
                                      Twine(Asm->getFunctionNumber()));
  Asm->OutStreamer->emitLabel(GCCETSym);
  MCSymbol *CstEndLabel = Asm->createTempSymbol(
      CallSiteRanges.size() > 1 ? "action_table_base" : "cst_end");

  MCSymbol *TTBaseLabel = nullptr;
  if (HaveTTData)
    TTBaseLabel = Asm->createTempSymbol("ttbase");

  const bool VerboseAsm = Asm->OutStreamer->isVerboseAsm();

  // Helper for emitting references (offsets) for type table and the end of the
  // call-site table (which marks the beginning of the action table).
  //  * For Itanium, these references will be emitted for every callsite range.
  //  * For SJLJ and Wasm, they will be emitted only once in the LSDA header.
  auto EmitTypeTableRefAndCallSiteTableEndRef = [&]() {
    Asm->emitEncodingByte(TTypeEncoding, "@TType");
    if (HaveTTData) {
      // N.B.: There is a dependency loop between the size of the TTBase uleb128
      // here and the amount of padding before the aligned type table. The
      // assembler must sometimes pad this uleb128 or insert extra padding
      // before the type table. See PR35809 or GNU as bug 4029.
      MCSymbol *TTBaseRefLabel = Asm->createTempSymbol("ttbaseref");
      Asm->emitLabelDifferenceAsULEB128(TTBaseLabel, TTBaseRefLabel);
      Asm->OutStreamer->emitLabel(TTBaseRefLabel);
    }

    // The Action table follows the call-site table. So we emit the
    // label difference from here (start of the call-site table for SJLJ and
    // Wasm, and start of a call-site range for Itanium) to the end of the
    // whole call-site table (end of the last call-site range for Itanium).
    MCSymbol *CstBeginLabel = Asm->createTempSymbol("cst_begin");
    Asm->emitEncodingByte(CallSiteEncoding, "Call site");
    Asm->emitLabelDifferenceAsULEB128(CstEndLabel, CstBeginLabel);
    Asm->OutStreamer->emitLabel(CstBeginLabel);
  };

  // An alternative path to EmitTypeTableRefAndCallSiteTableEndRef.
  // For some platforms, the system assembler does not accept the form of
  // `.uleb128 label2 - label1`. In those situations, we would need to calculate
  // the size between label1 and label2 manually.
  // In this case, we would need to calculate the LSDA size and the call
  // site table size.
  auto EmitTypeTableOffsetAndCallSiteTableOffset = [&]() {
    assert(CallSiteEncoding == dwarf::DW_EH_PE_udata4 && !HasLEB128Directives &&
           "Targets supporting .uleb128 do not need to take this path.");
    if (CallSiteRanges.size() > 1)
      report_fatal_error(
          "-fbasic-block-sections is not yet supported on "
          "platforms that do not have general LEB128 directive support.");

    uint64_t CallSiteTableSize = 0;
    const CallSiteRange &CSRange = CallSiteRanges.back();
    for (size_t CallSiteIdx = CSRange.CallSiteBeginIdx;
         CallSiteIdx < CSRange.CallSiteEndIdx; ++CallSiteIdx) {
      const CallSiteEntry &S = CallSites[CallSiteIdx];
      // Each call site entry consists of 3 udata4 fields (12 bytes) and
      // 1 ULEB128 field.
      CallSiteTableSize += 12 + getULEB128Size(S.Action);
      assert(isUInt<32>(CallSiteTableSize) && "CallSiteTableSize overflows.");
    }

    Asm->emitEncodingByte(TTypeEncoding, "@TType");
    if (HaveTTData) {
      const unsigned ByteSizeOfCallSiteOffset =
          getULEB128Size(CallSiteTableSize);
      uint64_t ActionTableSize = 0;
      for (const ActionEntry &Action : Actions) {
        // Each action entry consists of two SLEB128 fields.
        ActionTableSize += getSLEB128Size(Action.ValueForTypeID) +
                           getSLEB128Size(Action.NextAction);
        assert(isUInt<32>(ActionTableSize) && "ActionTableSize overflows.");
      }

      const unsigned TypeInfoSize =
          Asm->GetSizeOfEncodedValue(TTypeEncoding) * MF->getTypeInfos().size();

      const uint64_t LSDASizeBeforeAlign =
          1                          // Call site encoding byte.
          + ByteSizeOfCallSiteOffset // ULEB128 encoding of CallSiteTableSize.
          + CallSiteTableSize        // Call site table content.
          + ActionTableSize;         // Action table content.

      const uint64_t LSDASizeWithoutAlign = LSDASizeBeforeAlign + TypeInfoSize;
      const unsigned ByteSizeOfLSDAWithoutAlign =
          getULEB128Size(LSDASizeWithoutAlign);
      const uint64_t DisplacementBeforeAlign =
          2 // LPStartEncoding and TypeTableEncoding.
          + ByteSizeOfLSDAWithoutAlign + LSDASizeBeforeAlign;

      // The type info area starts with 4 byte alignment.
      const unsigned NeedAlignVal = (4 - DisplacementBeforeAlign % 4) % 4;
      uint64_t LSDASizeWithAlign = LSDASizeWithoutAlign + NeedAlignVal;
      const unsigned ByteSizeOfLSDAWithAlign =
          getULEB128Size(LSDASizeWithAlign);

      // The LSDASizeWithAlign could use 1 byte less padding for alignment
      // when the data we use to represent the LSDA Size "needs" to be 1 byte
      // larger than the one previously calculated without alignment.
      if (ByteSizeOfLSDAWithAlign > ByteSizeOfLSDAWithoutAlign)
        LSDASizeWithAlign -= 1;

      Asm->OutStreamer->emitULEB128IntValue(LSDASizeWithAlign,
                                            ByteSizeOfLSDAWithAlign);
    }

    Asm->emitEncodingByte(CallSiteEncoding, "Call site");
    Asm->OutStreamer->emitULEB128IntValue(CallSiteTableSize);
  };

  // SjLj / Wasm Exception handling
  if (IsSJLJ || IsWasm) {
    Asm->OutStreamer->emitLabel(Asm->getMBBExceptionSym(Asm->MF->front()));

    // emit the LSDA header.
    Asm->emitEncodingByte(dwarf::DW_EH_PE_omit, "@LPStart");
    EmitTypeTableRefAndCallSiteTableEndRef();

    unsigned idx = 0;
    for (SmallVectorImpl<CallSiteEntry>::const_iterator
         I = CallSites.begin(), E = CallSites.end(); I != E; ++I, ++idx) {
      const CallSiteEntry &S = *I;

      // Index of the call site entry.
      if (VerboseAsm) {
        Asm->OutStreamer->AddComment(">> Call Site " + Twine(idx) + " <<");
        Asm->OutStreamer->AddComment("  On exception at call site "+Twine(idx));
      }
      Asm->emitULEB128(idx);

      // Offset of the first associated action record, relative to the start of
      // the action table. This value is biased by 1 (1 indicates the start of
      // the action table), and 0 indicates that there are no actions.
      if (VerboseAsm) {
        if (S.Action == 0)
          Asm->OutStreamer->AddComment("  Action: cleanup");
        else
          Asm->OutStreamer->AddComment("  Action: " +
                                       Twine((S.Action - 1) / 2 + 1));
      }
      Asm->emitULEB128(S.Action);
    }
    Asm->OutStreamer->emitLabel(CstEndLabel);
  } else {
    // Itanium LSDA exception handling

    // The call-site table is a list of all call sites that may throw an
    // exception (including C++ 'throw' statements) in the procedure
    // fragment. It immediately follows the LSDA header. Each entry indicates,
    // for a given call, the first corresponding action record and corresponding
    // landing pad.
    //
    // The table begins with the number of bytes, stored as an LEB128
    // compressed, unsigned integer. The records immediately follow the record
    // count. They are sorted in increasing call-site address. Each record
    // indicates:
    //
    //   * The position of the call-site.
    //   * The position of the landing pad.
    //   * The first action record for that call site.
    //
    // A missing entry in the call-site table indicates that a call is not
    // supposed to throw.

    assert(CallSiteRanges.size() != 0 && "No call-site ranges!");

    // There should be only one call-site range which includes all the landing
    // pads. Find that call-site range here.
    const CallSiteRange *LandingPadRange = nullptr;
    for (const CallSiteRange &CSRange : CallSiteRanges) {
      if (CSRange.IsLPRange) {
        assert(LandingPadRange == nullptr &&
               "All landing pads must be in a single callsite range.");
        LandingPadRange = &CSRange;
      }
    }

    // The call-site table is split into its call-site ranges, each being
    // emitted as:
    //              [ LPStartEncoding | LPStart ]
    //              [ TypeTableEncoding | TypeTableOffset ]
    //              [ CallSiteEncoding | CallSiteTableEndOffset ]
    // cst_begin -> { call-site entries contained in this range }
    //
    // and is followed by the next call-site range.
    //
    // For each call-site range, CallSiteTableEndOffset is computed as the
    // difference between cst_begin of that range and the last call-site-table's
    // end label. This offset is used to find the action table.

    unsigned Entry = 0;
    for (const CallSiteRange &CSRange : CallSiteRanges) {
      if (CSRange.CallSiteBeginIdx != 0) {
        // Align the call-site range for all ranges except the first. The
        // first range is already aligned due to the exception table alignment.
        Asm->emitAlignment(Align(4));
      }
      Asm->OutStreamer->emitLabel(CSRange.ExceptionLabel);

      // Emit the LSDA header.
      // LPStart is omitted if either we have a single call-site range (in which
      // case the function entry is treated as @LPStart) or if this function has
      // no landing pads (in which case @LPStart is undefined).
      if (CallSiteRanges.size() == 1 || LandingPadRange == nullptr) {
        Asm->emitEncodingByte(dwarf::DW_EH_PE_omit, "@LPStart");
      } else if (!Asm->isPositionIndependent()) {
        // For more than one call-site ranges, LPStart must be explicitly
        // specified.
        // For non-PIC we can simply use the absolute value.
        Asm->emitEncodingByte(dwarf::DW_EH_PE_absptr, "@LPStart");
        Asm->OutStreamer->emitSymbolValue(LandingPadRange->FragmentBeginLabel,
                                          Asm->MAI->getCodePointerSize());
      } else {
        // For PIC mode, we Emit a PC-relative address for LPStart.
        Asm->emitEncodingByte(dwarf::DW_EH_PE_pcrel, "@LPStart");
        MCContext &Context = Asm->OutStreamer->getContext();
        MCSymbol *Dot = Context.createTempSymbol();
        Asm->OutStreamer->emitLabel(Dot);
        Asm->OutStreamer->emitValue(
            MCBinaryExpr::createSub(
                MCSymbolRefExpr::create(LandingPadRange->FragmentBeginLabel,
                                        Context),
                MCSymbolRefExpr::create(Dot, Context), Context),
            Asm->MAI->getCodePointerSize());
      }

      if (HasLEB128Directives)
        EmitTypeTableRefAndCallSiteTableEndRef();
      else
        EmitTypeTableOffsetAndCallSiteTableOffset();

      for (size_t CallSiteIdx = CSRange.CallSiteBeginIdx;
           CallSiteIdx != CSRange.CallSiteEndIdx; ++CallSiteIdx) {
        const CallSiteEntry &S = CallSites[CallSiteIdx];

        MCSymbol *EHFuncBeginSym = CSRange.FragmentBeginLabel;
        MCSymbol *EHFuncEndSym = CSRange.FragmentEndLabel;

        MCSymbol *BeginLabel = S.BeginLabel;
        if (!BeginLabel)
          BeginLabel = EHFuncBeginSym;
        MCSymbol *EndLabel = S.EndLabel;
        if (!EndLabel)
          EndLabel = EHFuncEndSym;

        // Offset of the call site relative to the start of the procedure.
        if (VerboseAsm)
          Asm->OutStreamer->AddComment(">> Call Site " + Twine(++Entry) +
                                       " <<");
        Asm->emitCallSiteOffset(BeginLabel, EHFuncBeginSym, CallSiteEncoding);
        if (VerboseAsm)
          Asm->OutStreamer->AddComment(Twine("  Call between ") +
                                       BeginLabel->getName() + " and " +
                                       EndLabel->getName());
        Asm->emitCallSiteOffset(EndLabel, BeginLabel, CallSiteEncoding);

        // Offset of the landing pad relative to the start of the landing pad
        // fragment.
        if (!S.LPad) {
          if (VerboseAsm)
            Asm->OutStreamer->AddComment("    has no landing pad");
          Asm->emitCallSiteValue(0, CallSiteEncoding);
        } else {
          if (VerboseAsm)
            Asm->OutStreamer->AddComment(Twine("    jumps to ") +
                                         S.LPad->LandingPadLabel->getName());
          Asm->emitCallSiteOffset(S.LPad->LandingPadLabel,
                                  LandingPadRange->FragmentBeginLabel,
                                  CallSiteEncoding);
        }

        // Offset of the first associated action record, relative to the start
        // of the action table. This value is biased by 1 (1 indicates the start
        // of the action table), and 0 indicates that there are no actions.
        if (VerboseAsm) {
          if (S.Action == 0)
            Asm->OutStreamer->AddComment("  On action: cleanup");
          else
            Asm->OutStreamer->AddComment("  On action: " +
                                         Twine((S.Action - 1) / 2 + 1));
        }
        Asm->emitULEB128(S.Action);
      }
    }
    Asm->OutStreamer->emitLabel(CstEndLabel);
  }

  // Emit the Action Table.
  int Entry = 0;
  for (const ActionEntry &Action : Actions) {
    if (VerboseAsm) {
      // Emit comments that decode the action table.
      Asm->OutStreamer->AddComment(">> Action Record " + Twine(++Entry) + " <<");
    }

    // Type Filter
    //
    //   Used by the runtime to match the type of the thrown exception to the
    //   type of the catch clauses or the types in the exception specification.
    if (VerboseAsm) {
      if (Action.ValueForTypeID > 0)
        Asm->OutStreamer->AddComment("  Catch TypeInfo " +
                                     Twine(Action.ValueForTypeID));
      else if (Action.ValueForTypeID < 0)
        Asm->OutStreamer->AddComment("  Filter TypeInfo " +
                                     Twine(Action.ValueForTypeID));
      else
        Asm->OutStreamer->AddComment("  Cleanup");
    }
    Asm->emitSLEB128(Action.ValueForTypeID);

    // Action Record
    if (VerboseAsm) {
      if (Action.Previous == unsigned(-1)) {
        Asm->OutStreamer->AddComment("  No further actions");
      } else {
        Asm->OutStreamer->AddComment("  Continue to action " +
                                     Twine(Action.Previous + 1));
      }
    }
    Asm->emitSLEB128(Action.NextAction);
  }

  if (HaveTTData) {
    Asm->emitAlignment(Align(4));
    emitTypeInfos(TTypeEncoding, TTBaseLabel);
  }

  Asm->emitAlignment(Align(4));
  return GCCETSym;
}

void EHStreamer::emitTypeInfos(unsigned TTypeEncoding, MCSymbol *TTBaseLabel) {
  const MachineFunction *MF = Asm->MF;
  const std::vector<const GlobalValue *> &TypeInfos = MF->getTypeInfos();
  const std::vector<unsigned> &FilterIds = MF->getFilterIds();

  const bool VerboseAsm = Asm->OutStreamer->isVerboseAsm();

  int Entry = 0;
  // Emit the Catch TypeInfos.
  if (VerboseAsm && !TypeInfos.empty()) {
    Asm->OutStreamer->AddComment(">> Catch TypeInfos <<");
    Asm->OutStreamer->addBlankLine();
    Entry = TypeInfos.size();
  }

  for (const GlobalValue *GV : llvm::reverse(TypeInfos)) {
    if (VerboseAsm)
      Asm->OutStreamer->AddComment("TypeInfo " + Twine(Entry--));
    Asm->emitTTypeReference(GV, TTypeEncoding);
  }

  Asm->OutStreamer->emitLabel(TTBaseLabel);

  // Emit the Exception Specifications.
  if (VerboseAsm && !FilterIds.empty()) {
    Asm->OutStreamer->AddComment(">> Filter TypeInfos <<");
    Asm->OutStreamer->addBlankLine();
    Entry = 0;
  }
  for (std::vector<unsigned>::const_iterator
         I = FilterIds.begin(), E = FilterIds.end(); I < E; ++I) {
    unsigned TypeID = *I;
    if (VerboseAsm) {
      --Entry;
      if (isFilterEHSelector(TypeID))
        Asm->OutStreamer->AddComment("FilterInfo " + Twine(Entry));
    }

    Asm->emitULEB128(TypeID);
  }
}
