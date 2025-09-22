//===- DwarfStreamer.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DWARFLinker/Classic/DWARFStreamer.h"
#include "llvm/CodeGen/NonRelocatableStringpool.h"
#include "llvm/DWARFLinker/Classic/DWARFLinkerCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugMacro.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::classic;

Expected<std::unique_ptr<DwarfStreamer>> DwarfStreamer::createStreamer(
    const Triple &TheTriple, DWARFLinkerBase::OutputFileType FileType,
    raw_pwrite_stream &OutFile, DWARFLinkerBase::MessageHandlerTy Warning) {
  std::unique_ptr<DwarfStreamer> Streamer =
      std::make_unique<DwarfStreamer>(FileType, OutFile, Warning);
  if (Error Err = Streamer->init(TheTriple, "__DWARF"))
    return std::move(Err);

  return std::move(Streamer);
}

Error DwarfStreamer::init(Triple TheTriple,
                          StringRef Swift5ReflectionSegmentName) {
  std::string ErrorStr;
  std::string TripleName;

  // Get the target.
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TripleName, TheTriple, ErrorStr);
  if (!TheTarget)
    return createStringError(std::errc::invalid_argument, ErrorStr.c_str());

  TripleName = TheTriple.getTriple();

  // Create all the MC Objects.
  MRI.reset(TheTarget->createMCRegInfo(TripleName));
  if (!MRI)
    return createStringError(std::errc::invalid_argument,
                             "no register info for target %s",
                             TripleName.c_str());

  MCTargetOptions MCOptions = mc::InitMCTargetOptionsFromFlags();
  MCOptions.AsmVerbose = true;
  MCOptions.MCUseDwarfDirectory = MCTargetOptions::EnableDwarfDirectory;
  MAI.reset(TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  if (!MAI)
    return createStringError(std::errc::invalid_argument,
                             "no asm info for target %s", TripleName.c_str());

  MSTI.reset(TheTarget->createMCSubtargetInfo(TripleName, "", ""));
  if (!MSTI)
    return createStringError(std::errc::invalid_argument,
                             "no subtarget info for target %s",
                             TripleName.c_str());

  MC.reset(new MCContext(TheTriple, MAI.get(), MRI.get(), MSTI.get(), nullptr,
                         nullptr, true, Swift5ReflectionSegmentName));
  MOFI.reset(TheTarget->createMCObjectFileInfo(*MC, /*PIC=*/false, false));
  MC->setObjectFileInfo(MOFI.get());

  MAB = TheTarget->createMCAsmBackend(*MSTI, *MRI, MCOptions);
  if (!MAB)
    return createStringError(std::errc::invalid_argument,
                             "no asm backend for target %s",
                             TripleName.c_str());

  MII.reset(TheTarget->createMCInstrInfo());
  if (!MII)
    return createStringError(std::errc::invalid_argument,
                             "no instr info info for target %s",
                             TripleName.c_str());

  MCE = TheTarget->createMCCodeEmitter(*MII, *MC);
  if (!MCE)
    return createStringError(std::errc::invalid_argument,
                             "no code emitter for target %s",
                             TripleName.c_str());

  switch (OutFileType) {
  case DWARFLinker::OutputFileType::Assembly: {
    MIP = TheTarget->createMCInstPrinter(TheTriple, MAI->getAssemblerDialect(),
                                         *MAI, *MII, *MRI);
    MS = TheTarget->createAsmStreamer(
        *MC, std::make_unique<formatted_raw_ostream>(OutFile), MIP,
        std::unique_ptr<MCCodeEmitter>(MCE),
        std::unique_ptr<MCAsmBackend>(MAB));
    break;
  }
  case DWARFLinker::OutputFileType::Object: {
    MS = TheTarget->createMCObjectStreamer(
        TheTriple, *MC, std::unique_ptr<MCAsmBackend>(MAB),
        MAB->createObjectWriter(OutFile), std::unique_ptr<MCCodeEmitter>(MCE),
        *MSTI);
    break;
  }
  }

  if (!MS)
    return createStringError(std::errc::invalid_argument,
                             "no object streamer for target %s",
                             TripleName.c_str());

  // Finally create the AsmPrinter we'll use to emit the DIEs.
  TM.reset(TheTarget->createTargetMachine(TripleName, "", "", TargetOptions(),
                                          std::nullopt));
  if (!TM)
    return createStringError(std::errc::invalid_argument,
                             "no target machine for target %s",
                             TripleName.c_str());

  Asm.reset(TheTarget->createAsmPrinter(*TM, std::unique_ptr<MCStreamer>(MS)));
  if (!Asm)
    return createStringError(std::errc::invalid_argument,
                             "no asm printer for target %s",
                             TripleName.c_str());
  Asm->setDwarfUsesRelocationsAcrossSections(false);

  RangesSectionSize = 0;
  RngListsSectionSize = 0;
  LocSectionSize = 0;
  LocListsSectionSize = 0;
  LineSectionSize = 0;
  FrameSectionSize = 0;
  DebugInfoSectionSize = 0;
  MacInfoSectionSize = 0;
  MacroSectionSize = 0;

  return Error::success();
}

void DwarfStreamer::finish() { MS->finish(); }

void DwarfStreamer::switchToDebugInfoSection(unsigned DwarfVersion) {
  MS->switchSection(MOFI->getDwarfInfoSection());
  MC->setDwarfVersion(DwarfVersion);
}

/// Emit the compilation unit header for \p Unit in the debug_info section.
///
/// A Dwarf 4 section header is encoded as:
///  uint32_t   Unit length (omitting this field)
///  uint16_t   Version
///  uint32_t   Abbreviation table offset
///  uint8_t    Address size
/// Leading to a total of 11 bytes.
///
/// A Dwarf 5 section header is encoded as:
///  uint32_t   Unit length (omitting this field)
///  uint16_t   Version
///  uint8_t    Unit type
///  uint8_t    Address size
///  uint32_t   Abbreviation table offset
/// Leading to a total of 12 bytes.
void DwarfStreamer::emitCompileUnitHeader(CompileUnit &Unit,
                                          unsigned DwarfVersion) {
  switchToDebugInfoSection(DwarfVersion);

  /// The start of the unit within its section.
  Unit.setLabelBegin(Asm->createTempSymbol("cu_begin"));
  Asm->OutStreamer->emitLabel(Unit.getLabelBegin());

  // Emit size of content not including length itself. The size has already
  // been computed in CompileUnit::computeOffsets(). Subtract 4 to that size to
  // account for the length field.
  Asm->emitInt32(Unit.getNextUnitOffset() - Unit.getStartOffset() - 4);
  Asm->emitInt16(DwarfVersion);

  if (DwarfVersion >= 5) {
    Asm->emitInt8(dwarf::DW_UT_compile);
    Asm->emitInt8(Unit.getOrigUnit().getAddressByteSize());
    // We share one abbreviations table across all units so it's always at the
    // start of the section.
    Asm->emitInt32(0);
    DebugInfoSectionSize += 12;
  } else {
    // We share one abbreviations table across all units so it's always at the
    // start of the section.
    Asm->emitInt32(0);
    Asm->emitInt8(Unit.getOrigUnit().getAddressByteSize());
    DebugInfoSectionSize += 11;
  }

  // Remember this CU.
  EmittedUnits.push_back({Unit.getUniqueID(), Unit.getLabelBegin()});
}

