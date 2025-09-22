//===--- lib/CodeGen/DIE.cpp - DWARF Info Entries -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data structures for DWARF info entries.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/DIE.h"
#include "DwarfCompileUnit.h"
#include "DwarfDebug.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "dwarfdebug"

//===----------------------------------------------------------------------===//
// DIEAbbrevData Implementation
//===----------------------------------------------------------------------===//

/// Profile - Used to gather unique data for the abbreviation folding set.
///
void DIEAbbrevData::Profile(FoldingSetNodeID &ID) const {
  // Explicitly cast to an integer type for which FoldingSetNodeID has
  // overloads.  Otherwise MSVC 2010 thinks this call is ambiguous.
  ID.AddInteger(unsigned(Attribute));
  ID.AddInteger(unsigned(Form));
  if (Form == dwarf::DW_FORM_implicit_const)
    ID.AddInteger(Value);
}

//===----------------------------------------------------------------------===//
// DIEAbbrev Implementation
//===----------------------------------------------------------------------===//

/// Profile - Used to gather unique data for the abbreviation folding set.
///
void DIEAbbrev::Profile(FoldingSetNodeID &ID) const {
  ID.AddInteger(unsigned(Tag));
  ID.AddInteger(unsigned(Children));

  // For each attribute description.
  for (const DIEAbbrevData &D : Data)
    D.Profile(ID);
}

/// Emit - Print the abbreviation using the specified asm printer.
///
void DIEAbbrev::Emit(const AsmPrinter *AP) const {
  // Emit its Dwarf tag type.
  AP->emitULEB128(Tag, dwarf::TagString(Tag).data());

  // Emit whether it has children DIEs.
  AP->emitULEB128((unsigned)Children, dwarf::ChildrenString(Children).data());

  // For each attribute description.
  for (const DIEAbbrevData &AttrData : Data) {
    // Emit attribute type.
    AP->emitULEB128(AttrData.getAttribute(),
                    dwarf::AttributeString(AttrData.getAttribute()).data());

    // Emit form type.
#ifndef NDEBUG
    // Could be an assertion, but this way we can see the failing form code
    // easily, which helps track down where it came from.
    if (!dwarf::isValidFormForVersion(AttrData.getForm(),
                                      AP->getDwarfVersion())) {
      LLVM_DEBUG(dbgs() << "Invalid form " << format("0x%x", AttrData.getForm())
                        << " for DWARF version " << AP->getDwarfVersion()
                        << "\n");
      llvm_unreachable("Invalid form for specified DWARF version");
    }
#endif
    AP->emitULEB128(AttrData.getForm(),
                    dwarf::FormEncodingString(AttrData.getForm()).data());

    // Emit value for DW_FORM_implicit_const.
    if (AttrData.getForm() == dwarf::DW_FORM_implicit_const)
      AP->emitSLEB128(AttrData.getValue());
  }

  // Mark end of abbreviation.
  AP->emitULEB128(0, "EOM(1)");
  AP->emitULEB128(0, "EOM(2)");
}

