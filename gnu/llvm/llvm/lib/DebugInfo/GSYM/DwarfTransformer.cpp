//===- DwarfTransformer.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <thread>
#include <unordered_set>

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/DebugInfo/GSYM/DwarfTransformer.h"
#include "llvm/DebugInfo/GSYM/FunctionInfo.h"
#include "llvm/DebugInfo/GSYM/GsymCreator.h"
#include "llvm/DebugInfo/GSYM/GsymReader.h"
#include "llvm/DebugInfo/GSYM/InlineInfo.h"
#include "llvm/DebugInfo/GSYM/OutputAggregator.h"

#include <optional>

using namespace llvm;
using namespace gsym;

struct llvm::gsym::CUInfo {
  const DWARFDebugLine::LineTable *LineTable;
  const char *CompDir;
  std::vector<uint32_t> FileCache;
  uint64_t Language = 0;
  uint8_t AddrSize = 0;

  CUInfo(DWARFContext &DICtx, DWARFCompileUnit *CU) {
    LineTable = DICtx.getLineTableForUnit(CU);
    CompDir = CU->getCompilationDir();
    FileCache.clear();
    if (LineTable)
      FileCache.assign(LineTable->Prologue.FileNames.size() + 1, UINT32_MAX);
    DWARFDie Die = CU->getUnitDIE();
    Language = dwarf::toUnsigned(Die.find(dwarf::DW_AT_language), 0);
    AddrSize = CU->getAddressByteSize();
  }

  /// Return true if Addr is the highest address for a given compile unit. The
  /// highest address is encoded as -1, of all ones in the address. These high
  /// addresses are used by some linkers to indicate that a function has been
  /// dead stripped or didn't end up in the linked executable.
  bool isHighestAddress(uint64_t Addr) const {
    if (AddrSize == 4)
      return Addr == UINT32_MAX;
    else if (AddrSize == 8)
      return Addr == UINT64_MAX;
    return false;
  }

  /// Convert a DWARF compile unit file index into a GSYM global file index.
  ///
  /// Each compile unit in DWARF has its own file table in the line table
  /// prologue. GSYM has a single large file table that applies to all files
  /// from all of the info in a GSYM file. This function converts between the
  /// two and caches and DWARF CU file index that has already been converted so
  /// the first client that asks for a compile unit file index will end up
  /// doing the conversion, and subsequent clients will get the cached GSYM
  /// index.
  std::optional<uint32_t> DWARFToGSYMFileIndex(GsymCreator &Gsym,
                                               uint32_t DwarfFileIdx) {
    if (!LineTable || DwarfFileIdx >= FileCache.size())
      return std::nullopt;
    uint32_t &GsymFileIdx = FileCache[DwarfFileIdx];
    if (GsymFileIdx != UINT32_MAX)
      return GsymFileIdx;
    std::string File;
    if (LineTable->getFileNameByIndex(
            DwarfFileIdx, CompDir,
            DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, File))
      GsymFileIdx = Gsym.insertFile(File);
    else
      GsymFileIdx = 0;
    return GsymFileIdx;
  }
};


static DWARFDie GetParentDeclContextDIE(DWARFDie &Die) {
  if (DWARFDie SpecDie =
          Die.getAttributeValueAsReferencedDie(dwarf::DW_AT_specification)) {
    if (DWARFDie SpecParent = GetParentDeclContextDIE(SpecDie))
      return SpecParent;
  }
  if (DWARFDie AbstDie =
          Die.getAttributeValueAsReferencedDie(dwarf::DW_AT_abstract_origin)) {
    if (DWARFDie AbstParent = GetParentDeclContextDIE(AbstDie))
      return AbstParent;
  }

  // We never want to follow parent for inlined subroutine - that would
  // give us information about where the function is inlined, not what
  // function is inlined
  if (Die.getTag() == dwarf::DW_TAG_inlined_subroutine)
    return DWARFDie();

  DWARFDie ParentDie = Die.getParent();
  if (!ParentDie)
    return DWARFDie();

  switch (ParentDie.getTag()) {
  case dwarf::DW_TAG_namespace:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_subprogram:
    return ParentDie; // Found parent decl context DIE
  case dwarf::DW_TAG_lexical_block:
    return GetParentDeclContextDIE(ParentDie);
  default:
    break;
  }

  return DWARFDie();
}

