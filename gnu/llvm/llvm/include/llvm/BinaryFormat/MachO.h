//===-- llvm/BinaryFormat/MachO.h - The MachO file format -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines manifest constants for the MachO object file format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MACHO_H
#define LLVM_BINARYFORMAT_MACHO_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SwapByteOrder.h"

namespace llvm {

class Triple;

namespace MachO {
// Enums from <mach-o/loader.h>
enum : uint32_t {
  // Constants for the "magic" field in llvm::MachO::mach_header and
  // llvm::MachO::mach_header_64
  MH_MAGIC = 0xFEEDFACEu,
  MH_CIGAM = 0xCEFAEDFEu,
  MH_MAGIC_64 = 0xFEEDFACFu,
  MH_CIGAM_64 = 0xCFFAEDFEu,
  FAT_MAGIC = 0xCAFEBABEu,
  FAT_CIGAM = 0xBEBAFECAu,
  FAT_MAGIC_64 = 0xCAFEBABFu,
  FAT_CIGAM_64 = 0xBFBAFECAu
};

enum HeaderFileType {
  // Constants for the "filetype" field in llvm::MachO::mach_header and
  // llvm::MachO::mach_header_64
  MH_OBJECT = 0x1u,
  MH_EXECUTE = 0x2u,
  MH_FVMLIB = 0x3u,
  MH_CORE = 0x4u,
  MH_PRELOAD = 0x5u,
  MH_DYLIB = 0x6u,
  MH_DYLINKER = 0x7u,
  MH_BUNDLE = 0x8u,
  MH_DYLIB_STUB = 0x9u,
  MH_DSYM = 0xAu,
  MH_KEXT_BUNDLE = 0xBu,
  MH_FILESET = 0xCu,
};

enum {
  // Constant bits for the "flags" field in llvm::MachO::mach_header and
  // llvm::MachO::mach_header_64
  MH_NOUNDEFS = 0x00000001u,
  MH_INCRLINK = 0x00000002u,
  MH_DYLDLINK = 0x00000004u,
  MH_BINDATLOAD = 0x00000008u,
  MH_PREBOUND = 0x00000010u,
  MH_SPLIT_SEGS = 0x00000020u,
  MH_LAZY_INIT = 0x00000040u,
  MH_TWOLEVEL = 0x00000080u,
  MH_FORCE_FLAT = 0x00000100u,
  MH_NOMULTIDEFS = 0x00000200u,
  MH_NOFIXPREBINDING = 0x00000400u,
  MH_PREBINDABLE = 0x00000800u,
  MH_ALLMODSBOUND = 0x00001000u,
  MH_SUBSECTIONS_VIA_SYMBOLS = 0x00002000u,
  MH_CANONICAL = 0x00004000u,
  MH_WEAK_DEFINES = 0x00008000u,
  MH_BINDS_TO_WEAK = 0x00010000u,
  MH_ALLOW_STACK_EXECUTION = 0x00020000u,
  MH_ROOT_SAFE = 0x00040000u,
  MH_SETUID_SAFE = 0x00080000u,
  MH_NO_REEXPORTED_DYLIBS = 0x00100000u,
  MH_PIE = 0x00200000u,
  MH_DEAD_STRIPPABLE_DYLIB = 0x00400000u,
  MH_HAS_TLV_DESCRIPTORS = 0x00800000u,
  MH_NO_HEAP_EXECUTION = 0x01000000u,
  MH_APP_EXTENSION_SAFE = 0x02000000u,
  MH_NLIST_OUTOFSYNC_WITH_DYLDINFO = 0x04000000u,
  MH_SIM_SUPPORT = 0x08000000u,
  MH_DYLIB_IN_CACHE = 0x80000000u,
};

enum : uint32_t {
  // Flags for the "cmd" field in llvm::MachO::load_command
  LC_REQ_DYLD = 0x80000000u
};

#define HANDLE_LOAD_COMMAND(LCName, LCValue, LCStruct) LCName = LCValue,

enum LoadCommandType : uint32_t {
#include "llvm/BinaryFormat/MachO.def"
};

#undef HANDLE_LOAD_COMMAND

enum : uint32_t {
  // Constant bits for the "flags" field in llvm::MachO::segment_command
  SG_HIGHVM = 0x1u,
  SG_FVMLIB = 0x2u,
  SG_NORELOC = 0x4u,
  SG_PROTECTED_VERSION_1 = 0x8u,
  SG_READ_ONLY = 0x10u,

  // Constant masks for the "flags" field in llvm::MachO::section and
  // llvm::MachO::section_64
  SECTION_TYPE = 0x000000ffu,           // SECTION_TYPE
  SECTION_ATTRIBUTES = 0xffffff00u,     // SECTION_ATTRIBUTES
  SECTION_ATTRIBUTES_USR = 0xff000000u, // SECTION_ATTRIBUTES_USR
  SECTION_ATTRIBUTES_SYS = 0x00ffff00u  // SECTION_ATTRIBUTES_SYS
};

/// These are the section type and attributes fields.  A MachO section can
/// have only one Type, but can have any of the attributes specified.
enum SectionType : uint32_t {
  // Constant masks for the "flags[7:0]" field in llvm::MachO::section and
  // llvm::MachO::section_64 (mask "flags" with SECTION_TYPE)

  /// S_REGULAR - Regular section.
  S_REGULAR = 0x00u,
  /// S_ZEROFILL - Zero fill on demand section.
  S_ZEROFILL = 0x01u,
  /// S_CSTRING_LITERALS - Section with literal C strings.
  S_CSTRING_LITERALS = 0x02u,
  /// S_4BYTE_LITERALS - Section with 4 byte literals.
  S_4BYTE_LITERALS = 0x03u,
  /// S_8BYTE_LITERALS - Section with 8 byte literals.
  S_8BYTE_LITERALS = 0x04u,
  /// S_LITERAL_POINTERS - Section with pointers to literals.
  S_LITERAL_POINTERS = 0x05u,
  /// S_NON_LAZY_SYMBOL_POINTERS - Section with non-lazy symbol pointers.
  S_NON_LAZY_SYMBOL_POINTERS = 0x06u,
  /// S_LAZY_SYMBOL_POINTERS - Section with lazy symbol pointers.
  S_LAZY_SYMBOL_POINTERS = 0x07u,
  /// S_SYMBOL_STUBS - Section with symbol stubs, byte size of stub in
  /// the Reserved2 field.
  S_SYMBOL_STUBS = 0x08u,
  /// S_MOD_INIT_FUNC_POINTERS - Section with only function pointers for
  /// initialization.
  S_MOD_INIT_FUNC_POINTERS = 0x09u,
  /// S_MOD_TERM_FUNC_POINTERS - Section with only function pointers for
  /// termination.
  S_MOD_TERM_FUNC_POINTERS = 0x0au,
  /// S_COALESCED - Section contains symbols that are to be coalesced.
  S_COALESCED = 0x0bu,
  /// S_GB_ZEROFILL - Zero fill on demand section (that can be larger than 4
  /// gigabytes).
  S_GB_ZEROFILL = 0x0cu,
  /// S_INTERPOSING - Section with only pairs of function pointers for
  /// interposing.
  S_INTERPOSING = 0x0du,
  /// S_16BYTE_LITERALS - Section with only 16 byte literals.
  S_16BYTE_LITERALS = 0x0eu,
  /// S_DTRACE_DOF - Section contains DTrace Object Format.
  S_DTRACE_DOF = 0x0fu,
  /// S_LAZY_DYLIB_SYMBOL_POINTERS - Section with lazy symbol pointers to
  /// lazy loaded dylibs.
  S_LAZY_DYLIB_SYMBOL_POINTERS = 0x10u,
  /// S_THREAD_LOCAL_REGULAR - Thread local data section.
  S_THREAD_LOCAL_REGULAR = 0x11u,
  /// S_THREAD_LOCAL_ZEROFILL - Thread local zerofill section.
  S_THREAD_LOCAL_ZEROFILL = 0x12u,
  /// S_THREAD_LOCAL_VARIABLES - Section with thread local variable
  /// structure data.
  S_THREAD_LOCAL_VARIABLES = 0x13u,
  /// S_THREAD_LOCAL_VARIABLE_POINTERS - Section with pointers to thread
  /// local structures.
  S_THREAD_LOCAL_VARIABLE_POINTERS = 0x14u,
  /// S_THREAD_LOCAL_INIT_FUNCTION_POINTERS - Section with thread local
  /// variable initialization pointers to functions.
  S_THREAD_LOCAL_INIT_FUNCTION_POINTERS = 0x15u,
  /// S_INIT_FUNC_OFFSETS - Section with 32-bit offsets to initializer
  /// functions.
  S_INIT_FUNC_OFFSETS = 0x16u,

  LAST_KNOWN_SECTION_TYPE = S_INIT_FUNC_OFFSETS
};

enum : uint32_t {
  // Constant masks for the "flags[31:24]" field in llvm::MachO::section and
  // llvm::MachO::section_64 (mask "flags" with SECTION_ATTRIBUTES_USR)

  /// S_ATTR_PURE_INSTRUCTIONS - Section contains only true machine
  /// instructions.
  S_ATTR_PURE_INSTRUCTIONS = 0x80000000u,
  /// S_ATTR_NO_TOC - Section contains coalesced symbols that are not to be
  /// in a ranlib table of contents.
  S_ATTR_NO_TOC = 0x40000000u,
  /// S_ATTR_STRIP_STATIC_SYMS - Ok to strip static symbols in this section
  /// in files with the MY_DYLDLINK flag.
  S_ATTR_STRIP_STATIC_SYMS = 0x20000000u,
  /// S_ATTR_NO_DEAD_STRIP - No dead stripping.
  S_ATTR_NO_DEAD_STRIP = 0x10000000u,
  /// S_ATTR_LIVE_SUPPORT - Blocks are live if they reference live blocks.
  S_ATTR_LIVE_SUPPORT = 0x08000000u,
  /// S_ATTR_SELF_MODIFYING_CODE - Used with i386 code stubs written on by
  /// dyld.
  S_ATTR_SELF_MODIFYING_CODE = 0x04000000u,
  /// S_ATTR_DEBUG - A debug section.
  S_ATTR_DEBUG = 0x02000000u,

  // Constant masks for the "flags[23:8]" field in llvm::MachO::section and
  // llvm::MachO::section_64 (mask "flags" with SECTION_ATTRIBUTES_SYS)

  /// S_ATTR_SOME_INSTRUCTIONS - Section contains some machine instructions.
  S_ATTR_SOME_INSTRUCTIONS = 0x00000400u,
  /// S_ATTR_EXT_RELOC - Section has external relocation entries.
  S_ATTR_EXT_RELOC = 0x00000200u,
  /// S_ATTR_LOC_RELOC - Section has local relocation entries.
  S_ATTR_LOC_RELOC = 0x00000100u,

