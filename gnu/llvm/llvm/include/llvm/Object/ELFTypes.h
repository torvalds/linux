//===- ELFTypes.h - Endian specific types for ELF ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ELFTYPES_H
#define LLVM_OBJECT_ELFTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace llvm {
namespace object {

template <class ELFT> struct Elf_Ehdr_Impl;
template <class ELFT> struct Elf_Shdr_Impl;
template <class ELFT> struct Elf_Sym_Impl;
template <class ELFT> struct Elf_Dyn_Impl;
template <class ELFT> struct Elf_Phdr_Impl;
template <class ELFT, bool isRela> struct Elf_Rel_Impl;
template <bool Is64> struct Elf_Crel_Impl;
template <class ELFT> struct Elf_Verdef_Impl;
template <class ELFT> struct Elf_Verdaux_Impl;
template <class ELFT> struct Elf_Verneed_Impl;
template <class ELFT> struct Elf_Vernaux_Impl;
template <class ELFT> struct Elf_Versym_Impl;
template <class ELFT> struct Elf_Hash_Impl;
template <class ELFT> struct Elf_GnuHash_Impl;
template <class ELFT> struct Elf_Chdr_Impl;
template <class ELFT> struct Elf_Nhdr_Impl;
template <class ELFT> class Elf_Note_Impl;
template <class ELFT> class Elf_Note_Iterator_Impl;
template <class ELFT> struct Elf_CGProfile_Impl;

template <endianness E, bool Is64> struct ELFType {
private:
  template <typename Ty>
  using packed = support::detail::packed_endian_specific_integral<Ty, E, 1>;

public:
  static const endianness Endianness = E;
  static const bool Is64Bits = Is64;

  using uint = std::conditional_t<Is64, uint64_t, uint32_t>;
  using Ehdr = Elf_Ehdr_Impl<ELFType<E, Is64>>;
  using Shdr = Elf_Shdr_Impl<ELFType<E, Is64>>;
  using Sym = Elf_Sym_Impl<ELFType<E, Is64>>;
  using Dyn = Elf_Dyn_Impl<ELFType<E, Is64>>;
  using Phdr = Elf_Phdr_Impl<ELFType<E, Is64>>;
  using Rel = Elf_Rel_Impl<ELFType<E, Is64>, false>;
  using Rela = Elf_Rel_Impl<ELFType<E, Is64>, true>;
  using Crel = Elf_Crel_Impl<Is64>;
  using Relr = packed<uint>;
  using Verdef = Elf_Verdef_Impl<ELFType<E, Is64>>;
  using Verdaux = Elf_Verdaux_Impl<ELFType<E, Is64>>;
  using Verneed = Elf_Verneed_Impl<ELFType<E, Is64>>;
  using Vernaux = Elf_Vernaux_Impl<ELFType<E, Is64>>;
  using Versym = Elf_Versym_Impl<ELFType<E, Is64>>;
  using Hash = Elf_Hash_Impl<ELFType<E, Is64>>;
  using GnuHash = Elf_GnuHash_Impl<ELFType<E, Is64>>;
  using Chdr = Elf_Chdr_Impl<ELFType<E, Is64>>;
  using Nhdr = Elf_Nhdr_Impl<ELFType<E, Is64>>;
  using Note = Elf_Note_Impl<ELFType<E, Is64>>;
  using NoteIterator = Elf_Note_Iterator_Impl<ELFType<E, Is64>>;
  using CGProfile = Elf_CGProfile_Impl<ELFType<E, Is64>>;
  using DynRange = ArrayRef<Dyn>;
  using ShdrRange = ArrayRef<Shdr>;
  using SymRange = ArrayRef<Sym>;
  using RelRange = ArrayRef<Rel>;
  using RelaRange = ArrayRef<Rela>;
  using RelrRange = ArrayRef<Relr>;
  using PhdrRange = ArrayRef<Phdr>;

  using Half = packed<uint16_t>;
  using Word = packed<uint32_t>;
  using Sword = packed<int32_t>;
  using Xword = packed<uint64_t>;
  using Sxword = packed<int64_t>;
  using Addr = packed<uint>;
  using Off = packed<uint>;
};

using ELF32LE = ELFType<llvm::endianness::little, false>;
using ELF32BE = ELFType<llvm::endianness::big, false>;
using ELF64LE = ELFType<llvm::endianness::little, true>;
using ELF64BE = ELFType<llvm::endianness::big, true>;

// Use an alignment of 2 for the typedefs since that is the worst case for
// ELF files in archives.

// I really don't like doing this, but the alternative is copypasta.
#define LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)                                       \
  using Elf_Addr = typename ELFT::Addr;                                        \
  using Elf_Off = typename ELFT::Off;                                          \
  using Elf_Half = typename ELFT::Half;                                        \
  using Elf_Word = typename ELFT::Word;                                        \
  using Elf_Sword = typename ELFT::Sword;                                      \
  using Elf_Xword = typename ELFT::Xword;                                      \
  using Elf_Sxword = typename ELFT::Sxword;                                    \
  using uintX_t = typename ELFT::uint;                                         \
  using Elf_Ehdr = typename ELFT::Ehdr;                                        \
  using Elf_Shdr = typename ELFT::Shdr;                                        \
  using Elf_Sym = typename ELFT::Sym;                                          \
  using Elf_Dyn = typename ELFT::Dyn;                                          \
  using Elf_Phdr = typename ELFT::Phdr;                                        \
  using Elf_Rel = typename ELFT::Rel;                                          \
  using Elf_Rela = typename ELFT::Rela;                                        \
  using Elf_Crel = typename ELFT::Crel;                                        \
  using Elf_Relr = typename ELFT::Relr;                                        \
  using Elf_Verdef = typename ELFT::Verdef;                                    \
  using Elf_Verdaux = typename ELFT::Verdaux;                                  \
  using Elf_Verneed = typename ELFT::Verneed;                                  \
  using Elf_Vernaux = typename ELFT::Vernaux;                                  \
  using Elf_Versym = typename ELFT::Versym;                                    \
  using Elf_Hash = typename ELFT::Hash;                                        \
  using Elf_GnuHash = typename ELFT::GnuHash;                                  \
  using Elf_Chdr = typename ELFT::Chdr;                                        \
  using Elf_Nhdr = typename ELFT::Nhdr;                                        \
  using Elf_Note = typename ELFT::Note;                                        \
  using Elf_Note_Iterator = typename ELFT::NoteIterator;                       \
  using Elf_CGProfile = typename ELFT::CGProfile;                              \
  using Elf_Dyn_Range = typename ELFT::DynRange;                               \
  using Elf_Shdr_Range = typename ELFT::ShdrRange;                             \
  using Elf_Sym_Range = typename ELFT::SymRange;                               \
  using Elf_Rel_Range = typename ELFT::RelRange;                               \
  using Elf_Rela_Range = typename ELFT::RelaRange;                             \
  using Elf_Relr_Range = typename ELFT::RelrRange;                             \
  using Elf_Phdr_Range = typename ELFT::PhdrRange;

#define LLVM_ELF_COMMA ,
#define LLVM_ELF_IMPORT_TYPES(E, W)                                            \
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFType<E LLVM_ELF_COMMA W>)