/// Emit the \p Abbrevs array as the shared abbreviation table
/// for the linked Dwarf file.
void DwarfStreamer::emitAbbrevs(
    const std::vector<std::unique_ptr<DIEAbbrev>> &Abbrevs,
    unsigned DwarfVersion) {
  MS->switchSection(MOFI->getDwarfAbbrevSection());
  MC->setDwarfVersion(DwarfVersion);
  Asm->emitDwarfAbbrevs(Abbrevs);
}

/// Recursively emit the DIE tree rooted at \p Die.
void DwarfStreamer::emitDIE(DIE &Die) {
  MS->switchSection(MOFI->getDwarfInfoSection());
  Asm->emitDwarfDIE(Die);
  DebugInfoSectionSize += Die.getSize();
}

/// Emit contents of section SecName From Obj.
void DwarfStreamer::emitSectionContents(StringRef SecData,
                                        DebugSectionKind SecKind) {
  if (SecData.empty())
    return;

  if (MCSection *Section = getMCSection(SecKind)) {
    MS->switchSection(Section);

    MS->emitBytes(SecData);
  }
}

MCSection *DwarfStreamer::getMCSection(DebugSectionKind SecKind) {
  switch (SecKind) {
  case DebugSectionKind::DebugInfo:
    return MC->getObjectFileInfo()->getDwarfInfoSection();
  case DebugSectionKind::DebugLine:
    return MC->getObjectFileInfo()->getDwarfLineSection();
  case DebugSectionKind::DebugFrame:
    return MC->getObjectFileInfo()->getDwarfFrameSection();
  case DebugSectionKind::DebugRange:
    return MC->getObjectFileInfo()->getDwarfRangesSection();
  case DebugSectionKind::DebugRngLists:
    return MC->getObjectFileInfo()->getDwarfRnglistsSection();
  case DebugSectionKind::DebugLoc:
    return MC->getObjectFileInfo()->getDwarfLocSection();
  case DebugSectionKind::DebugLocLists:
    return MC->getObjectFileInfo()->getDwarfLoclistsSection();
  case DebugSectionKind::DebugARanges:
    return MC->getObjectFileInfo()->getDwarfARangesSection();
  case DebugSectionKind::DebugAbbrev:
    return MC->getObjectFileInfo()->getDwarfAbbrevSection();
  case DebugSectionKind::DebugMacinfo:
    return MC->getObjectFileInfo()->getDwarfMacinfoSection();
  case DebugSectionKind::DebugMacro:
    return MC->getObjectFileInfo()->getDwarfMacroSection();
  case DebugSectionKind::DebugAddr:
    return MC->getObjectFileInfo()->getDwarfAddrSection();
  case DebugSectionKind::DebugStr:
    return MC->getObjectFileInfo()->getDwarfStrSection();
  case DebugSectionKind::DebugLineStr:
    return MC->getObjectFileInfo()->getDwarfLineStrSection();
  case DebugSectionKind::DebugStrOffsets:
    return MC->getObjectFileInfo()->getDwarfStrOffSection();
  case DebugSectionKind::DebugPubNames:
    return MC->getObjectFileInfo()->getDwarfPubNamesSection();
  case DebugSectionKind::DebugPubTypes:
    return MC->getObjectFileInfo()->getDwarfPubTypesSection();
  case DebugSectionKind::DebugNames:
    return MC->getObjectFileInfo()->getDwarfDebugNamesSection();
  case DebugSectionKind::AppleNames:
    return MC->getObjectFileInfo()->getDwarfAccelNamesSection();
  case DebugSectionKind::AppleNamespaces:
    return MC->getObjectFileInfo()->getDwarfAccelNamespaceSection();
  case DebugSectionKind::AppleObjC:
    return MC->getObjectFileInfo()->getDwarfAccelObjCSection();
  case DebugSectionKind::AppleTypes:
    return MC->getObjectFileInfo()->getDwarfAccelTypesSection();
  case DebugSectionKind::NumberOfEnumEntries:
    llvm_unreachable("Unknown DebugSectionKind value");
    break;
  }

  return nullptr;
}

/// Emit the debug_str section stored in \p Pool.
void DwarfStreamer::emitStrings(const NonRelocatableStringpool &Pool) {
  Asm->OutStreamer->switchSection(MOFI->getDwarfStrSection());
  std::vector<DwarfStringPoolEntryRef> Entries = Pool.getEntriesForEmission();
  for (auto Entry : Entries) {
    // Emit the string itself.
    Asm->OutStreamer->emitBytes(Entry.getString());
    // Emit a null terminator.
    Asm->emitInt8(0);
  }
}

/// Emit the debug string offset table described by \p StringOffsets into the
/// .debug_str_offsets table.
void DwarfStreamer::emitStringOffsets(
    const SmallVector<uint64_t> &StringOffsets, uint16_t TargetDWARFVersion) {

  if (TargetDWARFVersion < 5 || StringOffsets.empty())
    return;

  Asm->OutStreamer->switchSection(MOFI->getDwarfStrOffSection());

  MCSymbol *BeginLabel = Asm->createTempSymbol("Bdebugstroff");
  MCSymbol *EndLabel = Asm->createTempSymbol("Edebugstroff");

  // Length.
  Asm->emitLabelDifference(EndLabel, BeginLabel, sizeof(uint32_t));
  Asm->OutStreamer->emitLabel(BeginLabel);
  StrOffsetSectionSize += sizeof(uint32_t);

  // Version.
  MS->emitInt16(5);
  StrOffsetSectionSize += sizeof(uint16_t);

  // Padding.
  MS->emitInt16(0);
  StrOffsetSectionSize += sizeof(uint16_t);

  for (auto Off : StringOffsets) {
    Asm->OutStreamer->emitInt32(Off);
    StrOffsetSectionSize += sizeof(uint32_t);
  }
  Asm->OutStreamer->emitLabel(EndLabel);
}

/// Emit the debug_line_str section stored in \p Pool.
void DwarfStreamer::emitLineStrings(const NonRelocatableStringpool &Pool) {
  Asm->OutStreamer->switchSection(MOFI->getDwarfLineStrSection());
  std::vector<DwarfStringPoolEntryRef> Entries = Pool.getEntriesForEmission();
  for (auto Entry : Entries) {
    // Emit the string itself.
    Asm->OutStreamer->emitBytes(Entry.getString());
    // Emit a null terminator.
    Asm->emitInt8(0);
  }
}

void DwarfStreamer::emitDebugNames(DWARF5AccelTable &Table) {
  if (EmittedUnits.empty())
    return;

  // Build up data structures needed to emit this section.
  std::vector<std::variant<MCSymbol *, uint64_t>> CompUnits;
  DenseMap<unsigned, unsigned> UniqueIdToCuMap;
  unsigned Id = 0;
  for (auto &CU : EmittedUnits) {
    CompUnits.push_back(CU.LabelBegin);
    // We might be omitting CUs, so we need to remap them.
    UniqueIdToCuMap[CU.ID] = Id++;
  }

  Asm->OutStreamer->switchSection(MOFI->getDwarfDebugNamesSection());
  dwarf::Form Form = DIEInteger::BestForm(/*IsSigned*/ false,
                                          (uint64_t)UniqueIdToCuMap.size() - 1);
  /// llvm-dwarfutil doesn't support type units + .debug_names right now.
  // FIXME: add support for type units + .debug_names. For now the behavior is
  // unsuported.
  emitDWARF5AccelTable(
      Asm.get(), Table, CompUnits,
      [&](const DWARF5AccelTableData &Entry)
          -> std::optional<DWARF5AccelTable::UnitIndexAndEncoding> {
        if (UniqueIdToCuMap.size() > 1)
          return {{UniqueIdToCuMap[Entry.getUnitID()],
                   {dwarf::DW_IDX_compile_unit, Form}}};
        return std::nullopt;
      });
}

