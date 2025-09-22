//===- DWARFAbbreviationDeclaration.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>

using namespace llvm;
using namespace dwarf;

void DWARFAbbreviationDeclaration::clear() {
  Code = 0;
  Tag = DW_TAG_null;
  CodeByteSize = 0;
  HasChildren = false;
  AttributeSpecs.clear();
  FixedAttributeSize.reset();
}

DWARFAbbreviationDeclaration::DWARFAbbreviationDeclaration() {
  clear();
}

llvm::Expected<DWARFAbbreviationDeclaration::ExtractState>
DWARFAbbreviationDeclaration::extract(DataExtractor Data, uint64_t *OffsetPtr) {
  clear();
  const uint64_t Offset = *OffsetPtr;
  Error Err = Error::success();
  Code = Data.getULEB128(OffsetPtr, &Err);
  if (Err)
    return std::move(Err);

  if (Code == 0)
    return ExtractState::Complete;

  CodeByteSize = *OffsetPtr - Offset;
  Tag = static_cast<llvm::dwarf::Tag>(Data.getULEB128(OffsetPtr, &Err));
  if (Err)
    return std::move(Err);

  if (Tag == DW_TAG_null) {
    clear();
    return make_error<llvm::object::GenericBinaryError>(
        "abbreviation declaration requires a non-null tag");
  }
  uint8_t ChildrenByte = Data.getU8(OffsetPtr, &Err);
  if (Err)
    return std::move(Err);

  HasChildren = (ChildrenByte == DW_CHILDREN_yes);
  // Assign a value to our optional FixedAttributeSize member variable. If
  // this member variable still has a value after the while loop below, then
  // all attribute data in this abbreviation declaration has a fixed byte size.
  FixedAttributeSize = FixedSizeInfo();

  // Read all of the abbreviation attributes and forms.
  while (Data.isValidOffset(*OffsetPtr)) {
    auto A = static_cast<Attribute>(Data.getULEB128(OffsetPtr, &Err));
    if (Err)
      return std::move(Err);

    auto F = static_cast<Form>(Data.getULEB128(OffsetPtr, &Err));
    if (Err)
      return std::move(Err);

    // We successfully reached the end of this abbreviation declaration
    // since both attribute and form are zero. There may be more abbreviation
    // declarations afterwards.
    if (!A && !F)
      return ExtractState::MoreItems;

    if (!A || !F) {
      // Attribute and form pairs must either both be non-zero, in which case
      // they are added to the abbreviation declaration, or both be zero to
      // terminate the abbrevation declaration. In this case only one was
      // zero which is an error.
      clear();
      return make_error<llvm::object::GenericBinaryError>(
          "malformed abbreviation declaration attribute. Either the attribute "
          "or the form is zero while the other is not");
    }

    bool IsImplicitConst = (F == DW_FORM_implicit_const);
    if (IsImplicitConst) {
      int64_t V = Data.getSLEB128(OffsetPtr);
      AttributeSpecs.push_back(AttributeSpec(A, F, V));
      continue;
    }
    std::optional<uint8_t> ByteSize;
    // If this abbrevation still has a fixed byte size, then update the
    // FixedAttributeSize as needed.
    switch (F) {
    case DW_FORM_addr:
      if (FixedAttributeSize)
        ++FixedAttributeSize->NumAddrs;
      break;

    case DW_FORM_ref_addr:
      if (FixedAttributeSize)
        ++FixedAttributeSize->NumRefAddrs;
      break;

    case DW_FORM_strp:
    case DW_FORM_GNU_ref_alt:
    case DW_FORM_GNU_strp_alt:
    case DW_FORM_line_strp:
    case DW_FORM_sec_offset:
    case DW_FORM_strp_sup:
      if (FixedAttributeSize)
        ++FixedAttributeSize->NumDwarfOffsets;
      break;

    default:
      // The form has a byte size that doesn't depend on Params.
      // If it's a fixed size, keep track of it.
      if ((ByteSize = dwarf::getFixedFormByteSize(F, dwarf::FormParams()))) {
        if (FixedAttributeSize)
          FixedAttributeSize->NumBytes += *ByteSize;
        break;
      }
      // Indicate we no longer have a fixed byte size for this
      // abbreviation by clearing the FixedAttributeSize optional value
      // so it doesn't have a value.
      FixedAttributeSize.reset();
      break;
    }
    // Record this attribute and its fixed size if it has one.
    AttributeSpecs.push_back(AttributeSpec(A, F, ByteSize));
  }
  return make_error<llvm::object::GenericBinaryError>(
      "abbreviation declaration attribute list was not terminated with a null "
      "entry");
}