// Section header.
template <class ELFT> struct Elf_Shdr_Base;

template <endianness Endianness>
struct Elf_Shdr_Base<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word sh_name;      // Section name (index into string table)
  Elf_Word sh_type;      // Section type (SHT_*)
  Elf_Word sh_flags;     // Section flags (SHF_*)
  Elf_Addr sh_addr;      // Address where section is to be loaded
  Elf_Off sh_offset;     // File offset of section data, in bytes
  Elf_Word sh_size;      // Size of section, in bytes
  Elf_Word sh_link;      // Section type-specific header table index link
  Elf_Word sh_info;      // Section type-specific extra information
  Elf_Word sh_addralign; // Section address alignment
  Elf_Word sh_entsize;   // Size of records contained within the section
};

template <endianness Endianness>
struct Elf_Shdr_Base<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word sh_name;       // Section name (index into string table)
  Elf_Word sh_type;       // Section type (SHT_*)
  Elf_Xword sh_flags;     // Section flags (SHF_*)
  Elf_Addr sh_addr;       // Address where section is to be loaded
  Elf_Off sh_offset;      // File offset of section data, in bytes
  Elf_Xword sh_size;      // Size of section, in bytes
  Elf_Word sh_link;       // Section type-specific header table index link
  Elf_Word sh_info;       // Section type-specific extra information
  Elf_Xword sh_addralign; // Section address alignment
  Elf_Xword sh_entsize;   // Size of records contained within the section
};

template <class ELFT>
struct Elf_Shdr_Impl : Elf_Shdr_Base<ELFT> {
  using Elf_Shdr_Base<ELFT>::sh_entsize;
  using Elf_Shdr_Base<ELFT>::sh_size;

  /// Get the number of entities this section contains if it has any.
  unsigned getEntityCount() const {
    if (sh_entsize == 0)
      return 0;
    return sh_size / sh_entsize;
  }
};

template <class ELFT> struct Elf_Sym_Base;

template <endianness Endianness>
struct Elf_Sym_Base<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word st_name;       // Symbol name (index into string table)
  Elf_Addr st_value;      // Value or address associated with the symbol
  Elf_Word st_size;       // Size of the symbol
  unsigned char st_info;  // Symbol's type and binding attributes
  unsigned char st_other; // Must be zero; reserved
  Elf_Half st_shndx;      // Which section (header table index) it's defined in
};

template <endianness Endianness>
struct Elf_Sym_Base<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word st_name;       // Symbol name (index into string table)
  unsigned char st_info;  // Symbol's type and binding attributes
  unsigned char st_other; // Must be zero; reserved
  Elf_Half st_shndx;      // Which section (header table index) it's defined in
  Elf_Addr st_value;      // Value or address associated with the symbol
  Elf_Xword st_size;      // Size of the symbol
};

template <class ELFT>
struct Elf_Sym_Impl : Elf_Sym_Base<ELFT> {
  using Elf_Sym_Base<ELFT>::st_info;
  using Elf_Sym_Base<ELFT>::st_shndx;
  using Elf_Sym_Base<ELFT>::st_other;
  using Elf_Sym_Base<ELFT>::st_value;

