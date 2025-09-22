//===-- llvm/BinaryFormat/Dwarf.h ---Dwarf Constants-------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains constants used for implementing Dwarf
/// debug support.
///
/// For details on the Dwarf specfication see the latest DWARF Debugging
/// Information Format standard document on http://www.dwarfstd.org. This
/// file often includes support for non-released standard features.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_DWARF_H
#define LLVM_BINARYFORMAT_DWARF_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadicDetails.h"
#include "llvm/TargetParser/Triple.h"

#include <limits>

namespace llvm {
class StringRef;

namespace dwarf {

//===----------------------------------------------------------------------===//
// DWARF constants as gleaned from the DWARF Debugging Information Format V.5
// reference manual http://www.dwarfstd.org/.
//

// Do not mix the following two enumerations sets.  DW_TAG_invalid changes the
// enumeration base type.

enum LLVMConstants : uint32_t {
  /// LLVM mock tags (see also llvm/BinaryFormat/Dwarf.def).
  /// \{
  DW_TAG_invalid = ~0U,        ///< Tag for invalid results.
  DW_VIRTUALITY_invalid = ~0U, ///< Virtuality for invalid results.
  DW_MACINFO_invalid = ~0U,    ///< Macinfo type for invalid results.
  /// \}

  /// Special values for an initial length field.
  /// \{
  DW_LENGTH_lo_reserved = 0xfffffff0, ///< Lower bound of the reserved range.
  DW_LENGTH_DWARF64 = 0xffffffff,     ///< Indicator of 64-bit DWARF format.
  DW_LENGTH_hi_reserved = 0xffffffff, ///< Upper bound of the reserved range.
  /// \}

  /// Other constants.
  /// \{
  DWARF_VERSION = 4,       ///< Default dwarf version we output.
  DW_PUBTYPES_VERSION = 2, ///< Section version number for .debug_pubtypes.
  DW_PUBNAMES_VERSION = 2, ///< Section version number for .debug_pubnames.
  DW_ARANGES_VERSION = 2,  ///< Section version number for .debug_aranges.
  /// \}