void DwarfStreamer::emitAppleNamespaces(
    AccelTable<AppleAccelTableStaticOffsetData> &Table) {
  Asm->OutStreamer->switchSection(MOFI->getDwarfAccelNamespaceSection());
  auto *SectionBegin = Asm->createTempSymbol("namespac_begin");
  Asm->OutStreamer->emitLabel(SectionBegin);
  emitAppleAccelTable(Asm.get(), Table, "namespac", SectionBegin);
}

void DwarfStreamer::emitAppleNames(
    AccelTable<AppleAccelTableStaticOffsetData> &Table) {
  Asm->OutStreamer->switchSection(MOFI->getDwarfAccelNamesSection());
  auto *SectionBegin = Asm->createTempSymbol("names_begin");
  Asm->OutStreamer->emitLabel(SectionBegin);
  emitAppleAccelTable(Asm.get(), Table, "names", SectionBegin);
}

void DwarfStreamer::emitAppleObjc(
    AccelTable<AppleAccelTableStaticOffsetData> &Table) {
  Asm->OutStreamer->switchSection(MOFI->getDwarfAccelObjCSection());
  auto *SectionBegin = Asm->createTempSymbol("objc_begin");
  Asm->OutStreamer->emitLabel(SectionBegin);
  emitAppleAccelTable(Asm.get(), Table, "objc", SectionBegin);
}

void DwarfStreamer::emitAppleTypes(
    AccelTable<AppleAccelTableStaticTypeData> &Table) {
  Asm->OutStreamer->switchSection(MOFI->getDwarfAccelTypesSection());
  auto *SectionBegin = Asm->createTempSymbol("types_begin");
  Asm->OutStreamer->emitLabel(SectionBegin);
  emitAppleAccelTable(Asm.get(), Table, "types", SectionBegin);
}

/// Emit the swift_ast section stored in \p Buffers.
void DwarfStreamer::emitSwiftAST(StringRef Buffer) {
  MCSection *SwiftASTSection = MOFI->getDwarfSwiftASTSection();
  SwiftASTSection->setAlignment(Align(32));
  MS->switchSection(SwiftASTSection);
  MS->emitBytes(Buffer);
}

void DwarfStreamer::emitSwiftReflectionSection(
    llvm::binaryformat::Swift5ReflectionSectionKind ReflSectionKind,
    StringRef Buffer, uint32_t Alignment, uint32_t Size) {
  MCSection *ReflectionSection =
      MOFI->getSwift5ReflectionSection(ReflSectionKind);
  if (ReflectionSection == nullptr)
    return;
  ReflectionSection->setAlignment(Align(Alignment));
  MS->switchSection(ReflectionSection);
  MS->emitBytes(Buffer);
}

void DwarfStreamer::emitDwarfDebugArangesTable(
    const CompileUnit &Unit, const AddressRanges &LinkedRanges) {
  unsigned AddressSize = Unit.getOrigUnit().getAddressByteSize();

  // Make .debug_aranges to be current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfARangesSection());

  // Emit Header.
  MCSymbol *BeginLabel = Asm->createTempSymbol("Barange");
  MCSymbol *EndLabel = Asm->createTempSymbol("Earange");

  unsigned HeaderSize =
      sizeof(int32_t) + // Size of contents (w/o this field
      sizeof(int16_t) + // DWARF ARange version number
      sizeof(int32_t) + // Offset of CU in the .debug_info section
      sizeof(int8_t) +  // Pointer Size (in bytes)
      sizeof(int8_t);   // Segment Size (in bytes)

  unsigned TupleSize = AddressSize * 2;
  unsigned Padding = offsetToAlignment(HeaderSize, Align(TupleSize));

  Asm->emitLabelDifference(EndLabel, BeginLabel, 4); // Arange length
  Asm->OutStreamer->emitLabel(BeginLabel);
  Asm->emitInt16(dwarf::DW_ARANGES_VERSION); // Version number
  Asm->emitInt32(Unit.getStartOffset());     // Corresponding unit's offset
  Asm->emitInt8(AddressSize);                // Address size
  Asm->emitInt8(0);                          // Segment size

  Asm->OutStreamer->emitFill(Padding, 0x0);

  // Emit linked ranges.
  for (const AddressRange &Range : LinkedRanges) {
    MS->emitIntValue(Range.start(), AddressSize);
    MS->emitIntValue(Range.end() - Range.start(), AddressSize);
  }

  // Emit terminator.
  Asm->OutStreamer->emitIntValue(0, AddressSize);
  Asm->OutStreamer->emitIntValue(0, AddressSize);
  Asm->OutStreamer->emitLabel(EndLabel);
}

void DwarfStreamer::emitDwarfDebugRangesTableFragment(
    const CompileUnit &Unit, const AddressRanges &LinkedRanges,
    PatchLocation Patch) {
  Patch.set(RangesSectionSize);

  // Make .debug_ranges to be current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfRangesSection());
  unsigned AddressSize = Unit.getOrigUnit().getAddressByteSize();

  // Emit ranges.
  uint64_t BaseAddress = 0;
  if (std::optional<uint64_t> LowPC = Unit.getLowPc())
    BaseAddress = *LowPC;

  for (const AddressRange &Range : LinkedRanges) {
    MS->emitIntValue(Range.start() - BaseAddress, AddressSize);
    MS->emitIntValue(Range.end() - BaseAddress, AddressSize);

    RangesSectionSize += AddressSize;
    RangesSectionSize += AddressSize;
  }

  // Add the terminator entry.
  MS->emitIntValue(0, AddressSize);
  MS->emitIntValue(0, AddressSize);

  RangesSectionSize += AddressSize;
  RangesSectionSize += AddressSize;
}

MCSymbol *
DwarfStreamer::emitDwarfDebugRangeListHeader(const CompileUnit &Unit) {
  if (Unit.getOrigUnit().getVersion() < 5)
    return nullptr;

  // Make .debug_rnglists to be current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfRnglistsSection());

  MCSymbol *BeginLabel = Asm->createTempSymbol("Brnglists");
  MCSymbol *EndLabel = Asm->createTempSymbol("Ernglists");
  unsigned AddressSize = Unit.getOrigUnit().getAddressByteSize();

  // Length
  Asm->emitLabelDifference(EndLabel, BeginLabel, sizeof(uint32_t));
  Asm->OutStreamer->emitLabel(BeginLabel);
  RngListsSectionSize += sizeof(uint32_t);

  // Version.
  MS->emitInt16(5);
  RngListsSectionSize += sizeof(uint16_t);

  // Address size.
  MS->emitInt8(AddressSize);
  RngListsSectionSize++;

  // Seg_size
  MS->emitInt8(0);
  RngListsSectionSize++;

  // Offset entry count
  MS->emitInt32(0);
  RngListsSectionSize += sizeof(uint32_t);

  return EndLabel;
}

void DwarfStreamer::emitDwarfDebugRangeListFragment(
    const CompileUnit &Unit, const AddressRanges &LinkedRanges,
    PatchLocation Patch, DebugDieValuePool &AddrPool) {
  if (Unit.getOrigUnit().getVersion() < 5) {
    emitDwarfDebugRangesTableFragment(Unit, LinkedRanges, Patch);
    return;
  }

  emitDwarfDebugRngListsTableFragment(Unit, LinkedRanges, Patch, AddrPool);
}

void DwarfStreamer::emitDwarfDebugRangeListFooter(const CompileUnit &Unit,
                                                  MCSymbol *EndLabel) {
  if (Unit.getOrigUnit().getVersion() < 5)
    return;

  // Make .debug_rnglists to be current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfRnglistsSection());

  if (EndLabel != nullptr)
    Asm->OutStreamer->emitLabel(EndLabel);
}