  // These accessors and mutators correspond to the ELF32_ST_BIND,
  // ELF32_ST_TYPE, and ELF32_ST_INFO macros defined in the ELF specification:
  unsigned char getBinding() const { return st_info >> 4; }
  unsigned char getType() const { return st_info & 0x0f; }
  uint64_t getValue() const { return st_value; }
  void setBinding(unsigned char b) { setBindingAndType(b, getType()); }
  void setType(unsigned char t) { setBindingAndType(getBinding(), t); }

  void setBindingAndType(unsigned char b, unsigned char t) {
    st_info = (b << 4) + (t & 0x0f);
  }

  /// Access to the STV_xxx flag stored in the first two bits of st_other.
  /// STV_DEFAULT: 0
  /// STV_INTERNAL: 1
  /// STV_HIDDEN: 2
  /// STV_PROTECTED: 3
  unsigned char getVisibility() const { return st_other & 0x3; }
  void setVisibility(unsigned char v) {
    assert(v < 4 && "Invalid value for visibility");
    st_other = (st_other & ~0x3) | v;
  }

  bool isAbsolute() const { return st_shndx == ELF::SHN_ABS; }

  bool isCommon() const {
    return getType() == ELF::STT_COMMON || st_shndx == ELF::SHN_COMMON;
  }

  bool isDefined() const { return !isUndefined(); }

  bool isProcessorSpecific() const {
    return st_shndx >= ELF::SHN_LOPROC && st_shndx <= ELF::SHN_HIPROC;
  }

  bool isOSSpecific() const {
    return st_shndx >= ELF::SHN_LOOS && st_shndx <= ELF::SHN_HIOS;
  }

  bool isReserved() const {
    // ELF::SHN_HIRESERVE is 0xffff so st_shndx <= ELF::SHN_HIRESERVE is always
    // true and some compilers warn about it.
    return st_shndx >= ELF::SHN_LORESERVE;
  }

  bool isUndefined() const { return st_shndx == ELF::SHN_UNDEF; }

  bool isExternal() const {
    return getBinding() != ELF::STB_LOCAL;
  }

  Expected<StringRef> getName(StringRef StrTab) const;
};

template <class ELFT>
Expected<StringRef> Elf_Sym_Impl<ELFT>::getName(StringRef StrTab) const {
  uint32_t Offset = this->st_name;
  if (Offset >= StrTab.size())
    return createStringError(object_error::parse_failed,
                             "st_name (0x%" PRIx32
                             ") is past the end of the string table"
                             " of size 0x%zx",
                             Offset, StrTab.size());
  return StringRef(StrTab.data() + Offset);
}

/// Elf_Versym: This is the structure of entries in the SHT_GNU_versym section
/// (.gnu.version). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Versym_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Half vs_index; // Version index with flags (e.g. VERSYM_HIDDEN)
};

/// Elf_Verdef: This is the structure of entries in the SHT_GNU_verdef section
/// (.gnu.version_d). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Verdef_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Half vd_version; // Version of this structure (e.g. VER_DEF_CURRENT)
  Elf_Half vd_flags;   // Bitwise flags (VER_DEF_*)
  Elf_Half vd_ndx;     // Version index, used in .gnu.version entries
  Elf_Half vd_cnt;     // Number of Verdaux entries
  Elf_Word vd_hash;    // Hash of name
  Elf_Word vd_aux;     // Offset to the first Verdaux entry (in bytes)
  Elf_Word vd_next;    // Offset to the next Verdef entry (in bytes)

  /// Get the first Verdaux entry for this Verdef.
  const Elf_Verdaux *getAux() const {
    return reinterpret_cast<const Elf_Verdaux *>((const char *)this + vd_aux);
  }
};

/// Elf_Verdaux: This is the structure of auxiliary data in the SHT_GNU_verdef
/// section (.gnu.version_d). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Verdaux_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word vda_name; // Version name (offset in string table)
  Elf_Word vda_next; // Offset to next Verdaux entry (in bytes)
};

/// Elf_Verneed: This is the structure of entries in the SHT_GNU_verneed
/// section (.gnu.version_r). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Verneed_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Half vn_version; // Version of this structure (e.g. VER_NEED_CURRENT)
  Elf_Half vn_cnt;     // Number of associated Vernaux entries
  Elf_Word vn_file;    // Library name (string table offset)
  Elf_Word vn_aux;     // Offset to first Vernaux entry (in bytes)
  Elf_Word vn_next;    // Offset to next Verneed entry (in bytes)
};

/// Elf_Vernaux: This is the structure of auxiliary data in SHT_GNU_verneed
/// section (.gnu.version_r). This structure is identical for ELF32 and ELF64.
template <class ELFT>
struct Elf_Vernaux_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word vna_hash;  // Hash of dependency name
  Elf_Half vna_flags; // Bitwise Flags (VER_FLAG_*)
  Elf_Half vna_other; // Version index, used in .gnu.version entries
  Elf_Word vna_name;  // Dependency name
  Elf_Word vna_next;  // Offset to next Vernaux entry (in bytes)
};

/// Elf_Dyn_Base: This structure matches the form of entries in the dynamic
///               table section (.dynamic) look like.
template <class ELFT> struct Elf_Dyn_Base;

template <endianness Endianness>
struct Elf_Dyn_Base<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Sword d_tag;
  union {
    Elf_Word d_val;
    Elf_Addr d_ptr;
  } d_un;
};