/// Get the GsymCreator string table offset for the qualified name for the
/// DIE passed in. This function will avoid making copies of any strings in
/// the GsymCreator when possible. We don't need to copy a string when the
/// string comes from our .debug_str section or is an inlined string in the
/// .debug_info. If we create a qualified name string in this function by
/// combining multiple strings in the DWARF string table or info, we will make
/// a copy of the string when we add it to the string table.
static std::optional<uint32_t>
getQualifiedNameIndex(DWARFDie &Die, uint64_t Language, GsymCreator &Gsym) {
  // If the dwarf has mangled name, use mangled name
  if (auto LinkageName = Die.getLinkageName()) {
    // We have seen cases were linkage name is actually empty.
    if (strlen(LinkageName) > 0)
      return Gsym.insertString(LinkageName, /* Copy */ false);
  }

  StringRef ShortName(Die.getName(DINameKind::ShortName));
  if (ShortName.empty())
    return std::nullopt;

  // For C++ and ObjC, prepend names of all parent declaration contexts
  if (!(Language == dwarf::DW_LANG_C_plus_plus ||
        Language == dwarf::DW_LANG_C_plus_plus_03 ||
        Language == dwarf::DW_LANG_C_plus_plus_11 ||
        Language == dwarf::DW_LANG_C_plus_plus_14 ||
        Language == dwarf::DW_LANG_ObjC_plus_plus ||
        // This should not be needed for C, but we see C++ code marked as C
        // in some binaries. This should hurt, so let's do it for C as well
        Language == dwarf::DW_LANG_C))
    return Gsym.insertString(ShortName, /* Copy */ false);

  // Some GCC optimizations create functions with names ending with .isra.<num>
  // or .part.<num> and those names are just DW_AT_name, not DW_AT_linkage_name
  // If it looks like it could be the case, don't add any prefix
  if (ShortName.starts_with("_Z") &&
      (ShortName.contains(".isra.") || ShortName.contains(".part.")))
    return Gsym.insertString(ShortName, /* Copy */ false);

  DWARFDie ParentDeclCtxDie = GetParentDeclContextDIE(Die);
  if (ParentDeclCtxDie) {
    std::string Name = ShortName.str();
    while (ParentDeclCtxDie) {
      StringRef ParentName(ParentDeclCtxDie.getName(DINameKind::ShortName));
      if (!ParentName.empty()) {
        // "lambda" names are wrapped in < >. Replace with { }
        // to be consistent with demangled names and not to confuse with
        // templates
        if (ParentName.front() == '<' && ParentName.back() == '>')
          Name = "{" + ParentName.substr(1, ParentName.size() - 2).str() + "}" +
                "::" + Name;
        else
          Name = ParentName.str() + "::" + Name;
      }
      ParentDeclCtxDie = GetParentDeclContextDIE(ParentDeclCtxDie);
    }
    // Copy the name since we created a new name in a std::string.
    return Gsym.insertString(Name, /* Copy */ true);
  }
  // Don't copy the name since it exists in the DWARF object file.
  return Gsym.insertString(ShortName, /* Copy */ false);
}

static bool hasInlineInfo(DWARFDie Die, uint32_t Depth) {
  bool CheckChildren = true;
  switch (Die.getTag()) {
  case dwarf::DW_TAG_subprogram:
    // Don't look into functions within functions.
    CheckChildren = Depth == 0;
    break;
  case dwarf::DW_TAG_inlined_subroutine:
    return true;
  default:
    break;
  }
  if (!CheckChildren)
    return false;
  for (DWARFDie ChildDie : Die.children()) {
    if (hasInlineInfo(ChildDie, Depth + 1))
      return true;
  }
  return false;
}

