//===- DWARFDebugRnglists.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugRnglists.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

Error RangeListEntry::extract(DWARFDataExtractor Data, uint64_t *OffsetPtr) {
  Offset = *OffsetPtr;
  SectionIndex = -1ULL;
  // The caller should guarantee that we have at least 1 byte available, so
  // we just assert instead of revalidate.
  assert(*OffsetPtr < Data.size() &&
         "not enough space to extract a rangelist encoding");
  uint8_t Encoding = Data.getU8(OffsetPtr);

  DataExtractor::Cursor C(*OffsetPtr);
  switch (Encoding) {
  case dwarf::DW_RLE_end_of_list:
    Value0 = Value1 = 0;
    break;
  // TODO: Support other encodings.
  case dwarf::DW_RLE_base_addressx: {
    Value0 = Data.getULEB128(C);
    break;
  }
  case dwarf::DW_RLE_startx_endx:
    Value0 = Data.getULEB128(C);
    Value1 = Data.getULEB128(C);
    break;
  case dwarf::DW_RLE_startx_length: {
    Value0 = Data.getULEB128(C);
    Value1 = Data.getULEB128(C);
    break;
  }
  case dwarf::DW_RLE_offset_pair: {
    Value0 = Data.getULEB128(C);
    Value1 = Data.getULEB128(C);
    break;
  }
  case dwarf::DW_RLE_base_address: {
    Value0 = Data.getRelocatedAddress(C, &SectionIndex);
    break;
  }
  case dwarf::DW_RLE_start_end: {
    Value0 = Data.getRelocatedAddress(C, &SectionIndex);
    Value1 = Data.getRelocatedAddress(C);
    break;
  }
  case dwarf::DW_RLE_start_length: {
    Value0 = Data.getRelocatedAddress(C, &SectionIndex);
    Value1 = Data.getULEB128(C);
    break;
  }
  default:
    consumeError(C.takeError());
    return createStringError(errc::not_supported,
                             "unknown rnglists encoding 0x%" PRIx32
                             " at offset 0x%" PRIx64,
                             uint32_t(Encoding), Offset);
  }

  if (!C) {
    consumeError(C.takeError());
    return createStringError(
        errc::invalid_argument,
        "read past end of table when reading %s encoding at offset 0x%" PRIx64,
        dwarf::RLEString(Encoding).data(), Offset);
  }

  *OffsetPtr = C.tell();
  EntryKind = Encoding;
  return Error::success();
}

DWARFAddressRangesVector DWARFDebugRnglist::getAbsoluteRanges(
    std::optional<object::SectionedAddress> BaseAddr, DWARFUnit &U) const {
  return getAbsoluteRanges(
      BaseAddr, U.getAddressByteSize(),
      [&](uint32_t Index) { return U.getAddrOffsetSectionItem(Index); });
}

DWARFAddressRangesVector DWARFDebugRnglist::getAbsoluteRanges(
    std::optional<object::SectionedAddress> BaseAddr, uint8_t AddressByteSize,
    function_ref<std::optional<object::SectionedAddress>(uint32_t)>
        LookupPooledAddress) const {
  DWARFAddressRangesVector Res;
  uint64_t Tombstone = dwarf::computeTombstoneAddress(AddressByteSize);
  for (const RangeListEntry &RLE : Entries) {
    if (RLE.EntryKind == dwarf::DW_RLE_end_of_list)
      break;
    if (RLE.EntryKind == dwarf::DW_RLE_base_addressx) {
      BaseAddr = LookupPooledAddress(RLE.Value0);
      if (!BaseAddr)
        BaseAddr = {RLE.Value0, -1ULL};
      continue;
    }
    if (RLE.EntryKind == dwarf::DW_RLE_base_address) {
      BaseAddr = {RLE.Value0, RLE.SectionIndex};
      continue;
    }

    DWARFAddressRange E;
    E.SectionIndex = RLE.SectionIndex;
    if (BaseAddr && E.SectionIndex == -1ULL)
      E.SectionIndex = BaseAddr->SectionIndex;

    switch (RLE.EntryKind) {
    case dwarf::DW_RLE_offset_pair:
      E.LowPC = RLE.Value0;
      if (E.LowPC == Tombstone)
        continue;
      E.HighPC = RLE.Value1;
      if (BaseAddr) {
        if (BaseAddr->Address == Tombstone)
          continue;
        E.LowPC += BaseAddr->Address;
        E.HighPC += BaseAddr->Address;
      }
      break;
    case dwarf::DW_RLE_start_end:
      E.LowPC = RLE.Value0;
      E.HighPC = RLE.Value1;
      break;
    case dwarf::DW_RLE_start_length:
      E.LowPC = RLE.Value0;
      E.HighPC = E.LowPC + RLE.Value1;
      break;
    case dwarf::DW_RLE_startx_length: {
      auto Start = LookupPooledAddress(RLE.Value0);
      if (!Start)
        Start = {0, -1ULL};
      E.SectionIndex = Start->SectionIndex;
      E.LowPC = Start->Address;
      E.HighPC = E.LowPC + RLE.Value1;
      break;
    }
    case dwarf::DW_RLE_startx_endx: {
      auto Start = LookupPooledAddress(RLE.Value0);
      if (!Start)
        Start = {0, -1ULL};
      auto End = LookupPooledAddress(RLE.Value1);
      if (!End)
        End = {0, -1ULL};
      // FIXME: Some error handling if Start.SectionIndex != End.SectionIndex
      E.SectionIndex = Start->SectionIndex;
      E.LowPC = Start->Address;
      E.HighPC = End->Address;
      break;
    }
    default:
      // Unsupported encodings should have been reported during extraction,
      // so we should not run into any here.
      llvm_unreachable("Unsupported range list encoding");
    }
    if (E.LowPC == Tombstone)
      continue;
    Res.push_back(E);
  }
  return Res;
}