  /// Identifiers we use to distinguish vendor extensions.
  /// \{
  DWARF_VENDOR_DWARF = 0, ///< Defined in v2 or later of the DWARF standard.
  DWARF_VENDOR_APPLE = 1,
  DWARF_VENDOR_BORLAND = 2,
  DWARF_VENDOR_GNU = 3,
  DWARF_VENDOR_GOOGLE = 4,
  DWARF_VENDOR_LLVM = 5,
  DWARF_VENDOR_MIPS = 6,
  DWARF_VENDOR_WASM = 7,
  DWARF_VENDOR_ALTIUM,
  DWARF_VENDOR_COMPAQ,
  DWARF_VENDOR_GHS,
  DWARF_VENDOR_GO,
  DWARF_VENDOR_HP,
  DWARF_VENDOR_IBM,
  DWARF_VENDOR_INTEL,
  DWARF_VENDOR_PGI,
  DWARF_VENDOR_SUN,
  DWARF_VENDOR_UPC,
  ///\}
};

/// Constants that define the DWARF format as 32 or 64 bit.
enum DwarfFormat : uint8_t { DWARF32, DWARF64 };

/// Special ID values that distinguish a CIE from a FDE in DWARF CFI.
/// Not inside an enum because a 64-bit value is needed.
/// @{
const uint32_t DW_CIE_ID = UINT32_MAX;
const uint64_t DW64_CIE_ID = UINT64_MAX;
/// @}

/// Identifier of an invalid DIE offset in the .debug_info section.
const uint32_t DW_INVALID_OFFSET = UINT32_MAX;

enum Tag : uint16_t {
#define HANDLE_DW_TAG(ID, NAME, VERSION, VENDOR, KIND) DW_TAG_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_TAG_lo_user = 0x4080,
  DW_TAG_hi_user = 0xffff,
  DW_TAG_user_base = 0x1000 ///< Recommended base for user tags.
};

inline bool isType(Tag T) {
  switch (T) {
  default:
    return false;
#define HANDLE_DW_TAG(ID, NAME, VERSION, VENDOR, KIND)                         \
  case DW_TAG_##NAME:                                                          \
    return (KIND == DW_KIND_TYPE);
#include "llvm/BinaryFormat/Dwarf.def"
  }
}

/// Attributes.
enum Attribute : uint16_t {
#define HANDLE_DW_AT(ID, NAME, VERSION, VENDOR) DW_AT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_AT_lo_user = 0x2000,
  DW_AT_hi_user = 0x3fff,
};

enum Form : uint16_t {
#define HANDLE_DW_FORM(ID, NAME, VERSION, VENDOR) DW_FORM_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_FORM_lo_user = 0x1f00, ///< Not specified by DWARF.
};

enum LocationAtom {
#define HANDLE_DW_OP(ID, NAME, OPERANDS, ARITY, VERSION, VENDOR)               \
  DW_OP_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_OP_lo_user = 0xe0,
  DW_OP_hi_user = 0xff,
  DW_OP_LLVM_fragment = 0x1000,          ///< Only used in LLVM metadata.
  DW_OP_LLVM_convert = 0x1001,           ///< Only used in LLVM metadata.
  DW_OP_LLVM_tag_offset = 0x1002,        ///< Only used in LLVM metadata.
  DW_OP_LLVM_entry_value = 0x1003,       ///< Only used in LLVM metadata.
  DW_OP_LLVM_implicit_pointer = 0x1004,  ///< Only used in LLVM metadata.
  DW_OP_LLVM_arg = 0x1005,               ///< Only used in LLVM metadata.
  DW_OP_LLVM_extract_bits_sext = 0x1006, ///< Only used in LLVM metadata.
  DW_OP_LLVM_extract_bits_zext = 0x1007, ///< Only used in LLVM metadata.
};

enum LlvmUserLocationAtom {
#define HANDLE_DW_OP_LLVM_USEROP(ID, NAME) DW_OP_LLVM_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

enum TypeKind : uint8_t {
#define HANDLE_DW_ATE(ID, NAME, VERSION, VENDOR) DW_ATE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_ATE_lo_user = 0x80,
  DW_ATE_hi_user = 0xff
};

enum DecimalSignEncoding {
  // Decimal sign attribute values
  DW_DS_unsigned = 0x01,
  DW_DS_leading_overpunch = 0x02,
  DW_DS_trailing_overpunch = 0x03,
  DW_DS_leading_separate = 0x04,
  DW_DS_trailing_separate = 0x05
};

enum EndianityEncoding {
  // Endianity attribute values
#define HANDLE_DW_END(ID, NAME) DW_END_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_END_lo_user = 0x40,
  DW_END_hi_user = 0xff
};

enum AccessAttribute {
  // Accessibility codes
  DW_ACCESS_public = 0x01,
  DW_ACCESS_protected = 0x02,
  DW_ACCESS_private = 0x03
};

enum VisibilityAttribute {
  // Visibility codes
  DW_VIS_local = 0x01,
  DW_VIS_exported = 0x02,
  DW_VIS_qualified = 0x03
};

enum VirtualityAttribute {
#define HANDLE_DW_VIRTUALITY(ID, NAME) DW_VIRTUALITY_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_VIRTUALITY_max = 0x02
};

enum DefaultedMemberAttribute {
#define HANDLE_DW_DEFAULTED(ID, NAME) DW_DEFAULTED_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_DEFAULTED_max = 0x02
};

enum SourceLanguage {
#define HANDLE_DW_LANG(ID, NAME, LOWER_BOUND, VERSION, VENDOR)                 \
  DW_LANG_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LANG_lo_user = 0x8000,
  DW_LANG_hi_user = 0xffff
};

enum SourceLanguageName : uint16_t {
#define HANDLE_DW_LNAME(ID, NAME, DESC, LOWER_BOUND) DW_LNAME_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Convert a DWARF 6 pair of language name and version to a DWARF 5 DW_LANG.
/// If the version number doesn't exactly match a known version it is
/// rounded up to the next-highest known version number.
inline std::optional<SourceLanguage> toDW_LANG(SourceLanguageName name,
                                               uint32_t version) {
  switch (name) {
  case DW_LNAME_Ada: // YYYY
    if (version <= 1983)
      return DW_LANG_Ada83;
    if (version <= 1995)
      return DW_LANG_Ada95;
    if (version <= 2005)
      return DW_LANG_Ada2005;
    if (version <= 2012)
      return DW_LANG_Ada2012;
    return {};
  case DW_LNAME_BLISS:
    return DW_LANG_BLISS;
  case DW_LNAME_C: // YYYYMM, K&R 000000
    if (version == 0)
      return DW_LANG_C;
    if (version <= 198912)
      return DW_LANG_C89;
    if (version <= 199901)
      return DW_LANG_C99;
    if (version <= 201112)
      return DW_LANG_C11;
    if (version <= 201710)
      return DW_LANG_C17;
    return {};
  case DW_LNAME_C_plus_plus: // YYYYMM
    if (version == 0)
      return DW_LANG_C_plus_plus;
    if (version <= 199711)
      return DW_LANG_C_plus_plus;
    if (version <= 200310)
      return DW_LANG_C_plus_plus_03;
    if (version <= 201103)
      return DW_LANG_C_plus_plus_11;
    if (version <= 201402)
      return DW_LANG_C_plus_plus_14;
    if (version <= 201703)
      return DW_LANG_C_plus_plus_17;
    if (version <= 202002)
      return DW_LANG_C_plus_plus_20;
    return {};
  case DW_LNAME_Cobol: // YYYY
    if (version <= 1974)
      return DW_LANG_Cobol74;
    if (version <= 1985)
      return DW_LANG_Cobol85;
    return {};
  case DW_LNAME_Crystal:
    return DW_LANG_Crystal;
  case DW_LNAME_D:
    return DW_LANG_D;
  case DW_LNAME_Dylan:
    return DW_LANG_Dylan;
  case DW_LNAME_Fortran: // YYYY
    if (version <= 1977)
      return DW_LANG_Fortran77;
    if (version <= 1990)
      return DW_LANG_Fortran90;
    if (version <= 1995)
      return DW_LANG_Fortran95;
    if (version <= 2003)
      return DW_LANG_Fortran03;
    if (version <= 2008)
      return DW_LANG_Fortran08;
    if (version <= 2018)
      return DW_LANG_Fortran18;
    return {};
  case DW_LNAME_Go:
    return DW_LANG_Go;
  case DW_LNAME_Haskell:
    return DW_LANG_Haskell;
  // case DW_LNAME_HIP:
  //   return DW_LANG_HIP;
  case DW_LNAME_Java:
    return DW_LANG_Java;
  case DW_LNAME_Julia:
    return DW_LANG_Julia;
  case DW_LNAME_Kotlin:
    return DW_LANG_Kotlin;
  case DW_LNAME_Modula2:
    return DW_LANG_Modula2;
  case DW_LNAME_Modula3:
    return DW_LANG_Modula3;
  case DW_LNAME_ObjC:
    return DW_LANG_ObjC;
  case DW_LNAME_ObjC_plus_plus:
    return DW_LANG_ObjC_plus_plus;
  case DW_LNAME_OCaml:
    return DW_LANG_OCaml;
  case DW_LNAME_OpenCL_C:
    return DW_LANG_OpenCL;
  case DW_LNAME_Pascal:
    return DW_LANG_Pascal83;
  case DW_LNAME_PLI:
    return DW_LANG_PLI;
  case DW_LNAME_Python:
    return DW_LANG_Python;
  case DW_LNAME_RenderScript:
    return DW_LANG_RenderScript;
  case DW_LNAME_Rust:
    return DW_LANG_Rust;
  case DW_LNAME_Swift:
    return DW_LANG_Swift;
  case DW_LNAME_UPC:
    return DW_LANG_UPC;
  case DW_LNAME_Zig:
    return DW_LANG_Zig;
  case DW_LNAME_Assembly:
    return DW_LANG_Assembly;
  case DW_LNAME_C_sharp:
    return DW_LANG_C_sharp;
  case DW_LNAME_Mojo:
    return DW_LANG_Mojo;
  case DW_LNAME_GLSL:
    return DW_LANG_GLSL;
  case DW_LNAME_GLSL_ES:
    return DW_LANG_GLSL_ES;
  case DW_LNAME_HLSL:
    return DW_LANG_HLSL;
  case DW_LNAME_OpenCL_CPP:
    return DW_LANG_OpenCL_CPP;
  case DW_LNAME_CPP_for_OpenCL:
    return {};
  case DW_LNAME_SYCL:
    return DW_LANG_SYCL;
  case DW_LNAME_Ruby:
    return DW_LANG_Ruby;
  case DW_LNAME_Move:
    return DW_LANG_Move;
  case DW_LNAME_Hylo:
    return DW_LANG_Hylo;
  }
  return {};
}

/// Convert a DWARF 5 DW_LANG to a DWARF 6 pair of language name and version.
inline std::optional<std::pair<SourceLanguageName, uint32_t>>
toDW_LNAME(SourceLanguage language) {
  switch (language) {
  case DW_LANG_Ada83:
    return {{DW_LNAME_Ada, 1983}};
  case DW_LANG_Ada95:
    return {{DW_LNAME_Ada, 1995}};
  case DW_LANG_Ada2005:
    return {{DW_LNAME_Ada, 2005}};
  case DW_LANG_Ada2012:
    return {{DW_LNAME_Ada, 2012}};
  case DW_LANG_BLISS:
    return {{DW_LNAME_BLISS, 0}};
  case DW_LANG_C:
    return {{DW_LNAME_C, 0}};
  case DW_LANG_C89:
    return {{DW_LNAME_C, 198912}};
  case DW_LANG_C99:
    return {{DW_LNAME_C, 199901}};
  case DW_LANG_C11:
    return {{DW_LNAME_C, 201112}};
  case DW_LANG_C17:
    return {{DW_LNAME_C, 201712}};
  case DW_LANG_C_plus_plus:
    return {{DW_LNAME_C_plus_plus, 0}};
  case DW_LANG_C_plus_plus_03:
    return {{DW_LNAME_C_plus_plus, 200310}};
  case DW_LANG_C_plus_plus_11:
    return {{DW_LNAME_C_plus_plus, 201103}};
  case DW_LANG_C_plus_plus_14:
    return {{DW_LNAME_C_plus_plus, 201402}};
  case DW_LANG_C_plus_plus_17:
    return {{DW_LNAME_C_plus_plus, 201703}};
  case DW_LANG_C_plus_plus_20:
    return {{DW_LNAME_C_plus_plus, 202002}};
  case DW_LANG_Cobol74:
    return {{DW_LNAME_Cobol, 1974}};
  case DW_LANG_Cobol85:
    return {{DW_LNAME_Cobol, 1985}};
  case DW_LANG_Crystal:
    return {{DW_LNAME_Crystal, 0}};
  case DW_LANG_D:
    return {{DW_LNAME_D, 0}};
  case DW_LANG_Dylan:
    return {{DW_LNAME_Dylan, 0}};
  case DW_LANG_Fortran77:
    return {{DW_LNAME_Fortran, 1977}};
  case DW_LANG_Fortran90:
    return {{DW_LNAME_Fortran, 1990}};
  case DW_LANG_Fortran95:
    return {{DW_LNAME_Fortran, 1995}};
  case DW_LANG_Fortran03:
    return {{DW_LNAME_Fortran, 2003}};
  case DW_LANG_Fortran08:
    return {{DW_LNAME_Fortran, 2008}};
  case DW_LANG_Fortran18:
    return {{DW_LNAME_Fortran, 2018}};
  case DW_LANG_Go:
    return {{DW_LNAME_Go, 0}};
  case DW_LANG_Haskell:
    return {{DW_LNAME_Haskell, 0}};
  case DW_LANG_HIP:
    return {}; // return {{DW_LNAME_HIP, 0}};
  case DW_LANG_Java:
    return {{DW_LNAME_Java, 0}};
  case DW_LANG_Julia:
    return {{DW_LNAME_Julia, 0}};
  case DW_LANG_Kotlin:
    return {{DW_LNAME_Kotlin, 0}};
  case DW_LANG_Modula2:
    return {{DW_LNAME_Modula2, 0}};
  case DW_LANG_Modula3:
    return {{DW_LNAME_Modula3, 0}};
  case DW_LANG_ObjC:
    return {{DW_LNAME_ObjC, 0}};
  case DW_LANG_ObjC_plus_plus:
    return {{DW_LNAME_ObjC_plus_plus, 0}};
  case DW_LANG_OCaml:
    return {{DW_LNAME_OCaml, 0}};
  case DW_LANG_OpenCL:
    return {{DW_LNAME_OpenCL_C, 0}};
  case DW_LANG_Pascal83:
    return {{DW_LNAME_Pascal, 1983}};
  case DW_LANG_PLI:
    return {{DW_LNAME_PLI, 0}};
  case DW_LANG_Python:
    return {{DW_LNAME_Python, 0}};
  case DW_LANG_RenderScript:
  case DW_LANG_GOOGLE_RenderScript:
    return {{DW_LNAME_RenderScript, 0}};
  case DW_LANG_Rust:
    return {{DW_LNAME_Rust, 0}};
  case DW_LANG_Swift:
    return {{DW_LNAME_Swift, 0}};
  case DW_LANG_UPC:
    return {{DW_LNAME_UPC, 0}};
  case DW_LANG_Zig:
    return {{DW_LNAME_Zig, 0}};
  case DW_LANG_Assembly:
  case DW_LANG_Mips_Assembler:
    return {{DW_LNAME_Assembly, 0}};
  case DW_LANG_C_sharp:
    return {{DW_LNAME_C_sharp, 0}};
  case DW_LANG_Mojo:
    return {{DW_LNAME_Mojo, 0}};
  case DW_LANG_GLSL:
    return {{DW_LNAME_GLSL, 0}};
  case DW_LANG_GLSL_ES:
    return {{DW_LNAME_GLSL_ES, 0}};
  case DW_LANG_HLSL:
    return {{DW_LNAME_HLSL, 0}};
  case DW_LANG_OpenCL_CPP:
    return {{DW_LNAME_OpenCL_CPP, 0}};
  case DW_LANG_SYCL:
    return {{DW_LNAME_SYCL, 0}};
  case DW_LANG_Ruby:
    return {{DW_LNAME_Ruby, 0}};
  case DW_LANG_Move:
    return {{DW_LNAME_Move, 0}};
  case DW_LANG_Hylo:
    return {{DW_LNAME_Hylo, 0}};
  case DW_LANG_BORLAND_Delphi:
  case DW_LANG_CPP_for_OpenCL:
  case DW_LANG_lo_user:
  case DW_LANG_hi_user:
    return {};
  }
  return {};
}

llvm::StringRef LanguageDescription(SourceLanguageName name);

inline bool isCPlusPlus(SourceLanguage S) {
  bool result = false;
  // Deliberately enumerate all the language options so we get a warning when
  // new language options are added (-Wswitch) that'll hopefully help keep this
  // switch up-to-date when new C++ versions are added.
  switch (S) {
  case DW_LANG_C_plus_plus:
  case DW_LANG_C_plus_plus_03:
  case DW_LANG_C_plus_plus_11:
  case DW_LANG_C_plus_plus_14:
  case DW_LANG_C_plus_plus_17:
  case DW_LANG_C_plus_plus_20:
    result = true;
    break;
  case DW_LANG_C89:
  case DW_LANG_C:
  case DW_LANG_Ada83:
  case DW_LANG_Cobol74:
  case DW_LANG_Cobol85:
  case DW_LANG_Fortran77:
  case DW_LANG_Fortran90:
  case DW_LANG_Pascal83:
  case DW_LANG_Modula2:
  case DW_LANG_Java:
  case DW_LANG_C99:
  case DW_LANG_Ada95:
  case DW_LANG_Fortran95:
  case DW_LANG_PLI:
  case DW_LANG_ObjC:
  case DW_LANG_ObjC_plus_plus:
  case DW_LANG_UPC:
  case DW_LANG_D:
  case DW_LANG_Python:
  case DW_LANG_OpenCL:
  case DW_LANG_Go:
  case DW_LANG_Modula3:
  case DW_LANG_Haskell:
  case DW_LANG_OCaml:
  case DW_LANG_Rust:
  case DW_LANG_C11:
  case DW_LANG_Swift:
  case DW_LANG_Julia:
  case DW_LANG_Dylan:
  case DW_LANG_Fortran03:
  case DW_LANG_Fortran08:
  case DW_LANG_RenderScript:
  case DW_LANG_BLISS:
  case DW_LANG_Mips_Assembler:
  case DW_LANG_GOOGLE_RenderScript:
  case DW_LANG_BORLAND_Delphi:
  case DW_LANG_lo_user:
  case DW_LANG_hi_user:
  case DW_LANG_Kotlin:
  case DW_LANG_Zig:
  case DW_LANG_Crystal:
  case DW_LANG_C17:
  case DW_LANG_Fortran18:
  case DW_LANG_Ada2005:
  case DW_LANG_Ada2012:
  case DW_LANG_HIP:
  case DW_LANG_Assembly:
  case DW_LANG_C_sharp:
  case DW_LANG_Mojo:
  case DW_LANG_GLSL:
  case DW_LANG_GLSL_ES:
  case DW_LANG_HLSL:
  case DW_LANG_OpenCL_CPP:
  case DW_LANG_CPP_for_OpenCL:
  case DW_LANG_SYCL:
  case DW_LANG_Ruby:
  case DW_LANG_Move:
  case DW_LANG_Hylo:
    result = false;
    break;
  }

  return result;
}

inline bool isFortran(SourceLanguage S) {
  bool result = false;
  // Deliberately enumerate all the language options so we get a warning when
  // new language options are added (-Wswitch) that'll hopefully help keep this
  // switch up-to-date when new Fortran versions are added.
  switch (S) {
  case DW_LANG_Fortran77:
  case DW_LANG_Fortran90:
  case DW_LANG_Fortran95:
  case DW_LANG_Fortran03:
  case DW_LANG_Fortran08:
  case DW_LANG_Fortran18:
    result = true;
    break;
  case DW_LANG_C89:
  case DW_LANG_C:
  case DW_LANG_Ada83:
  case DW_LANG_C_plus_plus:
  case DW_LANG_Cobol74:
  case DW_LANG_Cobol85:
  case DW_LANG_Pascal83:
  case DW_LANG_Modula2:
  case DW_LANG_Java:
  case DW_LANG_C99:
  case DW_LANG_Ada95:
  case DW_LANG_PLI:
  case DW_LANG_ObjC:
  case DW_LANG_ObjC_plus_plus:
  case DW_LANG_UPC:
  case DW_LANG_D:
  case DW_LANG_Python:
  case DW_LANG_OpenCL:
  case DW_LANG_Go:
  case DW_LANG_Modula3:
  case DW_LANG_Haskell:
  case DW_LANG_C_plus_plus_03:
  case DW_LANG_C_plus_plus_11:
  case DW_LANG_OCaml:
  case DW_LANG_Rust:
  case DW_LANG_C11:
  case DW_LANG_Swift:
  case DW_LANG_Julia:
  case DW_LANG_Dylan:
  case DW_LANG_C_plus_plus_14:
  case DW_LANG_RenderScript:
  case DW_LANG_BLISS:
  case DW_LANG_Mips_Assembler:
  case DW_LANG_GOOGLE_RenderScript:
  case DW_LANG_BORLAND_Delphi:
  case DW_LANG_lo_user:
  case DW_LANG_hi_user:
  case DW_LANG_Kotlin:
  case DW_LANG_Zig:
  case DW_LANG_Crystal:
  case DW_LANG_C_plus_plus_17:
  case DW_LANG_C_plus_plus_20:
  case DW_LANG_C17:
  case DW_LANG_Ada2005:
  case DW_LANG_Ada2012:
  case DW_LANG_HIP:
  case DW_LANG_Assembly:
  case DW_LANG_C_sharp:
  case DW_LANG_Mojo:
  case DW_LANG_GLSL:
  case DW_LANG_GLSL_ES:
  case DW_LANG_HLSL:
  case DW_LANG_OpenCL_CPP:
  case DW_LANG_CPP_for_OpenCL:
  case DW_LANG_SYCL:
  case DW_LANG_Ruby:
  case DW_LANG_Move:
  case DW_LANG_Hylo:
    result = false;
    break;
  }

  return result;
}

inline bool isC(SourceLanguage S) {
  // Deliberately enumerate all the language options so we get a warning when
  // new language options are added (-Wswitch) that'll hopefully help keep this
  // switch up-to-date when new C++ versions are added.
  switch (S) {
  case DW_LANG_C11:
  case DW_LANG_C17:
  case DW_LANG_C89:
  case DW_LANG_C99:
  case DW_LANG_C:
  case DW_LANG_ObjC:
    return true;
  case DW_LANG_C_plus_plus:
  case DW_LANG_C_plus_plus_03:
  case DW_LANG_C_plus_plus_11:
  case DW_LANG_C_plus_plus_14:
  case DW_LANG_C_plus_plus_17:
  case DW_LANG_C_plus_plus_20:
  case DW_LANG_Ada83:
  case DW_LANG_Cobol74:
  case DW_LANG_Cobol85:
  case DW_LANG_Fortran77:
  case DW_LANG_Fortran90:
  case DW_LANG_Pascal83:
  case DW_LANG_Modula2:
  case DW_LANG_Java:
  case DW_LANG_Ada95:
  case DW_LANG_Fortran95:
  case DW_LANG_PLI:
  case DW_LANG_ObjC_plus_plus:
  case DW_LANG_UPC:
  case DW_LANG_D:
  case DW_LANG_Python:
  case DW_LANG_OpenCL:
  case DW_LANG_Go:
  case DW_LANG_Modula3:
  case DW_LANG_Haskell:
  case DW_LANG_OCaml:
  case DW_LANG_Rust:
  case DW_LANG_Swift:
  case DW_LANG_Julia:
  case DW_LANG_Dylan:
  case DW_LANG_Fortran03:
  case DW_LANG_Fortran08:
  case DW_LANG_RenderScript:
  case DW_LANG_BLISS:
  case DW_LANG_Mips_Assembler:
  case DW_LANG_GOOGLE_RenderScript:
  case DW_LANG_BORLAND_Delphi:
  case DW_LANG_lo_user:
  case DW_LANG_hi_user:
  case DW_LANG_Kotlin:
  case DW_LANG_Zig:
  case DW_LANG_Crystal:
  case DW_LANG_Fortran18:
  case DW_LANG_Ada2005:
  case DW_LANG_Ada2012:
  case DW_LANG_HIP:
  case DW_LANG_Assembly:
  case DW_LANG_C_sharp:
  case DW_LANG_Mojo:
  case DW_LANG_GLSL:
  case DW_LANG_GLSL_ES:
  case DW_LANG_HLSL:
  case DW_LANG_OpenCL_CPP:
  case DW_LANG_CPP_for_OpenCL:
  case DW_LANG_SYCL:
  case DW_LANG_Ruby:
  case DW_LANG_Move:
  case DW_LANG_Hylo:
    return false;
  }
  llvm_unreachable("Unknown language kind.");
}

inline TypeKind getArrayIndexTypeEncoding(SourceLanguage S) {
  return isFortran(S) ? DW_ATE_signed : DW_ATE_unsigned;
}

enum CaseSensitivity {
  // Identifier case codes
  DW_ID_case_sensitive = 0x00,
  DW_ID_up_case = 0x01,
  DW_ID_down_case = 0x02,
  DW_ID_case_insensitive = 0x03
};

enum CallingConvention {
// Calling convention codes
#define HANDLE_DW_CC(ID, NAME) DW_CC_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_CC_lo_user = 0x40,
  DW_CC_hi_user = 0xff
};

enum InlineAttribute {
  // Inline codes
  DW_INL_not_inlined = 0x00,
  DW_INL_inlined = 0x01,
  DW_INL_declared_not_inlined = 0x02,
  DW_INL_declared_inlined = 0x03
};

enum ArrayDimensionOrdering {
  // Array ordering
  DW_ORD_row_major = 0x00,
  DW_ORD_col_major = 0x01
};

enum DiscriminantList {
  // Discriminant descriptor values
  DW_DSC_label = 0x00,
  DW_DSC_range = 0x01
};

/// Line Number Standard Opcode Encodings.
enum LineNumberOps : uint8_t {
#define HANDLE_DW_LNS(ID, NAME) DW_LNS_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Line Number Extended Opcode Encodings.
enum LineNumberExtendedOps {
#define HANDLE_DW_LNE(ID, NAME) DW_LNE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LNE_lo_user = 0x80,
  DW_LNE_hi_user = 0xff
};

enum LineNumberEntryFormat {
#define HANDLE_DW_LNCT(ID, NAME) DW_LNCT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LNCT_lo_user = 0x2000,
  DW_LNCT_hi_user = 0x3fff,
};

enum MacinfoRecordType {
  // Macinfo Type Encodings
  DW_MACINFO_define = 0x01,
  DW_MACINFO_undef = 0x02,
  DW_MACINFO_start_file = 0x03,
  DW_MACINFO_end_file = 0x04,
  DW_MACINFO_vendor_ext = 0xff
};

/// DWARF v5 macro information entry type encodings.
enum MacroEntryType {
#define HANDLE_DW_MACRO(ID, NAME) DW_MACRO_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_MACRO_lo_user = 0xe0,
  DW_MACRO_hi_user = 0xff
};

/// GNU .debug_macro macro information entry type encodings.
enum GnuMacroEntryType {
#define HANDLE_DW_MACRO_GNU(ID, NAME) DW_MACRO_GNU_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_MACRO_GNU_lo_user = 0xe0,
  DW_MACRO_GNU_hi_user = 0xff
};

/// DWARF v5 range list entry encoding values.
enum RnglistEntries {
#define HANDLE_DW_RLE(ID, NAME) DW_RLE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// DWARF v5 loc list entry encoding values.
enum LoclistEntries {
#define HANDLE_DW_LLE(ID, NAME) DW_LLE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Call frame instruction encodings.
enum CallFrameInfo {
#define HANDLE_DW_CFA(ID, NAME) DW_CFA_##NAME = ID,
#define HANDLE_DW_CFA_PRED(ID, NAME, ARCH) DW_CFA_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_CFA_extended = 0x00,