template <endianness Endianness>
struct Elf_Dyn_Base<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Sxword d_tag;
  union {
    Elf_Xword d_val;
    Elf_Addr d_ptr;
  } d_un;
};

/// Elf_Dyn_Impl: This inherits from Elf_Dyn_Base, adding getters.
template <class ELFT>
struct Elf_Dyn_Impl : Elf_Dyn_Base<ELFT> {
  using Elf_Dyn_Base<ELFT>::d_tag;
  using Elf_Dyn_Base<ELFT>::d_un;
  using intX_t = std::conditional_t<ELFT::Is64Bits, int64_t, int32_t>;
  using uintX_t = std::conditional_t<ELFT::Is64Bits, uint64_t, uint32_t>;
  intX_t getTag() const { return d_tag; }
  uintX_t getVal() const { return d_un.d_val; }
  uintX_t getPtr() const { return d_un.d_ptr; }
};

template <endianness Endianness>
struct Elf_Rel_Impl<ELFType<Endianness, false>, false> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  static const bool HasAddend = false;
  static const bool IsCrel = false;
  Elf_Addr r_offset; // Location (file byte offset, or program virtual addr)
  Elf_Word r_info;   // Symbol table index and type of relocation to apply

  uint32_t getRInfo(bool isMips64EL) const {
    assert(!isMips64EL);
    return r_info;
  }
  void setRInfo(uint32_t R, bool IsMips64EL) {
    assert(!IsMips64EL);
    r_info = R;
  }

  // These accessors and mutators correspond to the ELF32_R_SYM, ELF32_R_TYPE,
  // and ELF32_R_INFO macros defined in the ELF specification:
  uint32_t getSymbol(bool isMips64EL) const {
    return this->getRInfo(isMips64EL) >> 8;
  }
  unsigned char getType(bool isMips64EL) const {
    return (unsigned char)(this->getRInfo(isMips64EL) & 0x0ff);
  }
  void setSymbol(uint32_t s, bool IsMips64EL) {
    setSymbolAndType(s, getType(IsMips64EL), IsMips64EL);
  }
  void setType(unsigned char t, bool IsMips64EL) {
    setSymbolAndType(getSymbol(IsMips64EL), t, IsMips64EL);
  }
  void setSymbolAndType(uint32_t s, unsigned char t, bool IsMips64EL) {
    this->setRInfo((s << 8) + t, IsMips64EL);
  }
};

template <endianness Endianness>
struct Elf_Rel_Impl<ELFType<Endianness, false>, true>
    : public Elf_Rel_Impl<ELFType<Endianness, false>, false> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  static const bool HasAddend = true;
  static const bool IsCrel = false;
  Elf_Sword r_addend; // Compute value for relocatable field by adding this
};

template <endianness Endianness>
struct Elf_Rel_Impl<ELFType<Endianness, true>, false> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  static const bool HasAddend = false;
  static const bool IsCrel = false;
  Elf_Addr r_offset; // Location (file byte offset, or program virtual addr)
  Elf_Xword r_info;  // Symbol table index and type of relocation to apply

  uint64_t getRInfo(bool isMips64EL) const {
    uint64_t t = r_info;
    if (!isMips64EL)
      return t;
    // Mips64 little endian has a "special" encoding of r_info. Instead of one
    // 64 bit little endian number, it is a little endian 32 bit number followed
    // by a 32 bit big endian number.
    return (t << 32) | ((t >> 8) & 0xff000000) | ((t >> 24) & 0x00ff0000) |
           ((t >> 40) & 0x0000ff00) | ((t >> 56) & 0x000000ff);
  }

  void setRInfo(uint64_t R, bool IsMips64EL) {
    if (IsMips64EL)
      r_info = (R >> 32) | ((R & 0xff000000) << 8) | ((R & 0x00ff0000) << 24) |
               ((R & 0x0000ff00) << 40) | ((R & 0x000000ff) << 56);
    else
      r_info = R;
  }

  // These accessors and mutators correspond to the ELF64_R_SYM, ELF64_R_TYPE,
  // and ELF64_R_INFO macros defined in the ELF specification:
  uint32_t getSymbol(bool isMips64EL) const {
    return (uint32_t)(this->getRInfo(isMips64EL) >> 32);
  }
  uint32_t getType(bool isMips64EL) const {
    return (uint32_t)(this->getRInfo(isMips64EL) & 0xffffffffL);
  }
  void setSymbol(uint32_t s, bool IsMips64EL) {
    setSymbolAndType(s, getType(IsMips64EL), IsMips64EL);
  }
  void setType(uint32_t t, bool IsMips64EL) {
    setSymbolAndType(getSymbol(IsMips64EL), t, IsMips64EL);
  }
  void setSymbolAndType(uint32_t s, uint32_t t, bool IsMips64EL) {
    this->setRInfo(((uint64_t)s << 32) + (t & 0xffffffffL), IsMips64EL);
  }
};

template <endianness Endianness>
struct Elf_Rel_Impl<ELFType<Endianness, true>, true>
    : public Elf_Rel_Impl<ELFType<Endianness, true>, false> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  static const bool HasAddend = true;
  static const bool IsCrel = false;
  Elf_Sxword r_addend; // Compute value for relocatable field by adding this.
};

