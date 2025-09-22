//===- lib/MC/MCSectionMachO.cpp - MachO Code Section Representation ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class MCAsmInfo;
class MCExpr;
class MCSymbol;
class Triple;
} // namespace llvm

using namespace llvm;

/// SectionTypeDescriptors - These are strings that describe the various section
/// types.  This *must* be kept in order with and stay synchronized with the
/// section type list.
static constexpr struct {
  StringLiteral AssemblerName, EnumName;
} SectionTypeDescriptors[MachO::LAST_KNOWN_SECTION_TYPE + 1] = {
    {StringLiteral("regular"), StringLiteral("S_REGULAR")}, // 0x00
    {StringLiteral("zerofill"), StringLiteral("S_ZEROFILL")}, // 0x01
    {StringLiteral("cstring_literals"),
     StringLiteral("S_CSTRING_LITERALS")}, // 0x02
    {StringLiteral("4byte_literals"),
     StringLiteral("S_4BYTE_LITERALS")}, // 0x03
    {StringLiteral("8byte_literals"),
     StringLiteral("S_8BYTE_LITERALS")}, // 0x04
    {StringLiteral("literal_pointers"),
     StringLiteral("S_LITERAL_POINTERS")}, // 0x05
    {StringLiteral("non_lazy_symbol_pointers"),
     StringLiteral("S_NON_LAZY_SYMBOL_POINTERS")}, // 0x06
    {StringLiteral("lazy_symbol_pointers"),
     StringLiteral("S_LAZY_SYMBOL_POINTERS")},                        // 0x07
    {StringLiteral("symbol_stubs"), StringLiteral("S_SYMBOL_STUBS")}, // 0x08
    {StringLiteral("mod_init_funcs"),
     StringLiteral("S_MOD_INIT_FUNC_POINTERS")}, // 0x09
    {StringLiteral("mod_term_funcs"),
     StringLiteral("S_MOD_TERM_FUNC_POINTERS")},                     // 0x0A
    {StringLiteral("coalesced"), StringLiteral("S_COALESCED")},      // 0x0B
    {StringLiteral("") /*FIXME??*/, StringLiteral("S_GB_ZEROFILL")}, // 0x0C
    {StringLiteral("interposing"), StringLiteral("S_INTERPOSING")},  // 0x0D
    {StringLiteral("16byte_literals"),
     StringLiteral("S_16BYTE_LITERALS")},                           // 0x0E
    {StringLiteral("") /*FIXME??*/, StringLiteral("S_DTRACE_DOF")}, // 0x0F
    {StringLiteral("") /*FIXME??*/,
     StringLiteral("S_LAZY_DYLIB_SYMBOL_POINTERS")}, // 0x10
    {StringLiteral("thread_local_regular"),
     StringLiteral("S_THREAD_LOCAL_REGULAR")}, // 0x11
    {StringLiteral("thread_local_zerofill"),
     StringLiteral("S_THREAD_LOCAL_ZEROFILL")}, // 0x12
    {StringLiteral("thread_local_variables"),
     StringLiteral("S_THREAD_LOCAL_VARIABLES")}, // 0x13
    {StringLiteral("thread_local_variable_pointers"),
     StringLiteral("S_THREAD_LOCAL_VARIABLE_POINTERS")}, // 0x14
    {StringLiteral("thread_local_init_function_pointers"),
     StringLiteral("S_THREAD_LOCAL_INIT_FUNCTION_POINTERS")}, // 0x15
    {StringLiteral("") /* linker-synthesized */,
     StringLiteral("S_INIT_FUNC_OFFSETS")}, // 0x16
};