  DW_CFA_lo_user = 0x1c,
  DW_CFA_hi_user = 0x3f
};

enum Constants {
  // Children flag
  DW_CHILDREN_no = 0x00,
  DW_CHILDREN_yes = 0x01,

  DW_EH_PE_absptr = 0x00,
  DW_EH_PE_omit = 0xff,
  DW_EH_PE_uleb128 = 0x01,
  DW_EH_PE_udata2 = 0x02,
  DW_EH_PE_udata4 = 0x03,
  DW_EH_PE_udata8 = 0x04,
  DW_EH_PE_sleb128 = 0x09,
  DW_EH_PE_sdata2 = 0x0A,
  DW_EH_PE_sdata4 = 0x0B,
  DW_EH_PE_sdata8 = 0x0C,
  DW_EH_PE_signed = 0x08,
  DW_EH_PE_pcrel = 0x10,
  DW_EH_PE_textrel = 0x20,
  DW_EH_PE_datarel = 0x30,
  DW_EH_PE_funcrel = 0x40,
  DW_EH_PE_aligned = 0x50,
  DW_EH_PE_indirect = 0x80
};

/// Constants for the DW_APPLE_PROPERTY_attributes attribute.
/// Keep this list in sync with clang's DeclObjCCommon.h
/// ObjCPropertyAttribute::Kind!
enum ApplePropertyAttributes {
#define HANDLE_DW_APPLE_PROPERTY(ID, NAME) DW_APPLE_PROPERTY_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Constants for unit types in DWARF v5.
enum UnitType : unsigned char {
#define HANDLE_DW_UT(ID, NAME) DW_UT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_UT_lo_user = 0x80,
  DW_UT_hi_user = 0xff
};

enum Index {
#define HANDLE_DW_IDX(ID, NAME) DW_IDX_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_IDX_lo_user = 0x2000,
  DW_IDX_hi_user = 0x3fff
};

inline bool isUnitType(uint8_t UnitType) {
  switch (UnitType) {
  case DW_UT_compile:
  case DW_UT_type:
  case DW_UT_partial:
  case DW_UT_skeleton:
  case DW_UT_split_compile:
  case DW_UT_split_type:
    return true;
  default:
    return false;
  }
}

inline bool isUnitType(dwarf::Tag T) {
  switch (T) {
  case DW_TAG_compile_unit:
  case DW_TAG_type_unit:
  case DW_TAG_partial_unit:
  case DW_TAG_skeleton_unit:
    return true;
  default:
    return false;
  }
}

// Constants for the DWARF v5 Accelerator Table Proposal
enum AcceleratorTable {
  // Data layout descriptors.
  DW_ATOM_null = 0u,       ///  Marker as the end of a list of atoms.
  DW_ATOM_die_offset = 1u, // DIE offset in the debug_info section.
  DW_ATOM_cu_offset = 2u, // Offset of the compile unit header that contains the
                          // item in question.
  DW_ATOM_die_tag = 3u,   // A tag entry.
  DW_ATOM_type_flags = 4u, // Set of flags for a type.