LLVM_DUMP_METHOD
void DIEAbbrev::print(raw_ostream &O) const {
  O << "Abbreviation @"
    << format("0x%lx", (long)(intptr_t)this)
    << "  "
    << dwarf::TagString(Tag)
    << " "
    << dwarf::ChildrenString(Children)
    << '\n';

  for (const DIEAbbrevData &D : Data) {
    O << "  " << dwarf::AttributeString(D.getAttribute()) << "  "
      << dwarf::FormEncodingString(D.getForm());

    if (D.getForm() == dwarf::DW_FORM_implicit_const)
      O << " " << D.getValue();

    O << '\n';
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DIEAbbrev::dump() const {
  print(dbgs());
}
#endif

//===----------------------------------------------------------------------===//
// DIEAbbrevSet Implementation
//===----------------------------------------------------------------------===//

DIEAbbrevSet::~DIEAbbrevSet() {
  for (DIEAbbrev *Abbrev : Abbreviations)
    Abbrev->~DIEAbbrev();
}

DIEAbbrev &DIEAbbrevSet::uniqueAbbreviation(DIE &Die) {

  FoldingSetNodeID ID;
  DIEAbbrev Abbrev = Die.generateAbbrev();
  Abbrev.Profile(ID);

  void *InsertPos;
  if (DIEAbbrev *Existing =
          AbbreviationsSet.FindNodeOrInsertPos(ID, InsertPos)) {
    Die.setAbbrevNumber(Existing->getNumber());
    return *Existing;
  }

  // Move the abbreviation to the heap and assign a number.
  DIEAbbrev *New = new (Alloc) DIEAbbrev(std::move(Abbrev));
  Abbreviations.push_back(New);
  New->setNumber(Abbreviations.size());
  Die.setAbbrevNumber(Abbreviations.size());

  // Store it for lookup.
  AbbreviationsSet.InsertNode(New, InsertPos);
  return *New;
}

void DIEAbbrevSet::Emit(const AsmPrinter *AP, MCSection *Section) const {
  if (!Abbreviations.empty()) {
    // Start the debug abbrev section.
    AP->OutStreamer->switchSection(Section);
    AP->emitDwarfAbbrevs(Abbreviations);
  }
}

//===----------------------------------------------------------------------===//
// DIE Implementation
//===----------------------------------------------------------------------===//

DIE *DIE::getParent() const { return dyn_cast_if_present<DIE *>(Owner); }

DIEAbbrev DIE::generateAbbrev() const {
  DIEAbbrev Abbrev(Tag, hasChildren());
  for (const DIEValue &V : values())
    if (V.getForm() == dwarf::DW_FORM_implicit_const)
      Abbrev.AddImplicitConstAttribute(V.getAttribute(),
                                       V.getDIEInteger().getValue());
    else
      Abbrev.AddAttribute(V.getAttribute(), V.getForm());
  return Abbrev;
}

uint64_t DIE::getDebugSectionOffset() const {
  const DIEUnit *Unit = getUnit();
  assert(Unit && "DIE must be owned by a DIEUnit to get its absolute offset");
  return Unit->getDebugSectionOffset() + getOffset();
}

const DIE *DIE::getUnitDie() const {
  const DIE *p = this;
  while (p) {
    if (p->getTag() == dwarf::DW_TAG_compile_unit ||
        p->getTag() == dwarf::DW_TAG_skeleton_unit ||
        p->getTag() == dwarf::DW_TAG_type_unit)
      return p;
    p = p->getParent();
  }
  return nullptr;
}

DIEUnit *DIE::getUnit() const {
  const DIE *UnitDie = getUnitDie();
  if (UnitDie)
    return dyn_cast_if_present<DIEUnit *>(UnitDie->Owner);
  return nullptr;
}

DIEValue DIE::findAttribute(dwarf::Attribute Attribute) const {
  // Iterate through all the attributes until we find the one we're
  // looking for, if we can't find it return NULL.
  for (const auto &V : values())
    if (V.getAttribute() == Attribute)
      return V;
  return DIEValue();
}

LLVM_DUMP_METHOD
static void printValues(raw_ostream &O, const DIEValueList &Values,
                        StringRef Type, unsigned Size, unsigned IndentCount) {
  O << Type << ": Size: " << Size << "\n";

  unsigned I = 0;
  const std::string Indent(IndentCount, ' ');
  for (const auto &V : Values.values()) {
    O << Indent;
    O << "Blk[" << I++ << "]";
    O << "  " << dwarf::FormEncodingString(V.getForm()) << " ";
    V.print(O);
    O << "\n";
  }
}

LLVM_DUMP_METHOD
void DIE::print(raw_ostream &O, unsigned IndentCount) const {
  const std::string Indent(IndentCount, ' ');
  O << Indent << "Die: " << format("0x%lx", (long)(intptr_t) this)
    << ", Offset: " << Offset << ", Size: " << Size << "\n";

  O << Indent << dwarf::TagString(getTag()) << " "
    << dwarf::ChildrenString(hasChildren()) << "\n";

  IndentCount += 2;
  for (const auto &V : values()) {
    O << Indent;
    O << dwarf::AttributeString(V.getAttribute());
    O << "  " << dwarf::FormEncodingString(V.getForm()) << " ";
    V.print(O);
    O << "\n";
  }
  IndentCount -= 2;

  for (const auto &Child : children())
    Child.print(O, IndentCount + 4);

  O << "\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DIE::dump() const {
  print(dbgs());
}
#endif

unsigned DIE::computeOffsetsAndAbbrevs(const dwarf::FormParams &FormParams,
                                       DIEAbbrevSet &AbbrevSet,
                                       unsigned CUOffset) {
  // Unique the abbreviation and fill in the abbreviation number so this DIE
  // can be emitted.
  const DIEAbbrev &Abbrev = AbbrevSet.uniqueAbbreviation(*this);

  // Set compile/type unit relative offset of this DIE.
  setOffset(CUOffset);

  // Add the byte size of the abbreviation code.
  CUOffset += getULEB128Size(getAbbrevNumber());

  // Add the byte size of all the DIE attribute values.
  for (const auto &V : values())
    CUOffset += V.sizeOf(FormParams);

  // Let the children compute their offsets and abbreviation numbers.
  if (hasChildren()) {
    (void)Abbrev;
    assert(Abbrev.hasChildren() && "Children flag not set");

    for (auto &Child : children())
      CUOffset =
          Child.computeOffsetsAndAbbrevs(FormParams, AbbrevSet, CUOffset);

    // Each child chain is terminated with a zero byte, adjust the offset.
    CUOffset += sizeof(int8_t);
  }

  // Compute the byte size of this DIE and all of its children correctly. This
  // is needed so that top level DIE can help the compile unit set its length
  // correctly.
  setSize(CUOffset - getOffset());
  return CUOffset;
}

//===----------------------------------------------------------------------===//
// DIEUnit Implementation
//===----------------------------------------------------------------------===//
DIEUnit::DIEUnit(dwarf::Tag UnitTag) : Die(UnitTag) {
  Die.Owner = this;
  assert((UnitTag == dwarf::DW_TAG_compile_unit ||
          UnitTag == dwarf::DW_TAG_skeleton_unit ||
          UnitTag == dwarf::DW_TAG_type_unit ||
          UnitTag == dwarf::DW_TAG_partial_unit) &&
         "expected a unit TAG");
}

void DIEValue::emitValue(const AsmPrinter *AP) const {
  switch (Ty) {
  case isNone:
    llvm_unreachable("Expected valid DIEValue");
#define HANDLE_DIEVALUE(T)                                                     \
  case is##T:                                                                  \
    getDIE##T().emitValue(AP, Form);                                           \
    break;
#include "llvm/CodeGen/DIEValue.def"
  }
}

unsigned DIEValue::sizeOf(const dwarf::FormParams &FormParams) const {
  switch (Ty) {
  case isNone:
    llvm_unreachable("Expected valid DIEValue");
#define HANDLE_DIEVALUE(T)                                                     \
  case is##T:                                                                  \
    return getDIE##T().sizeOf(FormParams, Form);
#include "llvm/CodeGen/DIEValue.def"
  }
  llvm_unreachable("Unknown DIE kind");
}

LLVM_DUMP_METHOD
void DIEValue::print(raw_ostream &O) const {
  switch (Ty) {
  case isNone:
    llvm_unreachable("Expected valid DIEValue");
#define HANDLE_DIEVALUE(T)                                                     \
  case is##T:                                                                  \
    getDIE##T().print(O);                                                      \
    break;
#include "llvm/CodeGen/DIEValue.def"
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DIEValue::dump() const {
  print(dbgs());
}
#endif

//===----------------------------------------------------------------------===//
// DIEInteger Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit integer of appropriate size.
///
void DIEInteger::emitValue(const AsmPrinter *Asm, dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_implicit_const:
  case dwarf::DW_FORM_flag_present:
    // Emit something to keep the lines and comments in sync.
    // FIXME: Is there a better way to do this?
    Asm->OutStreamer->addBlankLine();
    return;
  case dwarf::DW_FORM_flag:
  case dwarf::DW_FORM_ref1:
  case dwarf::DW_FORM_data1:
  case dwarf::DW_FORM_strx1:
  case dwarf::DW_FORM_addrx1:
  case dwarf::DW_FORM_ref2:
  case dwarf::DW_FORM_data2:
  case dwarf::DW_FORM_strx2:
  case dwarf::DW_FORM_addrx2:
  case dwarf::DW_FORM_strx3:
  case dwarf::DW_FORM_addrx3:
  case dwarf::DW_FORM_strp:
  case dwarf::DW_FORM_ref4:
  case dwarf::DW_FORM_data4:
  case dwarf::DW_FORM_ref_sup4:
  case dwarf::DW_FORM_strx4:
  case dwarf::DW_FORM_addrx4:
  case dwarf::DW_FORM_ref8:
  case dwarf::DW_FORM_ref_sig8:
  case dwarf::DW_FORM_data8:
  case dwarf::DW_FORM_ref_sup8:
  case dwarf::DW_FORM_GNU_ref_alt:
  case dwarf::DW_FORM_GNU_strp_alt:
  case dwarf::DW_FORM_line_strp:
  case dwarf::DW_FORM_sec_offset:
  case dwarf::DW_FORM_strp_sup:
  case dwarf::DW_FORM_addr:
  case dwarf::DW_FORM_ref_addr:
    Asm->OutStreamer->emitIntValue(Integer,
                                   sizeOf(Asm->getDwarfFormParams(), Form));
    return;
  case dwarf::DW_FORM_GNU_str_index:
  case dwarf::DW_FORM_GNU_addr_index:
  case dwarf::DW_FORM_ref_udata:
  case dwarf::DW_FORM_strx:
  case dwarf::DW_FORM_addrx:
  case dwarf::DW_FORM_rnglistx:
  case dwarf::DW_FORM_udata:
    Asm->emitULEB128(Integer);
    return;
  case dwarf::DW_FORM_sdata:
    Asm->emitSLEB128(Integer);
    return;
  default: llvm_unreachable("DIE Value form not supported yet");
  }
}

/// sizeOf - Determine size of integer value in bytes.
///
unsigned DIEInteger::sizeOf(const dwarf::FormParams &FormParams,
                            dwarf::Form Form) const {
  if (std::optional<uint8_t> FixedSize =
          dwarf::getFixedFormByteSize(Form, FormParams))
    return *FixedSize;

  switch (Form) {
  case dwarf::DW_FORM_GNU_str_index:
  case dwarf::DW_FORM_GNU_addr_index:
  case dwarf::DW_FORM_ref_udata:
  case dwarf::DW_FORM_strx:
  case dwarf::DW_FORM_addrx:
  case dwarf::DW_FORM_rnglistx:
  case dwarf::DW_FORM_udata:
    return getULEB128Size(Integer);
  case dwarf::DW_FORM_sdata:
    return getSLEB128Size(Integer);
  default: llvm_unreachable("DIE Value form not supported yet");
  }
}

LLVM_DUMP_METHOD
void DIEInteger::print(raw_ostream &O) const {
  O << "Int: " << (int64_t)Integer << "  0x";
  O.write_hex(Integer);
}

//===----------------------------------------------------------------------===//
// DIEExpr Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit expression value.
///
void DIEExpr::emitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  AP->emitDebugValue(Expr, sizeOf(AP->getDwarfFormParams(), Form));
}

/// SizeOf - Determine size of expression value in bytes.
///
unsigned DIEExpr::sizeOf(const dwarf::FormParams &FormParams,
                         dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_data4:
    return 4;
  case dwarf::DW_FORM_data8:
    return 8;
  case dwarf::DW_FORM_sec_offset:
    return FormParams.getDwarfOffsetByteSize();
  default:
    llvm_unreachable("DIE Value form not supported yet");
  }
}

LLVM_DUMP_METHOD
void DIEExpr::print(raw_ostream &O) const { O << "Expr: " << *Expr; }

//===----------------------------------------------------------------------===//
// DIELabel Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit label value.
///
void DIELabel::emitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  bool IsSectionRelative = Form != dwarf::DW_FORM_addr;
  AP->emitLabelReference(Label, sizeOf(AP->getDwarfFormParams(), Form),
                         IsSectionRelative);
}

