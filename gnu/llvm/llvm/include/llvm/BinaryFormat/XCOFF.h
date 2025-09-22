//===-- llvm/BinaryFormat/XCOFF.h - The XCOFF file format -------*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines manifest constants for the XCOFF object file format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_XCOFF_H
#define LLVM_BINARYFORMAT_XCOFF_H

#include <stddef.h>
#include <stdint.h>

namespace llvm {
class StringRef;
template <unsigned> class SmallString;
template <typename T> class Expected;

namespace XCOFF {

// Constants used in the XCOFF definition.

constexpr size_t FileNamePadSize = 6;
constexpr size_t NameSize = 8;
constexpr size_t AuxFileEntNameSize = 14;
constexpr size_t FileHeaderSize32 = 20;
constexpr size_t FileHeaderSize64 = 24;
constexpr size_t AuxFileHeaderSize32 = 72;
constexpr size_t AuxFileHeaderSize64 = 110;
constexpr size_t AuxFileHeaderSizeShort = 28;
constexpr size_t SectionHeaderSize32 = 40;
constexpr size_t SectionHeaderSize64 = 72;
constexpr size_t SymbolTableEntrySize = 18;
constexpr size_t RelocationSerializationSize32 = 10;
constexpr size_t RelocationSerializationSize64 = 14;
constexpr size_t ExceptionSectionEntrySize32 = 6;
constexpr size_t ExceptionSectionEntrySize64 = 10;
constexpr uint16_t RelocOverflow = 65535;
constexpr uint8_t AllocRegNo = 31;

enum ReservedSectionNum : int16_t { N_DEBUG = -2, N_ABS = -1, N_UNDEF = 0 };

enum MagicNumber : uint16_t { XCOFF32 = 0x01DF, XCOFF64 = 0x01F7 };

// Masks for packing/unpacking the r_rsize field of relocations.

// The msb is used to indicate if the bits being relocated are signed or
// unsigned.
static constexpr uint8_t XR_SIGN_INDICATOR_MASK = 0x80;
// The 2nd msb is used to indicate that the binder has replaced/modified the
// original instruction.
static constexpr uint8_t XR_FIXUP_INDICATOR_MASK = 0x40;
// The remaining bits specify the bit length of the relocatable reference
// minus one.
static constexpr uint8_t XR_BIASED_LENGTH_MASK = 0x3f;

// This field only exists in the XCOFF64 definition.
enum AuxHeaderFlags64 : uint16_t {
  SHR_SYMTAB = 0x8000,  ///< At exec time, create shared symbol table for program
                        ///< (main program only).
  FORK_POLICY = 0x4000, ///< Forktree policy specified (main program only).
  FORK_COR = 0x2000     ///< If _AOUT_FORK_POLICY is set, specify copy-on-reference
                        ///< if this bit is set. Specify copy-on- write otherwise.
                        ///< If _AOUT_FORK_POLICY is 0, this bit is reserved for
                        ///< future use and should be set to 0.
};

enum XCOFFInterpret : uint16_t {
  OLD_XCOFF_INTERPRET = 1,
  NEW_XCOFF_INTERPRET = 2
};

enum FileFlag : uint16_t {
  F_RELFLG = 0x0001,    ///< relocation info stripped from file
  F_EXEC = 0x0002,      ///< file is executable (i.e., it
                        ///< has a loader section)
  F_LNNO = 0x0004,      ///< line numbers stripped from file
  F_LSYMS = 0x0008,     ///< local symbols stripped from file
  F_FDPR_PROF = 0x0010, ///< file was profiled with FDPR
  F_FDPR_OPTI = 0x0020, ///< file was reordered with FDPR
  F_DSA = 0x0040,       ///< file uses Dynamic Segment Allocation (32-bit
                        ///< only)
  F_DEP_1 = 0x0080,     ///< Data Execution Protection bit 1
  F_VARPG = 0x0100,     ///< executable requests using variable size pages
  F_LPTEXT = 0x0400,    ///< executable requires large pages for text
  F_LPDATA = 0x0800,    ///< executable requires large pages for data
  F_DYNLOAD = 0x1000,   ///< file is dynamically loadable and
                        ///< executable (equivalent to F_EXEC on AIX)
  F_SHROBJ = 0x2000,    ///< file is a shared object
  F_LOADONLY =
      0x4000,      ///< file can be loaded by the system loader, but it is
                   ///< ignored by the linker if it is a member of an archive.
  F_DEP_2 = 0x8000 ///< Data Execution Protection bit 2
};

// x_smclas field of x_csect from system header: /usr/include/syms.h
/// Storage Mapping Class definitions.
enum StorageMappingClass : uint8_t {
  //     READ ONLY CLASSES
  XMC_PR = 0,      ///< Program Code
  XMC_RO = 1,      ///< Read Only Constant
  XMC_DB = 2,      ///< Debug Dictionary Table
  XMC_GL = 6,      ///< Global Linkage (Interfile Interface Code)
  XMC_XO = 7,      ///< Extended Operation (Pseudo Machine Instruction)
  XMC_SV = 8,      ///< Supervisor Call (32-bit process only)
  XMC_SV64 = 17,   ///< Supervisor Call for 64-bit process
  XMC_SV3264 = 18, ///< Supervisor Call for both 32- and 64-bit processes
  XMC_TI = 12,     ///< Traceback Index csect
  XMC_TB = 13,     ///< Traceback Table csect