  DW_ATOM_type_type_flags = 5u, // Dsymutil type extension.
  DW_ATOM_qual_name_hash = 6u,  // Dsymutil qualified hash extension.

  // DW_ATOM_type_flags values.

  // Always set for C++, only set for ObjC if this is the @implementation for a
  // class.
  DW_FLAG_type_implementation = 2u,

  // Hash functions.

  // Daniel J. Bernstein hash.
  DW_hash_function_djb = 0u
};

// Return a suggested bucket count for the DWARF v5 Accelerator Table.
inline uint32_t getDebugNamesBucketCount(uint32_t UniqueHashCount) {
  if (UniqueHashCount > 1024)
    return UniqueHashCount / 4;
  if (UniqueHashCount > 16)
    return UniqueHashCount / 2;
  return std::max<uint32_t>(UniqueHashCount, 1);
}

// Constants for the GNU pubnames/pubtypes extensions supporting gdb index.
enum GDBIndexEntryKind {
  GIEK_NONE,
  GIEK_TYPE,
  GIEK_VARIABLE,
  GIEK_FUNCTION,
  GIEK_OTHER,
  GIEK_UNUSED5,
  GIEK_UNUSED6,
  GIEK_UNUSED7
};

enum GDBIndexEntryLinkage { GIEL_EXTERNAL, GIEL_STATIC };

/// \defgroup DwarfConstantsDumping Dwarf constants dumping functions
///
/// All these functions map their argument's value back to the
/// corresponding enumerator name or return an empty StringRef if the value
/// isn't known.
///
/// @{
StringRef TagString(unsigned Tag);
StringRef ChildrenString(unsigned Children);
StringRef AttributeString(unsigned Attribute);
StringRef FormEncodingString(unsigned Encoding);
StringRef OperationEncodingString(unsigned Encoding);
StringRef SubOperationEncodingString(unsigned OpEncoding,
                                     unsigned SubOpEncoding);
StringRef AttributeEncodingString(unsigned Encoding);
StringRef DecimalSignString(unsigned Sign);
StringRef EndianityString(unsigned Endian);
StringRef AccessibilityString(unsigned Access);
StringRef DefaultedMemberString(unsigned DefaultedEncodings);
StringRef VisibilityString(unsigned Visibility);
StringRef VirtualityString(unsigned Virtuality);
StringRef LanguageString(unsigned Language);
StringRef CaseString(unsigned Case);
StringRef ConventionString(unsigned Convention);
StringRef InlineCodeString(unsigned Code);
StringRef ArrayOrderString(unsigned Order);
StringRef LNStandardString(unsigned Standard);
StringRef LNExtendedString(unsigned Encoding);
StringRef MacinfoString(unsigned Encoding);
StringRef MacroString(unsigned Encoding);
StringRef GnuMacroString(unsigned Encoding);
StringRef RangeListEncodingString(unsigned Encoding);
StringRef LocListEncodingString(unsigned Encoding);
StringRef CallFrameString(unsigned Encoding, Triple::ArchType Arch);
StringRef ApplePropertyString(unsigned);
StringRef UnitTypeString(unsigned);
StringRef AtomTypeString(unsigned Atom);
StringRef GDBIndexEntryKindString(GDBIndexEntryKind Kind);
StringRef GDBIndexEntryLinkageString(GDBIndexEntryLinkage Linkage);
StringRef IndexString(unsigned Idx);
StringRef FormatString(DwarfFormat Format);
StringRef FormatString(bool IsDWARF64);
StringRef RLEString(unsigned RLE);
/// @}

/// \defgroup DwarfConstantsParsing Dwarf constants parsing functions
///
/// These functions map their strings back to the corresponding enumeration
/// value or return 0 if there is none, except for these exceptions:
///
/// \li \a getTag() returns \a DW_TAG_invalid on invalid input.
/// \li \a getVirtuality() returns \a DW_VIRTUALITY_invalid on invalid input.
/// \li \a getMacinfo() returns \a DW_MACINFO_invalid on invalid input.
///
/// @{
unsigned getTag(StringRef TagString);
unsigned getOperationEncoding(StringRef OperationEncodingString);
unsigned getSubOperationEncoding(unsigned OpEncoding,
                                 StringRef SubOperationEncodingString);
unsigned getVirtuality(StringRef VirtualityString);
unsigned getLanguage(StringRef LanguageString);
unsigned getCallingConvention(StringRef LanguageString);
unsigned getAttributeEncoding(StringRef EncodingString);
unsigned getMacinfo(StringRef MacinfoString);
unsigned getMacro(StringRef MacroString);
/// @}

/// \defgroup DwarfConstantsVersioning Dwarf version for constants
///
/// For constants defined by DWARF, returns the DWARF version when the constant
/// was first defined. For vendor extensions, if there is a version-related
/// policy for when to emit it, returns a version number for that policy.
/// Otherwise returns 0.
///
/// @{
unsigned TagVersion(Tag T);
unsigned AttributeVersion(Attribute A);
unsigned FormVersion(Form F);
unsigned OperationVersion(LocationAtom O);
unsigned AttributeEncodingVersion(TypeKind E);
unsigned LanguageVersion(SourceLanguage L);
/// @}

/// \defgroup DwarfConstantsVendor Dwarf "vendor" for constants
///
/// These functions return an identifier describing "who" defined the constant,
/// either the DWARF standard itself or the vendor who defined the extension.
///
/// @{
unsigned TagVendor(Tag T);
unsigned AttributeVendor(Attribute A);
unsigned FormVendor(Form F);
unsigned OperationVendor(LocationAtom O);
unsigned AttributeEncodingVendor(TypeKind E);
unsigned LanguageVendor(SourceLanguage L);
/// @}

/// The number of operands for the given LocationAtom.
std::optional<unsigned> OperationOperands(LocationAtom O);

/// The arity of the given LocationAtom. This is the number of elements on the
/// stack this operation operates on. Returns -1 if the arity is variable (e.g.
/// depending on the argument) or unknown.
std::optional<unsigned> OperationArity(LocationAtom O);

std::optional<unsigned> LanguageLowerBound(SourceLanguage L);

/// The size of a reference determined by the DWARF 32/64-bit format.
inline uint8_t getDwarfOffsetByteSize(DwarfFormat Format) {
  switch (Format) {
  case DwarfFormat::DWARF32:
    return 4;
  case DwarfFormat::DWARF64:
    return 8;
  }
  llvm_unreachable("Invalid Format value");
}

/// A helper struct providing information about the byte size of DW_FORM
/// values that vary in size depending on the DWARF version, address byte
/// size, or DWARF32/DWARF64.
struct FormParams {
  uint16_t Version;
  uint8_t AddrSize;
  DwarfFormat Format;
  /// True if DWARF v2 output generally uses relocations for references
  /// to other .debug_* sections.
  bool DwarfUsesRelocationsAcrossSections = false;