/// sizeOf - Determine size of label value in bytes.
///
unsigned DIELabel::sizeOf(const dwarf::FormParams &FormParams,
                          dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_data4:
    return 4;
  case dwarf::DW_FORM_data8:
    return 8;
  case dwarf::DW_FORM_sec_offset:
  case dwarf::DW_FORM_strp:
    return FormParams.getDwarfOffsetByteSize();
  case dwarf::DW_FORM_addr:
    return FormParams.AddrSize;
  default:
    llvm_unreachable("DIE Value form not supported yet");
  }
}

LLVM_DUMP_METHOD
void DIELabel::print(raw_ostream &O) const { O << "Lbl: " << Label->getName(); }

//===----------------------------------------------------------------------===//
// DIEBaseTypeRef Implementation
//===----------------------------------------------------------------------===//

void DIEBaseTypeRef::emitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  uint64_t Offset = CU->ExprRefedBaseTypes[Index].Die->getOffset();
  assert(Offset < (1ULL << (ULEB128PadSize * 7)) && "Offset wont fit");
  AP->emitULEB128(Offset, nullptr, ULEB128PadSize);
}

unsigned DIEBaseTypeRef::sizeOf(const dwarf::FormParams &, dwarf::Form) const {
  return ULEB128PadSize;
}