// In-memory representation. The serialized representation uses LEB128.
template <bool Is64> struct Elf_Crel_Impl {
  using uint = std::conditional_t<Is64, uint64_t, uint32_t>;
  static const bool HasAddend = true;
  static const bool IsCrel = true;
  uint r_offset;
  uint32_t r_symidx;
  uint32_t r_type;
  std::conditional_t<Is64, int64_t, int32_t> r_addend;

  // Dummy bool parameter is for compatibility with Elf_Rel_Impl.
  uint32_t getType(bool) const { return r_type; }
  uint32_t getSymbol(bool) const { return r_symidx; }
  void setSymbolAndType(uint32_t s, unsigned char t, bool) {
    r_symidx = s;
    r_type = t;
  }
};

template <class ELFT>
struct Elf_Ehdr_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  unsigned char e_ident[ELF::EI_NIDENT]; // ELF Identification bytes
  Elf_Half e_type;                       // Type of file (see ET_*)
  Elf_Half e_machine;   // Required architecture for this file (see EM_*)
  Elf_Word e_version;   // Must be equal to 1
  Elf_Addr e_entry;     // Address to jump to in order to start program
  Elf_Off e_phoff;      // Program header table's file offset, in bytes
  Elf_Off e_shoff;      // Section header table's file offset, in bytes
  Elf_Word e_flags;     // Processor-specific flags
  Elf_Half e_ehsize;    // Size of ELF header, in bytes
  Elf_Half e_phentsize; // Size of an entry in the program header table
  Elf_Half e_phnum;     // Number of entries in the program header table
  Elf_Half e_shentsize; // Size of an entry in the section header table
  Elf_Half e_shnum;     // Number of entries in the section header table
  Elf_Half e_shstrndx;  // Section header table index of section name
                        // string table

  bool checkMagic() const {
    return (memcmp(e_ident, ELF::ElfMagic, strlen(ELF::ElfMagic))) == 0;
  }

  unsigned char getFileClass() const { return e_ident[ELF::EI_CLASS]; }
  unsigned char getDataEncoding() const { return e_ident[ELF::EI_DATA]; }
};

template <endianness Endianness>
struct Elf_Phdr_Impl<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word p_type;   // Type of segment
  Elf_Off p_offset;  // FileOffset where segment is located, in bytes
  Elf_Addr p_vaddr;  // Virtual Address of beginning of segment
  Elf_Addr p_paddr;  // Physical address of beginning of segment (OS-specific)
  Elf_Word p_filesz; // Num. of bytes in file image of segment (may be zero)
  Elf_Word p_memsz;  // Num. of bytes in mem image of segment (may be zero)
  Elf_Word p_flags;  // Segment flags
  Elf_Word p_align;  // Segment alignment constraint
};

template <endianness Endianness>
struct Elf_Phdr_Impl<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word p_type;    // Type of segment
  Elf_Word p_flags;   // Segment flags
  Elf_Off p_offset;   // FileOffset where segment is located, in bytes
  Elf_Addr p_vaddr;   // Virtual Address of beginning of segment
  Elf_Addr p_paddr;   // Physical address of beginning of segment (OS-specific)
  Elf_Xword p_filesz; // Num. of bytes in file image of segment (may be zero)
  Elf_Xword p_memsz;  // Num. of bytes in mem image of segment (may be zero)
  Elf_Xword p_align;  // Segment alignment constraint
};

// ELFT needed for endianness.
template <class ELFT>
struct Elf_Hash_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word nbucket;
  Elf_Word nchain;

  ArrayRef<Elf_Word> buckets() const {
    return ArrayRef<Elf_Word>(&nbucket + 2, &nbucket + 2 + nbucket);
  }

  ArrayRef<Elf_Word> chains() const {
    return ArrayRef<Elf_Word>(&nbucket + 2 + nbucket,
                              &nbucket + 2 + nbucket + nchain);
  }
};

// .gnu.hash section
template <class ELFT>
struct Elf_GnuHash_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word nbuckets;
  Elf_Word symndx;
  Elf_Word maskwords;
  Elf_Word shift2;

  ArrayRef<Elf_Off> filter() const {
    return ArrayRef<Elf_Off>(reinterpret_cast<const Elf_Off *>(&shift2 + 1),
                             maskwords);
  }

  ArrayRef<Elf_Word> buckets() const {
    return ArrayRef<Elf_Word>(
        reinterpret_cast<const Elf_Word *>(filter().end()), nbuckets);
  }

  ArrayRef<Elf_Word> values(unsigned DynamicSymCount) const {
    assert(DynamicSymCount >= symndx);
    return ArrayRef<Elf_Word>(buckets().end(), DynamicSymCount - symndx);
  }
};

// Compressed section headers.
// http://www.sco.com/developers/gabi/latest/ch4.sheader.html#compression_header
template <endianness Endianness>
struct Elf_Chdr_Impl<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word ch_type;
  Elf_Word ch_size;
  Elf_Word ch_addralign;
};

template <endianness Endianness>
struct Elf_Chdr_Impl<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word ch_type;
  Elf_Word ch_reserved;
  Elf_Xword ch_size;
  Elf_Xword ch_addralign;
};