  //       READ WRITE CLASSES
  XMC_RW = 5,   ///< Read Write Data
  XMC_TC0 = 15, ///< TOC Anchor for TOC Addressability
  XMC_TC = 3,   ///< General TOC item
  XMC_TD = 16,  ///< Scalar data item in the TOC
  XMC_DS = 10,  ///< Descriptor csect
  XMC_UA = 4,   ///< Unclassified - Treated as Read Write
  XMC_BS = 9,   ///< BSS class (uninitialized static internal)
  XMC_UC = 11,  ///< Un-named Fortran Common

  XMC_TL = 20, ///< Initialized thread-local variable
  XMC_UL = 21, ///< Uninitialized thread-local variable
  XMC_TE = 22  ///< Symbol mapped at the end of TOC
};

// Flags for defining the section type. Masks for use with the (signed, 32-bit)
// s_flags field of the section header structure, selecting for values in the
// lower 16 bits. Defined in the system header `scnhdr.h`.
enum SectionTypeFlags : int32_t {
  STYP_PAD = 0x0008,
  STYP_DWARF = 0x0010,
  STYP_TEXT = 0x0020,
  STYP_DATA = 0x0040,
  STYP_BSS = 0x0080,
  STYP_EXCEPT = 0x0100,
  STYP_INFO = 0x0200,
  STYP_TDATA = 0x0400,
  STYP_TBSS = 0x0800,
  STYP_LOADER = 0x1000,
  STYP_DEBUG = 0x2000,
  STYP_TYPCHK = 0x4000,
  STYP_OVRFLO = 0x8000
};

/// Values for defining the section subtype of sections of type STYP_DWARF as
/// they would appear in the (signed, 32-bit) s_flags field of the section
/// header structure, contributing to the 16 most significant bits. Defined in
/// the system header `scnhdr.h`.
enum DwarfSectionSubtypeFlags : int32_t {
  SSUBTYP_DWINFO = 0x1'0000,  ///< DWARF info section
  SSUBTYP_DWLINE = 0x2'0000,  ///< DWARF line section
  SSUBTYP_DWPBNMS = 0x3'0000, ///< DWARF pubnames section
  SSUBTYP_DWPBTYP = 0x4'0000, ///< DWARF pubtypes section
  SSUBTYP_DWARNGE = 0x5'0000, ///< DWARF aranges section
  SSUBTYP_DWABREV = 0x6'0000, ///< DWARF abbrev section
  SSUBTYP_DWSTR = 0x7'0000,   ///< DWARF str section
  SSUBTYP_DWRNGES = 0x8'0000, ///< DWARF ranges section
  SSUBTYP_DWLOC = 0x9'0000,   ///< DWARF loc section
  SSUBTYP_DWFRAME = 0xA'0000, ///< DWARF frame section
  SSUBTYP_DWMAC = 0xB'0000    ///< DWARF macinfo section
};

// STORAGE CLASSES, n_sclass field of syment.
// The values come from `storclass.h` and `dbxstclass.h`.
enum StorageClass : uint8_t {
  // Storage classes used for symbolic debugging symbols.
  C_FILE = 103,  // File name
  C_BINCL = 108, // Beginning of include file
  C_EINCL = 109, // Ending of include file
  C_GSYM = 128,  // Global variable
  C_STSYM = 133, // Statically allocated symbol
  C_BCOMM = 135, // Beginning of common block
  C_ECOMM = 137, // End of common block
  C_ENTRY = 141, // Alternate entry
  C_BSTAT = 143, // Beginning of static block
  C_ESTAT = 144, // End of static block
  C_GTLS = 145,  // Global thread-local variable
  C_STTLS = 146, // Static thread-local variable

