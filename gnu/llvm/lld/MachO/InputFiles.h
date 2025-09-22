//===- InputFiles.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_INPUT_FILES_H
#define LLD_MACHO_INPUT_FILES_H

#include "MachOStructs.h"
#include "Target.h"

#include "lld/Common/DWARF.h"
#include "lld/Common/LLVM.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Threading.h"
#include "llvm/TextAPI/TextAPIReader.h"

#include <vector>

namespace llvm {
namespace lto {
class InputFile;
} // namespace lto
namespace MachO {
class InterfaceFile;
} // namespace MachO
class TarWriter;
} // namespace llvm

namespace lld {
namespace macho {

struct PlatformInfo;
class ConcatInputSection;
class Symbol;
class Defined;
class AliasSymbol;
struct Reloc;
enum class RefState : uint8_t;

// If --reproduce option is given, all input files are written
// to this tar archive.
extern std::unique_ptr<llvm::TarWriter> tar;

// If .subsections_via_symbols is set, each InputSection will be split along
// symbol boundaries. The field offset represents the offset of the subsection
// from the start of the original pre-split InputSection.
struct Subsection {
  uint64_t offset = 0;
  InputSection *isec = nullptr;
};

using Subsections = std::vector<Subsection>;
class InputFile;

class Section {
public:
  InputFile *file;
  StringRef segname;
  StringRef name;
  uint32_t flags;
  uint64_t addr;
  Subsections subsections;

  Section(InputFile *file, StringRef segname, StringRef name, uint32_t flags,
          uint64_t addr)
      : file(file), segname(segname), name(name), flags(flags), addr(addr) {}
  // Ensure pointers to Sections are never invalidated.
  Section(const Section &) = delete;
  Section &operator=(const Section &) = delete;
  Section(Section &&) = delete;
  Section &operator=(Section &&) = delete;

private:
  // Whether we have already split this section into individual subsections.
  // For sections that cannot be split (e.g. literal sections), this is always
  // false.
  bool doneSplitting = false;
  friend class ObjFile;
};

// Represents a call graph profile edge.
struct CallGraphEntry {
  // The index of the caller in the symbol table.
  uint32_t fromIndex;
  // The index of the callee in the symbol table.
  uint32_t toIndex;
  // Number of calls from callee to caller in the profile.
  uint64_t count;

  CallGraphEntry(uint32_t fromIndex, uint32_t toIndex, uint64_t count)
      : fromIndex(fromIndex), toIndex(toIndex), count(count) {}
};

class InputFile {
public:
  enum Kind {
    ObjKind,
    OpaqueKind,
    DylibKind,
    ArchiveKind,
    BitcodeKind,
  };

  virtual ~InputFile() = default;
  Kind kind() const { return fileKind; }
  StringRef getName() const { return name; }
  static void resetIdCount() { idCount = 0; }

  MemoryBufferRef mb;

  std::vector<Symbol *> symbols;
  std::vector<Section *> sections;
  ArrayRef<uint8_t> objCImageInfo;

  // If not empty, this stores the name of the archive containing this file.
  // We use this string for creating error messages.
  std::string archiveName;

  // Provides an easy way to sort InputFiles deterministically.
  const int id;

  // True if this is a lazy ObjFile or BitcodeFile.
  bool lazy = false;

protected:
  InputFile(Kind kind, MemoryBufferRef mb, bool lazy = false)
      : mb(mb), id(idCount++), lazy(lazy), fileKind(kind),
        name(mb.getBufferIdentifier()) {}

  InputFile(Kind, const llvm::MachO::InterfaceFile &);

  // If true, this input's arch is compatible with target.
  bool compatArch = true;

private:
  const Kind fileKind;
  const StringRef name;

  static int idCount;
};

struct FDE {
  uint32_t funcLength;
  Symbol *personality;
  InputSection *lsda;
};

// .o file
class ObjFile final : public InputFile {
public:
  ObjFile(MemoryBufferRef mb, uint32_t modTime, StringRef archiveName,
          bool lazy = false, bool forceHidden = false, bool compatArch = true,
          bool builtFromBitcode = false);
  ArrayRef<llvm::MachO::data_in_code_entry> getDataInCode() const;
  ArrayRef<uint8_t> getOptimizationHints() const;
  template <class LP> void parse();
  template <class LP>
  void parseLinkerOptions(llvm::SmallVectorImpl<StringRef> &LinkerOptions);