/// Note header
template <class ELFT>
struct Elf_Nhdr_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Word n_namesz;
  Elf_Word n_descsz;
  Elf_Word n_type;

  /// Get the size of the note, including name, descriptor, and padding. Both
  /// the start and the end of the descriptor are aligned by the section
  /// alignment. In practice many 64-bit systems deviate from the generic ABI by
  /// using sh_addralign=4.
  size_t getSize(size_t Align) const {
    return alignToPowerOf2(sizeof(*this) + n_namesz, Align) +
           alignToPowerOf2(n_descsz, Align);
  }
};

/// An ELF note.
///
/// Wraps a note header, providing methods for accessing the name and
/// descriptor safely.
template <class ELFT>
class Elf_Note_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  const Elf_Nhdr_Impl<ELFT> &Nhdr;

  template <class NoteIteratorELFT> friend class Elf_Note_Iterator_Impl;

public:
  Elf_Note_Impl(const Elf_Nhdr_Impl<ELFT> &Nhdr) : Nhdr(Nhdr) {}

  /// Get the note's name, excluding the terminating null byte.
  StringRef getName() const {
    if (!Nhdr.n_namesz)
      return StringRef();
    return StringRef(reinterpret_cast<const char *>(&Nhdr) + sizeof(Nhdr),
                     Nhdr.n_namesz - 1);
  }

  /// Get the note's descriptor.
  ArrayRef<uint8_t> getDesc(size_t Align) const {
    if (!Nhdr.n_descsz)
      return ArrayRef<uint8_t>();
    return ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(&Nhdr) +
            alignToPowerOf2(sizeof(Nhdr) + Nhdr.n_namesz, Align),
        Nhdr.n_descsz);
  }

  /// Get the note's descriptor as StringRef
  StringRef getDescAsStringRef(size_t Align) const {
    ArrayRef<uint8_t> Desc = getDesc(Align);
    return StringRef(reinterpret_cast<const char *>(Desc.data()), Desc.size());
  }

  /// Get the note's type.
  Elf_Word getType() const { return Nhdr.n_type; }
};

template <class ELFT> class Elf_Note_Iterator_Impl {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = Elf_Note_Impl<ELFT>;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using reference = value_type &;

private:
  // Nhdr being a nullptr marks the end of iteration.
  const Elf_Nhdr_Impl<ELFT> *Nhdr = nullptr;
  size_t RemainingSize = 0u;
  size_t Align = 0;
  Error *Err = nullptr;

  template <class ELFFileELFT> friend class ELFFile;

  // Stop iteration and indicate an overflow.
  void stopWithOverflowError() {
    Nhdr = nullptr;
    *Err = make_error<StringError>("ELF note overflows container",
                                   object_error::parse_failed);
  }

  // Advance Nhdr by NoteSize bytes, starting from NhdrPos.
  //
  // Assumes NoteSize <= RemainingSize. Ensures Nhdr->getSize() <= RemainingSize
  // upon returning. Handles stopping iteration when reaching the end of the
  // container, either cleanly or with an overflow error.
  void advanceNhdr(const uint8_t *NhdrPos, size_t NoteSize) {
    RemainingSize -= NoteSize;
    if (RemainingSize == 0u) {
      // Ensure that if the iterator walks to the end, the error is checked
      // afterwards.
      *Err = Error::success();
      Nhdr = nullptr;
    } else if (sizeof(*Nhdr) > RemainingSize)
      stopWithOverflowError();
    else {
      Nhdr = reinterpret_cast<const Elf_Nhdr_Impl<ELFT> *>(NhdrPos + NoteSize);
      if (Nhdr->getSize(Align) > RemainingSize)
        stopWithOverflowError();
      else
        *Err = Error::success();
    }
  }

  Elf_Note_Iterator_Impl() = default;
  explicit Elf_Note_Iterator_Impl(Error &Err) : Err(&Err) {}
  Elf_Note_Iterator_Impl(const uint8_t *Start, size_t Size, size_t Align,
                         Error &Err)
      : RemainingSize(Size), Align(Align), Err(&Err) {
    consumeError(std::move(Err));
    assert(Start && "ELF note iterator starting at NULL");
    advanceNhdr(Start, 0u);
  }

public:
  Elf_Note_Iterator_Impl &operator++() {
    assert(Nhdr && "incremented ELF note end iterator");
    const uint8_t *NhdrPos = reinterpret_cast<const uint8_t *>(Nhdr);
    size_t NoteSize = Nhdr->getSize(Align);
    advanceNhdr(NhdrPos, NoteSize);
    return *this;
  }
  bool operator==(Elf_Note_Iterator_Impl Other) const {
    if (!Nhdr && Other.Err)
      (void)(bool)(*Other.Err);
    if (!Other.Nhdr && Err)
      (void)(bool)(*Err);
    return Nhdr == Other.Nhdr;
  }
  bool operator!=(Elf_Note_Iterator_Impl Other) const {
    return !(*this == Other);
  }
  Elf_Note_Impl<ELFT> operator*() const {
    assert(Nhdr && "dereferenced ELF note end iterator");
    return Elf_Note_Impl<ELFT>(*Nhdr);
  }
};

template <class ELFT> struct Elf_CGProfile_Impl {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Xword cgp_weight;
};

// MIPS .reginfo section
template <class ELFT>
struct Elf_Mips_RegInfo;