  // Constant masks for the value of an indirect symbol in an indirect
  // symbol table
  INDIRECT_SYMBOL_LOCAL = 0x80000000u,
  INDIRECT_SYMBOL_ABS = 0x40000000u
};

enum DataRegionType {
  // Constants for the "kind" field in a data_in_code_entry structure
  DICE_KIND_DATA = 1u,
  DICE_KIND_JUMP_TABLE8 = 2u,
  DICE_KIND_JUMP_TABLE16 = 3u,
  DICE_KIND_JUMP_TABLE32 = 4u,
  DICE_KIND_ABS_JUMP_TABLE32 = 5u
};

enum RebaseType {
  REBASE_TYPE_POINTER = 1u,
  REBASE_TYPE_TEXT_ABSOLUTE32 = 2u,
  REBASE_TYPE_TEXT_PCREL32 = 3u
};

enum { REBASE_OPCODE_MASK = 0xF0u, REBASE_IMMEDIATE_MASK = 0x0Fu };

enum RebaseOpcode {
  REBASE_OPCODE_DONE = 0x00u,
  REBASE_OPCODE_SET_TYPE_IMM = 0x10u,
  REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x20u,
  REBASE_OPCODE_ADD_ADDR_ULEB = 0x30u,
  REBASE_OPCODE_ADD_ADDR_IMM_SCALED = 0x40u,
  REBASE_OPCODE_DO_REBASE_IMM_TIMES = 0x50u,
  REBASE_OPCODE_DO_REBASE_ULEB_TIMES = 0x60u,
  REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB = 0x70u,
  REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB = 0x80u
};

enum BindType {
  BIND_TYPE_POINTER = 1u,
  BIND_TYPE_TEXT_ABSOLUTE32 = 2u,
  BIND_TYPE_TEXT_PCREL32 = 3u
};

enum BindSpecialDylib {
  BIND_SPECIAL_DYLIB_SELF = 0,
  BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE = -1,
  BIND_SPECIAL_DYLIB_FLAT_LOOKUP = -2,
  BIND_SPECIAL_DYLIB_WEAK_LOOKUP = -3
};

enum {
  BIND_SYMBOL_FLAGS_WEAK_IMPORT = 0x1u,
  BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION = 0x8u,

  BIND_OPCODE_MASK = 0xF0u,
  BIND_IMMEDIATE_MASK = 0x0Fu
};

enum BindOpcode {
  BIND_OPCODE_DONE = 0x00u,
  BIND_OPCODE_SET_DYLIB_ORDINAL_IMM = 0x10u,
  BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB = 0x20u,
  BIND_OPCODE_SET_DYLIB_SPECIAL_IMM = 0x30u,
  BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM = 0x40u,
  BIND_OPCODE_SET_TYPE_IMM = 0x50u,
  BIND_OPCODE_SET_ADDEND_SLEB = 0x60u,
  BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x70u,
  BIND_OPCODE_ADD_ADDR_ULEB = 0x80u,
  BIND_OPCODE_DO_BIND = 0x90u,
  BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB = 0xA0u,
  BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED = 0xB0u,
  BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB = 0xC0u
};

enum {
  EXPORT_SYMBOL_FLAGS_KIND_MASK = 0x03u,
  EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION = 0x04u,
  EXPORT_SYMBOL_FLAGS_REEXPORT = 0x08u,
  EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER = 0x10u
};

enum ExportSymbolKind {
  EXPORT_SYMBOL_FLAGS_KIND_REGULAR = 0x00u,
  EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL = 0x01u,
  EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE = 0x02u
};

enum {
  // Constant masks for the "n_type" field in llvm::MachO::nlist and
  // llvm::MachO::nlist_64
  N_STAB = 0xe0,
  N_PEXT = 0x10,
  N_TYPE = 0x0e,
  N_EXT = 0x01
};

enum NListType : uint8_t {
  // Constants for the "n_type & N_TYPE" llvm::MachO::nlist and
  // llvm::MachO::nlist_64
  N_UNDF = 0x0u,
  N_ABS = 0x2u,
  N_SECT = 0xeu,
  N_PBUD = 0xcu,
  N_INDR = 0xau
};

enum SectionOrdinal {
  // Constants for the "n_sect" field in llvm::MachO::nlist and
  // llvm::MachO::nlist_64
  NO_SECT = 0u,
  MAX_SECT = 0xffu
};

enum {
  // Constant masks for the "n_desc" field in llvm::MachO::nlist and
  // llvm::MachO::nlist_64
  // The low 3 bits are the for the REFERENCE_TYPE.
  REFERENCE_TYPE = 0x7,
  REFERENCE_FLAG_UNDEFINED_NON_LAZY = 0,
  REFERENCE_FLAG_UNDEFINED_LAZY = 1,
  REFERENCE_FLAG_DEFINED = 2,
  REFERENCE_FLAG_PRIVATE_DEFINED = 3,
  REFERENCE_FLAG_PRIVATE_UNDEFINED_NON_LAZY = 4,
  REFERENCE_FLAG_PRIVATE_UNDEFINED_LAZY = 5,
  // Flag bits (some overlap with the library ordinal bits).
  N_ARM_THUMB_DEF = 0x0008u,
  REFERENCED_DYNAMICALLY = 0x0010u,
  N_NO_DEAD_STRIP = 0x0020u,
  N_WEAK_REF = 0x0040u,
  N_WEAK_DEF = 0x0080u,
  N_SYMBOL_RESOLVER = 0x0100u,
  N_ALT_ENTRY = 0x0200u,
  N_COLD_FUNC = 0x0400u,
  // For undefined symbols coming from libraries, see GET_LIBRARY_ORDINAL()
  // as these are in the top 8 bits.
  SELF_LIBRARY_ORDINAL = 0x0,
  MAX_LIBRARY_ORDINAL = 0xfd,
  DYNAMIC_LOOKUP_ORDINAL = 0xfe,
  EXECUTABLE_ORDINAL = 0xff
};

enum StabType {
  // Constant values for the "n_type" field in llvm::MachO::nlist and
  // llvm::MachO::nlist_64 when "(n_type & N_STAB) != 0"
  N_GSYM = 0x20u,
  N_FNAME = 0x22u,
  N_FUN = 0x24u,
  N_STSYM = 0x26u,
  N_LCSYM = 0x28u,
  N_BNSYM = 0x2Eu,
  N_PC = 0x30u,
  N_AST = 0x32u,
  N_OPT = 0x3Cu,
  N_RSYM = 0x40u,
  N_SLINE = 0x44u,
  N_ENSYM = 0x4Eu,
  N_SSYM = 0x60u,
  N_SO = 0x64u,
  N_OSO = 0x66u,
  N_LIB = 0x68u,
  N_LSYM = 0x80u,
  N_BINCL = 0x82u,
  N_SOL = 0x84u,
  N_PARAMS = 0x86u,
  N_VERSION = 0x88u,
  N_OLEVEL = 0x8Au,
  N_PSYM = 0xA0u,
  N_EINCL = 0xA2u,
  N_ENTRY = 0xA4u,
  N_LBRAC = 0xC0u,
  N_EXCL = 0xC2u,
  N_RBRAC = 0xE0u,
  N_BCOMM = 0xE2u,
  N_ECOMM = 0xE4u,
  N_ECOML = 0xE8u,
  N_LENG = 0xFEu
};

enum : uint32_t {
  // Constant values for the r_symbolnum field in an
  // llvm::MachO::relocation_info structure when r_extern is 0.
  R_ABS = 0,

  // Constant bits for the r_address field in an
  // llvm::MachO::relocation_info structure.
  R_SCATTERED = 0x80000000
};

enum RelocationInfoType {
  // Constant values for the r_type field in an
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure.
  GENERIC_RELOC_INVALID = 0xff,
  GENERIC_RELOC_VANILLA = 0,
  GENERIC_RELOC_PAIR = 1,
  GENERIC_RELOC_SECTDIFF = 2,
  GENERIC_RELOC_PB_LA_PTR = 3,
  GENERIC_RELOC_LOCAL_SECTDIFF = 4,
  GENERIC_RELOC_TLV = 5,

  // Constant values for the r_type field in a PowerPC architecture
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure.
  PPC_RELOC_VANILLA = GENERIC_RELOC_VANILLA,
  PPC_RELOC_PAIR = GENERIC_RELOC_PAIR,
  PPC_RELOC_BR14 = 2,
  PPC_RELOC_BR24 = 3,
  PPC_RELOC_HI16 = 4,
  PPC_RELOC_LO16 = 5,
  PPC_RELOC_HA16 = 6,
  PPC_RELOC_LO14 = 7,
  PPC_RELOC_SECTDIFF = 8,
  PPC_RELOC_PB_LA_PTR = 9,
  PPC_RELOC_HI16_SECTDIFF = 10,
  PPC_RELOC_LO16_SECTDIFF = 11,
  PPC_RELOC_HA16_SECTDIFF = 12,
  PPC_RELOC_JBSR = 13,
  PPC_RELOC_LO14_SECTDIFF = 14,
  PPC_RELOC_LOCAL_SECTDIFF = 15,

  // Constant values for the r_type field in an ARM architecture
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure.
  ARM_RELOC_VANILLA = GENERIC_RELOC_VANILLA,
  ARM_RELOC_PAIR = GENERIC_RELOC_PAIR,
  ARM_RELOC_SECTDIFF = GENERIC_RELOC_SECTDIFF,
  ARM_RELOC_LOCAL_SECTDIFF = 3,
  ARM_RELOC_PB_LA_PTR = 4,
  ARM_RELOC_BR24 = 5,
  ARM_THUMB_RELOC_BR22 = 6,
  ARM_THUMB_32BIT_BRANCH = 7, // obsolete
  ARM_RELOC_HALF = 8,
  ARM_RELOC_HALF_SECTDIFF = 9,

  // Constant values for the r_type field in an ARM64 architecture
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure.

  // For pointers.
  ARM64_RELOC_UNSIGNED = 0,
  // Must be followed by an ARM64_RELOC_UNSIGNED
  ARM64_RELOC_SUBTRACTOR = 1,
  // A B/BL instruction with 26-bit displacement.
  ARM64_RELOC_BRANCH26 = 2,
  // PC-rel distance to page of target.
  ARM64_RELOC_PAGE21 = 3,
  // Offset within page, scaled by r_length.
  ARM64_RELOC_PAGEOFF12 = 4,
  // PC-rel distance to page of GOT slot.
  ARM64_RELOC_GOT_LOAD_PAGE21 = 5,
  // Offset within page of GOT slot, scaled by r_length.
  ARM64_RELOC_GOT_LOAD_PAGEOFF12 = 6,
  // For pointers to GOT slots.
  ARM64_RELOC_POINTER_TO_GOT = 7,
  // PC-rel distance to page of TLVP slot.
  ARM64_RELOC_TLVP_LOAD_PAGE21 = 8,
  // Offset within page of TLVP slot, scaled by r_length.
  ARM64_RELOC_TLVP_LOAD_PAGEOFF12 = 9,
  // Must be followed by ARM64_RELOC_PAGE21 or ARM64_RELOC_PAGEOFF12.
  ARM64_RELOC_ADDEND = 10,
  // An authenticated pointer.
  ARM64_RELOC_AUTHENTICATED_POINTER = 11,

