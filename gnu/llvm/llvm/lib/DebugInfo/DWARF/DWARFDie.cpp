//===- DWARFDie.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFTypePrinter.h"
#include "llvm/DebugInfo/DWARF/DWARFTypeUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <string>
#include <utility>

using namespace llvm;
using namespace dwarf;
using namespace object;

static void dumpApplePropertyAttribute(raw_ostream &OS, uint64_t Val) {
  OS << " (";
  do {
    uint64_t Shift = llvm::countr_zero(Val);
    assert(Shift < 64 && "undefined behavior");
    uint64_t Bit = 1ULL << Shift;
    auto PropName = ApplePropertyString(Bit);
    if (!PropName.empty())
      OS << PropName;
    else
      OS << format("DW_APPLE_PROPERTY_0x%" PRIx64, Bit);
    if (!(Val ^= Bit))
      break;
    OS << ", ";
  } while (true);
  OS << ")";
}

static void dumpRanges(const DWARFObject &Obj, raw_ostream &OS,
                       const DWARFAddressRangesVector &Ranges,
                       unsigned AddressSize, unsigned Indent,
                       const DIDumpOptions &DumpOpts) {
  if (!DumpOpts.ShowAddresses)
    return;

  for (const DWARFAddressRange &R : Ranges) {
    OS << '\n';
    OS.indent(Indent);
    R.dump(OS, AddressSize, DumpOpts, &Obj);
  }
}

static void dumpLocationList(raw_ostream &OS, const DWARFFormValue &FormValue,
                             DWARFUnit *U, unsigned Indent,
                             DIDumpOptions DumpOpts) {
  assert(FormValue.isFormClass(DWARFFormValue::FC_SectionOffset) &&
         "bad FORM for location list");
  DWARFContext &Ctx = U->getContext();
  uint64_t Offset = *FormValue.getAsSectionOffset();

  if (FormValue.getForm() == DW_FORM_loclistx) {
    FormValue.dump(OS, DumpOpts);

    if (auto LoclistOffset = U->getLoclistOffset(Offset))
      Offset = *LoclistOffset;
    else
      return;
  }
  U->getLocationTable().dumpLocationList(
      &Offset, OS, U->getBaseAddress(), Ctx.getDWARFObj(), U, DumpOpts, Indent);
}

static void dumpLocationExpr(raw_ostream &OS, const DWARFFormValue &FormValue,
                             DWARFUnit *U, unsigned Indent,
                             DIDumpOptions DumpOpts) {
  assert((FormValue.isFormClass(DWARFFormValue::FC_Block) ||
          FormValue.isFormClass(DWARFFormValue::FC_Exprloc)) &&
         "bad FORM for location expression");
  DWARFContext &Ctx = U->getContext();
  ArrayRef<uint8_t> Expr = *FormValue.getAsBlock();
  DataExtractor Data(StringRef((const char *)Expr.data(), Expr.size()),
                     Ctx.isLittleEndian(), 0);
  DWARFExpression(Data, U->getAddressByteSize(), U->getFormParams().Format)
      .print(OS, DumpOpts, U);
}

static DWARFDie resolveReferencedType(DWARFDie D, DWARFFormValue F) {
  return D.getAttributeValueAsReferencedDie(F).resolveTypeUnitReference();
}

