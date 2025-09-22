//===- DWARFDebugLoc.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFLocationExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cinttypes>
#include <cstdint>

using namespace llvm;
using object::SectionedAddress;

namespace llvm {
class DWARFObject;
}

namespace {
class DWARFLocationInterpreter {
  std::optional<object::SectionedAddress> Base;
  std::function<std::optional<object::SectionedAddress>(uint32_t)> LookupAddr;

public:
  DWARFLocationInterpreter(
      std::optional<object::SectionedAddress> Base,
      std::function<std::optional<object::SectionedAddress>(uint32_t)>
          LookupAddr)
      : Base(Base), LookupAddr(std::move(LookupAddr)) {}

  Expected<std::optional<DWARFLocationExpression>>
  Interpret(const DWARFLocationEntry &E);
};
} // namespace

static Error createResolverError(uint32_t Index, unsigned Kind) {
  return make_error<ResolverError>(Index, (dwarf::LoclistEntries)Kind);
}

Expected<std::optional<DWARFLocationExpression>>
DWARFLocationInterpreter::Interpret(const DWARFLocationEntry &E) {
  switch (E.Kind) {
  case dwarf::DW_LLE_end_of_list:
    return std::nullopt;
  case dwarf::DW_LLE_base_addressx: {
    Base = LookupAddr(E.Value0);
    if (!Base)
      return createResolverError(E.Value0, E.Kind);
    return std::nullopt;
  }
  case dwarf::DW_LLE_startx_endx: {
    std::optional<SectionedAddress> LowPC = LookupAddr(E.Value0);
    if (!LowPC)
      return createResolverError(E.Value0, E.Kind);
    std::optional<SectionedAddress> HighPC = LookupAddr(E.Value1);
    if (!HighPC)
      return createResolverError(E.Value1, E.Kind);
    return DWARFLocationExpression{
        DWARFAddressRange{LowPC->Address, HighPC->Address, LowPC->SectionIndex},
        E.Loc};
  }
  case dwarf::DW_LLE_startx_length: {
    std::optional<SectionedAddress> LowPC = LookupAddr(E.Value0);
    if (!LowPC)
      return createResolverError(E.Value0, E.Kind);
    return DWARFLocationExpression{DWARFAddressRange{LowPC->Address,
                                                     LowPC->Address + E.Value1,
                                                     LowPC->SectionIndex},
                                   E.Loc};
  }
  case dwarf::DW_LLE_offset_pair: {
    if (!Base) {
      return createStringError(inconvertibleErrorCode(),
                               "Unable to resolve location list offset pair: "
                               "Base address not defined");
    }
    DWARFAddressRange Range{Base->Address + E.Value0, Base->Address + E.Value1,
                            Base->SectionIndex};
    if (Range.SectionIndex == SectionedAddress::UndefSection)
      Range.SectionIndex = E.SectionIndex;
    return DWARFLocationExpression{Range, E.Loc};
  }
  case dwarf::DW_LLE_default_location:
    return DWARFLocationExpression{std::nullopt, E.Loc};
  case dwarf::DW_LLE_base_address:
    Base = SectionedAddress{E.Value0, E.SectionIndex};
    return std::nullopt;
  case dwarf::DW_LLE_start_end:
    return DWARFLocationExpression{
        DWARFAddressRange{E.Value0, E.Value1, E.SectionIndex}, E.Loc};
  case dwarf::DW_LLE_start_length:
    return DWARFLocationExpression{
        DWARFAddressRange{E.Value0, E.Value0 + E.Value1, E.SectionIndex},
        E.Loc};
  default:
    llvm_unreachable("unreachable locations list kind");
  }
}

static void dumpExpression(raw_ostream &OS, DIDumpOptions DumpOpts,
                           ArrayRef<uint8_t> Data, bool IsLittleEndian,
                           unsigned AddressSize, DWARFUnit *U) {
  DWARFDataExtractor Extractor(Data, IsLittleEndian, AddressSize);
  // Note. We do not pass any format to DWARFExpression, even if the
  // corresponding unit is known. For now, there is only one operation,
  // DW_OP_call_ref, which depends on the format; it is rarely used, and
  // is unexpected in location tables.
  DWARFExpression(Extractor, AddressSize).print(OS, DumpOpts, U);
}