LLVM_DUMP_METHOD
void DIEBaseTypeRef::print(raw_ostream &O) const { O << "BaseTypeRef: " << Index; }

//===----------------------------------------------------------------------===//
// DIEDelta Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit delta value.
///
void DIEDelta::emitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  AP->emitLabelDifference(LabelHi, LabelLo,
                          sizeOf(AP->getDwarfFormParams(), Form));
}

/// SizeOf - Determine size of delta value in bytes.
///
unsigned DIEDelta::sizeOf(const dwarf::FormParams &FormParams,
                          dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_data4:
    return 4;
  case dwarf::DW_FORM_data8:
    return 8;
  case dwarf::DW_FORM_sec_offset:
    return FormParams.getDwarfOffsetByteSize();
  default:
    llvm_unreachable("DIE Value form not supported yet");
  }
}

LLVM_DUMP_METHOD
void DIEDelta::print(raw_ostream &O) const {
  O << "Del: " << LabelHi->getName() << "-" << LabelLo->getName();
}

//===----------------------------------------------------------------------===//
// DIEString Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit string value.
///
void DIEString::emitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  // Index of string in symbol table.
  switch (Form) {
  case dwarf::DW_FORM_GNU_str_index:
  case dwarf::DW_FORM_strx:
  case dwarf::DW_FORM_strx1:
  case dwarf::DW_FORM_strx2:
  case dwarf::DW_FORM_strx3:
  case dwarf::DW_FORM_strx4:
    DIEInteger(S.getIndex()).emitValue(AP, Form);
    return;
  case dwarf::DW_FORM_strp:
    if (AP->doesDwarfUseRelocationsAcrossSections())
      DIELabel(S.getSymbol()).emitValue(AP, Form);
    else
      DIEInteger(S.getOffset()).emitValue(AP, Form);
    return;
  default:
    llvm_unreachable("Expected valid string form");
  }
}

