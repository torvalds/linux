//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  Parses ELF .eh_frame_hdr sections.
//
//===----------------------------------------------------------------------===//

#ifndef __EHHEADERPARSER_HPP__
#define __EHHEADERPARSER_HPP__

#include "libunwind.h"

#include "DwarfParser.hpp"

namespace libunwind {

/// \brief EHHeaderParser does basic parsing of an ELF .eh_frame_hdr section.
///
/// See DWARF spec for details:
///    http://refspecs.linuxbase.org/LSB_3.1.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
///
template <typename A> class EHHeaderParser {
public:
  typedef typename A::pint_t pint_t;

  /// Information encoded in the EH frame header.
  struct EHHeaderInfo {
    pint_t eh_frame_ptr;
    size_t fde_count;
    pint_t table;
    uint8_t table_enc;
  };

  static bool decodeEHHdr(A &addressSpace, pint_t ehHdrStart, pint_t ehHdrEnd,
                          EHHeaderInfo &ehHdrInfo);
  static bool findFDE(A &addressSpace, pint_t pc, pint_t ehHdrStart,
                      uint32_t sectionLength,
                      typename CFI_Parser<A>::FDE_Info *fdeInfo,
                      typename CFI_Parser<A>::CIE_Info *cieInfo);

private:
  static bool decodeTableEntry(A &addressSpace, pint_t &tableEntry,
                               pint_t ehHdrStart, pint_t ehHdrEnd,
                               uint8_t tableEnc,
                               typename CFI_Parser<A>::FDE_Info *fdeInfo,
                               typename CFI_Parser<A>::CIE_Info *cieInfo);
  static size_t getTableEntrySize(uint8_t tableEnc);
};

template <typename A>
bool EHHeaderParser<A>::decodeEHHdr(A &addressSpace, pint_t ehHdrStart,
                                    pint_t ehHdrEnd, EHHeaderInfo &ehHdrInfo) {
  pint_t p = ehHdrStart;

  // Ensure that we don't read data beyond the end of .eh_frame_hdr
  if (ehHdrEnd - ehHdrStart < 4) {
    // Don't print a message for an empty .eh_frame_hdr (this can happen if
    // the linker script defines symbols for it even in the empty case).
    if (ehHdrEnd == ehHdrStart)
      return false;
    _LIBUNWIND_LOG("unsupported .eh_frame_hdr at %" PRIx64
                   ": need at least 4 bytes of data but only got %zd",
                   static_cast<uint64_t>(ehHdrStart),
                   static_cast<size_t>(ehHdrEnd - ehHdrStart));
    return false;
  }
  uint8_t version = addressSpace.get8(p++);
  if (version != 1) {
    _LIBUNWIND_LOG("unsupported .eh_frame_hdr version: %" PRIu8 " at %" PRIx64,
                   version, static_cast<uint64_t>(ehHdrStart));
    return false;
  }

  uint8_t eh_frame_ptr_enc = addressSpace.get8(p++);
  uint8_t fde_count_enc = addressSpace.get8(p++);
  ehHdrInfo.table_enc = addressSpace.get8(p++);

  ehHdrInfo.eh_frame_ptr =
      addressSpace.getEncodedP(p, ehHdrEnd, eh_frame_ptr_enc, ehHdrStart);
  ehHdrInfo.fde_count =
      fde_count_enc == DW_EH_PE_omit
          ? 0
          : addressSpace.getEncodedP(p, ehHdrEnd, fde_count_enc, ehHdrStart);
  ehHdrInfo.table = p;

  return true;
}

template <typename A>
bool EHHeaderParser<A>::decodeTableEntry(
    A &addressSpace, pint_t &tableEntry, pint_t ehHdrStart, pint_t ehHdrEnd,
    uint8_t tableEnc, typename CFI_Parser<A>::FDE_Info *fdeInfo,
    typename CFI_Parser<A>::CIE_Info *cieInfo) {
  // Have to decode the whole FDE for the PC range anyway, so just throw away
  // the PC start.
  addressSpace.getEncodedP(tableEntry, ehHdrEnd, tableEnc, ehHdrStart);
  pint_t fde =
      addressSpace.getEncodedP(tableEntry, ehHdrEnd, tableEnc, ehHdrStart);
  const char *message =
      CFI_Parser<A>::decodeFDE(addressSpace, fde, fdeInfo, cieInfo);
  if (message != NULL) {
    _LIBUNWIND_DEBUG_LOG("EHHeaderParser::decodeTableEntry: bad fde: %s",
                         message);
    return false;
  }

  return true;
}

template <typename A>
bool EHHeaderParser<A>::findFDE(A &addressSpace, pint_t pc, pint_t ehHdrStart,
                                uint32_t sectionLength,
                                typename CFI_Parser<A>::FDE_Info *fdeInfo,
                                typename CFI_Parser<A>::CIE_Info *cieInfo) {
  pint_t ehHdrEnd = ehHdrStart + sectionLength;

  EHHeaderParser<A>::EHHeaderInfo hdrInfo;
  if (!EHHeaderParser<A>::decodeEHHdr(addressSpace, ehHdrStart, ehHdrEnd,
                                      hdrInfo))
    return false;

  if (hdrInfo.fde_count == 0) return false;

  size_t tableEntrySize = getTableEntrySize(hdrInfo.table_enc);
  pint_t tableEntry;

  size_t low = 0;
  for (size_t len = hdrInfo.fde_count; len > 1;) {
    size_t mid = low + (len / 2);
    tableEntry = hdrInfo.table + mid * tableEntrySize;
    pint_t start = addressSpace.getEncodedP(tableEntry, ehHdrEnd,
                                            hdrInfo.table_enc, ehHdrStart);

    if (start == pc) {
      low = mid;
      break;
    } else if (start < pc) {
      low = mid;
      len -= (len / 2);
    } else {
      len /= 2;
    }
  }

  tableEntry = hdrInfo.table + low * tableEntrySize;
  if (decodeTableEntry(addressSpace, tableEntry, ehHdrStart, ehHdrEnd,
                       hdrInfo.table_enc, fdeInfo, cieInfo)) {
    if (pc >= fdeInfo->pcStart && pc < fdeInfo->pcEnd)
      return true;
  }

  return false;
}

template <typename A>
size_t EHHeaderParser<A>::getTableEntrySize(uint8_t tableEnc) {
  switch (tableEnc & 0x0f) {
  case DW_EH_PE_sdata2:
  case DW_EH_PE_udata2:
    return 4;
  case DW_EH_PE_sdata4:
  case DW_EH_PE_udata4:
    return 8;
  case DW_EH_PE_sdata8:
  case DW_EH_PE_udata8:
    return 16;
  case DW_EH_PE_sleb128:
  case DW_EH_PE_uleb128:
    _LIBUNWIND_ABORT("Can't binary search on variable length encoded data.");
  case DW_EH_PE_omit:
    return 0;
  default:
    _LIBUNWIND_ABORT("Unknown DWARF encoding for search table.");
  }
}

}

#endif