static AddressRanges
ConvertDWARFRanges(const DWARFAddressRangesVector &DwarfRanges) {
  AddressRanges Ranges;
  for (const DWARFAddressRange &DwarfRange : DwarfRanges) {
    if (DwarfRange.LowPC < DwarfRange.HighPC)
      Ranges.insert({DwarfRange.LowPC, DwarfRange.HighPC});
  }
  return Ranges;
}

static void parseInlineInfo(GsymCreator &Gsym, OutputAggregator &Out,
                            CUInfo &CUI, DWARFDie Die, uint32_t Depth,
                            FunctionInfo &FI, InlineInfo &Parent,
                            const AddressRanges &AllParentRanges,
                            bool &WarnIfEmpty) {
  if (!hasInlineInfo(Die, Depth))
    return;

  dwarf::Tag Tag = Die.getTag();
  if (Tag == dwarf::DW_TAG_inlined_subroutine) {
    // create new InlineInfo and append to parent.children
    InlineInfo II;
    AddressRanges AllInlineRanges;
    Expected<DWARFAddressRangesVector> RangesOrError = Die.getAddressRanges();
    if (RangesOrError) {
      AllInlineRanges = ConvertDWARFRanges(RangesOrError.get());
      uint32_t EmptyCount = 0;
      for (const AddressRange &InlineRange : AllInlineRanges) {
        // Check for empty inline range in case inline function was outlined
        // or has not code
        if (InlineRange.empty()) {
          ++EmptyCount;
        } else {
          if (Parent.Ranges.contains(InlineRange)) {
            II.Ranges.insert(InlineRange);
          } else {
            // Only warn if the current inline range is not within any of all
            // of the parent ranges. If we have a DW_TAG_subpgram with multiple
            // ranges we will emit a FunctionInfo for each range of that
            // function that only emits information within the current range,
            // so we only want to emit an error if the DWARF has issues, not
            // when a range currently just isn't in the range we are currently
            // parsing for.
            if (AllParentRanges.contains(InlineRange)) {
              WarnIfEmpty = false;
            } else
              Out.Report("Function DIE has uncontained address range",
                         [&](raw_ostream &OS) {
                           OS << "error: inlined function DIE at "
                              << HEX32(Die.getOffset()) << " has a range ["
                              << HEX64(InlineRange.start()) << " - "
                              << HEX64(InlineRange.end())
                              << ") that isn't contained in "
                              << "any parent address ranges, this inline range "
                                 "will be "
                                 "removed.\n";
                         });
          }
        }
      }
      // If we have all empty ranges for the inlines, then don't warn if we
      // have an empty InlineInfo at the top level as all inline functions
      // were elided.
      if (EmptyCount == AllInlineRanges.size())
        WarnIfEmpty = false;
    }
    if (II.Ranges.empty())
      return;

    if (auto NameIndex = getQualifiedNameIndex(Die, CUI.Language, Gsym))
      II.Name = *NameIndex;
    const uint64_t DwarfFileIdx = dwarf::toUnsigned(
        Die.findRecursively(dwarf::DW_AT_call_file), UINT32_MAX);
    std::optional<uint32_t> OptGSymFileIdx =
        CUI.DWARFToGSYMFileIndex(Gsym, DwarfFileIdx);
    if (OptGSymFileIdx) {
      II.CallFile = OptGSymFileIdx.value();
      II.CallLine = dwarf::toUnsigned(Die.find(dwarf::DW_AT_call_line), 0);
      // parse all children and append to parent
      for (DWARFDie ChildDie : Die.children())
        parseInlineInfo(Gsym, Out, CUI, ChildDie, Depth + 1, FI, II,
                        AllInlineRanges, WarnIfEmpty);
      Parent.Children.emplace_back(std::move(II));
    } else
      Out.Report(
          "Inlined function die has invlaid file index in DW_AT_call_file",
          [&](raw_ostream &OS) {
            OS << "error: inlined function DIE at " << HEX32(Die.getOffset())
               << " has an invalid file index " << DwarfFileIdx
               << " in its DW_AT_call_file attribute, this inline entry and "
                  "all "
               << "children will be removed.\n";
          });
    return;
  }
  if (Tag == dwarf::DW_TAG_subprogram || Tag == dwarf::DW_TAG_lexical_block) {
    // skip this Die and just recurse down
    for (DWARFDie ChildDie : Die.children())
      parseInlineInfo(Gsym, Out, CUI, ChildDie, Depth + 1, FI, Parent,
                      AllParentRanges, WarnIfEmpty);
  }
}