/// sizeOf - Determine size of delta value in bytes.
///
unsigned DIEString::sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const {
  // Index of string in symbol table.
  switch (Form) {
  case dwarf::DW_FORM_GNU_str_index:
  case dwarf::DW_FORM_strx:
  case dwarf::DW_FORM_strx1:
  case dwarf::DW_FORM_strx2:
  case dwarf::DW_FORM_strx3:
  case dwarf::DW_FORM_strx4:
    return DIEInteger(S.getIndex()).sizeOf(FormParams, Form);
  case dwarf::DW_FORM_strp:
    if (FormParams.DwarfUsesRelocationsAcrossSections)
      return DIELabel(S.getSymbol()).sizeOf(FormParams, Form);
    return DIEInteger(S.getOffset()).sizeOf(FormParams, Form);
  default:
    llvm_unreachable("Expected valid string form");
  }
}

LLVM_DUMP_METHOD
void DIEString::print(raw_ostream &O) const {
  O << "String: " << S.getString();
}

//===----------------------------------------------------------------------===//
// DIEInlineString Implementation
//===----------------------------------------------------------------------===//
void DIEInlineString::emitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  if (Form == dwarf::DW_FORM_string) {
    AP->OutStreamer->emitBytes(S);
    AP->emitInt8(0);
    return;
  }
  llvm_unreachable("Expected valid string form");
}

unsigned DIEInlineString::sizeOf(const dwarf::FormParams &, dwarf::Form) const {
  // Emit string bytes + NULL byte.
  return S.size() + 1;
}