  // Constant values for the r_type field in an x86_64 architecture
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure
  X86_64_RELOC_UNSIGNED = 0,
  X86_64_RELOC_SIGNED = 1,
  X86_64_RELOC_BRANCH = 2,
  X86_64_RELOC_GOT_LOAD = 3,
  X86_64_RELOC_GOT = 4,
  X86_64_RELOC_SUBTRACTOR = 5,
  X86_64_RELOC_SIGNED_1 = 6,
  X86_64_RELOC_SIGNED_2 = 7,
  X86_64_RELOC_SIGNED_4 = 8,
  X86_64_RELOC_TLV = 9
};

// Values for segment_command.initprot.
// From <mach/vm_prot.h>
enum { VM_PROT_READ = 0x1, VM_PROT_WRITE = 0x2, VM_PROT_EXECUTE = 0x4 };

// Values for platform field in build_version_command.
enum PlatformType {
#define PLATFORM(platform, id, name, build_name, target, tapi_target,          \
                 marketing)                                                    \
  PLATFORM_##platform = id,
#include "MachO.def"
};

// Values for tools enum in build_tool_version.
enum { TOOL_CLANG = 1, TOOL_SWIFT = 2, TOOL_LD = 3, TOOL_LLD = 4 };

// Structs from <mach-o/loader.h>

struct mach_header {
  uint32_t magic;
  uint32_t cputype;
  uint32_t cpusubtype;
  uint32_t filetype;
  uint32_t ncmds;
  uint32_t sizeofcmds;
  uint32_t flags;
};

struct mach_header_64 {
  uint32_t magic;
  uint32_t cputype;
  uint32_t cpusubtype;
  uint32_t filetype;
  uint32_t ncmds;
  uint32_t sizeofcmds;
  uint32_t flags;
  uint32_t reserved;
};

struct load_command {
  uint32_t cmd;
  uint32_t cmdsize;
};

struct segment_command {
  uint32_t cmd;
  uint32_t cmdsize;
  char segname[16];
  uint32_t vmaddr;
  uint32_t vmsize;
  uint32_t fileoff;
  uint32_t filesize;
  uint32_t maxprot;
  uint32_t initprot;
  uint32_t nsects;
  uint32_t flags;
};

struct segment_command_64 {
  uint32_t cmd;
  uint32_t cmdsize;
  char segname[16];
  uint64_t vmaddr;
  uint64_t vmsize;
  uint64_t fileoff;
  uint64_t filesize;
  uint32_t maxprot;
  uint32_t initprot;
  uint32_t nsects;
  uint32_t flags;
};

struct section {
  char sectname[16];
  char segname[16];
  uint32_t addr;
  uint32_t size;
  uint32_t offset;
  uint32_t align;
  uint32_t reloff;
  uint32_t nreloc;
  uint32_t flags;
  uint32_t reserved1;
  uint32_t reserved2;
};

struct section_64 {
  char sectname[16];
  char segname[16];
  uint64_t addr;
  uint64_t size;
  uint32_t offset;
  uint32_t align;
  uint32_t reloff;
  uint32_t nreloc;
  uint32_t flags;
  uint32_t reserved1;
  uint32_t reserved2;
  uint32_t reserved3;
};

inline bool isVirtualSection(uint8_t type) {
  return (type == MachO::S_ZEROFILL || type == MachO::S_GB_ZEROFILL ||
          type == MachO::S_THREAD_LOCAL_ZEROFILL);
}

struct fvmlib {
  uint32_t name;
  uint32_t minor_version;
  uint32_t header_addr;
};

// The fvmlib_command is obsolete and no longer supported.
struct fvmlib_command {
  uint32_t cmd;
  uint32_t cmdsize;
  struct fvmlib fvmlib;
};

struct dylib {
  uint32_t name;
  uint32_t timestamp;
  uint32_t current_version;
  uint32_t compatibility_version;
};

struct dylib_command {
  uint32_t cmd;
  uint32_t cmdsize;
  struct dylib dylib;
};

struct sub_framework_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t umbrella;
};

struct sub_client_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t client;
};

struct sub_umbrella_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t sub_umbrella;
};

struct sub_library_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t sub_library;
};

// The prebound_dylib_command is obsolete and no longer supported.
struct prebound_dylib_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t name;
  uint32_t nmodules;
  uint32_t linked_modules;
};

struct dylinker_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t name;
};

struct thread_command {
  uint32_t cmd;
  uint32_t cmdsize;
};

struct routines_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t init_address;
  uint32_t init_module;
  uint32_t reserved1;
  uint32_t reserved2;
  uint32_t reserved3;
  uint32_t reserved4;
  uint32_t reserved5;
  uint32_t reserved6;
};

struct routines_command_64 {
  uint32_t cmd;
  uint32_t cmdsize;
  uint64_t init_address;
  uint64_t init_module;
  uint64_t reserved1;
  uint64_t reserved2;
  uint64_t reserved3;
  uint64_t reserved4;
  uint64_t reserved5;
  uint64_t reserved6;
};

struct symtab_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t symoff;
  uint32_t nsyms;
  uint32_t stroff;
  uint32_t strsize;
};

struct dysymtab_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t ilocalsym;
  uint32_t nlocalsym;
  uint32_t iextdefsym;
  uint32_t nextdefsym;
  uint32_t iundefsym;
  uint32_t nundefsym;
  uint32_t tocoff;
  uint32_t ntoc;
  uint32_t modtaboff;
  uint32_t nmodtab;
  uint32_t extrefsymoff;
  uint32_t nextrefsyms;
  uint32_t indirectsymoff;
  uint32_t nindirectsyms;
  uint32_t extreloff;
  uint32_t nextrel;
  uint32_t locreloff;
  uint32_t nlocrel;
};

struct dylib_table_of_contents {
  uint32_t symbol_index;
  uint32_t module_index;
};

struct dylib_module {
  uint32_t module_name;
  uint32_t iextdefsym;
  uint32_t nextdefsym;
  uint32_t irefsym;
  uint32_t nrefsym;
  uint32_t ilocalsym;
  uint32_t nlocalsym;
  uint32_t iextrel;
  uint32_t nextrel;
  uint32_t iinit_iterm;
  uint32_t ninit_nterm;
  uint32_t objc_module_info_addr;
  uint32_t objc_module_info_size;
};

struct dylib_module_64 {
  uint32_t module_name;
  uint32_t iextdefsym;
  uint32_t nextdefsym;
  uint32_t irefsym;
  uint32_t nrefsym;
  uint32_t ilocalsym;
  uint32_t nlocalsym;
  uint32_t iextrel;
  uint32_t nextrel;
  uint32_t iinit_iterm;
  uint32_t ninit_nterm;
  uint32_t objc_module_info_size;
  uint64_t objc_module_info_addr;
};

struct dylib_reference {
  uint32_t isym : 24, flags : 8;
};

// The twolevel_hints_command is obsolete and no longer supported.
struct twolevel_hints_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t offset;
  uint32_t nhints;
};

// The twolevel_hints_command is obsolete and no longer supported.
struct twolevel_hint {
  uint32_t isub_image : 8, itoc : 24;
};

// The prebind_cksum_command is obsolete and no longer supported.
struct prebind_cksum_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t cksum;
};

struct uuid_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint8_t uuid[16];
};

struct rpath_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t path;
};

struct linkedit_data_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t dataoff;
  uint32_t datasize;
};

struct data_in_code_entry {
  uint32_t offset;
  uint16_t length;
  uint16_t kind;
};

struct source_version_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint64_t version;
};

struct encryption_info_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t cryptoff;
  uint32_t cryptsize;
  uint32_t cryptid;
};

struct encryption_info_command_64 {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t cryptoff;
  uint32_t cryptsize;
  uint32_t cryptid;
  uint32_t pad;
};

struct version_min_command {
  uint32_t cmd;     // LC_VERSION_MIN_MACOSX or
                    // LC_VERSION_MIN_IPHONEOS
  uint32_t cmdsize; // sizeof(struct version_min_command)
  uint32_t version; // X.Y.Z is encoded in nibbles xxxx.yy.zz
  uint32_t sdk;     // X.Y.Z is encoded in nibbles xxxx.yy.zz
};

struct note_command {
  uint32_t cmd;        // LC_NOTE
  uint32_t cmdsize;    // sizeof(struct note_command)
  char data_owner[16]; // owner name for this LC_NOTE
  uint64_t offset;     // file offset of this data
  uint64_t size;       // length of data region
};

struct build_tool_version {
  uint32_t tool;    // enum for the tool
  uint32_t version; // version of the tool
};

struct build_version_command {
  uint32_t cmd;      // LC_BUILD_VERSION
  uint32_t cmdsize;  // sizeof(struct build_version_command) +
                     // ntools * sizeof(struct build_tool_version)
  uint32_t platform; // platform
  uint32_t minos;    // X.Y.Z is encoded in nibbles xxxx.yy.zz
  uint32_t sdk;      // X.Y.Z is encoded in nibbles xxxx.yy.zz
  uint32_t ntools;   // number of tool entries following this
};

struct dyld_env_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t name;
};

struct dyld_info_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t rebase_off;
  uint32_t rebase_size;
  uint32_t bind_off;
  uint32_t bind_size;
  uint32_t weak_bind_off;
  uint32_t weak_bind_size;
  uint32_t lazy_bind_off;
  uint32_t lazy_bind_size;
  uint32_t export_off;
  uint32_t export_size;
};

struct linker_option_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t count;
};

union lc_str {
  uint32_t offset;
};

struct fileset_entry_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint64_t vmaddr;
  uint64_t fileoff;
  union lc_str entry_id;
  uint32_t reserved;
};

// The symseg_command is obsolete and no longer supported.
struct symseg_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t offset;
  uint32_t size;
};

// The ident_command is obsolete and no longer supported.
struct ident_command {
  uint32_t cmd;
  uint32_t cmdsize;
};

// The fvmfile_command is obsolete and no longer supported.
struct fvmfile_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t name;
  uint32_t header_addr;
};

struct tlv_descriptor_32 {
  uint32_t thunk;
  uint32_t key;
  uint32_t offset;
};

struct tlv_descriptor_64 {
  uint64_t thunk;
  uint64_t key;
  uint64_t offset;
};

struct tlv_descriptor {
  uintptr_t thunk;
  uintptr_t key;
  uintptr_t offset;
};

struct entry_point_command {
  uint32_t cmd;
  uint32_t cmdsize;
  uint64_t entryoff;
  uint64_t stacksize;
};

// Structs from <mach-o/fat.h>
struct fat_header {
  uint32_t magic;
  uint32_t nfat_arch;
};

