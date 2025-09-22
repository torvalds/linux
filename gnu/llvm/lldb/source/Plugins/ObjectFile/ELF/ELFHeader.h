//===-- ELFHeader.h ------------------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Generic structures and typedefs for ELF files.
///
/// This file provides definitions for the various entities comprising an ELF
/// file.  The structures are generic in the sense that they do not correspond
/// to the exact binary layout of an ELF, but can be used to hold the
/// information present in both 32 and 64 bit variants of the format.  Each
/// entity provides a \c Parse method which is capable of transparently
/// reading both 32 and 64 bit instances of the object.
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_ELF_ELFHEADER_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_ELF_ELFHEADER_H

#include "llvm/BinaryFormat/ELF.h"

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"

namespace lldb_private {
class DataExtractor;
} // End namespace lldb_private.

namespace elf {

/// \name ELF type definitions.
///
/// Types used to represent the various components of ELF structures.  All
/// types are signed or unsigned integral types wide enough to hold values
/// from both
/// 32 and 64 bit ELF variants.
//@{
typedef uint64_t elf_addr;
typedef uint64_t elf_off;
typedef uint16_t elf_half;
typedef uint32_t elf_word;
typedef int32_t elf_sword;
typedef uint64_t elf_size;
typedef uint64_t elf_xword;
typedef int64_t elf_sxword;
//@}

/// \class ELFHeader
/// Generic representation of an ELF file header.
///
/// This object is used to identify the general attributes on an ELF file and
/// to locate additional sections within the file.
struct ELFHeader {
  unsigned char e_ident[llvm::ELF::EI_NIDENT]; ///< ELF file identification.
  elf_addr e_entry;     ///< Virtual address program entry point.
  elf_off e_phoff;      ///< File offset of program header table.
  elf_off e_shoff;      ///< File offset of section header table.
  elf_word e_flags;     ///< Processor specific flags.
  elf_word e_version;   ///< Version of object file (always 1).
  elf_half e_type;      ///< Object file type.
  elf_half e_machine;   ///< Target architecture.
  elf_half e_ehsize;    ///< Byte size of the ELF header.
  elf_half e_phentsize; ///< Size of a program header table entry.
  elf_half e_phnum_hdr; ///< Number of program header entries.
  elf_half e_shentsize; ///< Size of a section header table entry.
  elf_half e_shnum_hdr; ///< Number of section header entries.
  elf_half e_shstrndx_hdr; ///< String table section index.

  // In some cases these numbers do not fit in 16 bits and they are
  // stored outside of the header in section #0. Here are the actual
  // values.
  elf_word e_phnum;     ///< Number of program header entries.
  elf_word e_shnum;     ///< Number of section header entries.
  elf_word e_shstrndx;  ///< String table section index.

  ELFHeader();

  /// Returns true if this is a 32 bit ELF file header.
  ///
  /// \return
  ///    True if this is a 32 bit ELF file header.
  bool Is32Bit() const {
    return e_ident[llvm::ELF::EI_CLASS] == llvm::ELF::ELFCLASS32;
  }

  /// Returns true if this is a 64 bit ELF file header.
  ///
  /// \return
  ///   True if this is a 64 bit ELF file header.
  bool Is64Bit() const {
    return e_ident[llvm::ELF::EI_CLASS] == llvm::ELF::ELFCLASS64;
  }

  /// The byte order of this ELF file header.
  ///
  /// \return
  ///    The byte order of this ELF file as described by the header.
  lldb::ByteOrder GetByteOrder() const;

  /// The jump slot relocation type of this ELF.
  unsigned GetRelocationJumpSlotType() const;

  /// Check if there should be header extension in section header #0
  ///
  /// \return
  ///    True if parsing the ELFHeader requires reading header extension
  ///    and false otherwise.
  bool HasHeaderExtension() const;

  /// Parse an ELFHeader entry starting at position \p offset and update the
  /// data extractor with the address size and byte order attributes as
  /// defined by the header.
  ///
  /// \param[in,out] data
  ///    The DataExtractor to read from.  Updated with the address size and
  ///    byte order attributes appropriate to this header.
  ///
  /// \param[in,out] offset
  ///    Pointer to an offset in the data.  On return the offset will be
  ///    advanced by the number of bytes read.
  ///
  /// \return
  ///    True if the ELFHeader was successfully read and false
  ///    otherwise.
  bool Parse(lldb_private::DataExtractor &data, lldb::offset_t *offset);

  /// Examines at most EI_NIDENT bytes starting from the given pointer and
  /// determines if the magic ELF identification exists.
  ///
  /// \return
  ///    True if the given sequence of bytes identifies an ELF file.
  static bool MagicBytesMatch(const uint8_t *magic);