template <llvm::endianness Endianness>
struct Elf_Mips_RegInfo<ELFType<Endianness, false>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, false)
  Elf_Word ri_gprmask;     // bit-mask of used general registers
  Elf_Word ri_cprmask[4];  // bit-mask of used co-processor registers
  Elf_Addr ri_gp_value;    // gp register value
};

template <llvm::endianness Endianness>
struct Elf_Mips_RegInfo<ELFType<Endianness, true>> {
  LLVM_ELF_IMPORT_TYPES(Endianness, true)
  Elf_Word ri_gprmask;     // bit-mask of used general registers
  Elf_Word ri_pad;         // unused padding field
  Elf_Word ri_cprmask[4];  // bit-mask of used co-processor registers
  Elf_Addr ri_gp_value;    // gp register value
};

// .MIPS.options section
template <class ELFT> struct Elf_Mips_Options {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  uint8_t kind;     // Determines interpretation of variable part of descriptor
  uint8_t size;     // Byte size of descriptor, including this header
  Elf_Half section; // Section header index of section affected,
                    // or 0 for global options
  Elf_Word info;    // Kind-specific information

  Elf_Mips_RegInfo<ELFT> &getRegInfo() {
    assert(kind == ELF::ODK_REGINFO);
    return *reinterpret_cast<Elf_Mips_RegInfo<ELFT> *>(
        (uint8_t *)this + sizeof(Elf_Mips_Options));
  }
  const Elf_Mips_RegInfo<ELFT> &getRegInfo() const {
    return const_cast<Elf_Mips_Options *>(this)->getRegInfo();
  }
};

// .MIPS.abiflags section content
template <class ELFT> struct Elf_Mips_ABIFlags {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  Elf_Half version;  // Version of the structure
  uint8_t isa_level; // ISA level: 1-5, 32, and 64
  uint8_t isa_rev;   // ISA revision (0 for MIPS I - MIPS V)
  uint8_t gpr_size;  // General purpose registers size
  uint8_t cpr1_size; // Co-processor 1 registers size
  uint8_t cpr2_size; // Co-processor 2 registers size
  uint8_t fp_abi;    // Floating-point ABI flag
  Elf_Word isa_ext;  // Processor-specific extension
  Elf_Word ases;     // ASEs flags
  Elf_Word flags1;   // General flags
  Elf_Word flags2;   // General flags
};

// Struct representing the BBAddrMap for one function.
struct BBAddrMap {

  // Bitfield of optional features to control the extra information
  // emitted/encoded in the the section.
  struct Features {
    bool FuncEntryCount : 1;
    bool BBFreq : 1;
    bool BrProb : 1;
    bool MultiBBRange : 1;

    bool hasPGOAnalysis() const { return FuncEntryCount || BBFreq || BrProb; }

    bool hasPGOAnalysisBBData() const { return BBFreq || BrProb; }

    // Encodes to minimum bit width representation.
    uint8_t encode() const {
      return (static_cast<uint8_t>(FuncEntryCount) << 0) |
             (static_cast<uint8_t>(BBFreq) << 1) |
             (static_cast<uint8_t>(BrProb) << 2) |
             (static_cast<uint8_t>(MultiBBRange) << 3);
    }

    // Decodes from minimum bit width representation and validates no
    // unnecessary bits are used.
    static Expected<Features> decode(uint8_t Val) {
      Features Feat{
          static_cast<bool>(Val & (1 << 0)), static_cast<bool>(Val & (1 << 1)),
          static_cast<bool>(Val & (1 << 2)), static_cast<bool>(Val & (1 << 3))};
      if (Feat.encode() != Val)
        return createStringError(
            std::error_code(), "invalid encoding for BBAddrMap::Features: 0x%x",
            Val);
      return Feat;
    }

    bool operator==(const Features &Other) const {
      return std::tie(FuncEntryCount, BBFreq, BrProb, MultiBBRange) ==
             std::tie(Other.FuncEntryCount, Other.BBFreq, Other.BrProb,
                      Other.MultiBBRange);
    }
  };

  // Struct representing the BBAddrMap information for one basic block.
  struct BBEntry {
    struct Metadata {
      bool HasReturn : 1;         // If this block ends with a return (or tail
                                  // call).
      bool HasTailCall : 1;       // If this block ends with a tail call.
      bool IsEHPad : 1;           // If this is an exception handling block.
      bool CanFallThrough : 1;    // If this block can fall through to its next.
      bool HasIndirectBranch : 1; // If this block ends with an indirect branch
                                  // (branch via a register).

      bool operator==(const Metadata &Other) const {
        return HasReturn == Other.HasReturn &&
               HasTailCall == Other.HasTailCall && IsEHPad == Other.IsEHPad &&
               CanFallThrough == Other.CanFallThrough &&
               HasIndirectBranch == Other.HasIndirectBranch;
      }

      // Encodes this struct as a uint32_t value.
      uint32_t encode() const {
        return static_cast<uint32_t>(HasReturn) |
               (static_cast<uint32_t>(HasTailCall) << 1) |
               (static_cast<uint32_t>(IsEHPad) << 2) |
               (static_cast<uint32_t>(CanFallThrough) << 3) |
               (static_cast<uint32_t>(HasIndirectBranch) << 4);
      }