void DwarfStreamer::emitDwarfDebugRngListsTableFragment(
    const CompileUnit &Unit, const AddressRanges &LinkedRanges,
    PatchLocation Patch, DebugDieValuePool &AddrPool) {
  Patch.set(RngListsSectionSize);

  // Make .debug_rnglists to be current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfRnglistsSection());
  std::optional<uint64_t> BaseAddress;

  for (const AddressRange &Range : LinkedRanges) {

    if (!BaseAddress) {
      BaseAddress = Range.start();

      // Emit base address.
      MS->emitInt8(dwarf::DW_RLE_base_addressx);
      RngListsSectionSize += 1;
      RngListsSectionSize +=
          MS->emitULEB128IntValue(AddrPool.getValueIndex(*BaseAddress));
    }

    // Emit type of entry.
    MS->emitInt8(dwarf::DW_RLE_offset_pair);
    RngListsSectionSize += 1;

    // Emit start offset relative to base address.
    RngListsSectionSize +=
        MS->emitULEB128IntValue(Range.start() - *BaseAddress);

    // Emit end offset relative to base address.
    RngListsSectionSize += MS->emitULEB128IntValue(Range.end() - *BaseAddress);
  }

  // Emit the terminator entry.
  MS->emitInt8(dwarf::DW_RLE_end_of_list);
  RngListsSectionSize += 1;
}

/// Emit debug locations(.debug_loc, .debug_loclists) header.
MCSymbol *DwarfStreamer::emitDwarfDebugLocListHeader(const CompileUnit &Unit) {
  if (Unit.getOrigUnit().getVersion() < 5)
    return nullptr;

  // Make .debug_loclists the current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfLoclistsSection());

  MCSymbol *BeginLabel = Asm->createTempSymbol("Bloclists");
  MCSymbol *EndLabel = Asm->createTempSymbol("Eloclists");
  unsigned AddressSize = Unit.getOrigUnit().getAddressByteSize();

  // Length
  Asm->emitLabelDifference(EndLabel, BeginLabel, sizeof(uint32_t));
  Asm->OutStreamer->emitLabel(BeginLabel);
  LocListsSectionSize += sizeof(uint32_t);

  // Version.
  MS->emitInt16(5);
  LocListsSectionSize += sizeof(uint16_t);

  // Address size.
  MS->emitInt8(AddressSize);
  LocListsSectionSize++;

  // Seg_size
  MS->emitInt8(0);
  LocListsSectionSize++;

  // Offset entry count
  MS->emitInt32(0);
  LocListsSectionSize += sizeof(uint32_t);

  return EndLabel;
}

/// Emit debug locations(.debug_loc, .debug_loclists) fragment.
void DwarfStreamer::emitDwarfDebugLocListFragment(
    const CompileUnit &Unit,
    const DWARFLocationExpressionsVector &LinkedLocationExpression,
    PatchLocation Patch, DebugDieValuePool &AddrPool) {
  if (Unit.getOrigUnit().getVersion() < 5) {
    emitDwarfDebugLocTableFragment(Unit, LinkedLocationExpression, Patch);
    return;
  }

  emitDwarfDebugLocListsTableFragment(Unit, LinkedLocationExpression, Patch,
                                      AddrPool);
}

/// Emit debug locations(.debug_loc, .debug_loclists) footer.
void DwarfStreamer::emitDwarfDebugLocListFooter(const CompileUnit &Unit,
                                                MCSymbol *EndLabel) {
  if (Unit.getOrigUnit().getVersion() < 5)
    return;

  // Make .debug_loclists the current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfLoclistsSection());

  if (EndLabel != nullptr)
    Asm->OutStreamer->emitLabel(EndLabel);
}

/// Emit piece of .debug_loc for \p LinkedLocationExpression.
void DwarfStreamer::emitDwarfDebugLocTableFragment(
    const CompileUnit &Unit,
    const DWARFLocationExpressionsVector &LinkedLocationExpression,
    PatchLocation Patch) {
  Patch.set(LocSectionSize);

  // Make .debug_loc to be current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfLocSection());
  unsigned AddressSize = Unit.getOrigUnit().getAddressByteSize();

  // Emit ranges.
  uint64_t BaseAddress = 0;
  if (std::optional<uint64_t> LowPC = Unit.getLowPc())
    BaseAddress = *LowPC;

  for (const DWARFLocationExpression &LocExpression :
       LinkedLocationExpression) {
    if (LocExpression.Range) {
      MS->emitIntValue(LocExpression.Range->LowPC - BaseAddress, AddressSize);
      MS->emitIntValue(LocExpression.Range->HighPC - BaseAddress, AddressSize);

      LocSectionSize += AddressSize;
      LocSectionSize += AddressSize;
    }

    Asm->OutStreamer->emitIntValue(LocExpression.Expr.size(), 2);
    Asm->OutStreamer->emitBytes(StringRef(
        (const char *)LocExpression.Expr.data(), LocExpression.Expr.size()));
    LocSectionSize += LocExpression.Expr.size() + 2;
  }

  // Add the terminator entry.
  MS->emitIntValue(0, AddressSize);
  MS->emitIntValue(0, AddressSize);

  LocSectionSize += AddressSize;
  LocSectionSize += AddressSize;
}

/// Emit .debug_addr header.
MCSymbol *DwarfStreamer::emitDwarfDebugAddrsHeader(const CompileUnit &Unit) {

  // Make .debug_addr the current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfAddrSection());

  MCSymbol *BeginLabel = Asm->createTempSymbol("Bdebugaddr");
  MCSymbol *EndLabel = Asm->createTempSymbol("Edebugaddr");
  unsigned AddrSize = Unit.getOrigUnit().getAddressByteSize();

  // Emit length.
  Asm->emitLabelDifference(EndLabel, BeginLabel, sizeof(uint32_t));
  Asm->OutStreamer->emitLabel(BeginLabel);
  AddrSectionSize += sizeof(uint32_t);

  // Emit version.
  Asm->emitInt16(5);
  AddrSectionSize += 2;

  // Emit address size.
  Asm->emitInt8(AddrSize);
  AddrSectionSize += 1;

  // Emit segment size.
  Asm->emitInt8(0);
  AddrSectionSize += 1;

  return EndLabel;
}

/// Emit the .debug_addr addresses stored in \p Addrs.
void DwarfStreamer::emitDwarfDebugAddrs(const SmallVector<uint64_t> &Addrs,
                                        uint8_t AddrSize) {
  Asm->OutStreamer->switchSection(MOFI->getDwarfAddrSection());
  for (auto Addr : Addrs) {
    Asm->OutStreamer->emitIntValue(Addr, AddrSize);
    AddrSectionSize += AddrSize;
  }
}

/// Emit .debug_addr footer.
void DwarfStreamer::emitDwarfDebugAddrsFooter(const CompileUnit &Unit,
                                              MCSymbol *EndLabel) {

  // Make .debug_addr the current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfAddrSection());

  if (EndLabel != nullptr)
    Asm->OutStreamer->emitLabel(EndLabel);
}