/// SectionAttrDescriptors - This is an array of descriptors for section
/// attributes.  Unlike the SectionTypeDescriptors, this is not directly indexed
/// by attribute, instead it is searched.
static constexpr struct {
  unsigned AttrFlag;
  StringLiteral AssemblerName, EnumName;
} SectionAttrDescriptors[] = {
#define ENTRY(ASMNAME, ENUM) \
  { MachO::ENUM, StringLiteral(ASMNAME), StringLiteral(#ENUM) },
ENTRY("pure_instructions",   S_ATTR_PURE_INSTRUCTIONS)
ENTRY("no_toc",              S_ATTR_NO_TOC)
ENTRY("strip_static_syms",   S_ATTR_STRIP_STATIC_SYMS)
ENTRY("no_dead_strip",       S_ATTR_NO_DEAD_STRIP)
ENTRY("live_support",        S_ATTR_LIVE_SUPPORT)
ENTRY("self_modifying_code", S_ATTR_SELF_MODIFYING_CODE)
ENTRY("debug",               S_ATTR_DEBUG)
ENTRY("" /*FIXME*/,          S_ATTR_SOME_INSTRUCTIONS)
ENTRY("" /*FIXME*/,          S_ATTR_EXT_RELOC)
ENTRY("" /*FIXME*/,          S_ATTR_LOC_RELOC)
#undef ENTRY
  { 0, StringLiteral("none"), StringLiteral("") }, // used if section has no attributes but has a stub size
};

MCSectionMachO::MCSectionMachO(StringRef Segment, StringRef Section,
                               unsigned TAA, unsigned reserved2, SectionKind K,
                               MCSymbol *Begin)
    : MCSection(SV_MachO, Section, K.isText(),
                MachO::isVirtualSection(TAA & MachO::SECTION_TYPE), Begin),
      TypeAndAttributes(TAA), Reserved2(reserved2) {
  assert(Segment.size() <= 16 && Section.size() <= 16 &&
         "Segment or section string too long");
  for (unsigned i = 0; i != 16; ++i) {
    if (i < Segment.size())
      SegmentName[i] = Segment[i];
    else
      SegmentName[i] = 0;
  }
}

void MCSectionMachO::printSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                                          raw_ostream &OS,
                                          uint32_t Subsection) const {
  OS << "\t.section\t" << getSegmentName() << ',' << getName();

  // Get the section type and attributes.
  unsigned TAA = getTypeAndAttributes();
  if (TAA == 0) {
    OS << '\n';
    return;
  }

  MachO::SectionType SectionType = getType();
  assert(SectionType <= MachO::LAST_KNOWN_SECTION_TYPE &&
         "Invalid SectionType specified!");

  if (!SectionTypeDescriptors[SectionType].AssemblerName.empty()) {
    OS << ',';
    OS << SectionTypeDescriptors[SectionType].AssemblerName;
  } else {
    // If we have no name for the attribute, stop here.
    OS << '\n';
    return;
  }

  // If we don't have any attributes, we're done.
  unsigned SectionAttrs = TAA & MachO::SECTION_ATTRIBUTES;
  if (SectionAttrs == 0) {
    // If we have a S_SYMBOL_STUBS size specified, print it along with 'none' as
    // the attribute specifier.
    if (Reserved2 != 0)
      OS << ",none," << Reserved2;
    OS << '\n';
    return;
  }

  // Check each attribute to see if we have it.
  char Separator = ',';
  for (unsigned i = 0;
       SectionAttrs != 0 && SectionAttrDescriptors[i].AttrFlag;
       ++i) {
    // Check to see if we have this attribute.
    if ((SectionAttrDescriptors[i].AttrFlag & SectionAttrs) == 0)
      continue;

    // Yep, clear it and print it.
    SectionAttrs &= ~SectionAttrDescriptors[i].AttrFlag;

    OS << Separator;
    if (!SectionAttrDescriptors[i].AssemblerName.empty())
      OS << SectionAttrDescriptors[i].AssemblerName;
    else
      OS << "<<" << SectionAttrDescriptors[i].EnumName << ">>";
    Separator = '+';
  }

  assert(SectionAttrs == 0 && "Unknown section attributes!");

  // If we have a S_SYMBOL_STUBS size specified, print it.
  if (Reserved2 != 0)
    OS << ',' << Reserved2;
  OS << '\n';
}

bool MCSectionMachO::useCodeAlign() const {
  return hasAttribute(MachO::S_ATTR_PURE_INSTRUCTIONS);
}

/// ParseSectionSpecifier - Parse the section specifier indicated by "Spec".
/// This is a string that can appear after a .section directive in a mach-o
/// flavored .s file.  If successful, this fills in the specified Out
/// parameters and returns an empty string.  When an invalid section
/// specifier is present, this returns a string indicating the problem.
Error MCSectionMachO::ParseSectionSpecifier(StringRef Spec,       // In.
                                            StringRef &Segment,   // Out.
                                            StringRef &Section,   // Out.
                                            unsigned &TAA,        // Out.
                                            bool &TAAParsed,      // Out.
                                            unsigned &StubSize) { // Out.
  TAAParsed = false;

  SmallVector<StringRef, 5> SplitSpec;
  Spec.split(SplitSpec, ',');
  // Remove leading and trailing whitespace.
  auto GetEmptyOrTrim = [&SplitSpec](size_t Idx) -> StringRef {
    return SplitSpec.size() > Idx ? SplitSpec[Idx].trim() : StringRef();
  };
  Segment = GetEmptyOrTrim(0);
  Section = GetEmptyOrTrim(1);
  StringRef SectionType = GetEmptyOrTrim(2);
  StringRef Attrs = GetEmptyOrTrim(3);
  StringRef StubSizeStr = GetEmptyOrTrim(4);

  // Verify that the section is present.
  if (Section.empty())
    return createStringError(inconvertibleErrorCode(),
                             "mach-o section specifier requires a segment "
                             "and section separated by a comma");

  // Verify that the section is not too long.
  if (Section.size() > 16)
    return createStringError(inconvertibleErrorCode(),
                             "mach-o section specifier requires a section "
                             "whose length is between 1 and 16 characters");

  // If there is no comma after the section, we're done.
  TAA = 0;
  StubSize = 0;
  if (SectionType.empty())
    return Error::success();

  // Figure out which section type it is.
  auto TypeDescriptor =
      llvm::find_if(SectionTypeDescriptors,
                    [&](decltype(*SectionTypeDescriptors) &Descriptor) {
                      return SectionType == Descriptor.AssemblerName;
                    });

  // If we didn't find the section type, reject it.
  if (TypeDescriptor == std::end(SectionTypeDescriptors))
    return createStringError(inconvertibleErrorCode(),
                             "mach-o section specifier uses an unknown "
                             "section type");

  // Remember the TypeID.
  TAA = TypeDescriptor - std::begin(SectionTypeDescriptors);
  TAAParsed = true;

  // If we have no comma after the section type, there are no attributes.
  if (Attrs.empty()) {
    // S_SYMBOL_STUBS always require a symbol stub size specifier.
    if (TAA == MachO::S_SYMBOL_STUBS)
      return createStringError(inconvertibleErrorCode(),
                               "mach-o section specifier of type "
                               "'symbol_stubs' requires a size specifier");
    return Error::success();
  }

  // The attribute list is a '+' separated list of attributes.
  SmallVector<StringRef, 1> SectionAttrs;
  Attrs.split(SectionAttrs, '+', /*MaxSplit=*/-1, /*KeepEmpty=*/false);

  for (StringRef &SectionAttr : SectionAttrs) {
    auto AttrDescriptorI =
        llvm::find_if(SectionAttrDescriptors,
                      [&](decltype(*SectionAttrDescriptors) &Descriptor) {
                        return SectionAttr.trim() == Descriptor.AssemblerName;
                      });
    if (AttrDescriptorI == std::end(SectionAttrDescriptors))
      return createStringError(inconvertibleErrorCode(),
                               "mach-o section specifier has invalid "
                               "attribute");

    TAA |= AttrDescriptorI->AttrFlag;
  }

  // Okay, we've parsed the section attributes, see if we have a stub size spec.
  if (StubSizeStr.empty()) {
    // S_SYMBOL_STUBS always require a symbol stub size specifier.
    if (TAA == MachO::S_SYMBOL_STUBS)
      return createStringError(inconvertibleErrorCode(),
                               "mach-o section specifier of type "
                               "'symbol_stubs' requires a size specifier");
    return Error::success();
  }

  // If we have a stub size spec, we must have a sectiontype of S_SYMBOL_STUBS.
  if ((TAA & MachO::SECTION_TYPE) != MachO::S_SYMBOL_STUBS)
    return createStringError(inconvertibleErrorCode(),
                             "mach-o section specifier cannot have a stub "
                             "size specified because it does not have type "
                             "'symbol_stubs'");

  // Convert the stub size from a string to an integer.
  if (StubSizeStr.getAsInteger(0, StubSize))
    return createStringError(inconvertibleErrorCode(),
                             "mach-o section specifier has a malformed "
                             "stub size");

  return Error::success();
}

void MCSectionMachO::allocAtoms() {
  auto *L = curFragList();
  if (L->Tail)
    Atoms.resize(L->Tail->getLayoutOrder() + 1);
}

const MCSymbol *MCSectionMachO::getAtom(size_t I) const {
  return I < Atoms.size() ? Atoms[I] : nullptr;
}

void MCSectionMachO::setAtom(size_t I, const MCSymbol *Sym) { Atoms[I] = Sym; }