struct fat_arch {
  uint32_t cputype;
  uint32_t cpusubtype;
  uint32_t offset;
  uint32_t size;
  uint32_t align;
};

struct fat_arch_64 {
  uint32_t cputype;
  uint32_t cpusubtype;
  uint64_t offset;
  uint64_t size;
  uint32_t align;
  uint32_t reserved;
};

// Structs from <mach-o/reloc.h>
struct relocation_info {
  int32_t r_address;
  uint32_t r_symbolnum : 24, r_pcrel : 1, r_length : 2, r_extern : 1,
      r_type : 4;
};

struct scattered_relocation_info {
#if defined(BYTE_ORDER) && defined(BIG_ENDIAN) && (BYTE_ORDER == BIG_ENDIAN)
  uint32_t r_scattered : 1, r_pcrel : 1, r_length : 2, r_type : 4,
      r_address : 24;
#else
  uint32_t r_address : 24, r_type : 4, r_length : 2, r_pcrel : 1,
      r_scattered : 1;
#endif
  int32_t r_value;
};

// Structs NOT from <mach-o/reloc.h>, but that make LLVM's life easier
struct any_relocation_info {
  uint32_t r_word0, r_word1;
};

// Structs from <mach-o/nlist.h>
struct nlist_base {
  uint32_t n_strx;
  uint8_t n_type;
  uint8_t n_sect;
  uint16_t n_desc;
};

struct nlist {
  uint32_t n_strx;
  uint8_t n_type;
  uint8_t n_sect;
  int16_t n_desc;
  uint32_t n_value;
};

struct nlist_64 {
  uint32_t n_strx;
  uint8_t n_type;
  uint8_t n_sect;
  uint16_t n_desc;
  uint64_t n_value;
};

// Values for dyld_chained_fixups_header::imports_format.
enum ChainedImportFormat {
  DYLD_CHAINED_IMPORT = 1,
  DYLD_CHAINED_IMPORT_ADDEND = 2,
  DYLD_CHAINED_IMPORT_ADDEND64 = 3,
};

// Values for dyld_chained_fixups_header::symbols_format.
enum {
  DYLD_CHAINED_SYMBOL_UNCOMPRESSED = 0,
  DYLD_CHAINED_SYMBOL_ZLIB = 1,
};

// Values for dyld_chained_starts_in_segment::page_start.
enum {
  DYLD_CHAINED_PTR_START_NONE = 0xFFFF,
  DYLD_CHAINED_PTR_START_MULTI = 0x8000, // page which has multiple starts
  DYLD_CHAINED_PTR_START_LAST = 0x8000,  // last chain_start for a given page
};

// Values for dyld_chained_starts_in_segment::pointer_format.
enum {
  DYLD_CHAINED_PTR_ARM64E = 1,
  DYLD_CHAINED_PTR_64 = 2,
  DYLD_CHAINED_PTR_32 = 3,
  DYLD_CHAINED_PTR_32_CACHE = 4,
  DYLD_CHAINED_PTR_32_FIRMWARE = 5,
  DYLD_CHAINED_PTR_64_OFFSET = 6,
  DYLD_CHAINED_PTR_ARM64E_KERNEL = 7,
  DYLD_CHAINED_PTR_64_KERNEL_CACHE = 8,
  DYLD_CHAINED_PTR_ARM64E_USERLAND = 9,
  DYLD_CHAINED_PTR_ARM64E_FIRMWARE = 10,
  DYLD_CHAINED_PTR_X86_64_KERNEL_CACHE = 11,
  DYLD_CHAINED_PTR_ARM64E_USERLAND24 = 12,
};

/// Structs for dyld chained fixups.
/// dyld_chained_fixups_header is the data pointed to by LC_DYLD_CHAINED_FIXUPS
/// load command.
struct dyld_chained_fixups_header {
  uint32_t fixups_version; ///< 0
  uint32_t starts_offset;  ///< Offset of dyld_chained_starts_in_image.
  uint32_t imports_offset; ///< Offset of imports table in chain_data.
  uint32_t symbols_offset; ///< Offset of symbol strings in chain_data.
  uint32_t imports_count;  ///< Number of imported symbol names.
  uint32_t imports_format; ///< DYLD_CHAINED_IMPORT*
  uint32_t symbols_format; ///< 0 => uncompressed, 1 => zlib compressed
};

/// dyld_chained_starts_in_image is embedded in LC_DYLD_CHAINED_FIXUPS payload.
/// Each seg_info_offset entry is the offset into this struct for that
/// segment followed by pool of dyld_chain_starts_in_segment data.
struct dyld_chained_starts_in_image {
  uint32_t seg_count;
  uint32_t seg_info_offset[1];
};

struct dyld_chained_starts_in_segment {
  uint32_t size;              ///< Size of this, including chain_starts entries
  uint16_t page_size;         ///< Page size in bytes (0x1000 or 0x4000)
  uint16_t pointer_format;    ///< DYLD_CHAINED_PTR*
  uint64_t segment_offset;    ///< VM offset from the __TEXT segment
  uint32_t max_valid_pointer; ///< Values beyond this are not pointers on 32-bit
  uint16_t page_count;        ///< Length of the page_start array
  uint16_t page_start[1];     ///< Page offset of first fixup on each page, or
                              ///< DYLD_CHAINED_PTR_START_NONE if no fixups
};

// DYLD_CHAINED_IMPORT
struct dyld_chained_import {
  uint32_t lib_ordinal : 8;
  uint32_t weak_import : 1;
  uint32_t name_offset : 23;
};

// DYLD_CHAINED_IMPORT_ADDEND
struct dyld_chained_import_addend {
  uint32_t lib_ordinal : 8;
  uint32_t weak_import : 1;
  uint32_t name_offset : 23;
  int32_t addend;
};

// DYLD_CHAINED_IMPORT_ADDEND64
struct dyld_chained_import_addend64 {
  uint64_t lib_ordinal : 16;
  uint64_t weak_import : 1;
  uint64_t reserved : 15;
  uint64_t name_offset : 32;
  uint64_t addend;
};

// The `bind` field (most significant bit) of the encoded fixup determines
// whether it is dyld_chained_ptr_64_bind or dyld_chained_ptr_64_rebase.

// DYLD_CHAINED_PTR_64/DYLD_CHAINED_PTR_64_OFFSET
struct dyld_chained_ptr_64_bind {
  uint64_t ordinal : 24;
  uint64_t addend : 8;
  uint64_t reserved : 19;
  uint64_t next : 12;
  uint64_t bind : 1; // set to 1
};

// DYLD_CHAINED_PTR_64/DYLD_CHAINED_PTR_64_OFFSET
struct dyld_chained_ptr_64_rebase {
  uint64_t target : 36;
  uint64_t high8 : 8;
  uint64_t reserved : 7;
  uint64_t next : 12;
  uint64_t bind : 1; // set to 0
};

// Byte order swapping functions for MachO structs

inline void swapStruct(fat_header &mh) {
  sys::swapByteOrder(mh.magic);
  sys::swapByteOrder(mh.nfat_arch);
}

inline void swapStruct(fat_arch &mh) {
  sys::swapByteOrder(mh.cputype);
  sys::swapByteOrder(mh.cpusubtype);
  sys::swapByteOrder(mh.offset);
  sys::swapByteOrder(mh.size);
  sys::swapByteOrder(mh.align);
}

inline void swapStruct(fat_arch_64 &mh) {
  sys::swapByteOrder(mh.cputype);
  sys::swapByteOrder(mh.cpusubtype);
  sys::swapByteOrder(mh.offset);
  sys::swapByteOrder(mh.size);
  sys::swapByteOrder(mh.align);
  sys::swapByteOrder(mh.reserved);
}

inline void swapStruct(mach_header &mh) {
  sys::swapByteOrder(mh.magic);
  sys::swapByteOrder(mh.cputype);
  sys::swapByteOrder(mh.cpusubtype);
  sys::swapByteOrder(mh.filetype);
  sys::swapByteOrder(mh.ncmds);
  sys::swapByteOrder(mh.sizeofcmds);
  sys::swapByteOrder(mh.flags);
}

inline void swapStruct(mach_header_64 &H) {
  sys::swapByteOrder(H.magic);
  sys::swapByteOrder(H.cputype);
  sys::swapByteOrder(H.cpusubtype);
  sys::swapByteOrder(H.filetype);
  sys::swapByteOrder(H.ncmds);
  sys::swapByteOrder(H.sizeofcmds);
  sys::swapByteOrder(H.flags);
  sys::swapByteOrder(H.reserved);
}

inline void swapStruct(load_command &lc) {
  sys::swapByteOrder(lc.cmd);
  sys::swapByteOrder(lc.cmdsize);
}

inline void swapStruct(symtab_command &lc) {
  sys::swapByteOrder(lc.cmd);
  sys::swapByteOrder(lc.cmdsize);
  sys::swapByteOrder(lc.symoff);
  sys::swapByteOrder(lc.nsyms);
  sys::swapByteOrder(lc.stroff);
  sys::swapByteOrder(lc.strsize);
}

inline void swapStruct(segment_command_64 &seg) {
  sys::swapByteOrder(seg.cmd);
  sys::swapByteOrder(seg.cmdsize);
  sys::swapByteOrder(seg.vmaddr);
  sys::swapByteOrder(seg.vmsize);
  sys::swapByteOrder(seg.fileoff);
  sys::swapByteOrder(seg.filesize);
  sys::swapByteOrder(seg.maxprot);
  sys::swapByteOrder(seg.initprot);
  sys::swapByteOrder(seg.nsects);
  sys::swapByteOrder(seg.flags);
}

inline void swapStruct(segment_command &seg) {
  sys::swapByteOrder(seg.cmd);
  sys::swapByteOrder(seg.cmdsize);
  sys::swapByteOrder(seg.vmaddr);
  sys::swapByteOrder(seg.vmsize);
  sys::swapByteOrder(seg.fileoff);
  sys::swapByteOrder(seg.filesize);
  sys::swapByteOrder(seg.maxprot);
  sys::swapByteOrder(seg.initprot);
  sys::swapByteOrder(seg.nsects);
  sys::swapByteOrder(seg.flags);
}

inline void swapStruct(section_64 &sect) {
  sys::swapByteOrder(sect.addr);
  sys::swapByteOrder(sect.size);
  sys::swapByteOrder(sect.offset);
  sys::swapByteOrder(sect.align);
  sys::swapByteOrder(sect.reloff);
  sys::swapByteOrder(sect.nreloc);
  sys::swapByteOrder(sect.flags);
  sys::swapByteOrder(sect.reserved1);
  sys::swapByteOrder(sect.reserved2);
}

inline void swapStruct(section &sect) {
  sys::swapByteOrder(sect.addr);
  sys::swapByteOrder(sect.size);
  sys::swapByteOrder(sect.offset);
  sys::swapByteOrder(sect.align);
  sys::swapByteOrder(sect.reloff);
  sys::swapByteOrder(sect.nreloc);
  sys::swapByteOrder(sect.flags);
  sys::swapByteOrder(sect.reserved1);
  sys::swapByteOrder(sect.reserved2);
}

