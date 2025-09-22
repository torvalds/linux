#ifndef LLVM_DWP_DWP_H
#define LLVM_DWP_DWP_H

#include "DWPStringPool.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include <deque>
#include <vector>

namespace llvm {
enum OnCuIndexOverflow {
  HardStop,
  SoftStop,
  Continue,
};

struct UnitIndexEntry {
  DWARFUnitIndex::Entry::SectionContribution Contributions[8];
  std::string Name;
  std::string DWOName;
  StringRef DWPName;
};

// Holds data for Skeleton, Split Compilation, and Type Unit Headers (only in
// v5) as defined in Dwarf 5 specification, 7.5.1.2, 7.5.1.3 and Dwarf 4
// specification 7.5.1.1.
struct InfoSectionUnitHeader {
  // unit_length field. Note that the type is uint64_t even in 32-bit dwarf.
  uint64_t Length = 0;

  // version field.
  uint16_t Version = 0;

  // unit_type field. Initialized only if Version >= 5.
  uint8_t UnitType = 0;

  // address_size field.
  uint8_t AddrSize = 0;

  // debug_abbrev_offset field. Note that the type is uint64_t even in 32-bit
  // dwarf. It is assumed to be 0.
  uint64_t DebugAbbrevOffset = 0;

  // dwo_id field. This resides in the header only if Version >= 5.
  // In earlier versions, it is read from DW_AT_GNU_dwo_id.
  std::optional<uint64_t> Signature;

  // Derived from the length of Length field.
  dwarf::DwarfFormat Format = dwarf::DwarfFormat::DWARF32;

  // The size of the Header in bytes. This is derived while parsing the header,
  // and is stored as a convenience.
  uint8_t HeaderSize = 0;
};

struct CompileUnitIdentifiers {
  uint64_t Signature = 0;
  const char *Name = "";
  const char *DWOName = "";
};

Error write(MCStreamer &Out, ArrayRef<std::string> Inputs,
            OnCuIndexOverflow OverflowOptValue);

unsigned getContributionIndex(DWARFSectionKind Kind, uint32_t IndexVersion);

Error handleSection(
    const StringMap<std::pair<MCSection *, DWARFSectionKind>> &KnownSections,
    const MCSection *StrSection, const MCSection *StrOffsetSection,
    const MCSection *TypesSection, const MCSection *CUIndexSection,
    const MCSection *TUIndexSection, const MCSection *InfoSection,
    const object::SectionRef &Section, MCStreamer &Out,
    std::deque<SmallString<32>> &UncompressedSections,
    uint32_t (&ContributionOffsets)[8], UnitIndexEntry &CurEntry,
    StringRef &CurStrSection, StringRef &CurStrOffsetSection,
    std::vector<StringRef> &CurTypesSection,
    std::vector<StringRef> &CurInfoSection, StringRef &AbbrevSection,
    StringRef &CurCUIndexSection, StringRef &CurTUIndexSection,
    std::vector<std::pair<DWARFSectionKind, uint32_t>> &SectionLength);

Expected<InfoSectionUnitHeader> parseInfoSectionUnitHeader(StringRef Info);

void writeStringsAndOffsets(MCStreamer &Out, DWPStringPool &Strings,
                            MCSection *StrOffsetSection,
                            StringRef CurStrSection,
                            StringRef CurStrOffsetSection, uint16_t Version);

Error buildDuplicateError(const std::pair<uint64_t, UnitIndexEntry> &PrevE,
                          const CompileUnitIdentifiers &ID, StringRef DWPName);

void writeIndex(MCStreamer &Out, MCSection *Section,
                ArrayRef<unsigned> ContributionOffsets,
                const MapVector<uint64_t, UnitIndexEntry> &IndexEntries,
                uint32_t IndexVersion);

} // namespace llvm
#endif // LLVM_DWP_DWP_H
