//===-- MCObjectFileInfo.cpp - Object File Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSectionDXContainer.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionGOFF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSectionSPIRV.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/Support/Casting.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

static bool useCompactUnwind(const Triple &T) {
  // Only on darwin.
  if (!T.isOSDarwin())
    return false;

  // aarch64 always has it.
  if (T.getArch() == Triple::aarch64 || T.getArch() == Triple::aarch64_32)
    return true;

  // armv7k always has it.
  if (T.isWatchABI())
    return true;

  // Use it on newer version of OS X.
  if (T.isMacOSX() && !T.isMacOSXVersionLT(10, 6))
    return true;

  // And the iOS simulator.
  if (T.isiOS() && T.isX86())
    return true;

  // The rest of the simulators always have it.
  if (T.isSimulatorEnvironment())
    return true;

  // XROS always has it.
  if (T.isXROS())
    return true;

  return false;
}

void MCObjectFileInfo::initMachOMCObjectFileInfo(const Triple &T) {
  // MachO
  SupportsWeakOmittedEHFrame = false;

  EHFrameSection = Ctx->getMachOSection(
      "__TEXT", "__eh_frame",
      MachO::S_COALESCED | MachO::S_ATTR_NO_TOC |
          MachO::S_ATTR_STRIP_STATIC_SYMS | MachO::S_ATTR_LIVE_SUPPORT,
      SectionKind::getReadOnly());

  if (T.isOSDarwin() &&
      (T.getArch() == Triple::aarch64 || T.getArch() == Triple::aarch64_32 ||
      T.isSimulatorEnvironment()))
    SupportsCompactUnwindWithoutEHFrame = true;

  switch (Ctx->emitDwarfUnwindInfo()) {
  case EmitDwarfUnwindType::Always:
    OmitDwarfIfHaveCompactUnwind = false;
    break;
  case EmitDwarfUnwindType::NoCompactUnwind:
    OmitDwarfIfHaveCompactUnwind = true;
    break;
  case EmitDwarfUnwindType::Default:
    OmitDwarfIfHaveCompactUnwind =
        T.isWatchABI() || SupportsCompactUnwindWithoutEHFrame;
    break;
  }

  FDECFIEncoding = dwarf::DW_EH_PE_pcrel;

  TextSection // .text
    = Ctx->getMachOSection("__TEXT", "__text",
                           MachO::S_ATTR_PURE_INSTRUCTIONS,
                           SectionKind::getText());
  DataSection // .data
      = Ctx->getMachOSection("__DATA", "__data", 0, SectionKind::getData());

  // BSSSection might not be expected initialized on msvc.
  BSSSection = nullptr;

  TLSDataSection // .tdata
      = Ctx->getMachOSection("__DATA", "__thread_data",
                             MachO::S_THREAD_LOCAL_REGULAR,
                             SectionKind::getData());
  TLSBSSSection // .tbss
    = Ctx->getMachOSection("__DATA", "__thread_bss",
                           MachO::S_THREAD_LOCAL_ZEROFILL,
                           SectionKind::getThreadBSS());

  // TODO: Verify datarel below.
  TLSTLVSection // .tlv
      = Ctx->getMachOSection("__DATA", "__thread_vars",
                             MachO::S_THREAD_LOCAL_VARIABLES,
                             SectionKind::getData());

  TLSThreadInitSection = Ctx->getMachOSection(
      "__DATA", "__thread_init", MachO::S_THREAD_LOCAL_INIT_FUNCTION_POINTERS,
      SectionKind::getData());

  CStringSection // .cstring
    = Ctx->getMachOSection("__TEXT", "__cstring",
                           MachO::S_CSTRING_LITERALS,
                           SectionKind::getMergeable1ByteCString());
  UStringSection
    = Ctx->getMachOSection("__TEXT","__ustring", 0,
                           SectionKind::getMergeable2ByteCString());
  FourByteConstantSection // .literal4
    = Ctx->getMachOSection("__TEXT", "__literal4",
                           MachO::S_4BYTE_LITERALS,
                           SectionKind::getMergeableConst4());
  EightByteConstantSection // .literal8
    = Ctx->getMachOSection("__TEXT", "__literal8",
                           MachO::S_8BYTE_LITERALS,
                           SectionKind::getMergeableConst8());

  SixteenByteConstantSection // .literal16
      = Ctx->getMachOSection("__TEXT", "__literal16",
                             MachO::S_16BYTE_LITERALS,
                             SectionKind::getMergeableConst16());

  ReadOnlySection  // .const
    = Ctx->getMachOSection("__TEXT", "__const", 0,
                           SectionKind::getReadOnly());

  // If the target is not powerpc, map the coal sections to the non-coal
  // sections.
  //
  // "__TEXT/__textcoal_nt" => section "__TEXT/__text"
  // "__TEXT/__const_coal"  => section "__TEXT/__const"
  // "__DATA/__datacoal_nt" => section "__DATA/__data"
  Triple::ArchType ArchTy = T.getArch();

  ConstDataSection  // .const_data
    = Ctx->getMachOSection("__DATA", "__const", 0,
                           SectionKind::getReadOnlyWithRel());

  if (ArchTy == Triple::ppc || ArchTy == Triple::ppc64) {
    TextCoalSection
      = Ctx->getMachOSection("__TEXT", "__textcoal_nt",
                             MachO::S_COALESCED |
                             MachO::S_ATTR_PURE_INSTRUCTIONS,
                             SectionKind::getText());
    ConstTextCoalSection
      = Ctx->getMachOSection("__TEXT", "__const_coal",
                             MachO::S_COALESCED,
                             SectionKind::getReadOnly());
    DataCoalSection = Ctx->getMachOSection(
        "__DATA", "__datacoal_nt", MachO::S_COALESCED, SectionKind::getData());
    ConstDataCoalSection = DataCoalSection;
  } else {
    TextCoalSection = TextSection;
    ConstTextCoalSection = ReadOnlySection;
    DataCoalSection = DataSection;
    ConstDataCoalSection = ConstDataSection;
  }

  DataCommonSection
    = Ctx->getMachOSection("__DATA","__common",
                           MachO::S_ZEROFILL,
                           SectionKind::getBSS());
  DataBSSSection
    = Ctx->getMachOSection("__DATA","__bss", MachO::S_ZEROFILL,
                           SectionKind::getBSS());


  LazySymbolPointerSection
    = Ctx->getMachOSection("__DATA", "__la_symbol_ptr",
                           MachO::S_LAZY_SYMBOL_POINTERS,
                           SectionKind::getMetadata());
  NonLazySymbolPointerSection
    = Ctx->getMachOSection("__DATA", "__nl_symbol_ptr",
                           MachO::S_NON_LAZY_SYMBOL_POINTERS,
                           SectionKind::getMetadata());

  ThreadLocalPointerSection
    = Ctx->getMachOSection("__DATA", "__thread_ptr",
                           MachO::S_THREAD_LOCAL_VARIABLE_POINTERS,
                           SectionKind::getMetadata());

  AddrSigSection = Ctx->getMachOSection("__DATA", "__llvm_addrsig", 0,
                                        SectionKind::getData());

  // Exception Handling.
  LSDASection = Ctx->getMachOSection("__TEXT", "__gcc_except_tab", 0,
                                     SectionKind::getReadOnlyWithRel());

  COFFDebugSymbolsSection = nullptr;
  COFFDebugTypesSection = nullptr;
  COFFGlobalTypeHashesSection = nullptr;

  if (useCompactUnwind(T)) {
    CompactUnwindSection =
        Ctx->getMachOSection("__LD", "__compact_unwind", MachO::S_ATTR_DEBUG,
                             SectionKind::getReadOnly());

    if (T.isX86())
      CompactUnwindDwarfEHFrameOnly = 0x04000000;  // UNWIND_X86_64_MODE_DWARF
    else if (T.getArch() == Triple::aarch64 || T.getArch() == Triple::aarch64_32)
      CompactUnwindDwarfEHFrameOnly = 0x03000000;  // UNWIND_ARM64_MODE_DWARF
    else if (T.getArch() == Triple::arm || T.getArch() == Triple::thumb)
      CompactUnwindDwarfEHFrameOnly = 0x04000000;  // UNWIND_ARM_MODE_DWARF
  }

  // Debug Information.
  DwarfDebugNamesSection =
      Ctx->getMachOSection("__DWARF", "__debug_names", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_names_begin");
  DwarfAccelNamesSection =
      Ctx->getMachOSection("__DWARF", "__apple_names", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "names_begin");
  DwarfAccelObjCSection =
      Ctx->getMachOSection("__DWARF", "__apple_objc", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "objc_begin");
  // 16 character section limit...
  DwarfAccelNamespaceSection =
      Ctx->getMachOSection("__DWARF", "__apple_namespac", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "namespac_begin");
  DwarfAccelTypesSection =
      Ctx->getMachOSection("__DWARF", "__apple_types", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "types_begin");

  DwarfSwiftASTSection =
      Ctx->getMachOSection("__DWARF", "__swift_ast", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());

  DwarfAbbrevSection =
      Ctx->getMachOSection("__DWARF", "__debug_abbrev", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_abbrev");
  DwarfInfoSection =
      Ctx->getMachOSection("__DWARF", "__debug_info", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_info");
  DwarfLineSection =
      Ctx->getMachOSection("__DWARF", "__debug_line", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_line");
  DwarfLineStrSection =
      Ctx->getMachOSection("__DWARF", "__debug_line_str", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_line_str");
  DwarfFrameSection =
      Ctx->getMachOSection("__DWARF", "__debug_frame", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_frame");
  DwarfPubNamesSection =
      Ctx->getMachOSection("__DWARF", "__debug_pubnames", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfPubTypesSection =
      Ctx->getMachOSection("__DWARF", "__debug_pubtypes", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfGnuPubNamesSection =
      Ctx->getMachOSection("__DWARF", "__debug_gnu_pubn", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfGnuPubTypesSection =
      Ctx->getMachOSection("__DWARF", "__debug_gnu_pubt", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfStrSection =
      Ctx->getMachOSection("__DWARF", "__debug_str", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "info_string");
  DwarfStrOffSection =
      Ctx->getMachOSection("__DWARF", "__debug_str_offs", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_str_off");
  DwarfAddrSection =
      Ctx->getMachOSection("__DWARF", "__debug_addr", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_info");
  DwarfLocSection =
      Ctx->getMachOSection("__DWARF", "__debug_loc", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_debug_loc");
  DwarfLoclistsSection =
      Ctx->getMachOSection("__DWARF", "__debug_loclists", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "section_debug_loc");

  DwarfARangesSection =
      Ctx->getMachOSection("__DWARF", "__debug_aranges", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfRangesSection =
      Ctx->getMachOSection("__DWARF", "__debug_ranges", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_range");
  DwarfRnglistsSection =
      Ctx->getMachOSection("__DWARF", "__debug_rnglists", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_range");
  DwarfMacinfoSection =
      Ctx->getMachOSection("__DWARF", "__debug_macinfo", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_macinfo");
  DwarfMacroSection =
      Ctx->getMachOSection("__DWARF", "__debug_macro", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata(), "debug_macro");
  DwarfDebugInlineSection =
      Ctx->getMachOSection("__DWARF", "__debug_inlined", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfCUIndexSection =
      Ctx->getMachOSection("__DWARF", "__debug_cu_index", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  DwarfTUIndexSection =
      Ctx->getMachOSection("__DWARF", "__debug_tu_index", MachO::S_ATTR_DEBUG,
                           SectionKind::getMetadata());
  StackMapSection = Ctx->getMachOSection("__LLVM_STACKMAPS", "__llvm_stackmaps",
                                         0, SectionKind::getMetadata());

  FaultMapSection = Ctx->getMachOSection("__LLVM_FAULTMAPS", "__llvm_faultmaps",
                                         0, SectionKind::getMetadata());

  RemarksSection = Ctx->getMachOSection(
      "__LLVM", "__remarks", MachO::S_ATTR_DEBUG, SectionKind::getMetadata());

  // The architecture of dsymutil makes it very difficult to copy the Swift
  // reflection metadata sections into the __TEXT segment, so dsymutil creates
  // these sections in the __DWARF segment instead.
  if (!Ctx->getSwift5ReflectionSegmentName().empty()) {
#define HANDLE_SWIFT_SECTION(KIND, MACHO, ELF, COFF)                           \
  Swift5ReflectionSections                                                     \
      [llvm::binaryformat::Swift5ReflectionSectionKind::KIND] =                \
          Ctx->getMachOSection(Ctx->getSwift5ReflectionSegmentName().data(),   \
                               MACHO, 0, SectionKind::getMetadata());
#include "llvm/BinaryFormat/Swift.def"
  }

  TLSExtraDataSection = TLSTLVSection;
}

void MCObjectFileInfo::initELFMCObjectFileInfo(const Triple &T, bool Large) {
  switch (T.getArch()) {
  case Triple::mips:
  case Triple::mipsel:
  case Triple::mips64:
  case Triple::mips64el:
    // We cannot use DW_EH_PE_sdata8 for the large PositionIndependent case
    // since there is no R_MIPS_PC64 relocation (only a 32-bit version).
    // In fact DW_EH_PE_sdata4 is enough for us now, and GNU ld doesn't
    // support pcrel|sdata8 well. Let's use sdata4 for now.
    if (PositionIndependent)
      FDECFIEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
    else
      FDECFIEncoding = Ctx->getAsmInfo()->getCodePointerSize() == 4
                           ? dwarf::DW_EH_PE_sdata4
                           : dwarf::DW_EH_PE_sdata8;
    break;
  case Triple::ppc64:
  case Triple::ppc64le:
  case Triple::aarch64:
  case Triple::aarch64_be:
  case Triple::x86_64:
    FDECFIEncoding = dwarf::DW_EH_PE_pcrel |
                     (Large ? dwarf::DW_EH_PE_sdata8 : dwarf::DW_EH_PE_sdata4);
    break;
  case Triple::bpfel:
  case Triple::bpfeb:
    FDECFIEncoding = dwarf::DW_EH_PE_sdata8;
    break;
  case Triple::hexagon:
    FDECFIEncoding =
        PositionIndependent ? dwarf::DW_EH_PE_pcrel : dwarf::DW_EH_PE_absptr;
    break;
  case Triple::xtensa:
    FDECFIEncoding = dwarf::DW_EH_PE_sdata4;
    break;
  default:
    FDECFIEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
    break;
  }

  unsigned EHSectionType = T.getArch() == Triple::x86_64
                               ? ELF::SHT_X86_64_UNWIND
                               : ELF::SHT_PROGBITS;

  // Solaris requires different flags for .eh_frame to seemingly every other
  // platform.
  unsigned EHSectionFlags = ELF::SHF_ALLOC;
  if (T.isOSSolaris() && T.getArch() != Triple::x86_64)
    EHSectionFlags |= ELF::SHF_WRITE;

  // ELF
  BSSSection = Ctx->getELFSection(".bss", ELF::SHT_NOBITS,
                                  ELF::SHF_WRITE | ELF::SHF_ALLOC);

  TextSection = Ctx->getELFSection(".text", ELF::SHT_PROGBITS,
                                   ELF::SHF_EXECINSTR | ELF::SHF_ALLOC);

  DataSection = Ctx->getELFSection(".data", ELF::SHT_PROGBITS,
                                   ELF::SHF_WRITE | ELF::SHF_ALLOC);

  ReadOnlySection =
      Ctx->getELFSection(".rodata", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);

  TLSDataSection =
      Ctx->getELFSection(".tdata", ELF::SHT_PROGBITS,
                         ELF::SHF_ALLOC | ELF::SHF_TLS | ELF::SHF_WRITE);

  TLSBSSSection = Ctx->getELFSection(
      ".tbss", ELF::SHT_NOBITS, ELF::SHF_ALLOC | ELF::SHF_TLS | ELF::SHF_WRITE);

  DataRelROSection = Ctx->getELFSection(".data.rel.ro", ELF::SHT_PROGBITS,
                                        ELF::SHF_ALLOC | ELF::SHF_WRITE);

  MergeableConst4Section =
      Ctx->getELFSection(".rodata.cst4", ELF::SHT_PROGBITS,
                         ELF::SHF_ALLOC | ELF::SHF_MERGE, 4);

  MergeableConst8Section =
      Ctx->getELFSection(".rodata.cst8", ELF::SHT_PROGBITS,
                         ELF::SHF_ALLOC | ELF::SHF_MERGE, 8);

  MergeableConst16Section =
      Ctx->getELFSection(".rodata.cst16", ELF::SHT_PROGBITS,
                         ELF::SHF_ALLOC | ELF::SHF_MERGE, 16);

  MergeableConst32Section =
      Ctx->getELFSection(".rodata.cst32", ELF::SHT_PROGBITS,
                         ELF::SHF_ALLOC | ELF::SHF_MERGE, 32);

  // Exception Handling Sections.

  // FIXME: We're emitting LSDA info into a readonly section on ELF, even though
  // it contains relocatable pointers.  In PIC mode, this is probably a big
  // runtime hit for C++ apps.  Either the contents of the LSDA need to be
  // adjusted or this should be a data section.
  LSDASection = Ctx->getELFSection(".gcc_except_table", ELF::SHT_PROGBITS,
                                   ELF::SHF_ALLOC);

  COFFDebugSymbolsSection = nullptr;
  COFFDebugTypesSection = nullptr;

  unsigned DebugSecType = ELF::SHT_PROGBITS;

  // MIPS .debug_* sections should have SHT_MIPS_DWARF section type
  // to distinguish among sections contain DWARF and ECOFF debug formats.
  // Sections with ECOFF debug format are obsoleted and marked by SHT_PROGBITS.
  if (T.isMIPS())
    DebugSecType = ELF::SHT_MIPS_DWARF;

  // Debug Info Sections.
  DwarfAbbrevSection =
      Ctx->getELFSection(".debug_abbrev", DebugSecType, 0);
  DwarfInfoSection = Ctx->getELFSection(".debug_info", DebugSecType, 0);
  DwarfLineSection = Ctx->getELFSection(".debug_line", DebugSecType, 0);
  DwarfLineStrSection =
      Ctx->getELFSection(".debug_line_str", DebugSecType,
                         ELF::SHF_MERGE | ELF::SHF_STRINGS, 1);
  DwarfFrameSection = Ctx->getELFSection(".debug_frame", DebugSecType, 0);
  DwarfPubNamesSection =
      Ctx->getELFSection(".debug_pubnames", DebugSecType, 0);
  DwarfPubTypesSection =
      Ctx->getELFSection(".debug_pubtypes", DebugSecType, 0);
  DwarfGnuPubNamesSection =
      Ctx->getELFSection(".debug_gnu_pubnames", DebugSecType, 0);
  DwarfGnuPubTypesSection =
      Ctx->getELFSection(".debug_gnu_pubtypes", DebugSecType, 0);
  DwarfStrSection =
      Ctx->getELFSection(".debug_str", DebugSecType,
                         ELF::SHF_MERGE | ELF::SHF_STRINGS, 1);
  DwarfLocSection = Ctx->getELFSection(".debug_loc", DebugSecType, 0);
  DwarfARangesSection =
      Ctx->getELFSection(".debug_aranges", DebugSecType, 0);
  DwarfRangesSection =
      Ctx->getELFSection(".debug_ranges", DebugSecType, 0);
  DwarfMacinfoSection =
      Ctx->getELFSection(".debug_macinfo", DebugSecType, 0);
  DwarfMacroSection = Ctx->getELFSection(".debug_macro", DebugSecType, 0);

  // DWARF5 Experimental Debug Info

  // Accelerator Tables
  DwarfDebugNamesSection =
      Ctx->getELFSection(".debug_names", ELF::SHT_PROGBITS, 0);
  DwarfAccelNamesSection =
      Ctx->getELFSection(".apple_names", ELF::SHT_PROGBITS, 0);
  DwarfAccelObjCSection =
      Ctx->getELFSection(".apple_objc", ELF::SHT_PROGBITS, 0);
  DwarfAccelNamespaceSection =
      Ctx->getELFSection(".apple_namespaces", ELF::SHT_PROGBITS, 0);
  DwarfAccelTypesSection =
      Ctx->getELFSection(".apple_types", ELF::SHT_PROGBITS, 0);

  // String Offset and Address Sections
  DwarfStrOffSection =
      Ctx->getELFSection(".debug_str_offsets", DebugSecType, 0);
  DwarfAddrSection = Ctx->getELFSection(".debug_addr", DebugSecType, 0);
  DwarfRnglistsSection = Ctx->getELFSection(".debug_rnglists", DebugSecType, 0);
  DwarfLoclistsSection = Ctx->getELFSection(".debug_loclists", DebugSecType, 0);

  // Fission Sections
  DwarfInfoDWOSection =
      Ctx->getELFSection(".debug_info.dwo", DebugSecType, ELF::SHF_EXCLUDE);
  DwarfTypesDWOSection =
      Ctx->getELFSection(".debug_types.dwo", DebugSecType, ELF::SHF_EXCLUDE);
  DwarfAbbrevDWOSection =
      Ctx->getELFSection(".debug_abbrev.dwo", DebugSecType, ELF::SHF_EXCLUDE);
  DwarfStrDWOSection = Ctx->getELFSection(
      ".debug_str.dwo", DebugSecType,
      ELF::SHF_MERGE | ELF::SHF_STRINGS | ELF::SHF_EXCLUDE, 1);
  DwarfLineDWOSection =
      Ctx->getELFSection(".debug_line.dwo", DebugSecType, ELF::SHF_EXCLUDE);
  DwarfLocDWOSection =
      Ctx->getELFSection(".debug_loc.dwo", DebugSecType, ELF::SHF_EXCLUDE);
  DwarfStrOffDWOSection = Ctx->getELFSection(".debug_str_offsets.dwo",
                                             DebugSecType, ELF::SHF_EXCLUDE);
  DwarfRnglistsDWOSection =
      Ctx->getELFSection(".debug_rnglists.dwo", DebugSecType, ELF::SHF_EXCLUDE);
  DwarfMacinfoDWOSection =
      Ctx->getELFSection(".debug_macinfo.dwo", DebugSecType, ELF::SHF_EXCLUDE);
  DwarfMacroDWOSection =
      Ctx->getELFSection(".debug_macro.dwo", DebugSecType, ELF::SHF_EXCLUDE);

  DwarfLoclistsDWOSection =
      Ctx->getELFSection(".debug_loclists.dwo", DebugSecType, ELF::SHF_EXCLUDE);

  // DWP Sections
  DwarfCUIndexSection =
      Ctx->getELFSection(".debug_cu_index", DebugSecType, 0);
  DwarfTUIndexSection =
      Ctx->getELFSection(".debug_tu_index", DebugSecType, 0);

  StackMapSection =
      Ctx->getELFSection(".llvm_stackmaps", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);

  FaultMapSection =
      Ctx->getELFSection(".llvm_faultmaps", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);

  EHFrameSection =
      Ctx->getELFSection(".eh_frame", EHSectionType, EHSectionFlags);

  StackSizesSection = Ctx->getELFSection(".stack_sizes", ELF::SHT_PROGBITS, 0);

  PseudoProbeSection = Ctx->getELFSection(".pseudo_probe", DebugSecType, 0);
  PseudoProbeDescSection =
      Ctx->getELFSection(".pseudo_probe_desc", DebugSecType, 0);

  LLVMStatsSection = Ctx->getELFSection(".llvm_stats", ELF::SHT_PROGBITS, 0);
}

void MCObjectFileInfo::initGOFFMCObjectFileInfo(const Triple &T) {
  TextSection = Ctx->getGOFFSection(".text", SectionKind::getText(), nullptr);
  BSSSection = Ctx->getGOFFSection(".bss", SectionKind::getBSS(), nullptr);
  PPA1Section = Ctx->getGOFFSection(".ppa1", SectionKind::getMetadata(),
                                    TextSection, GOFF::SK_PPA1);
  PPA2Section = Ctx->getGOFFSection(".ppa2", SectionKind::getMetadata(),
                                    TextSection, GOFF::SK_PPA2);

  PPA2ListSection =
      Ctx->getGOFFSection(".ppa2list", SectionKind::getData(), nullptr);

  ADASection = Ctx->getGOFFSection(".ada", SectionKind::getData(), nullptr);
  IDRLSection = Ctx->getGOFFSection("B_IDRL", SectionKind::getData(), nullptr);
}

void MCObjectFileInfo::initCOFFMCObjectFileInfo(const Triple &T) {
  EHFrameSection =
      Ctx->getCOFFSection(".eh_frame", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                           COFF::IMAGE_SCN_MEM_READ);

  // Set the `IMAGE_SCN_MEM_16BIT` flag when compiling for thumb mode.  This is
  // used to indicate to the linker that the text segment contains thumb instructions
  // and to set the ISA selection bit for calls accordingly.
  const bool IsThumb = T.getArch() == Triple::thumb;

  // COFF
  BSSSection = Ctx->getCOFFSection(
      ".bss", COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA |
                  COFF::IMAGE_SCN_MEM_READ | COFF::IMAGE_SCN_MEM_WRITE);
  TextSection = Ctx->getCOFFSection(
      ".text",
      (IsThumb ? COFF::IMAGE_SCN_MEM_16BIT : (COFF::SectionCharacteristics)0) |
          COFF::IMAGE_SCN_CNT_CODE | COFF::IMAGE_SCN_MEM_EXECUTE |
          COFF::IMAGE_SCN_MEM_READ);
  DataSection = Ctx->getCOFFSection(
      ".data", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA | COFF::IMAGE_SCN_MEM_READ |
                   COFF::IMAGE_SCN_MEM_WRITE);
  ReadOnlySection =
      Ctx->getCOFFSection(".rdata", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                        COFF::IMAGE_SCN_MEM_READ);

  if (T.getArch() == Triple::x86_64 || T.getArch() == Triple::aarch64 ||
      T.getArch() == Triple::arm || T.getArch() == Triple::thumb) {
    // On Windows with SEH, the LSDA is emitted into the .xdata section
    LSDASection = nullptr;
  } else {
    LSDASection = Ctx->getCOFFSection(".gcc_except_table",
                                      COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                          COFF::IMAGE_SCN_MEM_READ);
  }

  // Debug info.
  COFFDebugSymbolsSection =
      Ctx->getCOFFSection(".debug$S", (COFF::IMAGE_SCN_MEM_DISCARDABLE |
                                       COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                       COFF::IMAGE_SCN_MEM_READ));
  COFFDebugTypesSection =
      Ctx->getCOFFSection(".debug$T", (COFF::IMAGE_SCN_MEM_DISCARDABLE |
                                       COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                       COFF::IMAGE_SCN_MEM_READ));
  COFFGlobalTypeHashesSection =
      Ctx->getCOFFSection(".debug$H", (COFF::IMAGE_SCN_MEM_DISCARDABLE |
                                       COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                       COFF::IMAGE_SCN_MEM_READ));

  DwarfAbbrevSection = Ctx->getCOFFSection(
      ".debug_abbrev", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                           COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                           COFF::IMAGE_SCN_MEM_READ);
  DwarfInfoSection = Ctx->getCOFFSection(
      ".debug_info", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                         COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                         COFF::IMAGE_SCN_MEM_READ);
  DwarfLineSection = Ctx->getCOFFSection(
      ".debug_line", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                         COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                         COFF::IMAGE_SCN_MEM_READ);
  DwarfLineStrSection = Ctx->getCOFFSection(
      ".debug_line_str", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                             COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                             COFF::IMAGE_SCN_MEM_READ);
  DwarfFrameSection = Ctx->getCOFFSection(
      ".debug_frame", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                          COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                          COFF::IMAGE_SCN_MEM_READ);
  DwarfPubNamesSection = Ctx->getCOFFSection(
      ".debug_pubnames", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                             COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                             COFF::IMAGE_SCN_MEM_READ);
  DwarfPubTypesSection = Ctx->getCOFFSection(
      ".debug_pubtypes", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                             COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                             COFF::IMAGE_SCN_MEM_READ);
  DwarfGnuPubNamesSection = Ctx->getCOFFSection(
      ".debug_gnu_pubnames", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                                 COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                 COFF::IMAGE_SCN_MEM_READ);
  DwarfGnuPubTypesSection = Ctx->getCOFFSection(
      ".debug_gnu_pubtypes", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                                 COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                 COFF::IMAGE_SCN_MEM_READ);
  DwarfStrSection = Ctx->getCOFFSection(
      ".debug_str", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                        COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                        COFF::IMAGE_SCN_MEM_READ);
  DwarfStrOffSection = Ctx->getCOFFSection(
      ".debug_str_offsets", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                                COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                COFF::IMAGE_SCN_MEM_READ);
  DwarfLocSection = Ctx->getCOFFSection(
      ".debug_loc", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                        COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                        COFF::IMAGE_SCN_MEM_READ);
  DwarfLoclistsSection = Ctx->getCOFFSection(
      ".debug_loclists", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                             COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                             COFF::IMAGE_SCN_MEM_READ);
  DwarfARangesSection = Ctx->getCOFFSection(
      ".debug_aranges", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                            COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                            COFF::IMAGE_SCN_MEM_READ);
  DwarfRangesSection = Ctx->getCOFFSection(
      ".debug_ranges", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                           COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                           COFF::IMAGE_SCN_MEM_READ);
  DwarfRnglistsSection = Ctx->getCOFFSection(
      ".debug_rnglists", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                             COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                             COFF::IMAGE_SCN_MEM_READ);
  DwarfMacinfoSection = Ctx->getCOFFSection(
      ".debug_macinfo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                            COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                            COFF::IMAGE_SCN_MEM_READ);
  DwarfMacroSection = Ctx->getCOFFSection(
      ".debug_macro", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                          COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                          COFF::IMAGE_SCN_MEM_READ);
  DwarfMacinfoDWOSection = Ctx->getCOFFSection(
      ".debug_macinfo.dwo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                                COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                COFF::IMAGE_SCN_MEM_READ);
  DwarfMacroDWOSection = Ctx->getCOFFSection(
      ".debug_macro.dwo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                              COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                              COFF::IMAGE_SCN_MEM_READ);
  DwarfInfoDWOSection = Ctx->getCOFFSection(
      ".debug_info.dwo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                             COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                             COFF::IMAGE_SCN_MEM_READ);
  DwarfTypesDWOSection = Ctx->getCOFFSection(
      ".debug_types.dwo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                              COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                              COFF::IMAGE_SCN_MEM_READ);
  DwarfAbbrevDWOSection = Ctx->getCOFFSection(
      ".debug_abbrev.dwo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                               COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                               COFF::IMAGE_SCN_MEM_READ);
  DwarfStrDWOSection = Ctx->getCOFFSection(
      ".debug_str.dwo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                            COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                            COFF::IMAGE_SCN_MEM_READ);
  DwarfLineDWOSection = Ctx->getCOFFSection(
      ".debug_line.dwo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                             COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                             COFF::IMAGE_SCN_MEM_READ);
  DwarfLocDWOSection = Ctx->getCOFFSection(
      ".debug_loc.dwo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                            COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                            COFF::IMAGE_SCN_MEM_READ);
  DwarfStrOffDWOSection = Ctx->getCOFFSection(
      ".debug_str_offsets.dwo", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                                    COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                    COFF::IMAGE_SCN_MEM_READ);
  DwarfAddrSection = Ctx->getCOFFSection(
      ".debug_addr", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                         COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                         COFF::IMAGE_SCN_MEM_READ);
  DwarfCUIndexSection = Ctx->getCOFFSection(
      ".debug_cu_index", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                             COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                             COFF::IMAGE_SCN_MEM_READ);
  DwarfTUIndexSection = Ctx->getCOFFSection(
      ".debug_tu_index", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                             COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                             COFF::IMAGE_SCN_MEM_READ);
  DwarfDebugNamesSection = Ctx->getCOFFSection(
      ".debug_names", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                          COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                          COFF::IMAGE_SCN_MEM_READ);
  DwarfAccelNamesSection = Ctx->getCOFFSection(
      ".apple_names", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                          COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                          COFF::IMAGE_SCN_MEM_READ);
  DwarfAccelNamespaceSection = Ctx->getCOFFSection(
      ".apple_namespaces", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                               COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                               COFF::IMAGE_SCN_MEM_READ);
  DwarfAccelTypesSection = Ctx->getCOFFSection(
      ".apple_types", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                          COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                          COFF::IMAGE_SCN_MEM_READ);
  DwarfAccelObjCSection = Ctx->getCOFFSection(
      ".apple_objc", COFF::IMAGE_SCN_MEM_DISCARDABLE |
                         COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                         COFF::IMAGE_SCN_MEM_READ);

  DrectveSection = Ctx->getCOFFSection(
      ".drectve", COFF::IMAGE_SCN_LNK_INFO | COFF::IMAGE_SCN_LNK_REMOVE);

  PDataSection =
      Ctx->getCOFFSection(".pdata", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                        COFF::IMAGE_SCN_MEM_READ);

  XDataSection =
      Ctx->getCOFFSection(".xdata", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                        COFF::IMAGE_SCN_MEM_READ);

  SXDataSection = Ctx->getCOFFSection(".sxdata", COFF::IMAGE_SCN_LNK_INFO);

  GEHContSection =
      Ctx->getCOFFSection(".gehcont$y", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                            COFF::IMAGE_SCN_MEM_READ);

  GFIDsSection =
      Ctx->getCOFFSection(".gfids$y", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                          COFF::IMAGE_SCN_MEM_READ);

  GIATsSection =
      Ctx->getCOFFSection(".giats$y", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                          COFF::IMAGE_SCN_MEM_READ);

  GLJMPSection =
      Ctx->getCOFFSection(".gljmp$y", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                          COFF::IMAGE_SCN_MEM_READ);

  TLSDataSection = Ctx->getCOFFSection(
      ".tls$", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA | COFF::IMAGE_SCN_MEM_READ |
                   COFF::IMAGE_SCN_MEM_WRITE);

  StackMapSection = Ctx->getCOFFSection(".llvm_stackmaps",
                                        COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                            COFF::IMAGE_SCN_MEM_READ);
}

void MCObjectFileInfo::initSPIRVMCObjectFileInfo(const Triple &T) {
  // Put everything in a single binary section.
  TextSection = Ctx->getSPIRVSection();
}

void MCObjectFileInfo::initWasmMCObjectFileInfo(const Triple &T) {
  TextSection = Ctx->getWasmSection(".text", SectionKind::getText());
  DataSection = Ctx->getWasmSection(".data", SectionKind::getData());

  DwarfLineSection =
      Ctx->getWasmSection(".debug_line", SectionKind::getMetadata());
  DwarfLineStrSection =
      Ctx->getWasmSection(".debug_line_str", SectionKind::getMetadata(),
                          wasm::WASM_SEG_FLAG_STRINGS);
  DwarfStrSection = Ctx->getWasmSection(
      ".debug_str", SectionKind::getMetadata(), wasm::WASM_SEG_FLAG_STRINGS);
  DwarfLocSection =
      Ctx->getWasmSection(".debug_loc", SectionKind::getMetadata());
  DwarfAbbrevSection =
      Ctx->getWasmSection(".debug_abbrev", SectionKind::getMetadata());
  DwarfARangesSection = Ctx->getWasmSection(".debug_aranges", SectionKind::getMetadata());
  DwarfRangesSection =
      Ctx->getWasmSection(".debug_ranges", SectionKind::getMetadata());
  DwarfMacinfoSection =
      Ctx->getWasmSection(".debug_macinfo", SectionKind::getMetadata());
  DwarfMacroSection =
      Ctx->getWasmSection(".debug_macro", SectionKind::getMetadata());
  DwarfCUIndexSection = Ctx->getWasmSection(".debug_cu_index", SectionKind::getMetadata());
  DwarfTUIndexSection = Ctx->getWasmSection(".debug_tu_index", SectionKind::getMetadata());
  DwarfInfoSection =
      Ctx->getWasmSection(".debug_info", SectionKind::getMetadata());
  DwarfFrameSection = Ctx->getWasmSection(".debug_frame", SectionKind::getMetadata());
  DwarfPubNamesSection = Ctx->getWasmSection(".debug_pubnames", SectionKind::getMetadata());
  DwarfPubTypesSection = Ctx->getWasmSection(".debug_pubtypes", SectionKind::getMetadata());
  DwarfGnuPubNamesSection =
      Ctx->getWasmSection(".debug_gnu_pubnames", SectionKind::getMetadata());
  DwarfGnuPubTypesSection =
      Ctx->getWasmSection(".debug_gnu_pubtypes", SectionKind::getMetadata());

  DwarfDebugNamesSection =
      Ctx->getWasmSection(".debug_names", SectionKind::getMetadata());
  DwarfStrOffSection =
      Ctx->getWasmSection(".debug_str_offsets", SectionKind::getMetadata());
  DwarfAddrSection =
      Ctx->getWasmSection(".debug_addr", SectionKind::getMetadata());
  DwarfRnglistsSection =
      Ctx->getWasmSection(".debug_rnglists", SectionKind::getMetadata());
  DwarfLoclistsSection =
      Ctx->getWasmSection(".debug_loclists", SectionKind::getMetadata());

  // Fission Sections
  DwarfInfoDWOSection =
      Ctx->getWasmSection(".debug_info.dwo", SectionKind::getMetadata());
  DwarfTypesDWOSection =
      Ctx->getWasmSection(".debug_types.dwo", SectionKind::getMetadata());
  DwarfAbbrevDWOSection =
      Ctx->getWasmSection(".debug_abbrev.dwo", SectionKind::getMetadata());
  DwarfStrDWOSection =
      Ctx->getWasmSection(".debug_str.dwo", SectionKind::getMetadata(),
                          wasm::WASM_SEG_FLAG_STRINGS);
  DwarfLineDWOSection =
      Ctx->getWasmSection(".debug_line.dwo", SectionKind::getMetadata());
  DwarfLocDWOSection =
      Ctx->getWasmSection(".debug_loc.dwo", SectionKind::getMetadata());
  DwarfStrOffDWOSection =
      Ctx->getWasmSection(".debug_str_offsets.dwo", SectionKind::getMetadata());
  DwarfRnglistsDWOSection =
      Ctx->getWasmSection(".debug_rnglists.dwo", SectionKind::getMetadata());
  DwarfMacinfoDWOSection =
      Ctx->getWasmSection(".debug_macinfo.dwo", SectionKind::getMetadata());
  DwarfMacroDWOSection =
      Ctx->getWasmSection(".debug_macro.dwo", SectionKind::getMetadata());

  DwarfLoclistsDWOSection =
      Ctx->getWasmSection(".debug_loclists.dwo", SectionKind::getMetadata());

  // DWP Sections
  DwarfCUIndexSection =
      Ctx->getWasmSection(".debug_cu_index", SectionKind::getMetadata());
  DwarfTUIndexSection =
      Ctx->getWasmSection(".debug_tu_index", SectionKind::getMetadata());

  // Wasm use data section for LSDA.
  // TODO Consider putting each function's exception table in a separate
  // section, as in -function-sections, to facilitate lld's --gc-section.
  LSDASection = Ctx->getWasmSection(".rodata.gcc_except_table",
                                    SectionKind::getReadOnlyWithRel());

  // TODO: Define more sections.
}

void MCObjectFileInfo::initXCOFFMCObjectFileInfo(const Triple &T) {
  // The default csect for program code. Functions without a specified section
  // get placed into this csect. The choice of csect name is not a property of
  // the ABI or object file format, but various tools rely on the section
  // name being empty (considering named symbols to be "user symbol names").
  TextSection = Ctx->getXCOFFSection(
      "..text..", // Use a non-null name to work around an AIX assembler bug...
      SectionKind::getText(),
      XCOFF::CsectProperties(XCOFF::StorageMappingClass::XMC_PR, XCOFF::XTY_SD),
      /* MultiSymbolsAllowed*/ true);

  // ... but use a null name when generating the symbol table.
  MCSectionXCOFF *TS = static_cast<MCSectionXCOFF *>(TextSection);
  TS->getQualNameSymbol()->setSymbolTableName("");
  TS->setSymbolTableName("");

  DataSection = Ctx->getXCOFFSection(
      ".data", SectionKind::getData(),
      XCOFF::CsectProperties(XCOFF::StorageMappingClass::XMC_RW, XCOFF::XTY_SD),
      /* MultiSymbolsAllowed*/ true);

  ReadOnlySection = Ctx->getXCOFFSection(
      ".rodata", SectionKind::getReadOnly(),
      XCOFF::CsectProperties(XCOFF::StorageMappingClass::XMC_RO, XCOFF::XTY_SD),
      /* MultiSymbolsAllowed*/ true);
  ReadOnlySection->setAlignment(Align(4));

  ReadOnly8Section = Ctx->getXCOFFSection(
      ".rodata.8", SectionKind::getReadOnly(),
      XCOFF::CsectProperties(XCOFF::StorageMappingClass::XMC_RO, XCOFF::XTY_SD),
      /* MultiSymbolsAllowed*/ true);
  ReadOnly8Section->setAlignment(Align(8));

  ReadOnly16Section = Ctx->getXCOFFSection(
      ".rodata.16", SectionKind::getReadOnly(),
      XCOFF::CsectProperties(XCOFF::StorageMappingClass::XMC_RO, XCOFF::XTY_SD),
      /* MultiSymbolsAllowed*/ true);
  ReadOnly16Section->setAlignment(Align(16));

  TLSDataSection = Ctx->getXCOFFSection(
      ".tdata", SectionKind::getThreadData(),
      XCOFF::CsectProperties(XCOFF::StorageMappingClass::XMC_TL, XCOFF::XTY_SD),
      /* MultiSymbolsAllowed*/ true);

  TOCBaseSection = Ctx->getXCOFFSection(
      "TOC", SectionKind::getData(),
      XCOFF::CsectProperties(XCOFF::StorageMappingClass::XMC_TC0,
                             XCOFF::XTY_SD));

  // The TOC-base always has 0 size, but 4 byte alignment.
  TOCBaseSection->setAlignment(Align(4));

  LSDASection = Ctx->getXCOFFSection(
      ".gcc_except_table", SectionKind::getReadOnly(),
      XCOFF::CsectProperties(XCOFF::StorageMappingClass::XMC_RO,
                             XCOFF::XTY_SD));

  CompactUnwindSection = Ctx->getXCOFFSection(
      ".eh_info_table", SectionKind::getData(),
      XCOFF::CsectProperties(XCOFF::StorageMappingClass::XMC_RW,
                             XCOFF::XTY_SD));

  // DWARF sections for XCOFF are not csects. They are special STYP_DWARF
  // sections, and the individual DWARF sections are distinguished by their
  // section subtype.
  DwarfAbbrevSection = Ctx->getXCOFFSection(
      ".dwabrev", SectionKind::getMetadata(),
      /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWABREV);

  DwarfInfoSection = Ctx->getXCOFFSection(
      ".dwinfo", SectionKind::getMetadata(), /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWINFO);

  DwarfLineSection = Ctx->getXCOFFSection(
      ".dwline", SectionKind::getMetadata(), /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWLINE);

  DwarfFrameSection = Ctx->getXCOFFSection(
      ".dwframe", SectionKind::getMetadata(),
      /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWFRAME);

  DwarfPubNamesSection = Ctx->getXCOFFSection(
      ".dwpbnms", SectionKind::getMetadata(),
      /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWPBNMS);

  DwarfPubTypesSection = Ctx->getXCOFFSection(
      ".dwpbtyp", SectionKind::getMetadata(),
      /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWPBTYP);

  DwarfStrSection = Ctx->getXCOFFSection(
      ".dwstr", SectionKind::getMetadata(), /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWSTR);

  DwarfLocSection = Ctx->getXCOFFSection(
      ".dwloc", SectionKind::getMetadata(), /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWLOC);

  DwarfARangesSection = Ctx->getXCOFFSection(
      ".dwarnge", SectionKind::getMetadata(),
      /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWARNGE);

  DwarfRangesSection = Ctx->getXCOFFSection(
      ".dwrnges", SectionKind::getMetadata(),
      /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWRNGES);

  DwarfMacinfoSection = Ctx->getXCOFFSection(
      ".dwmac", SectionKind::getMetadata(), /* CsectProperties */ std::nullopt,
      /* MultiSymbolsAllowed */ true, XCOFF::SSUBTYP_DWMAC);
}

void MCObjectFileInfo::initDXContainerObjectFileInfo(const Triple &T) {
  // At the moment the DXBC section should end up empty.
  TextSection = Ctx->getDXContainerSection("DXBC", SectionKind::getText());
}

MCObjectFileInfo::~MCObjectFileInfo() = default;

void MCObjectFileInfo::initMCObjectFileInfo(MCContext &MCCtx, bool PIC,
                                            bool LargeCodeModel) {
  PositionIndependent = PIC;
  Ctx = &MCCtx;

  // Common.
  SupportsWeakOmittedEHFrame = true;
  SupportsCompactUnwindWithoutEHFrame = false;
  OmitDwarfIfHaveCompactUnwind = false;

  FDECFIEncoding = dwarf::DW_EH_PE_absptr;

  CompactUnwindDwarfEHFrameOnly = 0;

  EHFrameSection = nullptr;             // Created on demand.
  CompactUnwindSection = nullptr;       // Used only by selected targets.
  DwarfAccelNamesSection = nullptr;     // Used only by selected targets.
  DwarfAccelObjCSection = nullptr;      // Used only by selected targets.
  DwarfAccelNamespaceSection = nullptr; // Used only by selected targets.
  DwarfAccelTypesSection = nullptr;     // Used only by selected targets.

  Triple TheTriple = Ctx->getTargetTriple();
  switch (Ctx->getObjectFileType()) {
  case MCContext::IsMachO:
    initMachOMCObjectFileInfo(TheTriple);
    break;
  case MCContext::IsCOFF:
    initCOFFMCObjectFileInfo(TheTriple);
    break;
  case MCContext::IsELF:
    initELFMCObjectFileInfo(TheTriple, LargeCodeModel);
    break;
  case MCContext::IsGOFF:
    initGOFFMCObjectFileInfo(TheTriple);
    break;
  case MCContext::IsSPIRV:
    initSPIRVMCObjectFileInfo(TheTriple);
    break;
  case MCContext::IsWasm:
    initWasmMCObjectFileInfo(TheTriple);
    break;
  case MCContext::IsXCOFF:
    initXCOFFMCObjectFileInfo(TheTriple);
    break;
  case MCContext::IsDXContainer:
    initDXContainerObjectFileInfo(TheTriple);
    break;
  }
}

MCSection *MCObjectFileInfo::getDwarfComdatSection(const char *Name,
                                                   uint64_t Hash) const {
  switch (Ctx->getTargetTriple().getObjectFormat()) {
  case Triple::ELF:
    return Ctx->getELFSection(Name, ELF::SHT_PROGBITS, ELF::SHF_GROUP, 0,
                              utostr(Hash), /*IsComdat=*/true);
  case Triple::Wasm:
    return Ctx->getWasmSection(Name, SectionKind::getMetadata(), 0,
                               utostr(Hash), MCContext::GenericSectionID);
  case Triple::MachO:
  case Triple::COFF:
  case Triple::GOFF:
  case Triple::SPIRV:
  case Triple::XCOFF:
  case Triple::DXContainer:
  case Triple::UnknownObjectFormat:
    report_fatal_error("Cannot get DWARF comdat section for this object file "
                       "format: not implemented.");
    break;
  }
  llvm_unreachable("Unknown ObjectFormatType");
}

MCSection *
MCObjectFileInfo::getStackSizesSection(const MCSection &TextSec) const {
  if ((Ctx->getObjectFileType() != MCContext::IsELF) ||
      Ctx->getTargetTriple().isPS4())
    return StackSizesSection;

  const MCSectionELF &ElfSec = static_cast<const MCSectionELF &>(TextSec);
  unsigned Flags = ELF::SHF_LINK_ORDER;
  StringRef GroupName;
  if (const MCSymbol *Group = ElfSec.getGroup()) {
    GroupName = Group->getName();
    Flags |= ELF::SHF_GROUP;
  }

  return Ctx->getELFSection(".stack_sizes", ELF::SHT_PROGBITS, Flags, 0,
                            GroupName, true, ElfSec.getUniqueID(),
                            cast<MCSymbolELF>(TextSec.getBeginSymbol()));
}

MCSection *
MCObjectFileInfo::getBBAddrMapSection(const MCSection &TextSec) const {
  if (Ctx->getObjectFileType() != MCContext::IsELF)
    return nullptr;

  const MCSectionELF &ElfSec = static_cast<const MCSectionELF &>(TextSec);
  unsigned Flags = ELF::SHF_LINK_ORDER;
  StringRef GroupName;
  if (const MCSymbol *Group = ElfSec.getGroup()) {
    GroupName = Group->getName();
    Flags |= ELF::SHF_GROUP;
  }

  // Use the text section's begin symbol and unique ID to create a separate
  // .llvm_bb_addr_map section associated with every unique text section.
  return Ctx->getELFSection(".llvm_bb_addr_map", ELF::SHT_LLVM_BB_ADDR_MAP,
                            Flags, 0, GroupName, true, ElfSec.getUniqueID(),
                            cast<MCSymbolELF>(TextSec.getBeginSymbol()));
}

MCSection *
MCObjectFileInfo::getKCFITrapSection(const MCSection &TextSec) const {
  if (Ctx->getObjectFileType() != MCContext::IsELF)
    return nullptr;

  const MCSectionELF &ElfSec = static_cast<const MCSectionELF &>(TextSec);
  unsigned Flags = ELF::SHF_LINK_ORDER | ELF::SHF_ALLOC;
  StringRef GroupName;
  if (const MCSymbol *Group = ElfSec.getGroup()) {
    GroupName = Group->getName();
    Flags |= ELF::SHF_GROUP;
  }

  return Ctx->getELFSection(".kcfi_traps", ELF::SHT_PROGBITS, Flags, 0,
                            GroupName,
                            /*IsComdat=*/true, ElfSec.getUniqueID(),
                            cast<MCSymbolELF>(TextSec.getBeginSymbol()));
}

MCSection *
MCObjectFileInfo::getPseudoProbeSection(const MCSection &TextSec) const {
  if (Ctx->getObjectFileType() != MCContext::IsELF)
    return PseudoProbeSection;

  const auto &ElfSec = static_cast<const MCSectionELF &>(TextSec);
  unsigned Flags = ELF::SHF_LINK_ORDER;
  StringRef GroupName;
  if (const MCSymbol *Group = ElfSec.getGroup()) {
    GroupName = Group->getName();
    Flags |= ELF::SHF_GROUP;
  }

  return Ctx->getELFSection(PseudoProbeSection->getName(), ELF::SHT_PROGBITS,
                            Flags, 0, GroupName, true, ElfSec.getUniqueID(),
                            cast<MCSymbolELF>(TextSec.getBeginSymbol()));
}

MCSection *
MCObjectFileInfo::getPseudoProbeDescSection(StringRef FuncName) const {
  if (Ctx->getObjectFileType() == MCContext::IsELF) {
    // Create a separate comdat group for each function's descriptor in order
    // for the linker to deduplicate. The duplication, must be from different
    // tranlation unit, can come from:
    //  1. Inline functions defined in header files;
    //  2. ThinLTO imported funcions;
    //  3. Weak-linkage definitions.
    // Use a concatenation of the section name and the function name as the
    // group name so that descriptor-only groups won't be folded with groups of
    // code.
    if (Ctx->getTargetTriple().supportsCOMDAT() && !FuncName.empty()) {
      auto *S = static_cast<MCSectionELF *>(PseudoProbeDescSection);
      auto Flags = S->getFlags() | ELF::SHF_GROUP;
      return Ctx->getELFSection(S->getName(), S->getType(), Flags,
                                S->getEntrySize(),
                                S->getName() + "_" + FuncName,
                                /*IsComdat=*/true);
    }
  }
  return PseudoProbeDescSection;
}

MCSection *MCObjectFileInfo::getLLVMStatsSection() const {
  return LLVMStatsSection;
}

MCSection *MCObjectFileInfo::getPCSection(StringRef Name,
                                          const MCSection *TextSec) const {
  if (Ctx->getObjectFileType() != MCContext::IsELF)
    return nullptr;

  // SHF_WRITE for relocations, and let user post-process data in-place.
  unsigned Flags = ELF::SHF_WRITE | ELF::SHF_ALLOC | ELF::SHF_LINK_ORDER;

  if (!TextSec)
    TextSec = getTextSection();

  StringRef GroupName;
  const auto &ElfSec = static_cast<const MCSectionELF &>(*TextSec);
  if (const MCSymbol *Group = ElfSec.getGroup()) {
    GroupName = Group->getName();
    Flags |= ELF::SHF_GROUP;
  }
  return Ctx->getELFSection(Name, ELF::SHT_PROGBITS, Flags, 0, GroupName, true,
                            ElfSec.getUniqueID(),
                            cast<MCSymbolELF>(TextSec->getBeginSymbol()));
}