LLVM_DUMP_METHOD
void DIEInlineString::print(raw_ostream &O) const {
  O << "InlineString: " << S;
}

//===----------------------------------------------------------------------===//
// DIEEntry Implementation
//===----------------------------------------------------------------------===//

/// EmitValue - Emit debug information entry offset.
///
void DIEEntry::emitValue(const AsmPrinter *AP, dwarf::Form Form) const {

  switch (Form) {
  case dwarf::DW_FORM_ref1:
  case dwarf::DW_FORM_ref2:
  case dwarf::DW_FORM_ref4:
  case dwarf::DW_FORM_ref8:
    AP->OutStreamer->emitIntValue(Entry->getOffset(),
                                  sizeOf(AP->getDwarfFormParams(), Form));
    return;

  case dwarf::DW_FORM_ref_udata:
    AP->emitULEB128(Entry->getOffset());
    return;

  case dwarf::DW_FORM_ref_addr: {
    // Get the absolute offset for this DIE within the debug info/types section.
    uint64_t Addr = Entry->getDebugSectionOffset();
    if (const MCSymbol *SectionSym =
            Entry->getUnit()->getCrossSectionRelativeBaseAddress()) {
      AP->emitLabelPlusOffset(SectionSym, Addr,
                              sizeOf(AP->getDwarfFormParams(), Form), true);
      return;
    }

    AP->OutStreamer->emitIntValue(Addr, sizeOf(AP->getDwarfFormParams(), Form));
    return;
  }
  default:
    llvm_unreachable("Improper form for DIE reference");
  }
}

unsigned DIEEntry::sizeOf(const dwarf::FormParams &FormParams,
                          dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_ref1:
    return 1;
  case dwarf::DW_FORM_ref2:
    return 2;
  case dwarf::DW_FORM_ref4:
    return 4;
  case dwarf::DW_FORM_ref8:
    return 8;
  case dwarf::DW_FORM_ref_udata:
    return getULEB128Size(Entry->getOffset());
  case dwarf::DW_FORM_ref_addr:
    return FormParams.getRefAddrByteSize();

  default:
    llvm_unreachable("Improper form for DIE reference");
  }
}

LLVM_DUMP_METHOD
void DIEEntry::print(raw_ostream &O) const {
  O << format("Die: 0x%lx", (long)(intptr_t)&Entry);
}

//===----------------------------------------------------------------------===//
// DIELoc Implementation
//===----------------------------------------------------------------------===//

unsigned DIELoc::computeSize(const dwarf::FormParams &FormParams) const {
  if (!Size) {
    for (const auto &V : values())
      Size += V.sizeOf(FormParams);
  }

  return Size;
}

/// EmitValue - Emit location data.
///
void DIELoc::emitValue(const AsmPrinter *Asm, dwarf::Form Form) const {
  switch (Form) {
  default: llvm_unreachable("Improper form for block");
  case dwarf::DW_FORM_block1: Asm->emitInt8(Size);    break;
  case dwarf::DW_FORM_block2: Asm->emitInt16(Size);   break;
  case dwarf::DW_FORM_block4: Asm->emitInt32(Size);   break;
  case dwarf::DW_FORM_block:
  case dwarf::DW_FORM_exprloc:
    Asm->emitULEB128(Size);
    break;
  }

  for (const auto &V : values())
    V.emitValue(Asm);
}

/// sizeOf - Determine size of location data in bytes.
///
unsigned DIELoc::sizeOf(const dwarf::FormParams &, dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_block1: return Size + sizeof(int8_t);
  case dwarf::DW_FORM_block2: return Size + sizeof(int16_t);
  case dwarf::DW_FORM_block4: return Size + sizeof(int32_t);
  case dwarf::DW_FORM_block:
  case dwarf::DW_FORM_exprloc:
    return Size + getULEB128Size(Size);
  default: llvm_unreachable("Improper form for block");
  }
}

LLVM_DUMP_METHOD
void DIELoc::print(raw_ostream &O) const {
  printValues(O, *this, "ExprLoc", Size, 5);
}

//===----------------------------------------------------------------------===//
// DIEBlock Implementation
//===----------------------------------------------------------------------===//