bool DWARFLocationTable::dumpLocationList(
    uint64_t *Offset, raw_ostream &OS, std::optional<SectionedAddress> BaseAddr,
    const DWARFObject &Obj, DWARFUnit *U, DIDumpOptions DumpOpts,
    unsigned Indent) const {
  DWARFLocationInterpreter Interp(
      BaseAddr, [U](uint32_t Index) -> std::optional<SectionedAddress> {
        if (U)
          return U->getAddrOffsetSectionItem(Index);
        return std::nullopt;
      });
  OS << format("0x%8.8" PRIx64 ": ", *Offset);
  Error E = visitLocationList(Offset, [&](const DWARFLocationEntry &E) {
    Expected<std::optional<DWARFLocationExpression>> Loc = Interp.Interpret(E);
    if (!Loc || DumpOpts.DisplayRawContents)
      dumpRawEntry(E, OS, Indent, DumpOpts, Obj);
    if (Loc && *Loc) {
      OS << "\n";
      OS.indent(Indent);
      if (DumpOpts.DisplayRawContents)
        OS << "          => ";

      DIDumpOptions RangeDumpOpts(DumpOpts);
      RangeDumpOpts.DisplayRawContents = false;
      if (Loc.get()->Range)
        Loc.get()->Range->dump(OS, Data.getAddressSize(), RangeDumpOpts, &Obj);
      else
        OS << "<default>";
    }
    if (!Loc)
      consumeError(Loc.takeError());

    if (E.Kind != dwarf::DW_LLE_base_address &&
        E.Kind != dwarf::DW_LLE_base_addressx &&
        E.Kind != dwarf::DW_LLE_end_of_list) {
      OS << ": ";
      dumpExpression(OS, DumpOpts, E.Loc, Data.isLittleEndian(),
                     Data.getAddressSize(), U);
    }
    return true;
  });
  if (E) {
    DumpOpts.RecoverableErrorHandler(std::move(E));
    return false;
  }
  return true;
}

Error DWARFLocationTable::visitAbsoluteLocationList(
    uint64_t Offset, std::optional<SectionedAddress> BaseAddr,
    std::function<std::optional<SectionedAddress>(uint32_t)> LookupAddr,
    function_ref<bool(Expected<DWARFLocationExpression>)> Callback) const {
  DWARFLocationInterpreter Interp(BaseAddr, std::move(LookupAddr));
  return visitLocationList(&Offset, [&](const DWARFLocationEntry &E) {
    Expected<std::optional<DWARFLocationExpression>> Loc = Interp.Interpret(E);
    if (!Loc)
      return Callback(Loc.takeError());
    if (*Loc)
      return Callback(**Loc);
    return true;
  });
}

void DWARFDebugLoc::dump(raw_ostream &OS, const DWARFObject &Obj,
                         DIDumpOptions DumpOpts,
                         std::optional<uint64_t> DumpOffset) const {
  auto BaseAddr = std::nullopt;
  unsigned Indent = 12;
  if (DumpOffset) {
    dumpLocationList(&*DumpOffset, OS, BaseAddr, Obj, nullptr, DumpOpts,
                     Indent);
  } else {
    uint64_t Offset = 0;
    StringRef Separator;
    bool CanContinue = true;
    while (CanContinue && Data.isValidOffset(Offset)) {
      OS << Separator;
      Separator = "\n";

      CanContinue = dumpLocationList(&Offset, OS, BaseAddr, Obj, nullptr,
                                     DumpOpts, Indent);
      OS << '\n';
    }
  }
}