static void dumpAttribute(raw_ostream &OS, const DWARFDie &Die,
                          const DWARFAttribute &AttrValue, unsigned Indent,
                          DIDumpOptions DumpOpts) {
  if (!Die.isValid())
    return;
  const char BaseIndent[] = "            ";
  OS << BaseIndent;
  OS.indent(Indent + 2);
  dwarf::Attribute Attr = AttrValue.Attr;
  WithColor(OS, HighlightColor::Attribute) << formatv("{0}", Attr);

  dwarf::Form Form = AttrValue.Value.getForm();
  if (DumpOpts.Verbose || DumpOpts.ShowForm)
    OS << formatv(" [{0}]", Form);

  DWARFUnit *U = Die.getDwarfUnit();
  const DWARFFormValue &FormValue = AttrValue.Value;

  OS << "\t(";

  StringRef Name;
  std::string File;
  auto Color = HighlightColor::Enumerator;
  if (Attr == DW_AT_decl_file || Attr == DW_AT_call_file) {
    Color = HighlightColor::String;
    if (const auto *LT = U->getContext().getLineTableForUnit(U)) {
      if (std::optional<uint64_t> Val = FormValue.getAsUnsignedConstant()) {
        if (LT->getFileNameByIndex(
                *Val, U->getCompilationDir(),
                DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
                File)) {
          File = '"' + File + '"';
          Name = File;
        }
      }
    }
  } else if (std::optional<uint64_t> Val = FormValue.getAsUnsignedConstant())
    Name = AttributeValueString(Attr, *Val);

  if (!Name.empty())
    WithColor(OS, Color) << Name;
  else if (Attr == DW_AT_decl_line || Attr == DW_AT_decl_column ||
           Attr == DW_AT_call_line || Attr == DW_AT_call_column) {
    if (std::optional<uint64_t> Val = FormValue.getAsUnsignedConstant())
      OS << *Val;
    else
      FormValue.dump(OS, DumpOpts);
  } else if (Attr == DW_AT_low_pc &&
             (FormValue.getAsAddress() ==
              dwarf::computeTombstoneAddress(U->getAddressByteSize()))) {
    if (DumpOpts.Verbose) {
      FormValue.dump(OS, DumpOpts);
      OS << " (";
    }
    OS << "dead code";
    if (DumpOpts.Verbose)
      OS << ')';
  } else if (Attr == DW_AT_high_pc && !DumpOpts.ShowForm && !DumpOpts.Verbose &&
             FormValue.getAsUnsignedConstant()) {
    if (DumpOpts.ShowAddresses) {
      // Print the actual address rather than the offset.
      uint64_t LowPC, HighPC, Index;
      if (Die.getLowAndHighPC(LowPC, HighPC, Index))
        DWARFFormValue::dumpAddress(OS, U->getAddressByteSize(), HighPC);
      else
        FormValue.dump(OS, DumpOpts);
    }
  } else if (DWARFAttribute::mayHaveLocationList(Attr) &&
             FormValue.isFormClass(DWARFFormValue::FC_SectionOffset))
    dumpLocationList(OS, FormValue, U, sizeof(BaseIndent) + Indent + 4,
                     DumpOpts);
  else if (FormValue.isFormClass(DWARFFormValue::FC_Exprloc) ||
           (DWARFAttribute::mayHaveLocationExpr(Attr) &&
            FormValue.isFormClass(DWARFFormValue::FC_Block)))
    dumpLocationExpr(OS, FormValue, U, sizeof(BaseIndent) + Indent + 4,
                     DumpOpts);
  else
    FormValue.dump(OS, DumpOpts);

  std::string Space = DumpOpts.ShowAddresses ? " " : "";

  // We have dumped the attribute raw value. For some attributes
  // having both the raw value and the pretty-printed value is
  // interesting. These attributes are handled below.
  if (Attr == DW_AT_specification || Attr == DW_AT_abstract_origin ||
      Attr == DW_AT_call_origin) {
    if (const char *Name =
            Die.getAttributeValueAsReferencedDie(FormValue).getName(
                DINameKind::LinkageName))
      OS << Space << "\"" << Name << '\"';
  } else if (Attr == DW_AT_type || Attr == DW_AT_containing_type) {
    DWARFDie D = resolveReferencedType(Die, FormValue);
    if (D && !D.isNULL()) {
      OS << Space << "\"";
      dumpTypeQualifiedName(D, OS);
      OS << '"';
    }
  } else if (Attr == DW_AT_APPLE_property_attribute) {
    if (std::optional<uint64_t> OptVal = FormValue.getAsUnsignedConstant())
      dumpApplePropertyAttribute(OS, *OptVal);
  } else if (Attr == DW_AT_ranges) {
    const DWARFObject &Obj = Die.getDwarfUnit()->getContext().getDWARFObj();
    // For DW_FORM_rnglistx we need to dump the offset separately, since
    // we have only dumped the index so far.
    if (FormValue.getForm() == DW_FORM_rnglistx)
      if (auto RangeListOffset =
              U->getRnglistOffset(*FormValue.getAsSectionOffset())) {
        DWARFFormValue FV = DWARFFormValue::createFromUValue(
            dwarf::DW_FORM_sec_offset, *RangeListOffset);
        FV.dump(OS, DumpOpts);
      }
    if (auto RangesOrError = Die.getAddressRanges())
      dumpRanges(Obj, OS, RangesOrError.get(), U->getAddressByteSize(),
                 sizeof(BaseIndent) + Indent + 4, DumpOpts);
    else
      DumpOpts.RecoverableErrorHandler(createStringError(
          errc::invalid_argument, "decoding address ranges: %s",
          toString(RangesOrError.takeError()).c_str()));
  }

  OS << ")\n";
}

