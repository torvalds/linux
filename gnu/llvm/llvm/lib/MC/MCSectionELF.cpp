//===- lib/MC/MCSectionELF.cpp - ELF Code Section Representation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSectionELF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>

using namespace llvm;

// Decides whether a '.section' directive
// should be printed before the section name.
bool MCSectionELF::shouldOmitSectionDirective(StringRef Name,
                                              const MCAsmInfo &MAI) const {
  if (isUnique())
    return false;

  return MAI.shouldOmitSectionDirective(Name);
}

static void printName(raw_ostream &OS, StringRef Name) {
  if (Name.find_first_not_of("0123456789_."
                             "abcdefghijklmnopqrstuvwxyz"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ") == Name.npos) {
    OS << Name;
    return;
  }
  OS << '"';
  for (const char *B = Name.begin(), *E = Name.end(); B < E; ++B) {
    if (*B == '"') // Unquoted "
      OS << "\\\"";
    else if (*B != '\\') // Neither " or backslash
      OS << *B;
    else if (B + 1 == E) // Trailing backslash
      OS << "\\\\";
    else {
      OS << B[0] << B[1]; // Quoted character
      ++B;
    }
  }
  OS << '"';
}

void MCSectionELF::printSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                                        raw_ostream &OS,
                                        uint32_t Subsection) const {
  if (shouldOmitSectionDirective(getName(), MAI)) {
    OS << '\t' << getName();
    if (Subsection)
      OS << '\t' << Subsection;
    OS << '\n';
    return;
  }

  OS << "\t.section\t";
  printName(OS, getName());

  // Handle the weird solaris syntax if desired.
  if (MAI.usesSunStyleELFSectionSwitchSyntax() &&
      !(Flags & ELF::SHF_MERGE)) {
    if (Flags & ELF::SHF_ALLOC)
      OS << ",#alloc";
    if (Flags & ELF::SHF_EXECINSTR)
      OS << ",#execinstr";
    if (Flags & ELF::SHF_WRITE)
      OS << ",#write";
    if (Flags & ELF::SHF_EXCLUDE)
      OS << ",#exclude";
    if (Flags & ELF::SHF_TLS)
      OS << ",#tls";
    OS << '\n';
    return;
  }

  OS << ",\"";
  if (Flags & ELF::SHF_ALLOC)
    OS << 'a';
  if (Flags & ELF::SHF_EXCLUDE)
    OS << 'e';
  if (Flags & ELF::SHF_EXECINSTR)
    OS << 'x';
  if (Flags & ELF::SHF_WRITE)
    OS << 'w';
  if (Flags & ELF::SHF_MERGE)
    OS << 'M';
  if (Flags & ELF::SHF_STRINGS)
    OS << 'S';
  if (Flags & ELF::SHF_TLS)
    OS << 'T';
  if (Flags & ELF::SHF_LINK_ORDER)
    OS << 'o';
  if (Flags & ELF::SHF_GROUP)
    OS << 'G';
  if (Flags & ELF::SHF_GNU_RETAIN)
    OS << 'R';

  // If there are os-specific flags, print them.
  if (T.isOSSolaris())
    if (Flags & ELF::SHF_SUNW_NODISCARD)
      OS << 'R';

  // If there are target-specific flags, print them.
  Triple::ArchType Arch = T.getArch();
  if (Arch == Triple::xcore) {
    if (Flags & ELF::XCORE_SHF_CP_SECTION)
      OS << 'c';
    if (Flags & ELF::XCORE_SHF_DP_SECTION)
      OS << 'd';
  } else if (T.isARM() || T.isThumb()) {
    if (Flags & ELF::SHF_ARM_PURECODE)
      OS << 'y';
  } else if (Arch == Triple::hexagon) {
    if (Flags & ELF::SHF_HEX_GPREL)
      OS << 's';
  } else if (Arch == Triple::x86_64) {
    if (Flags & ELF::SHF_X86_64_LARGE)
      OS << 'l';
  }

  OS << '"';

  OS << ',';

  // If comment string is '@', e.g. as on ARM - use '%' instead
  if (MAI.getCommentString()[0] == '@')
    OS << '%';
  else
    OS << '@';

  if (Type == ELF::SHT_INIT_ARRAY)
    OS << "init_array";
  else if (Type == ELF::SHT_FINI_ARRAY)
    OS << "fini_array";
  else if (Type == ELF::SHT_PREINIT_ARRAY)
    OS << "preinit_array";
  else if (Type == ELF::SHT_NOBITS)
    OS << "nobits";
  else if (Type == ELF::SHT_NOTE)
    OS << "note";
  else if (Type == ELF::SHT_PROGBITS)
    OS << "progbits";
  else if (Type == ELF::SHT_X86_64_UNWIND)
    OS << "unwind";
  else if (Type == ELF::SHT_MIPS_DWARF)
    // Print hex value of the flag while we do not have
    // any standard symbolic representation of the flag.
    OS << "0x7000001e";
  else if (Type == ELF::SHT_LLVM_ODRTAB)
    OS << "llvm_odrtab";
  else if (Type == ELF::SHT_LLVM_LINKER_OPTIONS)
    OS << "llvm_linker_options";
  else if (Type == ELF::SHT_LLVM_CALL_GRAPH_PROFILE)
    OS << "llvm_call_graph_profile";
  else if (Type == ELF::SHT_LLVM_DEPENDENT_LIBRARIES)
    OS << "llvm_dependent_libraries";
  else if (Type == ELF::SHT_LLVM_SYMPART)
    OS << "llvm_sympart";
  else if (Type == ELF::SHT_LLVM_BB_ADDR_MAP)
    OS << "llvm_bb_addr_map";
  else if (Type == ELF::SHT_LLVM_BB_ADDR_MAP_V0)
    OS << "llvm_bb_addr_map_v0";
  else if (Type == ELF::SHT_LLVM_OFFLOADING)
    OS << "llvm_offloading";
  else if (Type == ELF::SHT_LLVM_LTO)
    OS << "llvm_lto";
  else
    OS << "0x" << Twine::utohexstr(Type);

  if (EntrySize) {
    assert(Flags & ELF::SHF_MERGE);
    OS << "," << EntrySize;
  }

  if (Flags & ELF::SHF_LINK_ORDER) {
    OS << ",";
    if (LinkedToSym)
      printName(OS, LinkedToSym->getName());
    else
      OS << '0';
  }

  if (Flags & ELF::SHF_GROUP) {
    OS << ",";
    printName(OS, Group.getPointer()->getName());
    if (isComdat())
      OS << ",comdat";
  }

  if (isUnique())
    OS << ",unique," << UniqueID;

  OS << '\n';

  if (Subsection) {
    OS << "\t.subsection\t" << Subsection;
    OS << '\n';
  }
}

bool MCSectionELF::useCodeAlign() const {
  return getFlags() & ELF::SHF_EXECINSTR;
}

StringRef MCSectionELF::getVirtualSectionKind() const { return "SHT_NOBITS"; }