static void convertFunctionLineTable(OutputAggregator &Out, CUInfo &CUI,
                                     DWARFDie Die, GsymCreator &Gsym,
                                     FunctionInfo &FI) {
  std::vector<uint32_t> RowVector;
  const uint64_t StartAddress = FI.startAddress();
  const uint64_t EndAddress = FI.endAddress();
  const uint64_t RangeSize = EndAddress - StartAddress;
  const object::SectionedAddress SecAddress{
      StartAddress, object::SectionedAddress::UndefSection};


  if (!CUI.LineTable->lookupAddressRange(SecAddress, RangeSize, RowVector)) {
    // If we have a DW_TAG_subprogram but no line entries, fall back to using
    // the DW_AT_decl_file an d DW_AT_decl_line if we have both attributes.
    std::string FilePath = Die.getDeclFile(
        DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath);
    if (FilePath.empty()) {
      // If we had a DW_AT_decl_file, but got no file then we need to emit a
      // warning.
      Out.Report("Invalid file index in DW_AT_decl_file", [&](raw_ostream &OS) {
        const uint64_t DwarfFileIdx = dwarf::toUnsigned(
            Die.findRecursively(dwarf::DW_AT_decl_file), UINT32_MAX);
        OS << "error: function DIE at " << HEX32(Die.getOffset())
           << " has an invalid file index " << DwarfFileIdx
           << " in its DW_AT_decl_file attribute, unable to create a single "
           << "line entry from the DW_AT_decl_file/DW_AT_decl_line "
           << "attributes.\n";
      });
      return;
    }
    if (auto Line =
            dwarf::toUnsigned(Die.findRecursively({dwarf::DW_AT_decl_line}))) {
      LineEntry LE(StartAddress, Gsym.insertFile(FilePath), *Line);
      FI.OptLineTable = LineTable();
      FI.OptLineTable->push(LE);
    }
    return;
  }

  FI.OptLineTable = LineTable();
  DWARFDebugLine::Row PrevRow;
  for (uint32_t RowIndex : RowVector) {
    // Take file number and line/column from the row.
    const DWARFDebugLine::Row &Row = CUI.LineTable->Rows[RowIndex];
    std::optional<uint32_t> OptFileIdx =
        CUI.DWARFToGSYMFileIndex(Gsym, Row.File);
    if (!OptFileIdx) {
      Out.Report(
          "Invalid file index in DWARF line table", [&](raw_ostream &OS) {
            OS << "error: function DIE at " << HEX32(Die.getOffset()) << " has "
               << "a line entry with invalid DWARF file index, this entry will "
               << "be removed:\n";
            Row.dumpTableHeader(OS, /*Indent=*/0);
            Row.dump(OS);
            OS << "\n";
          });
      continue;
    }
    const uint32_t FileIdx = OptFileIdx.value();
    uint64_t RowAddress = Row.Address.Address;
    // Watch out for a RowAddress that is in the middle of a line table entry
    // in the DWARF. If we pass an address in between two line table entries
    // we will get a RowIndex for the previous valid line table row which won't
    // be contained in our function. This is usually a bug in the DWARF due to
    // linker problems or LTO or other DWARF re-linking so it is worth emitting
    // an error, but not worth stopping the creation of the GSYM.
    if (!FI.Range.contains(RowAddress)) {
      if (RowAddress < FI.Range.start()) {
        Out.Report("Start address lies between valid Row table entries",
                   [&](raw_ostream &OS) {
                     OS << "error: DIE has a start address whose LowPC is "
                           "between the "
                           "line table Row["
                        << RowIndex << "] with address " << HEX64(RowAddress)
                        << " and the next one.\n";
                     Die.dump(OS, 0, DIDumpOptions::getForSingleDIE());
                   });
        RowAddress = FI.Range.start();
      } else {
        continue;
      }
    }

    LineEntry LE(RowAddress, FileIdx, Row.Line);
    if (RowIndex != RowVector[0] && Row.Address < PrevRow.Address) {
      // We have seen full duplicate line tables for functions in some
      // DWARF files. Watch for those here by checking the last
      // row was the function's end address (HighPC) and that the
      // current line table entry's address is the same as the first
      // line entry we already have in our "function_info.Lines". If
      // so break out after printing a warning.
      auto FirstLE = FI.OptLineTable->first();
      if (FirstLE && *FirstLE == LE)
        // if (Log && !Gsym.isQuiet()) { TODO <-- This looks weird
        Out.Report("Duplicate line table detected", [&](raw_ostream &OS) {
          OS << "warning: duplicate line table detected for DIE:\n";
          Die.dump(OS, 0, DIDumpOptions::getForSingleDIE());
        });
      else
        Out.Report("Non-monotonically increasing addresses",
                   [&](raw_ostream &OS) {
                     OS << "error: line table has addresses that do not "
                        << "monotonically increase:\n";
                     for (uint32_t RowIndex2 : RowVector)
                       CUI.LineTable->Rows[RowIndex2].dump(OS);
                     Die.dump(OS, 0, DIDumpOptions::getForSingleDIE());
                   });
      break;
    }

    // Skip multiple line entries for the same file and line.
    auto LastLE = FI.OptLineTable->last();
    if (LastLE && LastLE->File == FileIdx && LastLE->Line == Row.Line)
        continue;
    // Only push a row if it isn't an end sequence. End sequence markers are
    // included for the last address in a function or the last contiguous
    // address in a sequence.
    if (Row.EndSequence) {
      // End sequence means that the next line entry could have a lower address
      // that the previous entries. So we clear the previous row so we don't
      // trigger the line table error about address that do not monotonically
      // increase.
      PrevRow = DWARFDebugLine::Row();
    } else {
      FI.OptLineTable->push(LE);
      PrevRow = Row;
    }
  }
  // If not line table rows were added, clear the line table so we don't encode
  // on in the GSYM file.
  if (FI.OptLineTable->empty())
    FI.OptLineTable = std::nullopt;
}