void DWARFDie::getFullName(raw_string_ostream &OS,
                           std::string *OriginalFullName) const {
  const char *NamePtr = getShortName();
  if (!NamePtr)
    return;
  if (getTag() == DW_TAG_GNU_template_parameter_pack)
    return;
  dumpTypeUnqualifiedName(*this, OS, OriginalFullName);
}

bool DWARFDie::isSubprogramDIE() const { return getTag() == DW_TAG_subprogram; }

bool DWARFDie::isSubroutineDIE() const {
  auto Tag = getTag();
  return Tag == DW_TAG_subprogram || Tag == DW_TAG_inlined_subroutine;
}

std::optional<DWARFFormValue> DWARFDie::find(dwarf::Attribute Attr) const {
  if (!isValid())
    return std::nullopt;
  auto AbbrevDecl = getAbbreviationDeclarationPtr();
  if (AbbrevDecl)
    return AbbrevDecl->getAttributeValue(getOffset(), Attr, *U);
  return std::nullopt;
}

std::optional<DWARFFormValue>
DWARFDie::find(ArrayRef<dwarf::Attribute> Attrs) const {
  if (!isValid())
    return std::nullopt;
  auto AbbrevDecl = getAbbreviationDeclarationPtr();
  if (AbbrevDecl) {
    for (auto Attr : Attrs) {
      if (auto Value = AbbrevDecl->getAttributeValue(getOffset(), Attr, *U))
        return Value;
    }
  }
  return std::nullopt;
}

std::optional<DWARFFormValue>
DWARFDie::findRecursively(ArrayRef<dwarf::Attribute> Attrs) const {
  SmallVector<DWARFDie, 3> Worklist;
  Worklist.push_back(*this);

  // Keep track if DIEs already seen to prevent infinite recursion.
  // Empirically we rarely see a depth of more than 3 when dealing with valid
  // DWARF. This corresponds to following the DW_AT_abstract_origin and
  // DW_AT_specification just once.
  SmallSet<DWARFDie, 3> Seen;
  Seen.insert(*this);

  while (!Worklist.empty()) {
    DWARFDie Die = Worklist.pop_back_val();

    if (!Die.isValid())
      continue;

    if (auto Value = Die.find(Attrs))
      return Value;

    if (auto D = Die.getAttributeValueAsReferencedDie(DW_AT_abstract_origin))
      if (Seen.insert(D).second)
        Worklist.push_back(D);

    if (auto D = Die.getAttributeValueAsReferencedDie(DW_AT_specification))
      if (Seen.insert(D).second)
        Worklist.push_back(D);
  }

  return std::nullopt;
}

DWARFDie
DWARFDie::getAttributeValueAsReferencedDie(dwarf::Attribute Attr) const {
  if (std::optional<DWARFFormValue> F = find(Attr))
    return getAttributeValueAsReferencedDie(*F);
  return DWARFDie();
}

DWARFDie
DWARFDie::getAttributeValueAsReferencedDie(const DWARFFormValue &V) const {
  DWARFDie Result;
  if (std::optional<uint64_t> Offset = V.getAsRelativeReference()) {
    Result = const_cast<DWARFUnit *>(V.getUnit())
                 ->getDIEForOffset(V.getUnit()->getOffset() + *Offset);
  } else if (Offset = V.getAsDebugInfoReference(); Offset) {
    if (DWARFUnit *SpecUnit = U->getUnitVector().getUnitForOffset(*Offset))
      Result = SpecUnit->getDIEForOffset(*Offset);
  }
  return Result;
}

DWARFDie DWARFDie::resolveTypeUnitReference() const {
  if (auto Attr = find(DW_AT_signature)) {
    if (std::optional<uint64_t> Sig = Attr->getAsReferenceUVal()) {
      if (DWARFTypeUnit *TU = U->getContext().getTypeUnitForHash(
              U->getVersion(), *Sig, U->isDWOUnit()))
        return TU->getDIEForOffset(TU->getTypeOffset() + TU->getOffset());
    }
  }
  return *this;
}

