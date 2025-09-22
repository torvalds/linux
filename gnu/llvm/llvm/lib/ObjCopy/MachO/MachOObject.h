//===- MachOObject.h - Mach-O object file model -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_MACHO_MACHOOBJECT_H
#define LLVM_LIB_OBJCOPY_MACHO_MACHOOBJECT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/ObjectYAML/DWARFYAML.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
namespace objcopy {
namespace macho {

struct MachHeader {
  uint32_t Magic;
  uint32_t CPUType;
  uint32_t CPUSubType;
  uint32_t FileType;
  uint32_t NCmds;
  uint32_t SizeOfCmds;
  uint32_t Flags;
  uint32_t Reserved = 0;
};

struct RelocationInfo;
struct Section {
  uint32_t Index;
  std::string Segname;
  std::string Sectname;
  // CanonicalName is a string formatted as “<Segname>,<Sectname>".
  std::string CanonicalName;
  uint64_t Addr = 0;
  uint64_t Size = 0;
  // Offset in the input file.
  std::optional<uint32_t> OriginalOffset;
  uint32_t Offset = 0;
  uint32_t Align = 0;
  uint32_t RelOff = 0;
  uint32_t NReloc = 0;
  uint32_t Flags = 0;
  uint32_t Reserved1 = 0;
  uint32_t Reserved2 = 0;
  uint32_t Reserved3 = 0;
  StringRef Content;
  std::vector<RelocationInfo> Relocations;

  Section(StringRef SegName, StringRef SectName);

  Section(StringRef SegName, StringRef SectName, StringRef Content);

  MachO::SectionType getType() const {
    return static_cast<MachO::SectionType>(Flags & MachO::SECTION_TYPE);
  }

  bool isVirtualSection() const {
    return (getType() == MachO::S_ZEROFILL ||
            getType() == MachO::S_GB_ZEROFILL ||
            getType() == MachO::S_THREAD_LOCAL_ZEROFILL);
  }

  bool hasValidOffset() const {
    return !(isVirtualSection() || (OriginalOffset && *OriginalOffset == 0));
  }
};

struct LoadCommand {
  // The type MachO::macho_load_command is defined in llvm/BinaryFormat/MachO.h
  // and it is a union of all the structs corresponding to various load
  // commands.
  MachO::macho_load_command MachOLoadCommand;

  // The raw content of the payload of the load command (located right after the
  // corresponding struct). In some cases it is either empty or can be
  // copied-over without digging into its structure.
  std::vector<uint8_t> Payload;

  // Some load commands can contain (inside the payload) an array of sections,
  // though the contents of the sections are stored separately. The struct
  // Section describes only sections' metadata and where to find the
  // corresponding content inside the binary.
  std::vector<std::unique_ptr<Section>> Sections;

  // Returns the segment name if the load command is a segment command.
  std::optional<StringRef> getSegmentName() const;

  // Returns the segment vm address if the load command is a segment command.
  std::optional<uint64_t> getSegmentVMAddr() const;
};

// A symbol information. Fields which starts with "n_" are same as them in the
// nlist.
struct SymbolEntry {
  std::string Name;
  bool Referenced = false;
  uint32_t Index;
  uint8_t n_type;
  uint8_t n_sect;
  uint16_t n_desc;
  uint64_t n_value;

  bool isExternalSymbol() const { return n_type & MachO::N_EXT; }

  bool isLocalSymbol() const { return !isExternalSymbol(); }

  bool isUndefinedSymbol() const {
    return (n_type & MachO::N_TYPE) == MachO::N_UNDF;
  }

  bool isSwiftSymbol() const {
    return StringRef(Name).starts_with("_$s") ||
           StringRef(Name).starts_with("_$S");
  }

  std::optional<uint32_t> section() const {
    return n_sect == MachO::NO_SECT ? std::nullopt
                                    : std::optional<uint32_t>(n_sect);
  }
};

/// The location of the symbol table inside the binary is described by LC_SYMTAB
/// load command.
struct SymbolTable {
  std::vector<std::unique_ptr<SymbolEntry>> Symbols;

  using iterator = pointee_iterator<
      std::vector<std::unique_ptr<SymbolEntry>>::const_iterator>;

  iterator begin() const { return iterator(Symbols.begin()); }
  iterator end() const { return iterator(Symbols.end()); }

  const SymbolEntry *getSymbolByIndex(uint32_t Index) const;
  SymbolEntry *getSymbolByIndex(uint32_t Index);
  void removeSymbols(
      function_ref<bool(const std::unique_ptr<SymbolEntry> &)> ToRemove);
};

struct IndirectSymbolEntry {
  // The original value in an indirect symbol table. Higher bits encode extra
  // information (INDIRECT_SYMBOL_LOCAL and INDIRECT_SYMBOL_ABS).
  uint32_t OriginalIndex;
  /// The Symbol referenced by this entry. It's std::nullopt if the index is
  /// INDIRECT_SYMBOL_LOCAL or INDIRECT_SYMBOL_ABS.
  std::optional<SymbolEntry *> Symbol;