  // Storage classes used for DWARF symbols.
  C_DWARF = 112, // DWARF section symbol

  // Storage classes used for absolute symbols.
  C_LSYM = 129,  // Automatic variable allocated on stack
  C_PSYM = 130,  // Argument to subroutine allocated on stack
  C_RSYM = 131,  // Register variable
  C_RPSYM = 132, // Argument to function or procedure stored in register
  C_ECOML = 136, // Local member of common block
  C_FUN = 142,   // Function or procedure

  // Storage classes used for undefined external symbols or
  // symbols of general sections.
  C_EXT = 2,       // External symbol
  C_WEAKEXT = 111, // Weak external symbol

  // Storage classes used for symbols of general sections.
  C_NULL = 0,
  C_STAT = 3,     // Static
  C_BLOCK = 100,  // ".bb" or ".eb"
  C_FCN = 101,    // ".bf" or ".ef"
  C_HIDEXT = 107, // Un-named external symbol
  C_INFO = 110,   // Comment string in .info section
  C_DECL = 140,   // Declaration of object (type)

  // Storage classes - Obsolete/Undocumented.
  C_AUTO = 1,     // Automatic variable
  C_REG = 4,      // Register variable
  C_EXTDEF = 5,   // External definition
  C_LABEL = 6,    // Label
  C_ULABEL = 7,   // Undefined label
  C_MOS = 8,      // Member of structure
  C_ARG = 9,      // Function argument
  C_STRTAG = 10,  // Structure tag
  C_MOU = 11,     // Member of union
  C_UNTAG = 12,   // Union tag
  C_TPDEF = 13,   // Type definition
  C_USTATIC = 14, // Undefined static
  C_ENTAG = 15,   // Enumeration tag
  C_MOE = 16,     // Member of enumeration
  C_REGPARM = 17, // Register parameter
  C_FIELD = 18,   // Bit field
  C_EOS = 102,    // End of structure
  C_LINE = 104,
  C_ALIAS = 105,  // Duplicate tag
  C_HIDDEN = 106, // Special storage class for external
  C_EFCN = 255,   // Physical end of function