std::optional<uint64_t> DWARFDie::getRangesBaseAttribute() const {
  return toSectionOffset(find({DW_AT_rnglists_base, DW_AT_GNU_ranges_base}));
}

std::optional<uint64_t> DWARFDie::getLocBaseAttribute() const {
  return toSectionOffset(find(DW_AT_loclists_base));
}

std::optional<uint64_t> DWARFDie::getHighPC(uint64_t LowPC) const {
  uint64_t Tombstone = dwarf::computeTombstoneAddress(U->getAddressByteSize());
  if (LowPC == Tombstone)
    return std::nullopt;
  if (auto FormValue = find(DW_AT_high_pc)) {
    if (auto Address = FormValue->getAsAddress()) {
      // High PC is an address.
      return Address;
    }
    if (auto Offset = FormValue->getAsUnsignedConstant()) {
      // High PC is an offset from LowPC.
      return LowPC + *Offset;
    }
  }
  return std::nullopt;
}

bool DWARFDie::getLowAndHighPC(uint64_t &LowPC, uint64_t &HighPC,
                               uint64_t &SectionIndex) const {
  auto F = find(DW_AT_low_pc);
  auto LowPcAddr = toSectionedAddress(F);
  if (!LowPcAddr)
    return false;
  if (auto HighPcAddr = getHighPC(LowPcAddr->Address)) {
    LowPC = LowPcAddr->Address;
    HighPC = *HighPcAddr;
    SectionIndex = LowPcAddr->SectionIndex;
    return true;
  }
  return false;
}

Expected<DWARFAddressRangesVector> DWARFDie::getAddressRanges() const {
  if (isNULL())
    return DWARFAddressRangesVector();
  // Single range specified by low/high PC.
  uint64_t LowPC, HighPC, Index;
  if (getLowAndHighPC(LowPC, HighPC, Index))
    return DWARFAddressRangesVector{{LowPC, HighPC, Index}};

  std::optional<DWARFFormValue> Value = find(DW_AT_ranges);
  if (Value) {
    if (Value->getForm() == DW_FORM_rnglistx)
      return U->findRnglistFromIndex(*Value->getAsSectionOffset());
    return U->findRnglistFromOffset(*Value->getAsSectionOffset());
  }
  return DWARFAddressRangesVector();
}

bool DWARFDie::addressRangeContainsAddress(const uint64_t Address) const {
  auto RangesOrError = getAddressRanges();
  if (!RangesOrError) {
    llvm::consumeError(RangesOrError.takeError());
    return false;
  }

  for (const auto &R : RangesOrError.get())
    if (R.LowPC <= Address && Address < R.HighPC)
      return true;
  return false;
}

Expected<DWARFLocationExpressionsVector>
DWARFDie::getLocations(dwarf::Attribute Attr) const {
  std::optional<DWARFFormValue> Location = find(Attr);
  if (!Location)
    return createStringError(inconvertibleErrorCode(), "No %s",
                             dwarf::AttributeString(Attr).data());

  if (std::optional<uint64_t> Off = Location->getAsSectionOffset()) {
    uint64_t Offset = *Off;

    if (Location->getForm() == DW_FORM_loclistx) {
      if (auto LoclistOffset = U->getLoclistOffset(Offset))
        Offset = *LoclistOffset;
      else
        return createStringError(inconvertibleErrorCode(),
                                 "Loclist table not found");
    }
    return U->findLoclistFromOffset(Offset);
  }

  if (std::optional<ArrayRef<uint8_t>> Expr = Location->getAsBlock()) {
    return DWARFLocationExpressionsVector{
        DWARFLocationExpression{std::nullopt, to_vector<4>(*Expr)}};
  }

  return createStringError(
      inconvertibleErrorCode(), "Unsupported %s encoding: %s",
      dwarf::AttributeString(Attr).data(),
      dwarf::FormEncodingString(Location->getForm()).data());
}

const char *DWARFDie::getSubroutineName(DINameKind Kind) const {
  if (!isSubroutineDIE())
    return nullptr;
  return getName(Kind);
}

const char *DWARFDie::getName(DINameKind Kind) const {
  if (!isValid() || Kind == DINameKind::None)
    return nullptr;
  // Try to get mangled name only if it was asked for.
  if (Kind == DINameKind::LinkageName) {
    if (auto Name = getLinkageName())
      return Name;
  }
  return getShortName();
}