void DWARFAbbreviationDeclaration::dump(raw_ostream &OS) const {
  OS << '[' << getCode() << "] ";
  OS << formatv("{0}", getTag());
  OS << "\tDW_CHILDREN_" << (hasChildren() ? "yes" : "no") << '\n';
  for (const AttributeSpec &Spec : AttributeSpecs) {
    OS << formatv("\t{0}\t{1}", Spec.Attr, Spec.Form);
    if (Spec.isImplicitConst())
      OS << '\t' << Spec.getImplicitConstValue();
    OS << '\n';
  }
  OS << '\n';
}

std::optional<uint32_t>
DWARFAbbreviationDeclaration::findAttributeIndex(dwarf::Attribute Attr) const {
  for (uint32_t i = 0, e = AttributeSpecs.size(); i != e; ++i) {
    if (AttributeSpecs[i].Attr == Attr)
      return i;
  }
  return std::nullopt;
}

uint64_t DWARFAbbreviationDeclaration::getAttributeOffsetFromIndex(
    uint32_t AttrIndex, uint64_t DIEOffset, const DWARFUnit &U) const {
  DWARFDataExtractor DebugInfoData = U.getDebugInfoExtractor();

  // Add the byte size of ULEB that for the abbrev Code so we can start
  // skipping the attribute data.
  uint64_t Offset = DIEOffset + CodeByteSize;
  for (uint32_t CurAttrIdx = 0; CurAttrIdx != AttrIndex; ++CurAttrIdx)
    // Match Offset along until we get to the attribute we want.
    if (auto FixedSize = AttributeSpecs[CurAttrIdx].getByteSize(U))
      Offset += *FixedSize;
    else
      DWARFFormValue::skipValue(AttributeSpecs[CurAttrIdx].Form, DebugInfoData,
                                &Offset, U.getFormParams());
  return Offset;
}

std::optional<DWARFFormValue>
DWARFAbbreviationDeclaration::getAttributeValueFromOffset(
    uint32_t AttrIndex, uint64_t Offset, const DWARFUnit &U) const {
  assert(AttributeSpecs.size() > AttrIndex &&
         "Attribute Index is out of bounds.");

  // We have arrived at the attribute to extract, extract if from Offset.
  const AttributeSpec &Spec = AttributeSpecs[AttrIndex];
  if (Spec.isImplicitConst())
    return DWARFFormValue::createFromSValue(Spec.Form,
                                            Spec.getImplicitConstValue());

  DWARFFormValue FormValue(Spec.Form);
  DWARFDataExtractor DebugInfoData = U.getDebugInfoExtractor();
  if (FormValue.extractValue(DebugInfoData, &Offset, U.getFormParams(), &U))
    return FormValue;
  return std::nullopt;
}

std::optional<DWARFFormValue>
DWARFAbbreviationDeclaration::getAttributeValue(const uint64_t DIEOffset,
                                                const dwarf::Attribute Attr,
                                                const DWARFUnit &U) const {
  // Check if this abbreviation has this attribute without needing to skip
  // any data so we can return quickly if it doesn't.
  std::optional<uint32_t> MatchAttrIndex = findAttributeIndex(Attr);
  if (!MatchAttrIndex)
    return std::nullopt;

  uint64_t Offset = getAttributeOffsetFromIndex(*MatchAttrIndex, DIEOffset, U);

  return getAttributeValueFromOffset(*MatchAttrIndex, Offset, U);
}

size_t DWARFAbbreviationDeclaration::FixedSizeInfo::getByteSize(
    const DWARFUnit &U) const {
  size_t ByteSize = NumBytes;
  if (NumAddrs)
    ByteSize += NumAddrs * U.getAddressByteSize();
  if (NumRefAddrs)
    ByteSize += NumRefAddrs * U.getRefAddrByteSize();
  if (NumDwarfOffsets)
    ByteSize += NumDwarfOffsets * U.getDwarfOffsetByteSize();
  return ByteSize;
}

std::optional<int64_t> DWARFAbbreviationDeclaration::AttributeSpec::getByteSize(
    const DWARFUnit &U) const {
  if (isImplicitConst())
    return 0;
  if (ByteSize.HasByteSize)
    return ByteSize.ByteSize;
  std::optional<int64_t> S;
  auto FixedByteSize = dwarf::getFixedFormByteSize(Form, U.getFormParams());
  if (FixedByteSize)
    S = *FixedByteSize;
  return S;
}

std::optional<size_t> DWARFAbbreviationDeclaration::getFixedAttributesByteSize(
    const DWARFUnit &U) const {
  if (FixedAttributeSize)
    return FixedAttributeSize->getByteSize(U);
  return std::nullopt;
}