/// Emit piece of .debug_loclists for \p LinkedLocationExpression.
void DwarfStreamer::emitDwarfDebugLocListsTableFragment(
    const CompileUnit &Unit,
    const DWARFLocationExpressionsVector &LinkedLocationExpression,
    PatchLocation Patch, DebugDieValuePool &AddrPool) {
  Patch.set(LocListsSectionSize);

  // Make .debug_loclists the current section.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfLoclistsSection());
  std::optional<uint64_t> BaseAddress;

  for (const DWARFLocationExpression &LocExpression :
       LinkedLocationExpression) {
    if (LocExpression.Range) {

      if (!BaseAddress) {

        BaseAddress = LocExpression.Range->LowPC;

        // Emit base address.
        MS->emitInt8(dwarf::DW_LLE_base_addressx);
        LocListsSectionSize += 1;
        LocListsSectionSize +=
            MS->emitULEB128IntValue(AddrPool.getValueIndex(*BaseAddress));
      }

      // Emit type of entry.
      MS->emitInt8(dwarf::DW_LLE_offset_pair);
      LocListsSectionSize += 1;

      // Emit start offset relative to base address.
      LocListsSectionSize +=
          MS->emitULEB128IntValue(LocExpression.Range->LowPC - *BaseAddress);

      // Emit end offset relative to base address.
      LocListsSectionSize +=
          MS->emitULEB128IntValue(LocExpression.Range->HighPC - *BaseAddress);
    } else {
      // Emit type of entry.
      MS->emitInt8(dwarf::DW_LLE_default_location);
      LocListsSectionSize += 1;
    }

    LocListsSectionSize += MS->emitULEB128IntValue(LocExpression.Expr.size());
    Asm->OutStreamer->emitBytes(StringRef(
        (const char *)LocExpression.Expr.data(), LocExpression.Expr.size()));
    LocListsSectionSize += LocExpression.Expr.size();
  }

  // Emit the terminator entry.
  MS->emitInt8(dwarf::DW_LLE_end_of_list);
  LocListsSectionSize += 1;
}

void DwarfStreamer::emitLineTableForUnit(
    const DWARFDebugLine::LineTable &LineTable, const CompileUnit &Unit,
    OffsetsStringPool &DebugStrPool, OffsetsStringPool &DebugLineStrPool) {
  // Switch to the section where the table will be emitted into.
  MS->switchSection(MC->getObjectFileInfo()->getDwarfLineSection());

  MCSymbol *LineStartSym = MC->createTempSymbol();
  MCSymbol *LineEndSym = MC->createTempSymbol();

  // unit_length.
  if (LineTable.Prologue.FormParams.Format == dwarf::DwarfFormat::DWARF64) {
    MS->emitInt32(dwarf::DW_LENGTH_DWARF64);
    LineSectionSize += 4;
  }
  emitLabelDifference(LineEndSym, LineStartSym,
                      LineTable.Prologue.FormParams.Format, LineSectionSize);
  Asm->OutStreamer->emitLabel(LineStartSym);

  // Emit prologue.
  emitLineTablePrologue(LineTable.Prologue, DebugStrPool, DebugLineStrPool);

  // Emit rows.
  emitLineTableRows(LineTable, LineEndSym,
                    Unit.getOrigUnit().getAddressByteSize());
}

void DwarfStreamer::emitLineTablePrologue(const DWARFDebugLine::Prologue &P,
                                          OffsetsStringPool &DebugStrPool,
                                          OffsetsStringPool &DebugLineStrPool) {
  MCSymbol *PrologueStartSym = MC->createTempSymbol();
  MCSymbol *PrologueEndSym = MC->createTempSymbol();

  // version (uhalf).
  MS->emitInt16(P.getVersion());
  LineSectionSize += 2;
  if (P.getVersion() == 5) {
    // address_size (ubyte).
    MS->emitInt8(P.getAddressSize());
    LineSectionSize += 1;

    // segment_selector_size (ubyte).
    MS->emitInt8(P.SegSelectorSize);
    LineSectionSize += 1;
  }

  // header_length.
  emitLabelDifference(PrologueEndSym, PrologueStartSym, P.FormParams.Format,
                      LineSectionSize);

  Asm->OutStreamer->emitLabel(PrologueStartSym);
  emitLineTableProloguePayload(P, DebugStrPool, DebugLineStrPool);
  Asm->OutStreamer->emitLabel(PrologueEndSym);
}

void DwarfStreamer::emitLineTablePrologueV2IncludeAndFileTable(
    const DWARFDebugLine::Prologue &P, OffsetsStringPool &DebugStrPool,
    OffsetsStringPool &DebugLineStrPool) {
  // include_directories (sequence of path names).
  for (const DWARFFormValue &Include : P.IncludeDirectories)
    emitLineTableString(P, Include, DebugStrPool, DebugLineStrPool);
  // The last entry is followed by a single null byte.
  MS->emitInt8(0);
  LineSectionSize += 1;

  // file_names (sequence of file entries).
  for (const DWARFDebugLine::FileNameEntry &File : P.FileNames) {
    // A null-terminated string containing the full or relative path name of a
    // source file.
    emitLineTableString(P, File.Name, DebugStrPool, DebugLineStrPool);
    // An unsigned LEB128 number representing the directory index of a directory
    // in the include_directories section.
    LineSectionSize += MS->emitULEB128IntValue(File.DirIdx);
    // An unsigned LEB128 number representing the (implementation-defined) time
    // of last modification for the file, or 0 if not available.
    LineSectionSize += MS->emitULEB128IntValue(File.ModTime);
    // An unsigned LEB128 number representing the length in bytes of the file,
    // or 0 if not available.
    LineSectionSize += MS->emitULEB128IntValue(File.Length);
  }
  // The last entry is followed by a single null byte.
  MS->emitInt8(0);
  LineSectionSize += 1;
}

void DwarfStreamer::emitLineTablePrologueV5IncludeAndFileTable(
    const DWARFDebugLine::Prologue &P, OffsetsStringPool &DebugStrPool,
    OffsetsStringPool &DebugLineStrPool) {
  if (P.IncludeDirectories.empty()) {
    // directory_entry_format_count(ubyte).
    MS->emitInt8(0);
    LineSectionSize += 1;
  } else {
    // directory_entry_format_count(ubyte).
    MS->emitInt8(1);
    LineSectionSize += 1;

    // directory_entry_format (sequence of ULEB128 pairs).
    LineSectionSize += MS->emitULEB128IntValue(dwarf::DW_LNCT_path);
    LineSectionSize +=
        MS->emitULEB128IntValue(P.IncludeDirectories[0].getForm());
  }

  // directories_count (ULEB128).
  LineSectionSize += MS->emitULEB128IntValue(P.IncludeDirectories.size());
  // directories (sequence of directory names).
  for (auto Include : P.IncludeDirectories)
    emitLineTableString(P, Include, DebugStrPool, DebugLineStrPool);

  bool HasChecksums = P.ContentTypes.HasMD5;
  bool HasInlineSources = P.ContentTypes.HasSource;

  if (P.FileNames.empty()) {
    // file_name_entry_format_count (ubyte).
    MS->emitInt8(0);
    LineSectionSize += 1;
  } else {
    // file_name_entry_format_count (ubyte).
    MS->emitInt8(2 + (HasChecksums ? 1 : 0) + (HasInlineSources ? 1 : 0));
    LineSectionSize += 1;

    // file_name_entry_format (sequence of ULEB128 pairs).
    auto StrForm = P.FileNames[0].Name.getForm();
    LineSectionSize += MS->emitULEB128IntValue(dwarf::DW_LNCT_path);
    LineSectionSize += MS->emitULEB128IntValue(StrForm);

    LineSectionSize += MS->emitULEB128IntValue(dwarf::DW_LNCT_directory_index);
    LineSectionSize += MS->emitULEB128IntValue(dwarf::DW_FORM_data1);

    if (HasChecksums) {
      LineSectionSize += MS->emitULEB128IntValue(dwarf::DW_LNCT_MD5);
      LineSectionSize += MS->emitULEB128IntValue(dwarf::DW_FORM_data16);
    }

    if (HasInlineSources) {
      LineSectionSize += MS->emitULEB128IntValue(dwarf::DW_LNCT_LLVM_source);
      LineSectionSize += MS->emitULEB128IntValue(StrForm);
    }
  }

  // file_names_count (ULEB128).
  LineSectionSize += MS->emitULEB128IntValue(P.FileNames.size());

  // file_names (sequence of file name entries).
  for (auto File : P.FileNames) {
    emitLineTableString(P, File.Name, DebugStrPool, DebugLineStrPool);
    MS->emitInt8(File.DirIdx);
    LineSectionSize += 1;
    if (HasChecksums) {
      MS->emitBinaryData(
          StringRef(reinterpret_cast<const char *>(File.Checksum.data()),
                    File.Checksum.size()));
      LineSectionSize += File.Checksum.size();
    }
    if (HasInlineSources)
      emitLineTableString(P, File.Source, DebugStrPool, DebugLineStrPool);
  }
}