      // Decodes and returns a Metadata struct from a uint32_t value.
      static Expected<Metadata> decode(uint32_t V) {
        Metadata MD{/*HasReturn=*/static_cast<bool>(V & 1),
                    /*HasTailCall=*/static_cast<bool>(V & (1 << 1)),
                    /*IsEHPad=*/static_cast<bool>(V & (1 << 2)),
                    /*CanFallThrough=*/static_cast<bool>(V & (1 << 3)),
                    /*HasIndirectBranch=*/static_cast<bool>(V & (1 << 4))};
        if (MD.encode() != V)
          return createStringError(
              std::error_code(), "invalid encoding for BBEntry::Metadata: 0x%x",
              V);
        return MD;
      }
    };

    uint32_t ID = 0;     // Unique ID of this basic block.
    uint32_t Offset = 0; // Offset of basic block relative to the base address.
    uint32_t Size = 0;   // Size of the basic block.
    Metadata MD = {false, false, false, false,
                   false}; // Metdata for this basic block.

    BBEntry(uint32_t ID, uint32_t Offset, uint32_t Size, Metadata MD)
        : ID(ID), Offset(Offset), Size(Size), MD(MD){};

    bool operator==(const BBEntry &Other) const {
      return ID == Other.ID && Offset == Other.Offset && Size == Other.Size &&
             MD == Other.MD;
    }

    bool hasReturn() const { return MD.HasReturn; }
    bool hasTailCall() const { return MD.HasTailCall; }
    bool isEHPad() const { return MD.IsEHPad; }
    bool canFallThrough() const { return MD.CanFallThrough; }
    bool hasIndirectBranch() const { return MD.HasIndirectBranch; }
  };

  // Struct representing the BBAddrMap information for a contiguous range of
  // basic blocks (a function or a basic block section).
  struct BBRangeEntry {
    uint64_t BaseAddress = 0;       // Base address of the range.
    std::vector<BBEntry> BBEntries; // Basic block entries for this range.

    // Equality operator for unit testing.
    bool operator==(const BBRangeEntry &Other) const {
      return BaseAddress == Other.BaseAddress &&
             std::equal(BBEntries.begin(), BBEntries.end(),
                        Other.BBEntries.begin());
    }
  };

  // All ranges for this function. Cannot be empty. The first range always
  // corresponds to the function entry.
  std::vector<BBRangeEntry> BBRanges;

  // Returns the function address associated with this BBAddrMap, which is
  // stored as the `BaseAddress` of its first BBRangeEntry.
  uint64_t getFunctionAddress() const {
    assert(!BBRanges.empty());
    return BBRanges.front().BaseAddress;
  }

  // Returns the total number of bb entries in all bb ranges.
  size_t getNumBBEntries() const {
    size_t NumBBEntries = 0;
    for (const auto &BBR : BBRanges)
      NumBBEntries += BBR.BBEntries.size();
    return NumBBEntries;
  }

  // Returns the index of the bb range with the given base address, or
  // `std::nullopt` if no such range exists.
  std::optional<size_t>
  getBBRangeIndexForBaseAddress(uint64_t BaseAddress) const {
    for (size_t I = 0; I < BBRanges.size(); ++I)
      if (BBRanges[I].BaseAddress == BaseAddress)
        return I;
    return {};
  }

  // Returns bb entries in the first range.
  const std::vector<BBEntry> &getBBEntries() const {
    return BBRanges.front().BBEntries;
  }

  const std::vector<BBRangeEntry> &getBBRanges() const { return BBRanges; }

  // Equality operator for unit testing.
  bool operator==(const BBAddrMap &Other) const {
    return std::equal(BBRanges.begin(), BBRanges.end(), Other.BBRanges.begin());
  }
};

/// A feature extension of BBAddrMap that holds information relevant to PGO.
struct PGOAnalysisMap {
  /// Extra basic block data with fields for block frequency and branch
  /// probability.
  struct PGOBBEntry {
    /// Single successor of a given basic block that contains the tag and branch
    /// probability associated with it.
    struct SuccessorEntry {
      /// Unique ID of this successor basic block.
      uint32_t ID;
      /// Branch Probability of the edge to this successor taken from MBPI.
      BranchProbability Prob;

      bool operator==(const SuccessorEntry &Other) const {
        return std::tie(ID, Prob) == std::tie(Other.ID, Other.Prob);
      }
    };

    /// Block frequency taken from MBFI
    BlockFrequency BlockFreq;
    /// List of successors of the current block
    llvm::SmallVector<SuccessorEntry, 2> Successors;

    bool operator==(const PGOBBEntry &Other) const {
      return std::tie(BlockFreq, Successors) ==
             std::tie(Other.BlockFreq, Other.Successors);
    }
  };

  uint64_t FuncEntryCount;           // Prof count from IR function
  std::vector<PGOBBEntry> BBEntries; // Extended basic block entries

  // Flags to indicate if each PGO related info was enabled in this function
  BBAddrMap::Features FeatEnable;

  bool operator==(const PGOAnalysisMap &Other) const {
    return std::tie(FuncEntryCount, BBEntries, FeatEnable) ==
           std::tie(Other.FuncEntryCount, Other.BBEntries, Other.FeatEnable);
  }
};

} // end namespace object.
} // end namespace llvm.

#endif // LLVM_OBJECT_ELFTYPES_H