  static bool classof(const InputFile *f) { return f->kind() == ObjKind; }

  std::string sourceFile() const;
  // Parses line table information for diagnostics. compileUnit should be used
  // for other purposes.
  lld::DWARFCache *getDwarf();

  llvm::DWARFUnit *compileUnit = nullptr;
  std::unique_ptr<lld::DWARFCache> dwarfCache;
  Section *addrSigSection = nullptr;
  const uint32_t modTime;
  bool forceHidden;
  bool builtFromBitcode;
  std::vector<ConcatInputSection *> debugSections;
  std::vector<CallGraphEntry> callGraph;
  llvm::DenseMap<ConcatInputSection *, FDE> fdes;
  std::vector<AliasSymbol *> aliases;

private:
  llvm::once_flag initDwarf;
  template <class LP> void parseLazy();
  template <class SectionHeader> void parseSections(ArrayRef<SectionHeader>);
  template <class LP>
  void parseSymbols(ArrayRef<typename LP::section> sectionHeaders,
                    ArrayRef<typename LP::nlist> nList, const char *strtab,
                    bool subsectionsViaSymbols);
  template <class NList>
  Symbol *parseNonSectionSymbol(const NList &sym, const char *strtab);
  template <class SectionHeader>
  void parseRelocations(ArrayRef<SectionHeader> sectionHeaders,
                        const SectionHeader &, Section &);
  void parseDebugInfo();
  void splitEhFrames(ArrayRef<uint8_t> dataArr, Section &ehFrameSection);
  void registerCompactUnwind(Section &compactUnwindSection);
  void registerEhFrames(Section &ehFrameSection);
};

// command-line -sectcreate file
class OpaqueFile final : public InputFile {
public:
  OpaqueFile(MemoryBufferRef mb, StringRef segName, StringRef sectName);
  static bool classof(const InputFile *f) { return f->kind() == OpaqueKind; }
};

// .dylib or .tbd file
class DylibFile final : public InputFile {
public:
  // Mach-O dylibs can re-export other dylibs as sub-libraries, meaning that the
  // symbols in those sub-libraries will be available under the umbrella
  // library's namespace. Those sub-libraries can also have their own
  // re-exports. When loading a re-exported dylib, `umbrella` should be set to
  // the root dylib to ensure symbols in the child library are correctly bound
  // to the root. On the other hand, if a dylib is being directly loaded
  // (through an -lfoo flag), then `umbrella` should be a nullptr.
  explicit DylibFile(MemoryBufferRef mb, DylibFile *umbrella,
                     bool isBundleLoader, bool explicitlyLinked);
  explicit DylibFile(const llvm::MachO::InterfaceFile &interface,
                     DylibFile *umbrella, bool isBundleLoader,
                     bool explicitlyLinked);
  explicit DylibFile(DylibFile *umbrella);

  void parseLoadCommands(MemoryBufferRef mb);
  void parseReexports(const llvm::MachO::InterfaceFile &interface);
  bool isReferenced() const { return numReferencedSymbols > 0; }
  bool isExplicitlyLinked() const;
  void setExplicitlyLinked() { explicitlyLinked = true; }

  static bool classof(const InputFile *f) { return f->kind() == DylibKind; }

  StringRef installName;
  DylibFile *exportingFile = nullptr;
  DylibFile *umbrella;
  SmallVector<StringRef, 2> rpaths;
  uint32_t compatibilityVersion = 0;
  uint32_t currentVersion = 0;
  int64_t ordinal = 0; // Ordinal numbering starts from 1, so 0 is a sentinel
  unsigned numReferencedSymbols = 0;
  RefState refState;
  bool reexport = false;
  bool forceNeeded = false;
  bool forceWeakImport = false;
  bool deadStrippable = false;

private:
  bool explicitlyLinked = false; // Access via isExplicitlyLinked().

public:
  // An executable can be used as a bundle loader that will load the output
  // file being linked, and that contains symbols referenced, but not
  // implemented in the bundle. When used like this, it is very similar
  // to a dylib, so we've used the same class to represent it.
  bool isBundleLoader;