  // Storage classes - reserved
  C_TCSYM = 134 // Reserved
};

// Flags for defining the symbol type. Values to be encoded into the lower 3
// bits of the (unsigned, 8-bit) x_smtyp field of csect auxiliary symbol table
// entries. Defined in the system header `syms.h`.
enum SymbolType : uint8_t {
  XTY_ER = 0, ///< External reference.
  XTY_SD = 1, ///< Csect definition for initialized storage.
  XTY_LD = 2, ///< Label definition.
              ///< Defines an entry point to an initialized csect.
  XTY_CM = 3  ///< Common csect definition. For uninitialized storage.
};

/// Values for visibility as they would appear when encoded in the high 4 bits
/// of the 16-bit unsigned n_type field of symbol table entries. Valid for
/// 32-bit XCOFF only when the vstamp in the auxiliary header is greater than 1.
enum VisibilityType : uint16_t {
  SYM_V_UNSPECIFIED = 0x0000,
  SYM_V_INTERNAL = 0x1000,
  SYM_V_HIDDEN = 0x2000,
  SYM_V_PROTECTED = 0x3000,
  SYM_V_EXPORTED = 0x4000
};

constexpr uint16_t VISIBILITY_MASK = 0x7000;

// Relocation types, defined in `/usr/include/reloc.h`.
enum RelocationType : uint8_t {
  R_POS = 0x00, ///< Positive relocation. Provides the address of the referenced
                ///< symbol.
  R_RL = 0x0c,  ///< Positive indirect load relocation. Modifiable instruction.
  R_RLA = 0x0d, ///< Positive load address relocation. Modifiable instruction.

  R_NEG = 0x01, ///< Negative relocation. Provides the negative of the address
                ///< of the referenced symbol.
  R_REL = 0x02, ///< Relative to self relocation. Provides a displacement value
                ///< between the address of the referenced symbol and the
                ///< address being relocated.

  R_TOC = 0x03, ///< Relative to the TOC relocation. Provides a displacement
                ///< that is the difference between the address of the
                ///< referenced symbol and the TOC anchor csect.
  R_TRL = 0x12, ///< TOC relative indirect load relocation. Similar to R_TOC,
                ///< but not modifiable instruction.

  R_TRLA =
      0x13, ///< Relative to the TOC or to the thread-local storage base
            ///< relocation. Compilers are not permitted to generate this
            ///< relocation type. It is the result of a reversible
            ///< transformation by the linker of an R_TOC relation that turned a
            ///< load instruction into an add-immediate instruction.

  R_GL = 0x05, ///< Global linkage-external TOC address relocation. Provides the
               ///< address of the external TOC associated with a defined
               ///< external symbol.
  R_TCL = 0x06, ///< Local object TOC address relocation. Provides the address
                ///< of the local TOC entry of a defined external symbol.

  R_REF = 0x0f, ///< A non-relocating relocation. Used to prevent the binder
                ///< from garbage collecting a csect (such as code used for
                ///< dynamic initialization of non-local statics) for which
                ///< another csect has an implicit dependency.

  R_BA = 0x08, ///< Branch absolute relocation. Provides the address of the
               ///< referenced symbol. References a non-modifiable instruction.
  R_BR = 0x0a, ///< Branch relative to self relocation. Provides the
               ///< displacement that is the difference between the address of
               ///< the referenced symbol and the address of the referenced
               ///< branch instruction. References a non-modifiable instruction.
  R_RBA = 0x18, ///< Branch absolute relocation. Similar to R_BA but
                ///< references a modifiable instruction.
  R_RBR = 0x1a, ///< Branch relative to self relocation. Similar to the R_BR
                ///< relocation type, but references a modifiable instruction.

  R_TLS = 0x20,    ///< General-dynamic reference to TLS symbol.
  R_TLS_IE = 0x21, ///< Initial-exec reference to TLS symbol.
  R_TLS_LD = 0x22, ///< Local-dynamic reference to TLS symbol.
  R_TLS_LE = 0x23, ///< Local-exec reference to TLS symbol.
  R_TLSM = 0x24,  ///< Module reference to TLS. Provides a handle for the module
                  ///< containing the referenced symbol.
  R_TLSML = 0x25, ///< Module reference to the local TLS storage.