void DwarfTransformer::handleDie(OutputAggregator &Out, CUInfo &CUI,
                                 DWARFDie Die) {
  switch (Die.getTag()) {
  case dwarf::DW_TAG_subprogram: {
    Expected<DWARFAddressRangesVector> RangesOrError = Die.getAddressRanges();
    if (!RangesOrError) {
      consumeError(RangesOrError.takeError());
      break;
    }
    const DWARFAddressRangesVector &Ranges = RangesOrError.get();
    if (Ranges.empty())
      break;
    auto NameIndex = getQualifiedNameIndex(Die, CUI.Language, Gsym);
    if (!NameIndex) {
      Out.Report("Function has no name", [&](raw_ostream &OS) {
        OS << "error: function at " << HEX64(Die.getOffset())
           << " has no name\n ";
        Die.dump(OS, 0, DIDumpOptions::getForSingleDIE());
      });
      break;
    }
    // All ranges for the subprogram DIE in case it has multiple. We need to
    // pass this down into parseInlineInfo so we don't warn about inline
    // ranges that are not in the current subrange of a function when they
    // actually are in another subgrange. We do this because when a function
    // has discontiguos ranges, we create multiple function entries with only
    // the info for that range contained inside of it.
    AddressRanges AllSubprogramRanges = ConvertDWARFRanges(Ranges);

    // Create a function_info for each range
    for (const DWARFAddressRange &Range : Ranges) {
      // The low PC must be less than the high PC. Many linkers don't remove
      // DWARF for functions that don't get linked into the final executable.
      // If both the high and low pc have relocations, linkers will often set
      // the address values for both to the same value to indicate the function
      // has been remove. Other linkers have been known to set the one or both
      // PC values to a UINT32_MAX for 4 byte addresses and UINT64_MAX for 8
      // byte addresses to indicate the function isn't valid. The check below
      // tries to watch for these cases and abort if it runs into them.
      if (Range.LowPC >= Range.HighPC || CUI.isHighestAddress(Range.LowPC))
        break;

      // Many linkers can't remove DWARF and might set the LowPC to zero. Since
      // high PC can be an offset from the low PC in more recent DWARF versions
      // we need to watch for a zero'ed low pc which we do using ValidTextRanges
      // below.
      if (!Gsym.IsValidTextAddress(Range.LowPC)) {
        // We expect zero and -1 to be invalid addresses in DWARF depending
        // on the linker of the DWARF. This indicates a function was stripped
        // and the debug info wasn't able to be stripped from the DWARF. If
        // the LowPC isn't zero or -1, then we should emit an error.
        if (Range.LowPC != 0) {
          if (!Gsym.isQuiet()) {
            // Unexpected invalid address, emit a warning
            Out.Report("Address range starts outside executable section",
                       [&](raw_ostream &OS) {
                         OS << "warning: DIE has an address range whose "
                               "start address "
                               "is not in any executable sections ("
                            << *Gsym.GetValidTextRanges()
                            << ") and will not be processed:\n";
                         Die.dump(OS, 0, DIDumpOptions::getForSingleDIE());
                       });
          }
        }
        break;
      }

      FunctionInfo FI;
      FI.Range = {Range.LowPC, Range.HighPC};
      FI.Name = *NameIndex;
      if (CUI.LineTable)
        convertFunctionLineTable(Out, CUI, Die, Gsym, FI);

      if (hasInlineInfo(Die, 0)) {
        FI.Inline = InlineInfo();
        FI.Inline->Name = *NameIndex;
        FI.Inline->Ranges.insert(FI.Range);
        bool WarnIfEmpty = true;
        parseInlineInfo(Gsym, Out, CUI, Die, 0, FI, *FI.Inline,
                        AllSubprogramRanges, WarnIfEmpty);
        // Make sure we at least got some valid inline info other than just
        // the top level function. If we didn't then remove the inline info
        // from the function info. We have seen cases where LTO tries to modify
        // the DWARF for functions and it messes up the address ranges for
        // the inline functions so it is no longer valid.
        //
        // By checking if there are any valid children on the top level inline
        // information object, we will know if we got anything valid from the
        // debug info.
        if (FI.Inline->Children.empty()) {
          if (WarnIfEmpty && !Gsym.isQuiet())
            Out.Report("DIE contains inline functions with no valid ranges",
                       [&](raw_ostream &OS) {
                         OS << "warning: DIE contains inline function "
                               "information that has no valid ranges, removing "
                               "inline information:\n";
                         Die.dump(OS, 0, DIDumpOptions::getForSingleDIE());
                       });
          FI.Inline = std::nullopt;
        }
      }
      Gsym.addFunctionInfo(std::move(FI));
    }
  } break;
  default:
    break;
  }
  for (DWARFDie ChildDie : Die.children())
    handleDie(Out, CUI, ChildDie);
}