Error DWARFDebugLoc::visitLocationList(
    uint64_t *Offset,
    function_ref<bool(const DWARFLocationEntry &)> Callback) const {
  DataExtractor::Cursor C(*Offset);
  while (true) {
    uint64_t SectionIndex;
    uint64_t Value0 = Data.getRelocatedAddress(C);
    uint64_t Value1 = Data.getRelocatedAddress(C, &SectionIndex);

    DWARFLocationEntry E;

    // The end of any given location list is marked by an end of list entry,
    // which consists of a 0 for the beginning address offset and a 0 for the
    // ending address offset. A beginning offset of 0xff...f marks the base
    // address selection entry.
    if (Value0 == 0 && Value1 == 0) {
      E.Kind = dwarf::DW_LLE_end_of_list;
    } else if (Value0 == (Data.getAddressSize() == 4 ? -1U : -1ULL)) {
      E.Kind = dwarf::DW_LLE_base_address;
      E.Value0 = Value1;
      E.SectionIndex = SectionIndex;
    } else {
      E.Kind = dwarf::DW_LLE_offset_pair;
      E.Value0 = Value0;
      E.Value1 = Value1;
      E.SectionIndex = SectionIndex;
      unsigned Bytes = Data.getU16(C);
      // A single location description describing the location of the object...
      Data.getU8(C, E.Loc, Bytes);
    }

    if (!C)
      return C.takeError();
    if (!Callback(E) || E.Kind == dwarf::DW_LLE_end_of_list)
      break;
  }
  *Offset = C.tell();
  return Error::success();
}

void DWARFDebugLoc::dumpRawEntry(const DWARFLocationEntry &Entry,
                                 raw_ostream &OS, unsigned Indent,
                                 DIDumpOptions DumpOpts,
                                 const DWARFObject &Obj) const {
  uint64_t Value0, Value1;
  switch (Entry.Kind) {
  case dwarf::DW_LLE_base_address:
    Value0 = Data.getAddressSize() == 4 ? -1U : -1ULL;
    Value1 = Entry.Value0;
    break;
  case dwarf::DW_LLE_offset_pair:
    Value0 = Entry.Value0;
    Value1 = Entry.Value1;
    break;
  case dwarf::DW_LLE_end_of_list:
    return;
  default:
    llvm_unreachable("Not possible in DWARF4!");
  }
  OS << '\n';
  OS.indent(Indent);
  OS << '(' << format_hex(Value0, 2 + Data.getAddressSize() * 2) << ", "
     << format_hex(Value1, 2 + Data.getAddressSize() * 2) << ')';
  DWARFFormValue::dumpAddressSection(Obj, OS, DumpOpts, Entry.SectionIndex);
}

Error DWARFDebugLoclists::visitLocationList(
    uint64_t *Offset, function_ref<bool(const DWARFLocationEntry &)> F) const {

  DataExtractor::Cursor C(*Offset);
  bool Continue = true;
  while (Continue) {
    DWARFLocationEntry E;
    E.Kind = Data.getU8(C);
    switch (E.Kind) {
    case dwarf::DW_LLE_end_of_list:
      break;
    case dwarf::DW_LLE_base_addressx:
      E.Value0 = Data.getULEB128(C);
      break;
    case dwarf::DW_LLE_startx_endx:
      E.Value0 = Data.getULEB128(C);
      E.Value1 = Data.getULEB128(C);
      break;
    case dwarf::DW_LLE_startx_length:
      E.Value0 = Data.getULEB128(C);
      // Pre-DWARF 5 has different interpretation of the length field. We have
      // to support both pre- and standartized styles for the compatibility.
      if (Version < 5)
        E.Value1 = Data.getU32(C);
      else
        E.Value1 = Data.getULEB128(C);
      break;
    case dwarf::DW_LLE_offset_pair:
      E.Value0 = Data.getULEB128(C);
      E.Value1 = Data.getULEB128(C);
      E.SectionIndex = SectionedAddress::UndefSection;
      break;
    case dwarf::DW_LLE_default_location:
      break;
    case dwarf::DW_LLE_base_address:
      E.Value0 = Data.getRelocatedAddress(C, &E.SectionIndex);
      break;
    case dwarf::DW_LLE_start_end:
      E.Value0 = Data.getRelocatedAddress(C, &E.SectionIndex);
      E.Value1 = Data.getRelocatedAddress(C);
      break;
    case dwarf::DW_LLE_start_length:
      E.Value0 = Data.getRelocatedAddress(C, &E.SectionIndex);
      E.Value1 = Data.getULEB128(C);
      break;
    default:
      cantFail(C.takeError());
      return createStringError(errc::illegal_byte_sequence,
                               "LLE of kind %x not supported", (int)E.Kind);
    }

    if (E.Kind != dwarf::DW_LLE_base_address &&
        E.Kind != dwarf::DW_LLE_base_addressx &&
        E.Kind != dwarf::DW_LLE_end_of_list) {
      unsigned Bytes = Version >= 5 ? Data.getULEB128(C) : Data.getU16(C);
      // A single location description describing the location of the object...
      Data.getU8(C, E.Loc, Bytes);
    }

    if (!C)
      return C.takeError();
    Continue = F(E) && E.Kind != dwarf::DW_LLE_end_of_list;
  }
  *Offset = C.tell();
  return Error::success();
}