  /// Examines at most EI_NIDENT bytes starting from the given address and
  /// determines the address size of the underlying ELF file.  This function
  /// should only be called on an pointer for which MagicBytesMatch returns
  /// true.
  ///
  /// \return
  ///    The number of bytes forming an address in the ELF file (either 4 or
  ///    8), else zero if the address size could not be determined.
  static unsigned AddressSizeInBytes(const uint8_t *magic);

private:

  /// Parse an ELFHeader header extension entry.  This method is called by
  /// Parse().
  ///
  /// \param[in] data
  ///    The DataExtractor to read from.
  void ParseHeaderExtension(lldb_private::DataExtractor &data);
};

/// \class ELFSectionHeader
/// Generic representation of an ELF section header.
struct ELFSectionHeader {
  elf_word sh_name;       ///< Section name string index.
  elf_word sh_type;       ///< Section type.
  elf_xword sh_flags;     ///< Section attributes.
  elf_addr sh_addr;       ///< Virtual address of the section in memory.
  elf_off sh_offset;      ///< Start of section from beginning of file.
  elf_xword sh_size;      ///< Number of bytes occupied in the file.
  elf_word sh_link;       ///< Index of associated section.
  elf_word sh_info;       ///< Extra section info (overloaded).
  elf_xword sh_addralign; ///< Power of two alignment constraint.
  elf_xword sh_entsize;   ///< Byte size of each section entry.

  ELFSectionHeader();

  /// Parse an ELFSectionHeader entry from the given DataExtracter starting at
  /// position \p offset.
  ///
  /// \param[in] data
  ///    The DataExtractor to read from.  The address size of the extractor
  ///    determines if a 32 or 64 bit object should be read.
  ///
  /// \param[in,out] offset
  ///    Pointer to an offset in the data.  On return the offset will be
  ///    advanced by the number of bytes read.
  ///
  /// \return
  ///    True if the ELFSectionHeader was successfully read and false
  ///    otherwise.
  bool Parse(const lldb_private::DataExtractor &data, lldb::offset_t *offset);
};

/// \class ELFProgramHeader
/// Generic representation of an ELF program header.
struct ELFProgramHeader {
  elf_word p_type;    ///< Type of program segment.
  elf_word p_flags;   ///< Segment attributes.
  elf_off p_offset;   ///< Start of segment from beginning of file.
  elf_addr p_vaddr;   ///< Virtual address of segment in memory.
  elf_addr p_paddr;   ///< Physical address (for non-VM systems).
  elf_xword p_filesz; ///< Byte size of the segment in file.
  elf_xword p_memsz;  ///< Byte size of the segment in memory.
  elf_xword p_align;  ///< Segment alignment constraint.

  ELFProgramHeader();

  /// Parse an ELFProgramHeader entry from the given DataExtractor starting at
  /// position \p offset.  The address size of the DataExtractor determines if
  /// a 32 or 64 bit object is to be parsed.
  ///
  /// \param[in] data
  ///    The DataExtractor to read from.  The address size of the extractor
  ///    determines if a 32 or 64 bit object should be read.
  ///
  /// \param[in,out] offset
  ///    Pointer to an offset in the data.  On return the offset will be
  ///    advanced by the number of bytes read.
  ///
  /// \return
  ///    True if the ELFProgramHeader was successfully read and false
  ///    otherwise.
  bool Parse(const lldb_private::DataExtractor &data, lldb::offset_t *offset);
};

/// \class ELFSymbol
/// Represents a symbol within an ELF symbol table.
struct ELFSymbol {
  elf_addr st_value;      ///< Absolute or relocatable address.
  elf_xword st_size;      ///< Size of the symbol or zero.
  elf_word st_name;       ///< Symbol name string index.
  unsigned char st_info;  ///< Symbol type and binding attributes.
  unsigned char st_other; ///< Reserved for future use.
  elf_half st_shndx;      ///< Section to which this symbol applies.

  ELFSymbol();

  /// Returns the binding attribute of the st_info member.
  unsigned char getBinding() const { return st_info >> 4; }

  /// Returns the type attribute of the st_info member.
  unsigned char getType() const { return st_info & 0x0F; }

  /// Sets the binding and type of the st_info member.
  void setBindingAndType(unsigned char binding, unsigned char type) {
    st_info = (binding << 4) + (type & 0x0F);
  }

  static const char *bindingToCString(unsigned char binding);

  static const char *typeToCString(unsigned char type);

  static const char *
  sectionIndexToCString(elf_half shndx,
                        const lldb_private::SectionList *section_list);

  /// Parse an ELFSymbol entry from the given DataExtractor starting at
  /// position \p offset.  The address size of the DataExtractor determines if
  /// a 32 or 64 bit object is to be parsed.
  ///
  /// \param[in] data
  ///    The DataExtractor to read from.  The address size of the extractor
  ///    determines if a 32 or 64 bit object should be read.
  ///
  /// \param[in,out] offset
  ///    Pointer to an offset in the data.  On return the offset will be
  ///    advanced by the number of bytes read.
  ///
  /// \return
  ///    True if the ELFSymbol was successfully read and false otherwise.
  bool Parse(const lldb_private::DataExtractor &data, lldb::offset_t *offset);