  IndirectSymbolEntry(uint32_t OriginalIndex,
                      std::optional<SymbolEntry *> Symbol)
      : OriginalIndex(OriginalIndex), Symbol(Symbol) {}
};

struct IndirectSymbolTable {
  std::vector<IndirectSymbolEntry> Symbols;
};

/// The location of the string table inside the binary is described by LC_SYMTAB
/// load command.
struct StringTable {
  std::vector<std::string> Strings;
};

struct RelocationInfo {
  // The referenced symbol entry. Set if !Scattered && Extern.
  std::optional<const SymbolEntry *> Symbol;
  // The referenced section. Set if !Scattered && !Extern.
  std::optional<const Section *> Sec;
  // True if Info is a scattered_relocation_info.
  bool Scattered;
  // True if the type is an ADDEND. r_symbolnum holds the addend instead of a
  // symbol index.
  bool IsAddend;
  // True if the r_symbolnum points to a section number (i.e. r_extern=0).
  bool Extern;
  MachO::any_relocation_info Info;

  unsigned getPlainRelocationSymbolNum(bool IsLittleEndian) {
    if (IsLittleEndian)
      return Info.r_word1 & 0xffffff;
    return Info.r_word1 >> 8;
  }

  void setPlainRelocationSymbolNum(unsigned SymbolNum, bool IsLittleEndian) {
    assert(SymbolNum < (1 << 24) && "SymbolNum out of range");
    if (IsLittleEndian)
      Info.r_word1 = (Info.r_word1 & ~0x00ffffff) | SymbolNum;
    else
      Info.r_word1 = (Info.r_word1 & ~0xffffff00) | (SymbolNum << 8);
  }
};

/// The location of the rebase info inside the binary is described by
/// LC_DYLD_INFO load command. Dyld rebases an image whenever dyld loads it at
/// an address different from its preferred address.  The rebase information is
/// a stream of byte sized opcodes whose symbolic names start with
/// REBASE_OPCODE_. Conceptually the rebase information is a table of tuples:
///   <seg-index, seg-offset, type>
/// The opcodes are a compressed way to encode the table by only
/// encoding when a column changes.  In addition simple patterns
/// like "every n'th offset for m times" can be encoded in a few
/// bytes.
struct RebaseInfo {
  // At the moment we do not parse this info (and it is simply copied over),
  // but the proper support will be added later.
  ArrayRef<uint8_t> Opcodes;
};

/// The location of the bind info inside the binary is described by
/// LC_DYLD_INFO load command. Dyld binds an image during the loading process,
/// if the image requires any pointers to be initialized to symbols in other
/// images. The bind information is a stream of byte sized opcodes whose
/// symbolic names start with BIND_OPCODE_. Conceptually the bind information is
/// a table of tuples: <seg-index, seg-offset, type, symbol-library-ordinal,
/// symbol-name, addend> The opcodes are a compressed way to encode the table by
/// only encoding when a column changes.  In addition simple patterns like for
/// runs of pointers initialized to the same value can be encoded in a few
/// bytes.
struct BindInfo {
  // At the moment we do not parse this info (and it is simply copied over),
  // but the proper support will be added later.
  ArrayRef<uint8_t> Opcodes;
};

/// The location of the weak bind info inside the binary is described by
/// LC_DYLD_INFO load command. Some C++ programs require dyld to unique symbols
/// so that all images in the process use the same copy of some code/data. This
/// step is done after binding. The content of the weak_bind info is an opcode
/// stream like the bind_info.  But it is sorted alphabetically by symbol name.
/// This enable dyld to walk all images with weak binding information in order
/// and look for collisions.  If there are no collisions, dyld does no updating.
/// That means that some fixups are also encoded in the bind_info.  For
/// instance, all calls to "operator new" are first bound to libstdc++.dylib
/// using the information in bind_info.  Then if some image overrides operator
/// new that is detected when the weak_bind information is processed and the
/// call to operator new is then rebound.
struct WeakBindInfo {
  // At the moment we do not parse this info (and it is simply copied over),
  // but the proper support will be added later.
  ArrayRef<uint8_t> Opcodes;
};

/// The location of the lazy bind info inside the binary is described by
/// LC_DYLD_INFO load command. Some uses of external symbols do not need to be
/// bound immediately. Instead they can be lazily bound on first use.  The
/// lazy_bind contains a stream of BIND opcodes to bind all lazy symbols. Normal
/// use is that dyld ignores the lazy_bind section when loading an image.
/// Instead the static linker arranged for the lazy pointer to initially point
/// to a helper function which pushes the offset into the lazy_bind area for the
/// symbol needing to be bound, then jumps to dyld which simply adds the offset
/// to lazy_bind_off to get the information on what to bind.
struct LazyBindInfo {
  ArrayRef<uint8_t> Opcodes;
};

/// The location of the export info inside the binary is described by
/// LC_DYLD_INFO load command. The symbols exported by a dylib are encoded in a
/// trie.  This is a compact representation that factors out common prefixes. It
/// also reduces LINKEDIT pages in RAM because it encodes all information (name,
/// address, flags) in one small, contiguous range. The export area is a stream
/// of nodes.  The first node sequentially is the start node for the trie. Nodes
/// for a symbol start with a uleb128 that is the length of the exported symbol
/// information for the string so far. If there is no exported symbol, the node
/// starts with a zero byte. If there is exported info, it follows the length.
/// First is a uleb128 containing flags. Normally, it is followed by
/// a uleb128 encoded offset which is location of the content named
/// by the symbol from the mach_header for the image.  If the flags
/// is EXPORT_SYMBOL_FLAGS_REEXPORT, then following the flags is
/// a uleb128 encoded library ordinal, then a zero terminated
/// UTF8 string.  If the string is zero length, then the symbol
/// is re-export from the specified dylib with the same name.
/// If the flags is EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER, then following
/// the flags is two uleb128s: the stub offset and the resolver offset.
/// The stub is used by non-lazy pointers.  The resolver is used
/// by lazy pointers and must be called to get the actual address to use.
/// After the optional exported symbol information is a byte of
/// how many edges (0-255) that this node has leaving it,
/// followed by each edge.
/// Each edge is a zero terminated UTF8 of the addition chars
/// in the symbol, followed by a uleb128 offset for the node that
/// edge points to.
struct ExportInfo {
  ArrayRef<uint8_t> Trie;
};

struct LinkData {
  ArrayRef<uint8_t> Data;
};

struct Object {
  MachHeader Header;
  std::vector<LoadCommand> LoadCommands;