const char *DWARFDie::getShortName() const {
  if (!isValid())
    return nullptr;

  return dwarf::toString(findRecursively(dwarf::DW_AT_name), nullptr);
}

const char *DWARFDie::getLinkageName() const {
  if (!isValid())
    return nullptr;

  return dwarf::toString(findRecursively({dwarf::DW_AT_MIPS_linkage_name,
                                          dwarf::DW_AT_linkage_name}),
                         nullptr);
}

uint64_t DWARFDie::getDeclLine() const {
  return toUnsigned(findRecursively(DW_AT_decl_line), 0);
}

std::string
DWARFDie::getDeclFile(DILineInfoSpecifier::FileLineInfoKind Kind) const {
  if (auto FormValue = findRecursively(DW_AT_decl_file))
    if (auto OptString = FormValue->getAsFile(Kind))
      return *OptString;
  return {};
}

void DWARFDie::getCallerFrame(uint32_t &CallFile, uint32_t &CallLine,
                              uint32_t &CallColumn,
                              uint32_t &CallDiscriminator) const {
  CallFile = toUnsigned(find(DW_AT_call_file), 0);
  CallLine = toUnsigned(find(DW_AT_call_line), 0);
  CallColumn = toUnsigned(find(DW_AT_call_column), 0);
  CallDiscriminator = toUnsigned(find(DW_AT_GNU_discriminator), 0);
}

static std::optional<uint64_t>
getTypeSizeImpl(DWARFDie Die, uint64_t PointerSize,
                SmallPtrSetImpl<const DWARFDebugInfoEntry *> &Visited) {
  // Cycle detected?
  if (!Visited.insert(Die.getDebugInfoEntry()).second)
    return {};
  if (auto SizeAttr = Die.find(DW_AT_byte_size))
    if (std::optional<uint64_t> Size = SizeAttr->getAsUnsignedConstant())
      return Size;

  switch (Die.getTag()) {
  case DW_TAG_pointer_type:
  case DW_TAG_reference_type:
  case DW_TAG_rvalue_reference_type:
    return PointerSize;
  case DW_TAG_ptr_to_member_type: {
    if (DWARFDie BaseType = Die.getAttributeValueAsReferencedDie(DW_AT_type))
      if (BaseType.getTag() == DW_TAG_subroutine_type)
        return 2 * PointerSize;
    return PointerSize;
  }
  case DW_TAG_const_type:
  case DW_TAG_immutable_type:
  case DW_TAG_volatile_type:
  case DW_TAG_restrict_type:
  case DW_TAG_template_alias:
  case DW_TAG_typedef: {
    if (DWARFDie BaseType = Die.getAttributeValueAsReferencedDie(DW_AT_type))
      return getTypeSizeImpl(BaseType, PointerSize, Visited);
    break;
  }
  case DW_TAG_array_type: {
    DWARFDie BaseType = Die.getAttributeValueAsReferencedDie(DW_AT_type);
    if (!BaseType)
      return std::nullopt;
    std::optional<uint64_t> BaseSize =
        getTypeSizeImpl(BaseType, PointerSize, Visited);
    if (!BaseSize)
      return std::nullopt;
    uint64_t Size = *BaseSize;
    for (DWARFDie Child : Die) {
      if (Child.getTag() != DW_TAG_subrange_type)
        continue;

      if (auto ElemCountAttr = Child.find(DW_AT_count))
        if (std::optional<uint64_t> ElemCount =
                ElemCountAttr->getAsUnsignedConstant())
          Size *= *ElemCount;
      if (auto UpperBoundAttr = Child.find(DW_AT_upper_bound))
        if (std::optional<int64_t> UpperBound =
                UpperBoundAttr->getAsSignedConstant()) {
          int64_t LowerBound = 0;
          if (auto LowerBoundAttr = Child.find(DW_AT_lower_bound))
            LowerBound = LowerBoundAttr->getAsSignedConstant().value_or(0);
          Size *= *UpperBound - LowerBound + 1;
        }
    }
    return Size;
  }
  default:
    if (DWARFDie BaseType = Die.getAttributeValueAsReferencedDie(DW_AT_type))
      return getTypeSizeImpl(BaseType, PointerSize, Visited);
    break;
  }
  return std::nullopt;
}