  R_TOCU = 0x30, ///< Relative to TOC upper. Specifies the high-order 16 bits of
                 ///< a large code model TOC-relative relocation.
  R_TOCL = 0x31 ///< Relative to TOC lower. Specifies the low-order 16 bits of a
                ///< large code model TOC-relative relocation.
};

enum CFileStringType : uint8_t {
  XFT_FN = 0,  ///< Specifies the source-file name.
  XFT_CT = 1,  ///< Specifies the compiler time stamp.
  XFT_CV = 2,  ///< Specifies the compiler version number.
  XFT_CD = 128 ///< Specifies compiler-defined information.
};

enum CFileLangId : uint8_t {
  TB_C = 0,        ///< C language.
  TB_Fortran = 1,  ///< Fortran language.
  TB_CPLUSPLUS = 9 ///< C++ language.
};

enum CFileCpuId : uint8_t {
  TCPU_PPC64 = 2, ///< PowerPC common architecture 64-bit mode.
  TCPU_COM = 3,   ///< POWER and PowerPC architecture common.
  TCPU_970 = 19   ///< PPC970 - PowerPC 64-bit architecture.
};

enum SymbolAuxType : uint8_t {
  AUX_EXCEPT = 255, ///< Identifies an exception auxiliary entry.
  AUX_FCN = 254,    ///< Identifies a function auxiliary entry.
  AUX_SYM = 253,    ///< Identifies a symbol auxiliary entry.
  AUX_FILE = 252,   ///< Identifies a file auxiliary entry.
  AUX_CSECT = 251,  ///< Identifies a csect auxiliary entry.
  AUX_SECT = 250    ///< Identifies a SECT auxiliary entry.
};                  // 64-bit XCOFF file only.

StringRef getMappingClassString(XCOFF::StorageMappingClass SMC);
StringRef getRelocationTypeString(XCOFF::RelocationType Type);
Expected<SmallString<32>> parseParmsType(uint32_t Value, unsigned FixedParmsNum,
                                         unsigned FloatingParmsNum);
Expected<SmallString<32>> parseParmsTypeWithVecInfo(uint32_t Value,
                                                    unsigned FixedParmsNum,
                                                    unsigned FloatingParmsNum,
                                                    unsigned VectorParmsNum);
Expected<SmallString<32>> parseVectorParmsType(uint32_t Value,
                                               unsigned ParmsNum);

struct TracebackTable {
  enum LanguageID : uint8_t {
    C,
    Fortran,
    Pascal,
    Ada,
    PL1,
    Basic,
    Lisp,
    Cobol,
    Modula2,
    CPlusPlus,
    Rpg,
    PL8,
    PLIX = PL8,
    Assembly,
    Java,
    ObjectiveC
  };
  // Byte 1
  static constexpr uint32_t VersionMask = 0xFF00'0000;
  static constexpr uint8_t VersionShift = 24;

  // Byte 2
  static constexpr uint32_t LanguageIdMask = 0x00FF'0000;
  static constexpr uint8_t LanguageIdShift = 16;

  // Byte 3
  static constexpr uint32_t IsGlobaLinkageMask = 0x0000'8000;
  static constexpr uint32_t IsOutOfLineEpilogOrPrologueMask = 0x0000'4000;
  static constexpr uint32_t HasTraceBackTableOffsetMask = 0x0000'2000;
  static constexpr uint32_t IsInternalProcedureMask = 0x0000'1000;
  static constexpr uint32_t HasControlledStorageMask = 0x0000'0800;
  static constexpr uint32_t IsTOClessMask = 0x0000'0400;
  static constexpr uint32_t IsFloatingPointPresentMask = 0x0000'0200;
  static constexpr uint32_t IsFloatingPointOperationLogOrAbortEnabledMask =
      0x0000'0100;

  // Byte 4
  static constexpr uint32_t IsInterruptHandlerMask = 0x0000'0080;
  static constexpr uint32_t IsFunctionNamePresentMask = 0x0000'0040;
  static constexpr uint32_t IsAllocaUsedMask = 0x0000'0020;
  static constexpr uint32_t OnConditionDirectiveMask = 0x0000'001C;
  static constexpr uint32_t IsCRSavedMask = 0x0000'0002;
  static constexpr uint32_t IsLRSavedMask = 0x0000'0001;
  static constexpr uint8_t OnConditionDirectiveShift = 2;