  // Synthetic Dylib objects created by $ld$previous symbols in this dylib.
  // Usually empty. These synthetic dylibs won't have synthetic dylibs
  // themselves.
  SmallVector<DylibFile *, 2> extraDylibs;

private:
  DylibFile *getSyntheticDylib(StringRef installName, uint32_t currentVersion,
                               uint32_t compatVersion);

  bool handleLDSymbol(StringRef originalName);
  void handleLDPreviousSymbol(StringRef name, StringRef originalName);
  void handleLDInstallNameSymbol(StringRef name, StringRef originalName);
  void handleLDHideSymbol(StringRef name, StringRef originalName);
  void checkAppExtensionSafety(bool dylibIsAppExtensionSafe) const;
  void parseExportedSymbols(uint32_t offset, uint32_t size);
  void loadReexport(StringRef path, DylibFile *umbrella,
                    const llvm::MachO::InterfaceFile *currentTopLevelTapi);

  llvm::DenseSet<llvm::CachedHashStringRef> hiddenSymbols;
};

// .a file
class ArchiveFile final : public InputFile {
public:
  explicit ArchiveFile(std::unique_ptr<llvm::object::Archive> &&file,
                       bool forceHidden);
  void addLazySymbols();
  void fetch(const llvm::object::Archive::Symbol &);
  // LLD normally doesn't use Error for error-handling, but the underlying
  // Archive library does, so this is the cleanest way to wrap it.
  Error fetch(const llvm::object::Archive::Child &, StringRef reason);
  const llvm::object::Archive &getArchive() const { return *file; };
  static bool classof(const InputFile *f) { return f->kind() == ArchiveKind; }

private:
  std::unique_ptr<llvm::object::Archive> file;
  // Keep track of children fetched from the archive by tracking
  // which address offsets have been fetched already.
  llvm::DenseSet<uint64_t> seen;
  // Load all symbols with hidden visibility (-load_hidden).
  bool forceHidden;
};

class BitcodeFile final : public InputFile {
public:
  explicit BitcodeFile(MemoryBufferRef mb, StringRef archiveName,
                       uint64_t offsetInArchive, bool lazy = false,
                       bool forceHidden = false, bool compatArch = true);
  static bool classof(const InputFile *f) { return f->kind() == BitcodeKind; }
  void parse();

  std::unique_ptr<llvm::lto::InputFile> obj;
  bool forceHidden;

private:
  void parseLazy();
};

extern llvm::SetVector<InputFile *> inputFiles;
extern llvm::DenseMap<llvm::CachedHashStringRef, MemoryBufferRef> cachedReads;
extern llvm::SmallVector<StringRef> unprocessedLCLinkerOptions;

std::optional<MemoryBufferRef> readFile(StringRef path);

void extract(InputFile &file, StringRef reason);

namespace detail {

template <class CommandType, class... Types>
std::vector<const CommandType *>
findCommands(const void *anyHdr, size_t maxCommands, Types... types) {
  std::vector<const CommandType *> cmds;
  std::initializer_list<uint32_t> typesList{types...};
  const auto *hdr = reinterpret_cast<const llvm::MachO::mach_header *>(anyHdr);
  const uint8_t *p =
      reinterpret_cast<const uint8_t *>(hdr) + target->headerSize;
  for (uint32_t i = 0, n = hdr->ncmds; i < n; ++i) {
    auto *cmd = reinterpret_cast<const CommandType *>(p);
    if (llvm::is_contained(typesList, cmd->cmd)) {
      cmds.push_back(cmd);
      if (cmds.size() == maxCommands)
        return cmds;
    }
    p += cmd->cmdsize;
  }
  return cmds;
}

} // namespace detail

// anyHdr should be a pointer to either mach_header or mach_header_64
template <class CommandType = llvm::MachO::load_command, class... Types>
const CommandType *findCommand(const void *anyHdr, Types... types) {
  std::vector<const CommandType *> cmds =
      detail::findCommands<CommandType>(anyHdr, 1, types...);
  return cmds.size() ? cmds[0] : nullptr;
}

template <class CommandType = llvm::MachO::load_command, class... Types>
std::vector<const CommandType *> findCommands(const void *anyHdr,
                                              Types... types) {
  return detail::findCommands<CommandType>(anyHdr, 0, types...);
}

std::string replaceThinLTOSuffix(StringRef path);
} // namespace macho

std::string toString(const macho::InputFile *file);
std::string toString(const macho::Section &);
} // namespace lld

#endif