void RangeListEntry::dump(
    raw_ostream &OS, uint8_t AddrSize, uint8_t MaxEncodingStringLength,
    uint64_t &CurrentBase, DIDumpOptions DumpOpts,
    llvm::function_ref<std::optional<object::SectionedAddress>(uint32_t)>
        LookupPooledAddress) const {
  auto PrintRawEntry = [](raw_ostream &OS, const RangeListEntry &Entry,
                          uint8_t AddrSize, DIDumpOptions DumpOpts) {
    if (DumpOpts.Verbose) {
      DumpOpts.DisplayRawContents = true;
      DWARFAddressRange(Entry.Value0, Entry.Value1)
          .dump(OS, AddrSize, DumpOpts);
      OS << " => ";
    }
  };

  if (DumpOpts.Verbose) {
    // Print the section offset in verbose mode.
    OS << format("0x%8.8" PRIx64 ":", Offset);
    auto EncodingString = dwarf::RangeListEncodingString(EntryKind);
    // Unsupported encodings should have been reported during parsing.
    assert(!EncodingString.empty() && "Unknown range entry encoding");
    OS << format(" [%s%*c", EncodingString.data(),
                 MaxEncodingStringLength - EncodingString.size() + 1, ']');
    if (EntryKind != dwarf::DW_RLE_end_of_list)
      OS << ": ";
  }

  uint64_t Tombstone = dwarf::computeTombstoneAddress(AddrSize);

  switch (EntryKind) {
  case dwarf::DW_RLE_end_of_list:
    OS << (DumpOpts.Verbose ? "" : "<End of list>");
    break;
  case dwarf::DW_RLE_base_addressx: {
    if (auto SA = LookupPooledAddress(Value0))
      CurrentBase = SA->Address;
    else
      CurrentBase = Value0;
    if (!DumpOpts.Verbose)
      return;
    DWARFFormValue::dumpAddress(OS << ' ', AddrSize, Value0);
    break;
  }
  case dwarf::DW_RLE_base_address:
    // In non-verbose mode we do not print anything for this entry.
    CurrentBase = Value0;
    if (!DumpOpts.Verbose)
      return;
    DWARFFormValue::dumpAddress(OS << ' ', AddrSize, Value0);
    break;
  case dwarf::DW_RLE_start_length:
    PrintRawEntry(OS, *this, AddrSize, DumpOpts);
    DWARFAddressRange(Value0, Value0 + Value1).dump(OS, AddrSize, DumpOpts);
    break;
  case dwarf::DW_RLE_offset_pair:
    PrintRawEntry(OS, *this, AddrSize, DumpOpts);
    if (CurrentBase != Tombstone)
      DWARFAddressRange(Value0 + CurrentBase, Value1 + CurrentBase)
          .dump(OS, AddrSize, DumpOpts);
    else
      OS << "dead code";
    break;
  case dwarf::DW_RLE_start_end:
    DWARFAddressRange(Value0, Value1).dump(OS, AddrSize, DumpOpts);
    break;
  case dwarf::DW_RLE_startx_length: {
    PrintRawEntry(OS, *this, AddrSize, DumpOpts);
    uint64_t Start = 0;
    if (auto SA = LookupPooledAddress(Value0))
      Start = SA->Address;
    DWARFAddressRange(Start, Start + Value1).dump(OS, AddrSize, DumpOpts);
    break;
  }
  case dwarf::DW_RLE_startx_endx: {
    PrintRawEntry(OS, *this, AddrSize, DumpOpts);
    uint64_t Start = 0;
    if (auto SA = LookupPooledAddress(Value0))
      Start = SA->Address;
    uint64_t End = 0;
    if (auto SA = LookupPooledAddress(Value1))
      End = SA->Address;
    DWARFAddressRange(Start, End).dump(OS, AddrSize, DumpOpts);
    break;
  }
  default:
    llvm_unreachable("Unsupported range list encoding");
  }
  OS << "\n";
}