void DWARFDebugLoclists::dumpRawEntry(const DWARFLocationEntry &Entry,
                                      raw_ostream &OS, unsigned Indent,
                                      DIDumpOptions DumpOpts,
                                      const DWARFObject &Obj) const {
  size_t MaxEncodingStringLength = 0;
#define HANDLE_DW_LLE(ID, NAME)                                                \
  MaxEncodingStringLength = std::max(MaxEncodingStringLength,                  \
                                     dwarf::LocListEncodingString(ID).size());
#include "llvm/BinaryFormat/Dwarf.def"

  OS << "\n";
  OS.indent(Indent);
  StringRef EncodingString = dwarf::LocListEncodingString(Entry.Kind);
  // Unsupported encodings should have been reported during parsing.
  assert(!EncodingString.empty() && "Unknown loclist entry encoding");
  OS << format("%-*s(", MaxEncodingStringLength, EncodingString.data());
  unsigned FieldSize = 2 + 2 * Data.getAddressSize();
  switch (Entry.Kind) {
  case dwarf::DW_LLE_end_of_list:
  case dwarf::DW_LLE_default_location:
    break;
  case dwarf::DW_LLE_startx_endx:
  case dwarf::DW_LLE_startx_length:
  case dwarf::DW_LLE_offset_pair:
  case dwarf::DW_LLE_start_end:
  case dwarf::DW_LLE_start_length:
    OS << format_hex(Entry.Value0, FieldSize) << ", "
       << format_hex(Entry.Value1, FieldSize);
    break;
  case dwarf::DW_LLE_base_addressx:
  case dwarf::DW_LLE_base_address:
    OS << format_hex(Entry.Value0, FieldSize);
    break;
  }
  OS << ')';
  switch (Entry.Kind) {
  case dwarf::DW_LLE_base_address:
  case dwarf::DW_LLE_start_end:
  case dwarf::DW_LLE_start_length:
    DWARFFormValue::dumpAddressSection(Obj, OS, DumpOpts, Entry.SectionIndex);
    break;
  default:
    break;
  }
}

void DWARFDebugLoclists::dumpRange(uint64_t StartOffset, uint64_t Size,
                                   raw_ostream &OS, const DWARFObject &Obj,
                                   DIDumpOptions DumpOpts) {
  if (!Data.isValidOffsetForDataOfSize(StartOffset, Size))  {
    OS << "Invalid dump range\n";
    return;
  }
  uint64_t Offset = StartOffset;
  StringRef Separator;
  bool CanContinue = true;
  while (CanContinue && Offset < StartOffset + Size) {
    OS << Separator;
    Separator = "\n";

    CanContinue = dumpLocationList(&Offset, OS, /*BaseAddr=*/std::nullopt, Obj,
                                   nullptr, DumpOpts, /*Indent=*/12);
    OS << '\n';
  }
}

void llvm::ResolverError::log(raw_ostream &OS) const {
  OS << format("unable to resolve indirect address %u for: %s", Index,
               dwarf::LocListEncodingString(Kind).data());
}

char llvm::ResolverError::ID;