void DwarfStreamer::emitLineTableString(const DWARFDebugLine::Prologue &P,
                                        const DWARFFormValue &String,
                                        OffsetsStringPool &DebugStrPool,
                                        OffsetsStringPool &DebugLineStrPool) {
  std::optional<const char *> StringVal = dwarf::toString(String);
  if (!StringVal) {
    warn("Cann't read string from line table.");
    return;
  }

  switch (String.getForm()) {
  case dwarf::DW_FORM_string: {
    StringRef Str = *StringVal;
    Asm->OutStreamer->emitBytes(Str.data());
    Asm->emitInt8(0);
    LineSectionSize += Str.size() + 1;
  } break;
  case dwarf::DW_FORM_strp:
  case dwarf::DW_FORM_line_strp: {
    DwarfStringPoolEntryRef StringRef =
        String.getForm() == dwarf::DW_FORM_strp
            ? DebugStrPool.getEntry(*StringVal)
            : DebugLineStrPool.getEntry(*StringVal);

    emitIntOffset(StringRef.getOffset(), P.FormParams.Format, LineSectionSize);
  } break;
  default:
    warn("Unsupported string form inside line table.");
    break;
  };
}

void DwarfStreamer::emitLineTableProloguePayload(
    const DWARFDebugLine::Prologue &P, OffsetsStringPool &DebugStrPool,
    OffsetsStringPool &DebugLineStrPool) {
  // minimum_instruction_length (ubyte).
  MS->emitInt8(P.MinInstLength);
  LineSectionSize += 1;
  if (P.FormParams.Version >= 4) {
    // maximum_operations_per_instruction (ubyte).
    MS->emitInt8(P.MaxOpsPerInst);
    LineSectionSize += 1;
  }
  // default_is_stmt (ubyte).
  MS->emitInt8(P.DefaultIsStmt);
  LineSectionSize += 1;
  // line_base (sbyte).
  MS->emitInt8(P.LineBase);
  LineSectionSize += 1;
  // line_range (ubyte).
  MS->emitInt8(P.LineRange);
  LineSectionSize += 1;
  // opcode_base (ubyte).
  MS->emitInt8(P.OpcodeBase);
  LineSectionSize += 1;

  // standard_opcode_lengths (array of ubyte).
  for (auto Length : P.StandardOpcodeLengths) {
    MS->emitInt8(Length);
    LineSectionSize += 1;
  }

  if (P.FormParams.Version < 5)
    emitLineTablePrologueV2IncludeAndFileTable(P, DebugStrPool,
                                               DebugLineStrPool);
  else
    emitLineTablePrologueV5IncludeAndFileTable(P, DebugStrPool,
                                               DebugLineStrPool);
}

void DwarfStreamer::emitLineTableRows(
    const DWARFDebugLine::LineTable &LineTable, MCSymbol *LineEndSym,
    unsigned AddressByteSize) {

  MCDwarfLineTableParams Params;
  Params.DWARF2LineOpcodeBase = LineTable.Prologue.OpcodeBase;
  Params.DWARF2LineBase = LineTable.Prologue.LineBase;
  Params.DWARF2LineRange = LineTable.Prologue.LineRange;

  SmallString<128> EncodingBuffer;

  if (LineTable.Rows.empty()) {
    // We only have the dummy entry, dsymutil emits an entry with a 0
    // address in that case.
    MCDwarfLineAddr::encode(*MC, Params, std::numeric_limits<int64_t>::max(), 0,
                            EncodingBuffer);
    MS->emitBytes(EncodingBuffer);
    LineSectionSize += EncodingBuffer.size();
    MS->emitLabel(LineEndSym);
    return;
  }

  // Line table state machine fields
  unsigned FileNum = 1;
  unsigned LastLine = 1;
  unsigned Column = 0;
  unsigned Discriminator = 0;
  unsigned IsStatement = 1;
  unsigned Isa = 0;
  uint64_t Address = -1ULL;

  unsigned RowsSinceLastSequence = 0;

  for (const DWARFDebugLine::Row &Row : LineTable.Rows) {
    int64_t AddressDelta;
    if (Address == -1ULL) {
      MS->emitIntValue(dwarf::DW_LNS_extended_op, 1);
      MS->emitULEB128IntValue(AddressByteSize + 1);
      MS->emitIntValue(dwarf::DW_LNE_set_address, 1);
      MS->emitIntValue(Row.Address.Address, AddressByteSize);
      LineSectionSize +=
          2 + AddressByteSize + getULEB128Size(AddressByteSize + 1);
      AddressDelta = 0;
    } else {
      AddressDelta =
          (Row.Address.Address - Address) / LineTable.Prologue.MinInstLength;
    }

    // FIXME: code copied and transformed from MCDwarf.cpp::EmitDwarfLineTable.
    // We should find a way to share this code, but the current compatibility
    // requirement with classic dsymutil makes it hard. Revisit that once this
    // requirement is dropped.

    if (FileNum != Row.File) {
      FileNum = Row.File;
      MS->emitIntValue(dwarf::DW_LNS_set_file, 1);
      MS->emitULEB128IntValue(FileNum);
      LineSectionSize += 1 + getULEB128Size(FileNum);
    }
    if (Column != Row.Column) {
      Column = Row.Column;
      MS->emitIntValue(dwarf::DW_LNS_set_column, 1);
      MS->emitULEB128IntValue(Column);
      LineSectionSize += 1 + getULEB128Size(Column);
    }
    if (Discriminator != Row.Discriminator &&
        MS->getContext().getDwarfVersion() >= 4) {
      Discriminator = Row.Discriminator;
      unsigned Size = getULEB128Size(Discriminator);
      MS->emitIntValue(dwarf::DW_LNS_extended_op, 1);
      MS->emitULEB128IntValue(Size + 1);
      MS->emitIntValue(dwarf::DW_LNE_set_discriminator, 1);
      MS->emitULEB128IntValue(Discriminator);
      LineSectionSize += /* extended op */ 1 + getULEB128Size(Size + 1) +
                         /* discriminator */ 1 + Size;
    }
    Discriminator = 0;

    if (Isa != Row.Isa) {
      Isa = Row.Isa;
      MS->emitIntValue(dwarf::DW_LNS_set_isa, 1);
      MS->emitULEB128IntValue(Isa);
      LineSectionSize += 1 + getULEB128Size(Isa);
    }
    if (IsStatement != Row.IsStmt) {
      IsStatement = Row.IsStmt;
      MS->emitIntValue(dwarf::DW_LNS_negate_stmt, 1);
      LineSectionSize += 1;
    }
    if (Row.BasicBlock) {
      MS->emitIntValue(dwarf::DW_LNS_set_basic_block, 1);
      LineSectionSize += 1;
    }

    if (Row.PrologueEnd) {
      MS->emitIntValue(dwarf::DW_LNS_set_prologue_end, 1);
      LineSectionSize += 1;
    }

    if (Row.EpilogueBegin) {
      MS->emitIntValue(dwarf::DW_LNS_set_epilogue_begin, 1);
      LineSectionSize += 1;
    }

    int64_t LineDelta = int64_t(Row.Line) - LastLine;
    if (!Row.EndSequence) {
      MCDwarfLineAddr::encode(*MC, Params, LineDelta, AddressDelta,
                              EncodingBuffer);
      MS->emitBytes(EncodingBuffer);
      LineSectionSize += EncodingBuffer.size();
      EncodingBuffer.resize(0);
      Address = Row.Address.Address;
      LastLine = Row.Line;
      RowsSinceLastSequence++;
    } else {
      if (LineDelta) {
        MS->emitIntValue(dwarf::DW_LNS_advance_line, 1);
        MS->emitSLEB128IntValue(LineDelta);
        LineSectionSize += 1 + getSLEB128Size(LineDelta);
      }
      if (AddressDelta) {
        MS->emitIntValue(dwarf::DW_LNS_advance_pc, 1);
        MS->emitULEB128IntValue(AddressDelta);
        LineSectionSize += 1 + getULEB128Size(AddressDelta);
      }
      MCDwarfLineAddr::encode(*MC, Params, std::numeric_limits<int64_t>::max(),
                              0, EncodingBuffer);
      MS->emitBytes(EncodingBuffer);
      LineSectionSize += EncodingBuffer.size();
      EncodingBuffer.resize(0);
      Address = -1ULL;
      LastLine = FileNum = IsStatement = 1;
      RowsSinceLastSequence = Column = Discriminator = Isa = 0;
    }
  }

  if (RowsSinceLastSequence) {
    MCDwarfLineAddr::encode(*MC, Params, std::numeric_limits<int64_t>::max(), 0,
                            EncodingBuffer);
    MS->emitBytes(EncodingBuffer);
    LineSectionSize += EncodingBuffer.size();
    EncodingBuffer.resize(0);
  }

  MS->emitLabel(LineEndSym);
}

