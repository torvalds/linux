//===- DWARFDebugInfoEntry.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Errc.h"
#include <cstddef>
#include <cstdint>

using namespace llvm;
using namespace dwarf;

bool DWARFDebugInfoEntry::extractFast(const DWARFUnit &U, uint64_t *OffsetPtr,
                                      const DWARFDataExtractor &DebugInfoData,
                                      uint64_t UEndOffset, uint32_t ParentIdx) {
  Offset = *OffsetPtr;
  this->ParentIdx = ParentIdx;
  if (Offset >= UEndOffset) {
    U.getContext().getWarningHandler()(
        createStringError(errc::invalid_argument,
                          "DWARF unit from offset 0x%8.8" PRIx64 " incl. "
                          "to offset 0x%8.8" PRIx64 " excl. "
                          "tries to read DIEs at offset 0x%8.8" PRIx64,
                          U.getOffset(), U.getNextUnitOffset(), *OffsetPtr));
    return false;
  }
  assert(DebugInfoData.isValidOffset(UEndOffset - 1));
  uint64_t AbbrCode = DebugInfoData.getULEB128(OffsetPtr);
  if (0 == AbbrCode) {
    // NULL debug tag entry.
    AbbrevDecl = nullptr;
    return true;
  }
  const auto *AbbrevSet = U.getAbbreviations();
  if (!AbbrevSet) {
    U.getContext().getWarningHandler()(
        createStringError(errc::invalid_argument,
                          "DWARF unit at offset 0x%8.8" PRIx64 " "
                          "contains invalid abbreviation set offset 0x%" PRIx64,
                          U.getOffset(), U.getAbbreviationsOffset()));
    // Restore the original offset.
    *OffsetPtr = Offset;
    return false;
  }
  AbbrevDecl = AbbrevSet->getAbbreviationDeclaration(AbbrCode);
  if (!AbbrevDecl) {
    U.getContext().getWarningHandler()(
        createStringError(errc::invalid_argument,
                          "DWARF unit at offset 0x%8.8" PRIx64 " "
                          "contains invalid abbreviation %" PRIu64 " at "
                          "offset 0x%8.8" PRIx64 ", valid abbreviations are %s",
                          U.getOffset(), AbbrCode, *OffsetPtr,
                          AbbrevSet->getCodeRange().c_str()));
    // Restore the original offset.
    *OffsetPtr = Offset;
    return false;
  }
  // See if all attributes in this DIE have fixed byte sizes. If so, we can
  // just add this size to the offset to skip to the next DIE.
  if (std::optional<size_t> FixedSize =
          AbbrevDecl->getFixedAttributesByteSize(U)) {
    *OffsetPtr += *FixedSize;
    return true;
  }

  // Skip all data in the .debug_info for the attributes
  for (const auto &AttrSpec : AbbrevDecl->attributes()) {
    // Check if this attribute has a fixed byte size.
    if (auto FixedSize = AttrSpec.getByteSize(U)) {
      // Attribute byte size if fixed, just add the size to the offset.
      *OffsetPtr += *FixedSize;
    } else if (!DWARFFormValue::skipValue(AttrSpec.Form, DebugInfoData,
                                          OffsetPtr, U.getFormParams())) {
      // We failed to skip this attribute's value, restore the original offset
      // and return the failure status.
      U.getContext().getWarningHandler()(createStringError(
          errc::invalid_argument,
          "DWARF unit at offset 0x%8.8" PRIx64 " "
          "contains invalid FORM_* 0x%" PRIx16 " at offset 0x%8.8" PRIx64,
          U.getOffset(), AttrSpec.Form, *OffsetPtr));
      *OffsetPtr = Offset;
      return false;
    }
  }
  return true;
}