Error DwarfTransformer::convert(uint32_t NumThreads, OutputAggregator &Out) {
  size_t NumBefore = Gsym.getNumFunctionInfos();
  auto getDie = [&](DWARFUnit &DwarfUnit) -> DWARFDie {
    DWARFDie ReturnDie = DwarfUnit.getUnitDIE(false);
    if (DwarfUnit.getDWOId()) {
      DWARFUnit *DWOCU = DwarfUnit.getNonSkeletonUnitDIE(false).getDwarfUnit();
      if (!DWOCU->isDWOUnit())
        Out.Report(
            "warning: Unable to retrieve DWO .debug_info section for some "
            "object files. (Remove the --quiet flag for full output)",
            [&](raw_ostream &OS) {
              std::string DWOName = dwarf::toString(
                  DwarfUnit.getUnitDIE().find(
                      {dwarf::DW_AT_dwo_name, dwarf::DW_AT_GNU_dwo_name}),
                  "");
              OS << "warning: Unable to retrieve DWO .debug_info section for "
                 << DWOName << "\n";
            });
      else {
        ReturnDie = DWOCU->getUnitDIE(false);
      }
    }
    return ReturnDie;
  };
  if (NumThreads == 1) {
    // Parse all DWARF data from this thread, use the same string/file table
    // for everything
    for (const auto &CU : DICtx.compile_units()) {
      DWARFDie Die = getDie(*CU);
      CUInfo CUI(DICtx, dyn_cast<DWARFCompileUnit>(CU.get()));
      handleDie(Out, CUI, Die);
    }
  } else {
    // LLVM Dwarf parser is not thread-safe and we need to parse all DWARF up
    // front before we start accessing any DIEs since there might be
    // cross compile unit references in the DWARF. If we don't do this we can
    // end up crashing.

    // We need to call getAbbreviations sequentially first so that getUnitDIE()
    // only works with its local data.
    for (const auto &CU : DICtx.compile_units())
      CU->getAbbreviations();

    // Now parse all DIEs in case we have cross compile unit references in a
    // thread pool.
    DefaultThreadPool pool(hardware_concurrency(NumThreads));
    for (const auto &CU : DICtx.compile_units())
      pool.async([&CU]() { CU->getUnitDIE(false /*CUDieOnly*/); });
    pool.wait();

    // Now convert all DWARF to GSYM in a thread pool.
    std::mutex LogMutex;
    for (const auto &CU : DICtx.compile_units()) {
      DWARFDie Die = getDie(*CU);
      if (Die) {
        CUInfo CUI(DICtx, dyn_cast<DWARFCompileUnit>(CU.get()));
        pool.async([this, CUI, &LogMutex, &Out, Die]() mutable {
          std::string storage;
          raw_string_ostream StrStream(storage);
          OutputAggregator ThreadOut(Out.GetOS() ? &StrStream : nullptr);
          handleDie(ThreadOut, CUI, Die);
          // Print ThreadLogStorage lines into an actual stream under a lock
          std::lock_guard<std::mutex> guard(LogMutex);
          if (Out.GetOS()) {
            StrStream.flush();
            Out << storage;
          }
          Out.Merge(ThreadOut);
        });
      }
    }
    pool.wait();
  }
  size_t FunctionsAddedCount = Gsym.getNumFunctionInfos() - NumBefore;
  Out << "Loaded " << FunctionsAddedCount << " functions from DWARF.\n";
  return Error::success();
}