  void Dump(lldb_private::Stream *s, uint32_t idx,
            const lldb_private::DataExtractor *strtab_data,
            const lldb_private::SectionList *section_list);
};

/// \class ELFDynamic
/// Represents an entry in an ELF dynamic table.
struct ELFDynamic {
  elf_sxword d_tag; ///< Type of dynamic table entry.
  union {
    elf_xword d_val; ///< Integer value of the table entry.
    elf_addr d_ptr;  ///< Pointer value of the table entry.
  };

  ELFDynamic();

  /// Parse an ELFDynamic entry from the given DataExtractor starting at
  /// position \p offset.  The address size of the DataExtractor determines if
  /// a 32 or 64 bit object is to be parsed.
  ///
  /// \param[in] data
  ///    The DataExtractor to read from.  The address size of the extractor
  ///    determines if a 32 or 64 bit object should be read.
  ///
  /// \param[in,out] offset
  ///    Pointer to an offset in the data.  On return the offset will be
  ///    advanced by the number of bytes read.
  ///
  /// \return
  ///    True if the ELFDynamic entry was successfully read and false
  ///    otherwise.
  bool Parse(const lldb_private::DataExtractor &data, lldb::offset_t *offset);
};

/// \class ELFRel
/// Represents a relocation entry with an implicit addend.
struct ELFRel {
  elf_addr r_offset; ///< Address of reference.
  elf_xword r_info;  ///< symbol index and type of relocation.

  ELFRel();

  /// Parse an ELFRel entry from the given DataExtractor starting at position
  /// \p offset.  The address size of the DataExtractor determines if a 32 or
  /// 64 bit object is to be parsed.
  ///
  /// \param[in] data
  ///    The DataExtractor to read from.  The address size of the extractor
  ///    determines if a 32 or 64 bit object should be read.
  ///
  /// \param[in,out] offset
  ///    Pointer to an offset in the data.  On return the offset will be
  ///    advanced by the number of bytes read.
  ///
  /// \return
  ///    True if the ELFRel entry was successfully read and false otherwise.
  bool Parse(const lldb_private::DataExtractor &data, lldb::offset_t *offset);

  /// Returns the type when the given entry represents a 32-bit relocation.
  static unsigned RelocType32(const ELFRel &rel) { return rel.r_info & 0x0ff; }

  /// Returns the type when the given entry represents a 64-bit relocation.
  static unsigned RelocType64(const ELFRel &rel) {
    return rel.r_info & 0xffffffff;
  }

  /// Returns the symbol index when the given entry represents a 32-bit
  /// relocation.
  static unsigned RelocSymbol32(const ELFRel &rel) { return rel.r_info >> 8; }

  /// Returns the symbol index when the given entry represents a 64-bit
  /// relocation.
  static unsigned RelocSymbol64(const ELFRel &rel) { return rel.r_info >> 32; }
};

/// \class ELFRela
/// Represents a relocation entry with an explicit addend.
struct ELFRela {
  elf_addr r_offset;   ///< Address of reference.
  elf_xword r_info;    ///< Symbol index and type of relocation.
  elf_sxword r_addend; ///< Constant part of expression.

  ELFRela();

  /// Parse an ELFRela entry from the given DataExtractor starting at position
  /// \p offset.  The address size of the DataExtractor determines if a 32 or
  /// 64 bit object is to be parsed.
  ///
  /// \param[in] data
  ///    The DataExtractor to read from.  The address size of the extractor
  ///    determines if a 32 or 64 bit object should be read.
  ///
  /// \param[in,out] offset
  ///    Pointer to an offset in the data.  On return the offset will be
  ///    advanced by the number of bytes read.
  ///
  /// \return
  ///    True if the ELFRela entry was successfully read and false otherwise.
  bool Parse(const lldb_private::DataExtractor &data, lldb::offset_t *offset);

  /// Returns the type when the given entry represents a 32-bit relocation.
  static unsigned RelocType32(const ELFRela &rela) {
    return rela.r_info & 0x0ff;
  }

  /// Returns the type when the given entry represents a 64-bit relocation.
  static unsigned RelocType64(const ELFRela &rela) {
    return rela.r_info & 0xffffffff;
  }

  /// Returns the symbol index when the given entry represents a 32-bit
  /// relocation.
  static unsigned RelocSymbol32(const ELFRela &rela) {
    return rela.r_info >> 8;
  }

  /// Returns the symbol index when the given entry represents a 64-bit
  /// relocation.
  static unsigned RelocSymbol64(const ELFRela &rela) {
    return rela.r_info >> 32;
  }
};

} // End namespace elf.

#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_ELF_ELFHEADER_H