  SymbolTable SymTable;
  StringTable StrTable;

  RebaseInfo Rebases;
  BindInfo Binds;
  WeakBindInfo WeakBinds;
  LazyBindInfo LazyBinds;
  ExportInfo Exports;
  IndirectSymbolTable IndirectSymTable;
  LinkData DataInCode;
  LinkData LinkerOptimizationHint;
  LinkData FunctionStarts;
  LinkData ExportsTrie;
  LinkData ChainedFixups;
  LinkData DylibCodeSignDRs;

  std::optional<uint32_t> SwiftVersion;

  /// The index of LC_CODE_SIGNATURE load command if present.
  std::optional<size_t> CodeSignatureCommandIndex;
  /// The index of LC_DYLIB_CODE_SIGN_DRS load command if present.
  std::optional<size_t> DylibCodeSignDRsIndex;
  /// The index of LC_SYMTAB load command if present.
  std::optional<size_t> SymTabCommandIndex;
  /// The index of LC_DYLD_INFO or LC_DYLD_INFO_ONLY load command if present.
  std::optional<size_t> DyLdInfoCommandIndex;
  /// The index LC_DYSYMTAB load command if present.
  std::optional<size_t> DySymTabCommandIndex;
  /// The index LC_DATA_IN_CODE load command if present.
  std::optional<size_t> DataInCodeCommandIndex;
  /// The index of LC_LINKER_OPTIMIZATIN_HINT load command if present.
  std::optional<size_t> LinkerOptimizationHintCommandIndex;
  /// The index LC_FUNCTION_STARTS load command if present.
  std::optional<size_t> FunctionStartsCommandIndex;
  /// The index LC_DYLD_CHAINED_FIXUPS load command if present.
  std::optional<size_t> ChainedFixupsCommandIndex;
  /// The index LC_DYLD_EXPORTS_TRIE load command if present.
  std::optional<size_t> ExportsTrieCommandIndex;
  /// The index of the LC_SEGMENT or LC_SEGMENT_64 load command
  /// corresponding to the __TEXT segment.
  std::optional<size_t> TextSegmentCommandIndex;

  BumpPtrAllocator Alloc;
  StringSaver NewSectionsContents;

  Object() : NewSectionsContents(Alloc) {}

  Error
  removeSections(function_ref<bool(const std::unique_ptr<Section> &)> ToRemove);

  Error removeLoadCommands(function_ref<bool(const LoadCommand &)> ToRemove);

  void updateLoadCommandIndexes();

  /// Creates a new segment load command in the object and returns a reference
  /// to the newly created load command. The caller should verify that SegName
  /// is not too long (SegName.size() should be less than or equal to 16).
  LoadCommand &addSegment(StringRef SegName, uint64_t SegVMSize);

  bool is64Bit() const {
    return Header.Magic == MachO::MH_MAGIC_64 ||
           Header.Magic == MachO::MH_CIGAM_64;
  }

  uint64_t nextAvailableSegmentAddress() const;
};

} // end namespace macho
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_MACHO_MACHOOBJECT_H