std::optional<uint64_t> DWARFDie::getTypeSize(uint64_t PointerSize) {
  SmallPtrSet<const DWARFDebugInfoEntry *, 4> Visited;
  return getTypeSizeImpl(*this, PointerSize, Visited);
}

/// Helper to dump a DIE with all of its parents, but no siblings.
static unsigned dumpParentChain(DWARFDie Die, raw_ostream &OS, unsigned Indent,
                                DIDumpOptions DumpOpts, unsigned Depth = 0) {
  if (!Die)
    return Indent;
  if (DumpOpts.ParentRecurseDepth > 0 && Depth >= DumpOpts.ParentRecurseDepth)
    return Indent;
  Indent = dumpParentChain(Die.getParent(), OS, Indent, DumpOpts, Depth + 1);
  Die.dump(OS, Indent, DumpOpts);
  return Indent + 2;
}

void DWARFDie::dump(raw_ostream &OS, unsigned Indent,
                    DIDumpOptions DumpOpts) const {
  if (!isValid())
    return;
  DWARFDataExtractor debug_info_data = U->getDebugInfoExtractor();
  const uint64_t Offset = getOffset();
  uint64_t offset = Offset;
  if (DumpOpts.ShowParents) {
    DIDumpOptions ParentDumpOpts = DumpOpts;
    ParentDumpOpts.ShowParents = false;
    ParentDumpOpts.ShowChildren = false;
    Indent = dumpParentChain(getParent(), OS, Indent, ParentDumpOpts);
  }

  if (debug_info_data.isValidOffset(offset)) {
    uint32_t abbrCode = debug_info_data.getULEB128(&offset);
    if (DumpOpts.ShowAddresses)
      WithColor(OS, HighlightColor::Address).get()
          << format("\n0x%8.8" PRIx64 ": ", Offset);

    if (abbrCode) {
      auto AbbrevDecl = getAbbreviationDeclarationPtr();
      if (AbbrevDecl) {
        WithColor(OS, HighlightColor::Tag).get().indent(Indent)
            << formatv("{0}", getTag());
        if (DumpOpts.Verbose) {
          OS << format(" [%u] %c", abbrCode,
                       AbbrevDecl->hasChildren() ? '*' : ' ');
          if (std::optional<uint32_t> ParentIdx = Die->getParentIdx())
            OS << format(" (0x%8.8" PRIx64 ")",
                         U->getDIEAtIndex(*ParentIdx).getOffset());
        }
        OS << '\n';

        // Dump all data in the DIE for the attributes.
        for (const DWARFAttribute &AttrValue : attributes())
          dumpAttribute(OS, *this, AttrValue, Indent, DumpOpts);

        if (DumpOpts.ShowChildren && DumpOpts.ChildRecurseDepth > 0) {
          DWARFDie Child = getFirstChild();
          DumpOpts.ChildRecurseDepth--;
          DIDumpOptions ChildDumpOpts = DumpOpts;
          ChildDumpOpts.ShowParents = false;
          while (Child) {
            Child.dump(OS, Indent + 2, ChildDumpOpts);
            Child = Child.getSibling();
          }
        }
      } else {
        OS << "Abbreviation code not found in 'debug_abbrev' class for code: "
           << abbrCode << '\n';
      }
    } else {
      OS.indent(Indent) << "NULL\n";
    }
  }
}

LLVM_DUMP_METHOD void DWARFDie::dump() const { dump(llvm::errs(), 0); }

DWARFDie DWARFDie::getParent() const {
  if (isValid())
    return U->getParent(Die);
  return DWARFDie();
}

DWARFDie DWARFDie::getSibling() const {
  if (isValid())
    return U->getSibling(Die);
  return DWARFDie();
}

DWARFDie DWARFDie::getPreviousSibling() const {
  if (isValid())
    return U->getPreviousSibling(Die);
  return DWARFDie();
}

DWARFDie DWARFDie::getFirstChild() const {
  if (isValid())
    return U->getFirstChild(Die);
  return DWARFDie();
}

DWARFDie DWARFDie::getLastChild() const {
  if (isValid())
    return U->getLastChild(Die);
  return DWARFDie();
}

iterator_range<DWARFDie::attribute_iterator> DWARFDie::attributes() const {
  return make_range(attribute_iterator(*this, false),
                    attribute_iterator(*this, true));
}