inline void swapStruct(dyld_info_command &info) {
  sys::swapByteOrder(info.cmd);
  sys::swapByteOrder(info.cmdsize);
  sys::swapByteOrder(info.rebase_off);
  sys::swapByteOrder(info.rebase_size);
  sys::swapByteOrder(info.bind_off);
  sys::swapByteOrder(info.bind_size);
  sys::swapByteOrder(info.weak_bind_off);
  sys::swapByteOrder(info.weak_bind_size);
  sys::swapByteOrder(info.lazy_bind_off);
  sys::swapByteOrder(info.lazy_bind_size);
  sys::swapByteOrder(info.export_off);
  sys::swapByteOrder(info.export_size);
}

inline void swapStruct(dylib_command &d) {
  sys::swapByteOrder(d.cmd);
  sys::swapByteOrder(d.cmdsize);
  sys::swapByteOrder(d.dylib.name);
  sys::swapByteOrder(d.dylib.timestamp);
  sys::swapByteOrder(d.dylib.current_version);
  sys::swapByteOrder(d.dylib.compatibility_version);
}

inline void swapStruct(sub_framework_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.umbrella);
}

inline void swapStruct(sub_umbrella_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.sub_umbrella);
}

inline void swapStruct(sub_library_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.sub_library);
}

inline void swapStruct(sub_client_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.client);
}

inline void swapStruct(routines_command &r) {
  sys::swapByteOrder(r.cmd);
  sys::swapByteOrder(r.cmdsize);
  sys::swapByteOrder(r.init_address);
  sys::swapByteOrder(r.init_module);
  sys::swapByteOrder(r.reserved1);
  sys::swapByteOrder(r.reserved2);
  sys::swapByteOrder(r.reserved3);
  sys::swapByteOrder(r.reserved4);
  sys::swapByteOrder(r.reserved5);
  sys::swapByteOrder(r.reserved6);
}

inline void swapStruct(routines_command_64 &r) {
  sys::swapByteOrder(r.cmd);
  sys::swapByteOrder(r.cmdsize);
  sys::swapByteOrder(r.init_address);
  sys::swapByteOrder(r.init_module);
  sys::swapByteOrder(r.reserved1);
  sys::swapByteOrder(r.reserved2);
  sys::swapByteOrder(r.reserved3);
  sys::swapByteOrder(r.reserved4);
  sys::swapByteOrder(r.reserved5);
  sys::swapByteOrder(r.reserved6);
}

inline void swapStruct(thread_command &t) {
  sys::swapByteOrder(t.cmd);
  sys::swapByteOrder(t.cmdsize);
}

inline void swapStruct(dylinker_command &d) {
  sys::swapByteOrder(d.cmd);
  sys::swapByteOrder(d.cmdsize);
  sys::swapByteOrder(d.name);
}

inline void swapStruct(uuid_command &u) {
  sys::swapByteOrder(u.cmd);
  sys::swapByteOrder(u.cmdsize);
}

inline void swapStruct(rpath_command &r) {
  sys::swapByteOrder(r.cmd);
  sys::swapByteOrder(r.cmdsize);
  sys::swapByteOrder(r.path);
}

inline void swapStruct(source_version_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.version);
}

inline void swapStruct(entry_point_command &e) {
  sys::swapByteOrder(e.cmd);
  sys::swapByteOrder(e.cmdsize);
  sys::swapByteOrder(e.entryoff);
  sys::swapByteOrder(e.stacksize);
}

inline void swapStruct(encryption_info_command &e) {
  sys::swapByteOrder(e.cmd);
  sys::swapByteOrder(e.cmdsize);
  sys::swapByteOrder(e.cryptoff);
  sys::swapByteOrder(e.cryptsize);
  sys::swapByteOrder(e.cryptid);
}

inline void swapStruct(encryption_info_command_64 &e) {
  sys::swapByteOrder(e.cmd);
  sys::swapByteOrder(e.cmdsize);
  sys::swapByteOrder(e.cryptoff);
  sys::swapByteOrder(e.cryptsize);
  sys::swapByteOrder(e.cryptid);
  sys::swapByteOrder(e.pad);
}

inline void swapStruct(dysymtab_command &dst) {
  sys::swapByteOrder(dst.cmd);
  sys::swapByteOrder(dst.cmdsize);
  sys::swapByteOrder(dst.ilocalsym);
  sys::swapByteOrder(dst.nlocalsym);
  sys::swapByteOrder(dst.iextdefsym);
  sys::swapByteOrder(dst.nextdefsym);
  sys::swapByteOrder(dst.iundefsym);
  sys::swapByteOrder(dst.nundefsym);
  sys::swapByteOrder(dst.tocoff);
  sys::swapByteOrder(dst.ntoc);
  sys::swapByteOrder(dst.modtaboff);
  sys::swapByteOrder(dst.nmodtab);
  sys::swapByteOrder(dst.extrefsymoff);
  sys::swapByteOrder(dst.nextrefsyms);
  sys::swapByteOrder(dst.indirectsymoff);
  sys::swapByteOrder(dst.nindirectsyms);
  sys::swapByteOrder(dst.extreloff);
  sys::swapByteOrder(dst.nextrel);
  sys::swapByteOrder(dst.locreloff);
  sys::swapByteOrder(dst.nlocrel);
}

inline void swapStruct(any_relocation_info &reloc) {
  sys::swapByteOrder(reloc.r_word0);
  sys::swapByteOrder(reloc.r_word1);
}

inline void swapStruct(nlist_base &S) {
  sys::swapByteOrder(S.n_strx);
  sys::swapByteOrder(S.n_desc);
}

inline void swapStruct(nlist &sym) {
  sys::swapByteOrder(sym.n_strx);
  sys::swapByteOrder(sym.n_desc);
  sys::swapByteOrder(sym.n_value);
}

inline void swapStruct(nlist_64 &sym) {
  sys::swapByteOrder(sym.n_strx);
  sys::swapByteOrder(sym.n_desc);
  sys::swapByteOrder(sym.n_value);
}

inline void swapStruct(linkedit_data_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.dataoff);
  sys::swapByteOrder(C.datasize);
}

inline void swapStruct(linker_option_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.count);
}

inline void swapStruct(fileset_entry_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.vmaddr);
  sys::swapByteOrder(C.fileoff);
  sys::swapByteOrder(C.entry_id.offset);
  sys::swapByteOrder(C.reserved);
}

inline void swapStruct(version_min_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.version);
  sys::swapByteOrder(C.sdk);
}

inline void swapStruct(note_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.offset);
  sys::swapByteOrder(C.size);
}

inline void swapStruct(build_version_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.platform);
  sys::swapByteOrder(C.minos);
  sys::swapByteOrder(C.sdk);
  sys::swapByteOrder(C.ntools);
}

inline void swapStruct(build_tool_version &C) {
  sys::swapByteOrder(C.tool);
  sys::swapByteOrder(C.version);
}

inline void swapStruct(data_in_code_entry &C) {
  sys::swapByteOrder(C.offset);
  sys::swapByteOrder(C.length);
  sys::swapByteOrder(C.kind);
}

inline void swapStruct(uint32_t &C) { sys::swapByteOrder(C); }

// The prebind_cksum_command is obsolete and no longer supported.
inline void swapStruct(prebind_cksum_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.cksum);
}

// The twolevel_hints_command is obsolete and no longer supported.
inline void swapStruct(twolevel_hints_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.offset);
  sys::swapByteOrder(C.nhints);
}

// The prebound_dylib_command is obsolete and no longer supported.
inline void swapStruct(prebound_dylib_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.name);
  sys::swapByteOrder(C.nmodules);
  sys::swapByteOrder(C.linked_modules);
}

// The fvmfile_command is obsolete and no longer supported.
inline void swapStruct(fvmfile_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.name);
  sys::swapByteOrder(C.header_addr);
}

// The symseg_command is obsolete and no longer supported.
inline void swapStruct(symseg_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.offset);
  sys::swapByteOrder(C.size);
}

// The ident_command is obsolete and no longer supported.
inline void swapStruct(ident_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
}

inline void swapStruct(fvmlib &C) {
  sys::swapByteOrder(C.name);
  sys::swapByteOrder(C.minor_version);
  sys::swapByteOrder(C.header_addr);
}

// The fvmlib_command is obsolete and no longer supported.
inline void swapStruct(fvmlib_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  swapStruct(C.fvmlib);
}

// Get/Set functions from <mach-o/nlist.h>

inline uint16_t GET_LIBRARY_ORDINAL(uint16_t n_desc) {
  return (((n_desc) >> 8u) & 0xffu);
}

inline void SET_LIBRARY_ORDINAL(uint16_t &n_desc, uint8_t ordinal) {
  n_desc = (((n_desc)&0x00ff) | (((ordinal)&0xff) << 8));
}

inline uint8_t GET_COMM_ALIGN(uint16_t n_desc) {
  return (n_desc >> 8u) & 0x0fu;
}

inline void SET_COMM_ALIGN(uint16_t &n_desc, uint8_t align) {
  n_desc = ((n_desc & 0xf0ffu) | ((align & 0x0fu) << 8u));
}

// Enums from <mach/machine.h>
enum : uint32_t {
  // Capability bits used in the definition of cpu_type.
  CPU_ARCH_MASK = 0xff000000, // Mask for architecture bits
  CPU_ARCH_ABI64 = 0x01000000, // 64 bit ABI
  CPU_ARCH_ABI64_32 = 0x02000000, // ILP32 ABI on 64-bit hardware
};

// Constants for the cputype field.
enum CPUType {
  CPU_TYPE_ANY = -1,
  CPU_TYPE_X86 = 7,
  CPU_TYPE_I386 = CPU_TYPE_X86,
  CPU_TYPE_X86_64 = CPU_TYPE_X86 | CPU_ARCH_ABI64,
  /* CPU_TYPE_MIPS      = 8, */
  CPU_TYPE_MC98000 = 10, // Old Motorola PowerPC
  CPU_TYPE_ARM = 12,
  CPU_TYPE_ARM64 = CPU_TYPE_ARM | CPU_ARCH_ABI64,
  CPU_TYPE_ARM64_32 = CPU_TYPE_ARM | CPU_ARCH_ABI64_32,
  CPU_TYPE_SPARC = 14,
  CPU_TYPE_POWERPC = 18,
  CPU_TYPE_POWERPC64 = CPU_TYPE_POWERPC | CPU_ARCH_ABI64
};

enum : uint32_t {
  // Capability bits used in the definition of cpusubtype.
  CPU_SUBTYPE_MASK = 0xff000000,  // Mask for architecture bits
  CPU_SUBTYPE_LIB64 = 0x80000000, // 64 bit libraries