void DwarfStreamer::emitIntOffset(uint64_t Offset, dwarf::DwarfFormat Format,
                                  uint64_t &SectionSize) {
  uint8_t Size = dwarf::getDwarfOffsetByteSize(Format);
  MS->emitIntValue(Offset, Size);
  SectionSize += Size;
}

void DwarfStreamer::emitLabelDifference(const MCSymbol *Hi, const MCSymbol *Lo,
                                        dwarf::DwarfFormat Format,
                                        uint64_t &SectionSize) {
  uint8_t Size = dwarf::getDwarfOffsetByteSize(Format);
  Asm->emitLabelDifference(Hi, Lo, Size);
  SectionSize += Size;
}

/// Emit the pubnames or pubtypes section contribution for \p
/// Unit into \p Sec. The data is provided in \p Names.
void DwarfStreamer::emitPubSectionForUnit(
    MCSection *Sec, StringRef SecName, const CompileUnit &Unit,
    const std::vector<CompileUnit::AccelInfo> &Names) {
  if (Names.empty())
    return;

  // Start the dwarf pubnames section.
  Asm->OutStreamer->switchSection(Sec);
  MCSymbol *BeginLabel = Asm->createTempSymbol("pub" + SecName + "_begin");
  MCSymbol *EndLabel = Asm->createTempSymbol("pub" + SecName + "_end");

  bool HeaderEmitted = false;
  // Emit the pubnames for this compilation unit.
  for (const auto &Name : Names) {
    if (Name.SkipPubSection)
      continue;

    if (!HeaderEmitted) {
      // Emit the header.
      Asm->emitLabelDifference(EndLabel, BeginLabel, 4); // Length
      Asm->OutStreamer->emitLabel(BeginLabel);
      Asm->emitInt16(dwarf::DW_PUBNAMES_VERSION); // Version
      Asm->emitInt32(Unit.getStartOffset());      // Unit offset
      Asm->emitInt32(Unit.getNextUnitOffset() - Unit.getStartOffset()); // Size
      HeaderEmitted = true;
    }
    Asm->emitInt32(Name.Die->getOffset());

    // Emit the string itself.
    Asm->OutStreamer->emitBytes(Name.Name.getString());
    // Emit a null terminator.
    Asm->emitInt8(0);
  }

  if (!HeaderEmitted)
    return;
  Asm->emitInt32(0); // End marker.
  Asm->OutStreamer->emitLabel(EndLabel);
}

/// Emit .debug_pubnames for \p Unit.
void DwarfStreamer::emitPubNamesForUnit(const CompileUnit &Unit) {
  emitPubSectionForUnit(MC->getObjectFileInfo()->getDwarfPubNamesSection(),
                        "names", Unit, Unit.getPubnames());
}

/// Emit .debug_pubtypes for \p Unit.
void DwarfStreamer::emitPubTypesForUnit(const CompileUnit &Unit) {
  emitPubSectionForUnit(MC->getObjectFileInfo()->getDwarfPubTypesSection(),
                        "types", Unit, Unit.getPubtypes());
}

/// Emit a CIE into the debug_frame section.
void DwarfStreamer::emitCIE(StringRef CIEBytes) {
  MS->switchSection(MC->getObjectFileInfo()->getDwarfFrameSection());

  MS->emitBytes(CIEBytes);
  FrameSectionSize += CIEBytes.size();
}

/// Emit a FDE into the debug_frame section. \p FDEBytes
/// contains the FDE data without the length, CIE offset and address
/// which will be replaced with the parameter values.
void DwarfStreamer::emitFDE(uint32_t CIEOffset, uint32_t AddrSize,
                            uint64_t Address, StringRef FDEBytes) {
  MS->switchSection(MC->getObjectFileInfo()->getDwarfFrameSection());

  MS->emitIntValue(FDEBytes.size() + 4 + AddrSize, 4);
  MS->emitIntValue(CIEOffset, 4);
  MS->emitIntValue(Address, AddrSize);
  MS->emitBytes(FDEBytes);
  FrameSectionSize += FDEBytes.size() + 8 + AddrSize;
}

void DwarfStreamer::emitMacroTables(DWARFContext *Context,
                                    const Offset2UnitMap &UnitMacroMap,
                                    OffsetsStringPool &StringPool) {
  assert(Context != nullptr && "Empty DWARF context");

  // Check for .debug_macinfo table.
  if (const DWARFDebugMacro *Table = Context->getDebugMacinfo()) {
    MS->switchSection(MC->getObjectFileInfo()->getDwarfMacinfoSection());
    emitMacroTableImpl(Table, UnitMacroMap, StringPool, MacInfoSectionSize);
  }

  // Check for .debug_macro table.
  if (const DWARFDebugMacro *Table = Context->getDebugMacro()) {
    MS->switchSection(MC->getObjectFileInfo()->getDwarfMacroSection());
    emitMacroTableImpl(Table, UnitMacroMap, StringPool, MacroSectionSize);
  }
}