DWARFDie::attribute_iterator::attribute_iterator(DWARFDie D, bool End)
    : Die(D), Index(0) {
  auto AbbrDecl = Die.getAbbreviationDeclarationPtr();
  assert(AbbrDecl && "Must have abbreviation declaration");
  if (End) {
    // This is the end iterator so we set the index to the attribute count.
    Index = AbbrDecl->getNumAttributes();
  } else {
    // This is the begin iterator so we extract the value for this->Index.
    AttrValue.Offset = D.getOffset() + AbbrDecl->getCodeByteSize();
    updateForIndex(*AbbrDecl, 0);
  }
}

void DWARFDie::attribute_iterator::updateForIndex(
    const DWARFAbbreviationDeclaration &AbbrDecl, uint32_t I) {
  Index = I;
  // AbbrDecl must be valid before calling this function.
  auto NumAttrs = AbbrDecl.getNumAttributes();
  if (Index < NumAttrs) {
    AttrValue.Attr = AbbrDecl.getAttrByIndex(Index);
    // Add the previous byte size of any previous attribute value.
    AttrValue.Offset += AttrValue.ByteSize;
    uint64_t ParseOffset = AttrValue.Offset;
    if (AbbrDecl.getAttrIsImplicitConstByIndex(Index))
      AttrValue.Value = DWARFFormValue::createFromSValue(
          AbbrDecl.getFormByIndex(Index),
          AbbrDecl.getAttrImplicitConstValueByIndex(Index));
    else {
      auto U = Die.getDwarfUnit();
      assert(U && "Die must have valid DWARF unit");
      AttrValue.Value = DWARFFormValue::createFromUnit(
          AbbrDecl.getFormByIndex(Index), U, &ParseOffset);
    }
    AttrValue.ByteSize = ParseOffset - AttrValue.Offset;
  } else {
    assert(Index == NumAttrs && "Indexes should be [0, NumAttrs) only");
    AttrValue = {};
  }
}

DWARFDie::attribute_iterator &DWARFDie::attribute_iterator::operator++() {
  if (auto AbbrDecl = Die.getAbbreviationDeclarationPtr())
    updateForIndex(*AbbrDecl, Index + 1);
  return *this;
}

bool DWARFAttribute::mayHaveLocationList(dwarf::Attribute Attr) {
  switch(Attr) {
  case DW_AT_location:
  case DW_AT_string_length:
  case DW_AT_return_addr:
  case DW_AT_data_member_location:
  case DW_AT_frame_base:
  case DW_AT_static_link:
  case DW_AT_segment:
  case DW_AT_use_location:
  case DW_AT_vtable_elem_location:
    return true;
  default:
    return false;
  }
}

bool DWARFAttribute::mayHaveLocationExpr(dwarf::Attribute Attr) {
  switch (Attr) {
  // From the DWARF v5 specification.
  case DW_AT_location:
  case DW_AT_byte_size:
  case DW_AT_bit_offset:
  case DW_AT_bit_size:
  case DW_AT_string_length:
  case DW_AT_lower_bound:
  case DW_AT_return_addr:
  case DW_AT_bit_stride:
  case DW_AT_upper_bound:
  case DW_AT_count:
  case DW_AT_data_member_location:
  case DW_AT_frame_base:
  case DW_AT_segment:
  case DW_AT_static_link:
  case DW_AT_use_location:
  case DW_AT_vtable_elem_location:
  case DW_AT_allocated:
  case DW_AT_associated:
  case DW_AT_data_location:
  case DW_AT_byte_stride:
  case DW_AT_rank:
  case DW_AT_call_value:
  case DW_AT_call_origin:
  case DW_AT_call_target:
  case DW_AT_call_target_clobbered:
  case DW_AT_call_data_location:
  case DW_AT_call_data_value:
  // Extensions.
  case DW_AT_GNU_call_site_value:
  case DW_AT_GNU_call_site_target:
    return true;
  default:
    return false;
  }
}

namespace llvm {

void dumpTypeQualifiedName(const DWARFDie &DIE, raw_ostream &OS) {
  DWARFTypePrinter(OS).appendQualifiedName(DIE);
}

void dumpTypeUnqualifiedName(const DWARFDie &DIE, raw_ostream &OS,
                             std::string *OriginalFullName) {
  DWARFTypePrinter(OS).appendUnqualifiedName(DIE, OriginalFullName);
}

} // namespace llvm