  // Byte 5
  static constexpr uint32_t IsBackChainStoredMask = 0x8000'0000;
  static constexpr uint32_t IsFixupMask = 0x4000'0000;
  static constexpr uint32_t FPRSavedMask = 0x3F00'0000;
  static constexpr uint32_t FPRSavedShift = 24;

  // Byte 6
  static constexpr uint32_t HasExtensionTableMask = 0x0080'0000;
  static constexpr uint32_t HasVectorInfoMask = 0x0040'0000;
  static constexpr uint32_t GPRSavedMask = 0x003F'0000;
  static constexpr uint32_t GPRSavedShift = 16;

  // Byte 7
  static constexpr uint32_t NumberOfFixedParmsMask = 0x0000'FF00;
  static constexpr uint8_t NumberOfFixedParmsShift = 8;

  // Byte 8
  static constexpr uint32_t NumberOfFloatingPointParmsMask = 0x0000'00FE;
  static constexpr uint32_t HasParmsOnStackMask = 0x0000'0001;
  static constexpr uint8_t NumberOfFloatingPointParmsShift = 1;

  // Masks to select leftmost bits for decoding parameter type information.
  // Bit to use when vector info is not presented.
  static constexpr uint32_t ParmTypeIsFloatingBit = 0x8000'0000;
  static constexpr uint32_t ParmTypeFloatingIsDoubleBit = 0x4000'0000;
  // Bits to use when vector info is presented.
  static constexpr uint32_t ParmTypeIsFixedBits = 0x0000'0000;
  static constexpr uint32_t ParmTypeIsVectorBits = 0x4000'0000;
  static constexpr uint32_t ParmTypeIsFloatingBits = 0x8000'0000;
  static constexpr uint32_t ParmTypeIsDoubleBits = 0xC000'0000;
  static constexpr uint32_t ParmTypeMask = 0xC000'0000;

  // Vector extension
  static constexpr uint16_t NumberOfVRSavedMask = 0xFC00;
  static constexpr uint16_t IsVRSavedOnStackMask = 0x0200;
  static constexpr uint16_t HasVarArgsMask = 0x0100;
  static constexpr uint8_t NumberOfVRSavedShift = 10;

  static constexpr uint16_t NumberOfVectorParmsMask = 0x00FE;
  static constexpr uint16_t HasVMXInstructionMask = 0x0001;
  static constexpr uint8_t NumberOfVectorParmsShift = 1;

  static constexpr uint32_t ParmTypeIsVectorCharBit = 0x0000'0000;
  static constexpr uint32_t ParmTypeIsVectorShortBit = 0x4000'0000;
  static constexpr uint32_t ParmTypeIsVectorIntBit = 0x8000'0000;
  static constexpr uint32_t ParmTypeIsVectorFloatBit = 0xC000'0000;

  static constexpr uint8_t WidthOfParamType = 2;
};

// Extended Traceback table flags.
enum ExtendedTBTableFlag : uint8_t {
  TB_OS1 = 0x80,         ///< Reserved for OS use.
  TB_RESERVED = 0x40,    ///< Reserved for compiler.
  TB_SSP_CANARY = 0x20,  ///< stack smasher canary present on stack.
  TB_OS2 = 0x10,         ///< Reserved for OS use.
  TB_EH_INFO = 0x08,     ///< Exception handling info present.
  TB_LONGTBTABLE2 = 0x01 ///< Additional tbtable extension exists.
};

StringRef getNameForTracebackTableLanguageId(TracebackTable::LanguageID LangId);
SmallString<32> getExtendedTBTableFlagString(uint8_t Flag);

struct CsectProperties {
  CsectProperties(StorageMappingClass SMC, SymbolType ST)
      : MappingClass(SMC), Type(ST) {}
  StorageMappingClass MappingClass;
  SymbolType Type;
};

} // end namespace XCOFF
} // end namespace llvm

#endif