  // Special CPU subtype constants.
  CPU_SUBTYPE_MULTIPLE = ~0u
};

// Constants for the cpusubtype field.
enum CPUSubTypeX86 {
  CPU_SUBTYPE_I386_ALL = 3,
  CPU_SUBTYPE_386 = 3,
  CPU_SUBTYPE_486 = 4,
  CPU_SUBTYPE_486SX = 0x84,
  CPU_SUBTYPE_586 = 5,
  CPU_SUBTYPE_PENT = CPU_SUBTYPE_586,
  CPU_SUBTYPE_PENTPRO = 0x16,
  CPU_SUBTYPE_PENTII_M3 = 0x36,
  CPU_SUBTYPE_PENTII_M5 = 0x56,
  CPU_SUBTYPE_CELERON = 0x67,
  CPU_SUBTYPE_CELERON_MOBILE = 0x77,
  CPU_SUBTYPE_PENTIUM_3 = 0x08,
  CPU_SUBTYPE_PENTIUM_3_M = 0x18,
  CPU_SUBTYPE_PENTIUM_3_XEON = 0x28,
  CPU_SUBTYPE_PENTIUM_M = 0x09,
  CPU_SUBTYPE_PENTIUM_4 = 0x0a,
  CPU_SUBTYPE_PENTIUM_4_M = 0x1a,
  CPU_SUBTYPE_ITANIUM = 0x0b,
  CPU_SUBTYPE_ITANIUM_2 = 0x1b,
  CPU_SUBTYPE_XEON = 0x0c,
  CPU_SUBTYPE_XEON_MP = 0x1c,

  CPU_SUBTYPE_X86_ALL = 3,
  CPU_SUBTYPE_X86_64_ALL = 3,
  CPU_SUBTYPE_X86_ARCH1 = 4,
  CPU_SUBTYPE_X86_64_H = 8
};
inline int CPU_SUBTYPE_INTEL(int Family, int Model) {
  return Family | (Model << 4);
}
inline int CPU_SUBTYPE_INTEL_FAMILY(CPUSubTypeX86 ST) {
  return ((int)ST) & 0x0f;
}
inline int CPU_SUBTYPE_INTEL_MODEL(CPUSubTypeX86 ST) { return ((int)ST) >> 4; }
enum { CPU_SUBTYPE_INTEL_FAMILY_MAX = 15, CPU_SUBTYPE_INTEL_MODEL_ALL = 0 };

enum CPUSubTypeARM {
  CPU_SUBTYPE_ARM_ALL = 0,
  CPU_SUBTYPE_ARM_V4T = 5,
  CPU_SUBTYPE_ARM_V6 = 6,
  CPU_SUBTYPE_ARM_V5 = 7,
  CPU_SUBTYPE_ARM_V5TEJ = 7,
  CPU_SUBTYPE_ARM_XSCALE = 8,
  CPU_SUBTYPE_ARM_V7 = 9,
  //  unused  ARM_V7F     = 10,
  CPU_SUBTYPE_ARM_V7S = 11,
  CPU_SUBTYPE_ARM_V7K = 12,
  CPU_SUBTYPE_ARM_V6M = 14,
  CPU_SUBTYPE_ARM_V7M = 15,
  CPU_SUBTYPE_ARM_V7EM = 16
};

enum CPUSubTypeARM64 {
  CPU_SUBTYPE_ARM64_ALL = 0,
  CPU_SUBTYPE_ARM64_V8 = 1,
  CPU_SUBTYPE_ARM64E = 2,
};

enum CPUSubTypeARM64_32 { CPU_SUBTYPE_ARM64_32_V8 = 1 };

enum CPUSubTypeSPARC { CPU_SUBTYPE_SPARC_ALL = 0 };

enum CPUSubTypePowerPC {
  CPU_SUBTYPE_POWERPC_ALL = 0,
  CPU_SUBTYPE_POWERPC_601 = 1,
  CPU_SUBTYPE_POWERPC_602 = 2,
  CPU_SUBTYPE_POWERPC_603 = 3,
  CPU_SUBTYPE_POWERPC_603e = 4,
  CPU_SUBTYPE_POWERPC_603ev = 5,
  CPU_SUBTYPE_POWERPC_604 = 6,
  CPU_SUBTYPE_POWERPC_604e = 7,
  CPU_SUBTYPE_POWERPC_620 = 8,
  CPU_SUBTYPE_POWERPC_750 = 9,
  CPU_SUBTYPE_POWERPC_7400 = 10,
  CPU_SUBTYPE_POWERPC_7450 = 11,
  CPU_SUBTYPE_POWERPC_970 = 100,

  CPU_SUBTYPE_MC980000_ALL = CPU_SUBTYPE_POWERPC_ALL,
  CPU_SUBTYPE_MC98601 = CPU_SUBTYPE_POWERPC_601
};

Expected<uint32_t> getCPUType(const Triple &T);
Expected<uint32_t> getCPUSubType(const Triple &T);

struct x86_thread_state32_t {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp;
  uint32_t ss;
  uint32_t eflags;
  uint32_t eip;
  uint32_t cs;
  uint32_t ds;
  uint32_t es;
  uint32_t fs;
  uint32_t gs;
};

struct x86_thread_state64_t {
  uint64_t rax;
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rbp;
  uint64_t rsp;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rip;
  uint64_t rflags;
  uint64_t cs;
  uint64_t fs;
  uint64_t gs;
};

enum x86_fp_control_precis {
  x86_FP_PREC_24B = 0,
  x86_FP_PREC_53B = 2,
  x86_FP_PREC_64B = 3
};

enum x86_fp_control_rc {
  x86_FP_RND_NEAR = 0,
  x86_FP_RND_DOWN = 1,
  x86_FP_RND_UP = 2,
  x86_FP_CHOP = 3
};

struct fp_control_t {
  unsigned short invalid : 1, denorm : 1, zdiv : 1, ovrfl : 1, undfl : 1,
      precis : 1, : 2, pc : 2, rc : 2, : 1, : 3;
};

struct fp_status_t {
  unsigned short invalid : 1, denorm : 1, zdiv : 1, ovrfl : 1, undfl : 1,
      precis : 1, stkflt : 1, errsumm : 1, c0 : 1, c1 : 1, c2 : 1, tos : 3,
      c3 : 1, busy : 1;
};

struct mmst_reg_t {
  char mmst_reg[10];
  char mmst_rsrv[6];
};

struct xmm_reg_t {
  char xmm_reg[16];
};

struct x86_float_state64_t {
  int32_t fpu_reserved[2];
  fp_control_t fpu_fcw;
  fp_status_t fpu_fsw;
  uint8_t fpu_ftw;
  uint8_t fpu_rsrv1;
  uint16_t fpu_fop;
  uint32_t fpu_ip;
  uint16_t fpu_cs;
  uint16_t fpu_rsrv2;
  uint32_t fpu_dp;
  uint16_t fpu_ds;
  uint16_t fpu_rsrv3;
  uint32_t fpu_mxcsr;
  uint32_t fpu_mxcsrmask;
  mmst_reg_t fpu_stmm0;
  mmst_reg_t fpu_stmm1;
  mmst_reg_t fpu_stmm2;
  mmst_reg_t fpu_stmm3;
  mmst_reg_t fpu_stmm4;
  mmst_reg_t fpu_stmm5;
  mmst_reg_t fpu_stmm6;
  mmst_reg_t fpu_stmm7;
  xmm_reg_t fpu_xmm0;
  xmm_reg_t fpu_xmm1;
  xmm_reg_t fpu_xmm2;
  xmm_reg_t fpu_xmm3;
  xmm_reg_t fpu_xmm4;
  xmm_reg_t fpu_xmm5;
  xmm_reg_t fpu_xmm6;
  xmm_reg_t fpu_xmm7;
  xmm_reg_t fpu_xmm8;
  xmm_reg_t fpu_xmm9;
  xmm_reg_t fpu_xmm10;
  xmm_reg_t fpu_xmm11;
  xmm_reg_t fpu_xmm12;
  xmm_reg_t fpu_xmm13;
  xmm_reg_t fpu_xmm14;
  xmm_reg_t fpu_xmm15;
  char fpu_rsrv4[6 * 16];
  uint32_t fpu_reserved1;
};

struct x86_exception_state64_t {
  uint16_t trapno;
  uint16_t cpu;
  uint32_t err;
  uint64_t faultvaddr;
};

inline void swapStruct(x86_thread_state32_t &x) {
  sys::swapByteOrder(x.eax);
  sys::swapByteOrder(x.ebx);
  sys::swapByteOrder(x.ecx);
  sys::swapByteOrder(x.edx);
  sys::swapByteOrder(x.edi);
  sys::swapByteOrder(x.esi);
  sys::swapByteOrder(x.ebp);
  sys::swapByteOrder(x.esp);
  sys::swapByteOrder(x.ss);
  sys::swapByteOrder(x.eflags);
  sys::swapByteOrder(x.eip);
  sys::swapByteOrder(x.cs);
  sys::swapByteOrder(x.ds);
  sys::swapByteOrder(x.es);
  sys::swapByteOrder(x.fs);
  sys::swapByteOrder(x.gs);
}

inline void swapStruct(x86_thread_state64_t &x) {
  sys::swapByteOrder(x.rax);
  sys::swapByteOrder(x.rbx);
  sys::swapByteOrder(x.rcx);
  sys::swapByteOrder(x.rdx);
  sys::swapByteOrder(x.rdi);
  sys::swapByteOrder(x.rsi);
  sys::swapByteOrder(x.rbp);
  sys::swapByteOrder(x.rsp);
  sys::swapByteOrder(x.r8);
  sys::swapByteOrder(x.r9);
  sys::swapByteOrder(x.r10);
  sys::swapByteOrder(x.r11);
  sys::swapByteOrder(x.r12);
  sys::swapByteOrder(x.r13);
  sys::swapByteOrder(x.r14);
  sys::swapByteOrder(x.r15);
  sys::swapByteOrder(x.rip);
  sys::swapByteOrder(x.rflags);
  sys::swapByteOrder(x.cs);
  sys::swapByteOrder(x.fs);
  sys::swapByteOrder(x.gs);
}

inline void swapStruct(x86_float_state64_t &x) {
  sys::swapByteOrder(x.fpu_reserved[0]);
  sys::swapByteOrder(x.fpu_reserved[1]);
  // TODO swap: fp_control_t fpu_fcw;
  // TODO swap: fp_status_t fpu_fsw;
  sys::swapByteOrder(x.fpu_fop);
  sys::swapByteOrder(x.fpu_ip);
  sys::swapByteOrder(x.fpu_cs);
  sys::swapByteOrder(x.fpu_rsrv2);
  sys::swapByteOrder(x.fpu_dp);
  sys::swapByteOrder(x.fpu_ds);
  sys::swapByteOrder(x.fpu_rsrv3);
  sys::swapByteOrder(x.fpu_mxcsr);
  sys::swapByteOrder(x.fpu_mxcsrmask);
  sys::swapByteOrder(x.fpu_reserved1);
}