unsigned DIEBlock::computeSize(const dwarf::FormParams &FormParams) const {
  if (!Size) {
    for (const auto &V : values())
      Size += V.sizeOf(FormParams);
  }

  return Size;
}

/// EmitValue - Emit block data.
///
void DIEBlock::emitValue(const AsmPrinter *Asm, dwarf::Form Form) const {
  switch (Form) {
  default: llvm_unreachable("Improper form for block");
  case dwarf::DW_FORM_block1: Asm->emitInt8(Size);    break;
  case dwarf::DW_FORM_block2: Asm->emitInt16(Size);   break;
  case dwarf::DW_FORM_block4: Asm->emitInt32(Size);   break;
  case dwarf::DW_FORM_exprloc:
  case dwarf::DW_FORM_block:
    Asm->emitULEB128(Size);
    break;
  case dwarf::DW_FORM_string: break;
  case dwarf::DW_FORM_data16: break;
  }

  for (const auto &V : values())
    V.emitValue(Asm);
}

/// sizeOf - Determine size of block data in bytes.
///
unsigned DIEBlock::sizeOf(const dwarf::FormParams &, dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_block1: return Size + sizeof(int8_t);
  case dwarf::DW_FORM_block2: return Size + sizeof(int16_t);
  case dwarf::DW_FORM_block4: return Size + sizeof(int32_t);
  case dwarf::DW_FORM_exprloc:
  case dwarf::DW_FORM_block:  return Size + getULEB128Size(Size);
  case dwarf::DW_FORM_data16: return 16;
  default: llvm_unreachable("Improper form for block");
  }
}

LLVM_DUMP_METHOD
void DIEBlock::print(raw_ostream &O) const {
  printValues(O, *this, "Blk", Size, 5);
}

//===----------------------------------------------------------------------===//
// DIELocList Implementation
//===----------------------------------------------------------------------===//

unsigned DIELocList::sizeOf(const dwarf::FormParams &FormParams,
                            dwarf::Form Form) const {
  switch (Form) {
  case dwarf::DW_FORM_loclistx:
    return getULEB128Size(Index);
  case dwarf::DW_FORM_data4:
    assert(FormParams.Format != dwarf::DWARF64 &&
           "DW_FORM_data4 is not suitable to emit a pointer to a location list "
           "in the 64-bit DWARF format");
    return 4;
  case dwarf::DW_FORM_data8:
    assert(FormParams.Format == dwarf::DWARF64 &&
           "DW_FORM_data8 is not suitable to emit a pointer to a location list "
           "in the 32-bit DWARF format");
    return 8;
  case dwarf::DW_FORM_sec_offset:
    return FormParams.getDwarfOffsetByteSize();
  default:
    llvm_unreachable("DIE Value form not supported yet");
  }
}

/// EmitValue - Emit label value.
///
void DIELocList::emitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  if (Form == dwarf::DW_FORM_loclistx) {
    AP->emitULEB128(Index);
    return;
  }
  DwarfDebug *DD = AP->getDwarfDebug();
  MCSymbol *Label = DD->getDebugLocs().getList(Index).Label;
  AP->emitDwarfSymbolReference(Label, /*ForceOffset*/ DD->useSplitDwarf());
}

LLVM_DUMP_METHOD
void DIELocList::print(raw_ostream &O) const { O << "LocList: " << Index; }

//===----------------------------------------------------------------------===//
// DIEAddrOffset Implementation
//===----------------------------------------------------------------------===//

unsigned DIEAddrOffset::sizeOf(const dwarf::FormParams &FormParams,
                               dwarf::Form) const {
  return Addr.sizeOf(FormParams, dwarf::DW_FORM_addrx) +
         Offset.sizeOf(FormParams, dwarf::DW_FORM_data4);
}

/// EmitValue - Emit label value.
///
void DIEAddrOffset::emitValue(const AsmPrinter *AP, dwarf::Form Form) const {
  Addr.emitValue(AP, dwarf::DW_FORM_addrx);
  Offset.emitValue(AP, dwarf::DW_FORM_data4);
}

LLVM_DUMP_METHOD
void DIEAddrOffset::print(raw_ostream &O) const {
  O << "AddrOffset: ";
  Addr.print(O);
  O << " + ";
  Offset.print(O);
}