void DwarfStreamer::emitMacroTableImpl(const DWARFDebugMacro *MacroTable,
                                       const Offset2UnitMap &UnitMacroMap,
                                       OffsetsStringPool &StringPool,
                                       uint64_t &OutOffset) {
  bool DefAttributeIsReported = false;
  bool UndefAttributeIsReported = false;
  bool ImportAttributeIsReported = false;
  for (const DWARFDebugMacro::MacroList &List : MacroTable->MacroLists) {
    Offset2UnitMap::const_iterator UnitIt = UnitMacroMap.find(List.Offset);
    if (UnitIt == UnitMacroMap.end()) {
      warn(formatv(
          "couldn`t find compile unit for the macro table with offset = {0:x}",
          List.Offset));
      continue;
    }

    // Skip macro table if the unit was not cloned.
    DIE *OutputUnitDIE = UnitIt->second->getOutputUnitDIE();
    if (OutputUnitDIE == nullptr)
      continue;

    // Update macro attribute of cloned compile unit with the proper offset to
    // the macro table.
    bool hasDWARFv5Header = false;
    for (auto &V : OutputUnitDIE->values()) {
      if (V.getAttribute() == dwarf::DW_AT_macro_info) {
        V = DIEValue(V.getAttribute(), V.getForm(), DIEInteger(OutOffset));
        break;
      } else if (V.getAttribute() == dwarf::DW_AT_macros) {
        hasDWARFv5Header = true;
        V = DIEValue(V.getAttribute(), V.getForm(), DIEInteger(OutOffset));
        break;
      }
    }

    // Write DWARFv5 header.
    if (hasDWARFv5Header) {
      // Write header version.
      MS->emitIntValue(List.Header.Version, sizeof(List.Header.Version));
      OutOffset += sizeof(List.Header.Version);

      uint8_t Flags = List.Header.Flags;

      // Check for OPCODE_OPERANDS_TABLE.
      if (Flags &
          DWARFDebugMacro::HeaderFlagMask::MACRO_OPCODE_OPERANDS_TABLE) {
        Flags &= ~DWARFDebugMacro::HeaderFlagMask::MACRO_OPCODE_OPERANDS_TABLE;
        warn("opcode_operands_table is not supported yet.");
      }

      // Check for DEBUG_LINE_OFFSET.
      std::optional<uint64_t> StmtListOffset;
      if (Flags & DWARFDebugMacro::HeaderFlagMask::MACRO_DEBUG_LINE_OFFSET) {
        // Get offset to the line table from the cloned compile unit.
        for (auto &V : OutputUnitDIE->values()) {
          if (V.getAttribute() == dwarf::DW_AT_stmt_list) {
            StmtListOffset = V.getDIEInteger().getValue();
            break;
          }
        }

        if (!StmtListOffset) {
          Flags &= ~DWARFDebugMacro::HeaderFlagMask::MACRO_DEBUG_LINE_OFFSET;
          warn("couldn`t find line table for macro table.");
        }
      }

      // Write flags.
      MS->emitIntValue(Flags, sizeof(Flags));
      OutOffset += sizeof(Flags);

      // Write offset to line table.
      if (StmtListOffset) {
        MS->emitIntValue(*StmtListOffset, List.Header.getOffsetByteSize());
        OutOffset += List.Header.getOffsetByteSize();
      }
    }

    // Write macro entries.
    for (const DWARFDebugMacro::Entry &MacroEntry : List.Macros) {
      if (MacroEntry.Type == 0) {
        OutOffset += MS->emitULEB128IntValue(MacroEntry.Type);
        continue;
      }

      uint8_t MacroType = MacroEntry.Type;
      switch (MacroType) {
      default: {
        bool HasVendorSpecificExtension =
            (!hasDWARFv5Header && MacroType == dwarf::DW_MACINFO_vendor_ext) ||
            (hasDWARFv5Header && (MacroType >= dwarf::DW_MACRO_lo_user &&
                                  MacroType <= dwarf::DW_MACRO_hi_user));

        if (HasVendorSpecificExtension) {
          // Write macinfo type.
          MS->emitIntValue(MacroType, 1);
          OutOffset++;

          // Write vendor extension constant.
          OutOffset += MS->emitULEB128IntValue(MacroEntry.ExtConstant);

          // Write vendor extension string.
          StringRef String = MacroEntry.ExtStr;
          MS->emitBytes(String);
          MS->emitIntValue(0, 1);
          OutOffset += String.size() + 1;
        } else
          warn("unknown macro type. skip.");
      } break;
      // debug_macro and debug_macinfo share some common encodings.
      // DW_MACRO_define     == DW_MACINFO_define
      // DW_MACRO_undef      == DW_MACINFO_undef
      // DW_MACRO_start_file == DW_MACINFO_start_file
      // DW_MACRO_end_file   == DW_MACINFO_end_file
      // For readibility/uniformity we are using DW_MACRO_*.
      case dwarf::DW_MACRO_define:
      case dwarf::DW_MACRO_undef: {
        // Write macinfo type.
        MS->emitIntValue(MacroType, 1);
        OutOffset++;

        // Write source line.
        OutOffset += MS->emitULEB128IntValue(MacroEntry.Line);

        // Write macro string.
        StringRef String = MacroEntry.MacroStr;
        MS->emitBytes(String);
        MS->emitIntValue(0, 1);
        OutOffset += String.size() + 1;
      } break;
      case dwarf::DW_MACRO_define_strp:
      case dwarf::DW_MACRO_undef_strp:
      case dwarf::DW_MACRO_define_strx:
      case dwarf::DW_MACRO_undef_strx: {
        assert(UnitIt->second->getOrigUnit().getVersion() >= 5);

        // DW_MACRO_*_strx forms are not supported currently.
        // Convert to *_strp.
        switch (MacroType) {
        case dwarf::DW_MACRO_define_strx: {
          MacroType = dwarf::DW_MACRO_define_strp;
          if (!DefAttributeIsReported) {
            warn("DW_MACRO_define_strx unsupported yet. Convert to "
                 "DW_MACRO_define_strp.");
            DefAttributeIsReported = true;
          }
        } break;
        case dwarf::DW_MACRO_undef_strx: {
          MacroType = dwarf::DW_MACRO_undef_strp;
          if (!UndefAttributeIsReported) {
            warn("DW_MACRO_undef_strx unsupported yet. Convert to "
                 "DW_MACRO_undef_strp.");
            UndefAttributeIsReported = true;
          }
        } break;
        default:
          // Nothing to do.
          break;
        }

        // Write macinfo type.
        MS->emitIntValue(MacroType, 1);
        OutOffset++;

        // Write source line.
        OutOffset += MS->emitULEB128IntValue(MacroEntry.Line);

        // Write macro string.
        DwarfStringPoolEntryRef EntryRef =
            StringPool.getEntry(MacroEntry.MacroStr);
        MS->emitIntValue(EntryRef.getOffset(), List.Header.getOffsetByteSize());
        OutOffset += List.Header.getOffsetByteSize();
        break;
      }
      case dwarf::DW_MACRO_start_file: {
        // Write macinfo type.
        MS->emitIntValue(MacroType, 1);
        OutOffset++;
        // Write source line.
        OutOffset += MS->emitULEB128IntValue(MacroEntry.Line);
        // Write source file id.
        OutOffset += MS->emitULEB128IntValue(MacroEntry.File);
      } break;
      case dwarf::DW_MACRO_end_file: {
        // Write macinfo type.
        MS->emitIntValue(MacroType, 1);
        OutOffset++;
      } break;
      case dwarf::DW_MACRO_import:
      case dwarf::DW_MACRO_import_sup: {
        if (!ImportAttributeIsReported) {
          warn("DW_MACRO_import and DW_MACRO_import_sup are unsupported yet. "
               "remove.");
          ImportAttributeIsReported = true;
        }
      } break;
      }
    }
  }
}