llvm::Error DwarfTransformer::verify(StringRef GsymPath,
                                     OutputAggregator &Out) {
  Out << "Verifying GSYM file \"" << GsymPath << "\":\n";

  auto Gsym = GsymReader::openFile(GsymPath);
  if (!Gsym)
    return Gsym.takeError();

  auto NumAddrs = Gsym->getNumAddresses();
  DILineInfoSpecifier DLIS(
      DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
      DILineInfoSpecifier::FunctionNameKind::LinkageName);
  std::string gsymFilename;
  for (uint32_t I = 0; I < NumAddrs; ++I) {
    auto FuncAddr = Gsym->getAddress(I);
    if (!FuncAddr)
        return createStringError(std::errc::invalid_argument,
                                  "failed to extract address[%i]", I);

    auto FI = Gsym->getFunctionInfo(*FuncAddr);
    if (!FI)
      return createStringError(
          std::errc::invalid_argument,
          "failed to extract function info for address 0x%" PRIu64, *FuncAddr);

    for (auto Addr = *FuncAddr; Addr < *FuncAddr + FI->size(); ++Addr) {
      const object::SectionedAddress SectAddr{
          Addr, object::SectionedAddress::UndefSection};
      auto LR = Gsym->lookup(Addr);
      if (!LR)
        return LR.takeError();

      auto DwarfInlineInfos =
          DICtx.getInliningInfoForAddress(SectAddr, DLIS);
      uint32_t NumDwarfInlineInfos = DwarfInlineInfos.getNumberOfFrames();
      if (NumDwarfInlineInfos == 0) {
        DwarfInlineInfos.addFrame(
            DICtx.getLineInfoForAddress(SectAddr, DLIS));
      }

      // Check for 1 entry that has no file and line info
      if (NumDwarfInlineInfos == 1 &&
          DwarfInlineInfos.getFrame(0).FileName == "<invalid>") {
        DwarfInlineInfos = DIInliningInfo();
        NumDwarfInlineInfos = 0;
      }
      if (NumDwarfInlineInfos > 0 &&
          NumDwarfInlineInfos != LR->Locations.size()) {
        if (Out.GetOS()) {
          raw_ostream &Log = *Out.GetOS();
          Log << "error: address " << HEX64(Addr) << " has "
              << NumDwarfInlineInfos << " DWARF inline frames and GSYM has "
              << LR->Locations.size() << "\n";
          Log << "    " << NumDwarfInlineInfos << " DWARF frames:\n";
          for (size_t Idx = 0; Idx < NumDwarfInlineInfos; ++Idx) {
            const auto &dii = DwarfInlineInfos.getFrame(Idx);
            Log << "    [" << Idx << "]: " << dii.FunctionName << " @ "
                << dii.FileName << ':' << dii.Line << '\n';
          }
          Log << "    " << LR->Locations.size() << " GSYM frames:\n";
          for (size_t Idx = 0, count = LR->Locations.size(); Idx < count;
               ++Idx) {
            const auto &gii = LR->Locations[Idx];
            Log << "    [" << Idx << "]: " << gii.Name << " @ " << gii.Dir
                << '/' << gii.Base << ':' << gii.Line << '\n';
          }
          DwarfInlineInfos = DICtx.getInliningInfoForAddress(SectAddr, DLIS);
          Gsym->dump(Log, *FI);
        }
        continue;
      }

      for (size_t Idx = 0, count = LR->Locations.size(); Idx < count;
            ++Idx) {
        const auto &gii = LR->Locations[Idx];
        if (Idx < NumDwarfInlineInfos) {
          const auto &dii = DwarfInlineInfos.getFrame(Idx);
          gsymFilename = LR->getSourceFile(Idx);
          // Verify function name
          if (dii.FunctionName.find(gii.Name.str()) != 0)
            Out << "error: address " << HEX64(Addr) << " DWARF function \""
                << dii.FunctionName.c_str()
                << "\" doesn't match GSYM function \"" << gii.Name << "\"\n";

          // Verify source file path
          if (dii.FileName != gsymFilename)
            Out << "error: address " << HEX64(Addr) << " DWARF path \""
                << dii.FileName.c_str() << "\" doesn't match GSYM path \""
                << gsymFilename.c_str() << "\"\n";
          // Verify source file line
          if (dii.Line != gii.Line)
            Out << "error: address " << HEX64(Addr) << " DWARF line "
                << dii.Line << " != GSYM line " << gii.Line << "\n";
        }
      }
    }
  }
  return Error::success();
}