inline void swapStruct(x86_exception_state64_t &x) {
  sys::swapByteOrder(x.trapno);
  sys::swapByteOrder(x.cpu);
  sys::swapByteOrder(x.err);
  sys::swapByteOrder(x.faultvaddr);
}

struct x86_state_hdr_t {
  uint32_t flavor;
  uint32_t count;
};

struct x86_thread_state_t {
  x86_state_hdr_t tsh;
  union {
    x86_thread_state64_t ts64;
    x86_thread_state32_t ts32;
  } uts;
};

struct x86_float_state_t {
  x86_state_hdr_t fsh;
  union {
    x86_float_state64_t fs64;
  } ufs;
};

struct x86_exception_state_t {
  x86_state_hdr_t esh;
  union {
    x86_exception_state64_t es64;
  } ues;
};

inline void swapStruct(x86_state_hdr_t &x) {
  sys::swapByteOrder(x.flavor);
  sys::swapByteOrder(x.count);
}

enum X86ThreadFlavors {
  x86_THREAD_STATE32 = 1,
  x86_FLOAT_STATE32 = 2,
  x86_EXCEPTION_STATE32 = 3,
  x86_THREAD_STATE64 = 4,
  x86_FLOAT_STATE64 = 5,
  x86_EXCEPTION_STATE64 = 6,
  x86_THREAD_STATE = 7,
  x86_FLOAT_STATE = 8,
  x86_EXCEPTION_STATE = 9,
  x86_DEBUG_STATE32 = 10,
  x86_DEBUG_STATE64 = 11,
  x86_DEBUG_STATE = 12
};

inline void swapStruct(x86_thread_state_t &x) {
  swapStruct(x.tsh);
  if (x.tsh.flavor == x86_THREAD_STATE64)
    swapStruct(x.uts.ts64);
}

inline void swapStruct(x86_float_state_t &x) {
  swapStruct(x.fsh);
  if (x.fsh.flavor == x86_FLOAT_STATE64)
    swapStruct(x.ufs.fs64);
}

inline void swapStruct(x86_exception_state_t &x) {
  swapStruct(x.esh);
  if (x.esh.flavor == x86_EXCEPTION_STATE64)
    swapStruct(x.ues.es64);
}

const uint32_t x86_THREAD_STATE32_COUNT =
    sizeof(x86_thread_state32_t) / sizeof(uint32_t);

const uint32_t x86_THREAD_STATE64_COUNT =
    sizeof(x86_thread_state64_t) / sizeof(uint32_t);
const uint32_t x86_FLOAT_STATE64_COUNT =
    sizeof(x86_float_state64_t) / sizeof(uint32_t);
const uint32_t x86_EXCEPTION_STATE64_COUNT =
    sizeof(x86_exception_state64_t) / sizeof(uint32_t);

const uint32_t x86_THREAD_STATE_COUNT =
    sizeof(x86_thread_state_t) / sizeof(uint32_t);
const uint32_t x86_FLOAT_STATE_COUNT =
    sizeof(x86_float_state_t) / sizeof(uint32_t);
const uint32_t x86_EXCEPTION_STATE_COUNT =
    sizeof(x86_exception_state_t) / sizeof(uint32_t);

struct arm_thread_state32_t {
  uint32_t r[13];
  uint32_t sp;
  uint32_t lr;
  uint32_t pc;
  uint32_t cpsr;
};

inline void swapStruct(arm_thread_state32_t &x) {
  for (int i = 0; i < 13; i++)
    sys::swapByteOrder(x.r[i]);
  sys::swapByteOrder(x.sp);
  sys::swapByteOrder(x.lr);
  sys::swapByteOrder(x.pc);
  sys::swapByteOrder(x.cpsr);
}

struct arm_thread_state64_t {
  uint64_t x[29];
  uint64_t fp;
  uint64_t lr;
  uint64_t sp;
  uint64_t pc;
  uint32_t cpsr;
  uint32_t pad;
};

inline void swapStruct(arm_thread_state64_t &x) {
  for (int i = 0; i < 29; i++)
    sys::swapByteOrder(x.x[i]);
  sys::swapByteOrder(x.fp);
  sys::swapByteOrder(x.lr);
  sys::swapByteOrder(x.sp);
  sys::swapByteOrder(x.pc);
  sys::swapByteOrder(x.cpsr);
}

struct arm_state_hdr_t {
  uint32_t flavor;
  uint32_t count;
};

struct arm_thread_state_t {
  arm_state_hdr_t tsh;
  union {
    arm_thread_state32_t ts32;
  } uts;
};

inline void swapStruct(arm_state_hdr_t &x) {
  sys::swapByteOrder(x.flavor);
  sys::swapByteOrder(x.count);
}

enum ARMThreadFlavors {
  ARM_THREAD_STATE = 1,
  ARM_VFP_STATE = 2,
  ARM_EXCEPTION_STATE = 3,
  ARM_DEBUG_STATE = 4,
  ARN_THREAD_STATE_NONE = 5,
  ARM_THREAD_STATE64 = 6,
  ARM_EXCEPTION_STATE64 = 7
};

inline void swapStruct(arm_thread_state_t &x) {
  swapStruct(x.tsh);
  if (x.tsh.flavor == ARM_THREAD_STATE)
    swapStruct(x.uts.ts32);
}

const uint32_t ARM_THREAD_STATE_COUNT =
    sizeof(arm_thread_state32_t) / sizeof(uint32_t);

const uint32_t ARM_THREAD_STATE64_COUNT =
    sizeof(arm_thread_state64_t) / sizeof(uint32_t);

struct ppc_thread_state32_t {
  uint32_t srr0;
  uint32_t srr1;
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;
  uint32_t r8;
  uint32_t r9;
  uint32_t r10;
  uint32_t r11;
  uint32_t r12;
  uint32_t r13;
  uint32_t r14;
  uint32_t r15;
  uint32_t r16;
  uint32_t r17;
  uint32_t r18;
  uint32_t r19;
  uint32_t r20;
  uint32_t r21;
  uint32_t r22;
  uint32_t r23;
  uint32_t r24;
  uint32_t r25;
  uint32_t r26;
  uint32_t r27;
  uint32_t r28;
  uint32_t r29;
  uint32_t r30;
  uint32_t r31;
  uint32_t ct;
  uint32_t xer;
  uint32_t lr;
  uint32_t ctr;
  uint32_t mq;
  uint32_t vrsave;
};

inline void swapStruct(ppc_thread_state32_t &x) {
  sys::swapByteOrder(x.srr0);
  sys::swapByteOrder(x.srr1);
  sys::swapByteOrder(x.r0);
  sys::swapByteOrder(x.r1);
  sys::swapByteOrder(x.r2);
  sys::swapByteOrder(x.r3);
  sys::swapByteOrder(x.r4);
  sys::swapByteOrder(x.r5);
  sys::swapByteOrder(x.r6);
  sys::swapByteOrder(x.r7);
  sys::swapByteOrder(x.r8);
  sys::swapByteOrder(x.r9);
  sys::swapByteOrder(x.r10);
  sys::swapByteOrder(x.r11);
  sys::swapByteOrder(x.r12);
  sys::swapByteOrder(x.r13);
  sys::swapByteOrder(x.r14);
  sys::swapByteOrder(x.r15);
  sys::swapByteOrder(x.r16);
  sys::swapByteOrder(x.r17);
  sys::swapByteOrder(x.r18);
  sys::swapByteOrder(x.r19);
  sys::swapByteOrder(x.r20);
  sys::swapByteOrder(x.r21);
  sys::swapByteOrder(x.r22);
  sys::swapByteOrder(x.r23);
  sys::swapByteOrder(x.r24);
  sys::swapByteOrder(x.r25);
  sys::swapByteOrder(x.r26);
  sys::swapByteOrder(x.r27);
  sys::swapByteOrder(x.r28);
  sys::swapByteOrder(x.r29);
  sys::swapByteOrder(x.r30);
  sys::swapByteOrder(x.r31);
  sys::swapByteOrder(x.ct);
  sys::swapByteOrder(x.xer);
  sys::swapByteOrder(x.lr);
  sys::swapByteOrder(x.ctr);
  sys::swapByteOrder(x.mq);
  sys::swapByteOrder(x.vrsave);
}

struct ppc_state_hdr_t {
  uint32_t flavor;
  uint32_t count;
};

struct ppc_thread_state_t {
  ppc_state_hdr_t tsh;
  union {
    ppc_thread_state32_t ts32;
  } uts;
};

inline void swapStruct(ppc_state_hdr_t &x) {
  sys::swapByteOrder(x.flavor);
  sys::swapByteOrder(x.count);
}

enum PPCThreadFlavors {
  PPC_THREAD_STATE = 1,
  PPC_FLOAT_STATE = 2,
  PPC_EXCEPTION_STATE = 3,
  PPC_VECTOR_STATE = 4,
  PPC_THREAD_STATE64 = 5,
  PPC_EXCEPTION_STATE64 = 6,
  PPC_THREAD_STATE_NONE = 7
};

inline void swapStruct(ppc_thread_state_t &x) {
  swapStruct(x.tsh);
  if (x.tsh.flavor == PPC_THREAD_STATE)
    swapStruct(x.uts.ts32);
}

const uint32_t PPC_THREAD_STATE_COUNT =
    sizeof(ppc_thread_state32_t) / sizeof(uint32_t);

// Define a union of all load command structs
#define LOAD_COMMAND_STRUCT(LCStruct) LCStruct LCStruct##_data;

LLVM_PACKED_START
union alignas(4) macho_load_command {
#include "llvm/BinaryFormat/MachO.def"
};
LLVM_PACKED_END

inline void swapStruct(dyld_chained_fixups_header &C) {
  sys::swapByteOrder(C.fixups_version);
  sys::swapByteOrder(C.starts_offset);
  sys::swapByteOrder(C.imports_offset);
  sys::swapByteOrder(C.symbols_offset);
  sys::swapByteOrder(C.imports_count);
  sys::swapByteOrder(C.imports_format);
  sys::swapByteOrder(C.symbols_format);
}

inline void swapStruct(dyld_chained_starts_in_image &C) {
  sys::swapByteOrder(C.seg_count);
  // getStructOrErr() cannot copy the variable-length seg_info_offset array.
  // Its elements must be byte swapped manually.
}

inline void swapStruct(dyld_chained_starts_in_segment &C) {
  sys::swapByteOrder(C.size);
  sys::swapByteOrder(C.page_size);
  sys::swapByteOrder(C.pointer_format);
  sys::swapByteOrder(C.segment_offset);
  sys::swapByteOrder(C.max_valid_pointer);
  sys::swapByteOrder(C.page_count);
  // seg_info_offset entries must be byte swapped manually.
}

/* code signing attributes of a process */