  /// The definition of the size of form DW_FORM_ref_addr depends on the
  /// version. In DWARF v2 it's the size of an address; after that, it's the
  /// size of a reference.
  uint8_t getRefAddrByteSize() const {
    if (Version == 2)
      return AddrSize;
    return getDwarfOffsetByteSize();
  }

  /// The size of a reference is determined by the DWARF 32/64-bit format.
  uint8_t getDwarfOffsetByteSize() const {
    return dwarf::getDwarfOffsetByteSize(Format);
  }

  explicit operator bool() const { return Version && AddrSize; }
};

/// Get the byte size of the unit length field depending on the DWARF format.
inline uint8_t getUnitLengthFieldByteSize(DwarfFormat Format) {
  switch (Format) {
  case DwarfFormat::DWARF32:
    return 4;
  case DwarfFormat::DWARF64:
    return 12;
  }
  llvm_unreachable("Invalid Format value");
}

/// Get the fixed byte size for a given form.
///
/// If the form has a fixed byte size, then an Optional with a value will be
/// returned. If the form is always encoded using a variable length storage
/// format (ULEB or SLEB numbers or blocks) then std::nullopt will be returned.
///
/// \param Form DWARF form to get the fixed byte size for.
/// \param Params DWARF parameters to help interpret forms.
/// \returns std::optional<uint8_t> value with the fixed byte size or
/// std::nullopt if \p Form doesn't have a fixed byte size.
std::optional<uint8_t> getFixedFormByteSize(dwarf::Form Form,
                                            FormParams Params);

/// Tells whether the specified form is defined in the specified version,
/// or is an extension if extensions are allowed.
bool isValidFormForVersion(Form F, unsigned Version, bool ExtensionsOk = true);

/// Returns the symbolic string representing Val when used as a value
/// for attribute Attr.
StringRef AttributeValueString(uint16_t Attr, unsigned Val);

/// Returns the symbolic string representing Val when used as a value
/// for atom Atom.
StringRef AtomValueString(uint16_t Atom, unsigned Val);

/// Describes an entry of the various gnu_pub* debug sections.
///
/// The gnu_pub* kind looks like:
///
/// 0-3  reserved
/// 4-6  symbol kind
/// 7    0 == global, 1 == static
///
/// A gdb_index descriptor includes the above kind, shifted 24 bits up with the
/// offset of the cu within the debug_info section stored in those 24 bits.
struct PubIndexEntryDescriptor {
  GDBIndexEntryKind Kind;
  GDBIndexEntryLinkage Linkage;
  PubIndexEntryDescriptor(GDBIndexEntryKind Kind, GDBIndexEntryLinkage Linkage)
      : Kind(Kind), Linkage(Linkage) {}
  /* implicit */ PubIndexEntryDescriptor(GDBIndexEntryKind Kind)
      : Kind(Kind), Linkage(GIEL_EXTERNAL) {}
  explicit PubIndexEntryDescriptor(uint8_t Value)
      : Kind(
            static_cast<GDBIndexEntryKind>((Value & KIND_MASK) >> KIND_OFFSET)),
        Linkage(static_cast<GDBIndexEntryLinkage>((Value & LINKAGE_MASK) >>
                                                  LINKAGE_OFFSET)) {}
  uint8_t toBits() const {
    return Kind << KIND_OFFSET | Linkage << LINKAGE_OFFSET;
  }

private:
  enum {
    KIND_OFFSET = 4,
    KIND_MASK = 7 << KIND_OFFSET,
    LINKAGE_OFFSET = 7,
    LINKAGE_MASK = 1 << LINKAGE_OFFSET
  };
};

template <typename Enum> struct EnumTraits : public std::false_type {};

template <> struct EnumTraits<Attribute> : public std::true_type {
  static constexpr char Type[3] = "AT";
  static constexpr StringRef (*StringFn)(unsigned) = &AttributeString;
};

template <> struct EnumTraits<Form> : public std::true_type {
  static constexpr char Type[5] = "FORM";
  static constexpr StringRef (*StringFn)(unsigned) = &FormEncodingString;
};

template <> struct EnumTraits<Index> : public std::true_type {
  static constexpr char Type[4] = "IDX";
  static constexpr StringRef (*StringFn)(unsigned) = &IndexString;
};

template <> struct EnumTraits<Tag> : public std::true_type {
  static constexpr char Type[4] = "TAG";
  static constexpr StringRef (*StringFn)(unsigned) = &TagString;
};

template <> struct EnumTraits<LineNumberOps> : public std::true_type {
  static constexpr char Type[4] = "LNS";
  static constexpr StringRef (*StringFn)(unsigned) = &LNStandardString;
};

template <> struct EnumTraits<LocationAtom> : public std::true_type {
  static constexpr char Type[3] = "OP";
  static constexpr StringRef (*StringFn)(unsigned) = &OperationEncodingString;
};

inline uint64_t computeTombstoneAddress(uint8_t AddressByteSize) {
  return std::numeric_limits<uint64_t>::max() >> (8 - AddressByteSize) * 8;
}

} // End of namespace dwarf

/// Dwarf constants format_provider
///
/// Specialization of the format_provider template for dwarf enums. Unlike the
/// dumping functions above, these format unknown enumerator values as
/// DW_TYPE_unknown_1234 (e.g. DW_TAG_unknown_ffff).
template <typename Enum>
struct format_provider<Enum, std::enable_if_t<dwarf::EnumTraits<Enum>::value>> {
  static void format(const Enum &E, raw_ostream &OS, StringRef Style) {
    StringRef Str = dwarf::EnumTraits<Enum>::StringFn(E);
    if (Str.empty()) {
      OS << "DW_" << dwarf::EnumTraits<Enum>::Type << "_unknown_"
         << llvm::format("%x", E);
    } else
      OS << Str;
  }
};
} // End of namespace llvm

#endif