enum CodeSignAttrs {
  CS_VALID = 0x00000001,          /* dynamically valid */
  CS_ADHOC = 0x00000002,          /* ad hoc signed */
  CS_GET_TASK_ALLOW = 0x00000004, /* has get-task-allow entitlement */
  CS_INSTALLER = 0x00000008,      /* has installer entitlement */

  CS_FORCED_LV =
      0x00000010, /* Library Validation required by Hardened System Policy */
  CS_INVALID_ALLOWED = 0x00000020, /* (macOS Only) Page invalidation allowed by
                                      task port policy */

  CS_HARD = 0x00000100,             /* don't load invalid pages */
  CS_KILL = 0x00000200,             /* kill process if it becomes invalid */
  CS_CHECK_EXPIRATION = 0x00000400, /* force expiration checking */
  CS_RESTRICT = 0x00000800,         /* tell dyld to treat restricted */

  CS_ENFORCEMENT = 0x00001000, /* require enforcement */
  CS_REQUIRE_LV = 0x00002000,  /* require library validation */
  CS_ENTITLEMENTS_VALIDATED =
      0x00004000, /* code signature permits restricted entitlements */
  CS_NVRAM_UNRESTRICTED =
      0x00008000, /* has com.apple.rootless.restricted-nvram-variables.heritable
                     entitlement */

  CS_RUNTIME = 0x00010000,       /* Apply hardened runtime policies */
  CS_LINKER_SIGNED = 0x00020000, /* Automatically signed by the linker */

  CS_ALLOWED_MACHO =
      (CS_ADHOC | CS_HARD | CS_KILL | CS_CHECK_EXPIRATION | CS_RESTRICT |
       CS_ENFORCEMENT | CS_REQUIRE_LV | CS_RUNTIME | CS_LINKER_SIGNED),

  CS_EXEC_SET_HARD = 0x00100000, /* set CS_HARD on any exec'ed process */
  CS_EXEC_SET_KILL = 0x00200000, /* set CS_KILL on any exec'ed process */
  CS_EXEC_SET_ENFORCEMENT =
      0x00400000, /* set CS_ENFORCEMENT on any exec'ed process */
  CS_EXEC_INHERIT_SIP =
      0x00800000, /* set CS_INSTALLER on any exec'ed process */

  CS_KILLED = 0x01000000, /* was killed by kernel for invalidity */
  CS_DYLD_PLATFORM =
      0x02000000, /* dyld used to load this is a platform binary */
  CS_PLATFORM_BINARY = 0x04000000, /* this is a platform binary */
  CS_PLATFORM_PATH =
      0x08000000, /* platform binary by the fact of path (osx only) */

  CS_DEBUGGED = 0x10000000, /* process is currently or has previously been
                debugged and allowed to run with invalid pages */
  CS_SIGNED = 0x20000000, /* process has a signature (may have gone invalid) */
  CS_DEV_CODE =
      0x40000000, /* code is dev signed, cannot be loaded into prod signed code
                     (will go away with rdar://problem/28322552) */
  CS_DATAVAULT_CONTROLLER =
      0x80000000, /* has Data Vault controller entitlement */

  CS_ENTITLEMENT_FLAGS = (CS_GET_TASK_ALLOW | CS_INSTALLER |
                          CS_DATAVAULT_CONTROLLER | CS_NVRAM_UNRESTRICTED),
};

/* executable segment flags */

enum CodeSignExecSegFlags {

  CS_EXECSEG_MAIN_BINARY = 0x1,     /* executable segment denotes main binary */
  CS_EXECSEG_ALLOW_UNSIGNED = 0x10, /* allow unsigned pages (for debugging) */
  CS_EXECSEG_DEBUGGER = 0x20,       /* main binary is debugger */
  CS_EXECSEG_JIT = 0x40,            /* JIT enabled */
  CS_EXECSEG_SKIP_LV = 0x80,        /* OBSOLETE: skip library validation */
  CS_EXECSEG_CAN_LOAD_CDHASH = 0x100, /* can bless cdhash for execution */
  CS_EXECSEG_CAN_EXEC_CDHASH = 0x200, /* can execute blessed cdhash */

};

/* Magic numbers used by Code Signing */

enum CodeSignMagic {
  CSMAGIC_REQUIREMENT = 0xfade0c00, /* single Requirement blob */
  CSMAGIC_REQUIREMENTS =
      0xfade0c01, /* Requirements vector (internal requirements) */
  CSMAGIC_CODEDIRECTORY = 0xfade0c02,      /* CodeDirectory blob */
  CSMAGIC_EMBEDDED_SIGNATURE = 0xfade0cc0, /* embedded form of signature data */
  CSMAGIC_EMBEDDED_SIGNATURE_OLD = 0xfade0b02, /* XXX */
  CSMAGIC_EMBEDDED_ENTITLEMENTS = 0xfade7171,  /* embedded entitlements */
  CSMAGIC_DETACHED_SIGNATURE =
      0xfade0cc1, /* multi-arch collection of embedded signatures */
  CSMAGIC_BLOBWRAPPER = 0xfade0b01, /* CMS Signature, among other things */

  CS_SUPPORTSSCATTER = 0x20100,
  CS_SUPPORTSTEAMID = 0x20200,
  CS_SUPPORTSCODELIMIT64 = 0x20300,
  CS_SUPPORTSEXECSEG = 0x20400,
  CS_SUPPORTSRUNTIME = 0x20500,
  CS_SUPPORTSLINKAGE = 0x20600,

  CSSLOT_CODEDIRECTORY = 0, /* slot index for CodeDirectory */
  CSSLOT_INFOSLOT = 1,
  CSSLOT_REQUIREMENTS = 2,
  CSSLOT_RESOURCEDIR = 3,
  CSSLOT_APPLICATION = 4,
  CSSLOT_ENTITLEMENTS = 5,

  CSSLOT_ALTERNATE_CODEDIRECTORIES =
      0x1000, /* first alternate CodeDirectory, if any */
  CSSLOT_ALTERNATE_CODEDIRECTORY_MAX = 5, /* max number of alternate CD slots */
  CSSLOT_ALTERNATE_CODEDIRECTORY_LIMIT =
      CSSLOT_ALTERNATE_CODEDIRECTORIES +
      CSSLOT_ALTERNATE_CODEDIRECTORY_MAX, /* one past the last */

  CSSLOT_SIGNATURESLOT = 0x10000, /* CMS Signature */
  CSSLOT_IDENTIFICATIONSLOT = 0x10001,
  CSSLOT_TICKETSLOT = 0x10002,

  CSTYPE_INDEX_REQUIREMENTS = 0x00000002, /* compat with amfi */
  CSTYPE_INDEX_ENTITLEMENTS = 0x00000005, /* compat with amfi */

  CS_HASHTYPE_SHA1 = 1,
  CS_HASHTYPE_SHA256 = 2,
  CS_HASHTYPE_SHA256_TRUNCATED = 3,
  CS_HASHTYPE_SHA384 = 4,

  CS_SHA1_LEN = 20,
  CS_SHA256_LEN = 32,
  CS_SHA256_TRUNCATED_LEN = 20,

  CS_CDHASH_LEN = 20,    /* always - larger hashes are truncated */
  CS_HASH_MAX_SIZE = 48, /* max size of the hash we'll support */

  /*
   * Currently only to support Legacy VPN plugins, and Mac App Store
   * but intended to replace all the various platform code, dev code etc. bits.
   */
  CS_SIGNER_TYPE_UNKNOWN = 0,
  CS_SIGNER_TYPE_LEGACYVPN = 5,
  CS_SIGNER_TYPE_MAC_APP_STORE = 6,

  CS_SUPPL_SIGNER_TYPE_UNKNOWN = 0,
  CS_SUPPL_SIGNER_TYPE_TRUSTCACHE = 7,
  CS_SUPPL_SIGNER_TYPE_LOCAL = 8,
};

struct CS_CodeDirectory {
  uint32_t magic;         /* magic number (CSMAGIC_CODEDIRECTORY) */
  uint32_t length;        /* total length of CodeDirectory blob */
  uint32_t version;       /* compatibility version */
  uint32_t flags;         /* setup and mode flags */
  uint32_t hashOffset;    /* offset of hash slot element at index zero */
  uint32_t identOffset;   /* offset of identifier string */
  uint32_t nSpecialSlots; /* number of special hash slots */
  uint32_t nCodeSlots;    /* number of ordinary (code) hash slots */
  uint32_t codeLimit;     /* limit to main image signature range */
  uint8_t hashSize;       /* size of each hash in bytes */
  uint8_t hashType;       /* type of hash (cdHashType* constants) */
  uint8_t platform;       /* platform identifier; zero if not platform binary */
  uint8_t pageSize;       /* log2(page size in bytes); 0 => infinite */
  uint32_t spare2;        /* unused (must be zero) */

  /* Version 0x20100 */
  uint32_t scatterOffset; /* offset of optional scatter vector */

  /* Version 0x20200 */
  uint32_t teamOffset; /* offset of optional team identifier */

  /* Version 0x20300 */
  uint32_t spare3;      /* unused (must be zero) */
  uint64_t codeLimit64; /* limit to main image signature range, 64 bits */

  /* Version 0x20400 */
  uint64_t execSegBase;  /* offset of executable segment */
  uint64_t execSegLimit; /* limit of executable segment */
  uint64_t execSegFlags; /* executable segment flags */
};

static_assert(sizeof(CS_CodeDirectory) == 88);

struct CS_BlobIndex {
  uint32_t type;   /* type of entry */
  uint32_t offset; /* offset of entry */
};

struct CS_SuperBlob {
  uint32_t magic;  /* magic number */
  uint32_t length; /* total length of SuperBlob */
  uint32_t count;  /* number of index entries following */
  /* followed by Blobs in no particular order as indicated by index offsets */
};

enum SecCSDigestAlgorithm {
  kSecCodeSignatureNoHash = 0,     /* null value */
  kSecCodeSignatureHashSHA1 = 1,   /* SHA-1 */
  kSecCodeSignatureHashSHA256 = 2, /* SHA-256 */
  kSecCodeSignatureHashSHA256Truncated =
      3,                           /* SHA-256 truncated to first 20 bytes */
  kSecCodeSignatureHashSHA384 = 4, /* SHA-384 */
  kSecCodeSignatureHashSHA512 = 5, /* SHA-512 */
};

enum LinkerOptimizationHintKind {
  LOH_ARM64_ADRP_ADRP = 1,
  LOH_ARM64_ADRP_LDR = 2,
  LOH_ARM64_ADRP_ADD_LDR = 3,
  LOH_ARM64_ADRP_LDR_GOT_LDR = 4,
  LOH_ARM64_ADRP_ADD_STR = 5,
  LOH_ARM64_ADRP_LDR_GOT_STR = 6,
  LOH_ARM64_ADRP_ADD = 7,
  LOH_ARM64_ADRP_LDR_GOT = 8,
};

} // end namespace MachO
} // end namespace llvm

#endif
