//===- InputFiles.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_INPUT_FILES_H
#define LLD_ELF_INPUT_FILES_H

#include "Config.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/LLVM.h"
#include "lld/Common/Reproduce.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/Threading.h"

namespace llvm {
struct DILineInfo;
class TarWriter;
namespace lto {
class InputFile;
}
} // namespace llvm

namespace lld {
class DWARFCache;

// Returns "<internal>", "foo.a(bar.o)" or "baz.o".
std::string toString(const elf::InputFile *f);

void parseGNUWarning(StringRef name, ArrayRef<char> data, size_t size);

namespace elf {

class InputSection;
class Symbol;

// If --reproduce is specified, all input files are written to this tar archive.
extern std::unique_ptr<llvm::TarWriter> tar;

// Opens a given file.
std::optional<MemoryBufferRef> readFile(StringRef path);

// Add symbols in File to the symbol table.
void parseFile(InputFile *file);
void parseFiles(const std::vector<InputFile *> &files,
                InputFile *armCmseImpLib);

// The root class of input files.
class InputFile {
protected:
  std::unique_ptr<Symbol *[]> symbols;
  uint32_t numSymbols = 0;
  SmallVector<InputSectionBase *, 0> sections;

public:
  enum Kind : uint8_t {
    ObjKind,
    SharedKind,
    BitcodeKind,
    BinaryKind,
    InternalKind,
  };

  InputFile(Kind k, MemoryBufferRef m);
  Kind kind() const { return fileKind; }

  bool isElf() const {
    Kind k = kind();
    return k == ObjKind || k == SharedKind;
  }
  bool isInternal() const { return kind() == InternalKind; }

  StringRef getName() const { return mb.getBufferIdentifier(); }
  MemoryBufferRef mb;

  // Returns sections. It is a runtime error to call this function
  // on files that don't have the notion of sections.
  ArrayRef<InputSectionBase *> getSections() const {
    assert(fileKind == ObjKind || fileKind == BinaryKind);
    return sections;
  }
  void cacheDecodedCrel(size_t i, InputSectionBase *s) { sections[i] = s; }

  // Returns object file symbols. It is a runtime error to call this
  // function on files of other types.
  ArrayRef<Symbol *> getSymbols() const {
    assert(fileKind == BinaryKind || fileKind == ObjKind ||
           fileKind == BitcodeKind);
    return {symbols.get(), numSymbols};
  }

  MutableArrayRef<Symbol *> getMutableSymbols() {
    assert(fileKind == BinaryKind || fileKind == ObjKind ||
           fileKind == BitcodeKind);
    return {symbols.get(), numSymbols};
  }

  Symbol &getSymbol(uint32_t symbolIndex) const {
    assert(fileKind == ObjKind);
    if (symbolIndex >= numSymbols)
      fatal(toString(this) + ": invalid symbol index");
    return *this->symbols[symbolIndex];
  }

  template <typename RelT> Symbol &getRelocTargetSym(const RelT &rel) const {
    uint32_t symIndex = rel.getSymbol(config->isMips64EL);
    return getSymbol(symIndex);
  }

  // Get filename to use for linker script processing.
  StringRef getNameForScript() const;

  // Check if a non-common symbol should be extracted to override a common
  // definition.
  bool shouldExtractForCommon(StringRef name) const;

  // .got2 in the current file. This is used by PPC32 -fPIC/-fPIE to compute
  // offsets in PLT call stubs.
  InputSection *ppc32Got2 = nullptr;

  // Index of MIPS GOT built for this file.
  uint32_t mipsGotIndex = -1;

  // groupId is used for --warn-backrefs which is an optional error
  // checking feature. All files within the same --{start,end}-group or
  // --{start,end}-lib get the same group ID. Otherwise, each file gets a new
  // group ID. For more info, see checkDependency() in SymbolTable.cpp.
  uint32_t groupId;
  static bool isInGroup;
  static uint32_t nextGroupId;

  // If this is an architecture-specific file, the following members
  // have ELF type (i.e. ELF{32,64}{LE,BE}) and target machine type.
  uint16_t emachine = llvm::ELF::EM_NONE;
  const Kind fileKind;
  ELFKind ekind = ELFNoneKind;
  uint8_t osabi = 0;
  uint8_t abiVersion = 0;

  // True if this is a relocatable object file/bitcode file in an ar archive
  // or between --start-lib and --end-lib.
  bool lazy = false;

  // True if this is an argument for --just-symbols. Usually false.
  bool justSymbols = false;

  std::string getSrcMsg(const Symbol &sym, const InputSectionBase &sec,
                        uint64_t offset);

  // On PPC64 we need to keep track of which files contain small code model
  // relocations that access the .toc section. To minimize the chance of a
  // relocation overflow, files that do contain said relocations should have
  // their .toc sections sorted closer to the .got section than files that do
  // not contain any small code model relocations. Thats because the toc-pointer
  // is defined to point at .got + 0x8000 and the instructions used with small
  // code model relocations support immediates in the range [-0x8000, 0x7FFC],
  // making the addressable range relative to the toc pointer
  // [.got, .got + 0xFFFC].
  bool ppc64SmallCodeModelTocRelocs = false;

  // True if the file has TLSGD/TLSLD GOT relocations without R_PPC64_TLSGD or
  // R_PPC64_TLSLD. Disable TLS relaxation to avoid bad code generation.
  bool ppc64DisableTLSRelax = false;

public:
  // If not empty, this stores the name of the archive containing this file.
  // We use this string for creating error messages.
  SmallString<0> archiveName;
  // Cache for toString(). Only toString() should use this member.
  mutable SmallString<0> toStringCache;

private:
  // Cache for getNameForScript().
  mutable SmallString<0> nameForScriptCache;
};

class ELFFileBase : public InputFile {
public:
  ELFFileBase(Kind k, ELFKind ekind, MemoryBufferRef m);
  static bool classof(const InputFile *f) { return f->isElf(); }

  void init();
  template <typename ELFT> llvm::object::ELFFile<ELFT> getObj() const {
    return check(llvm::object::ELFFile<ELFT>::create(mb.getBuffer()));
  }

  StringRef getStringTable() const { return stringTable; }

  ArrayRef<Symbol *> getLocalSymbols() {
    if (numSymbols == 0)
      return {};
    return llvm::ArrayRef(symbols.get() + 1, firstGlobal - 1);
  }
  ArrayRef<Symbol *> getGlobalSymbols() {
    return llvm::ArrayRef(symbols.get() + firstGlobal,
                          numSymbols - firstGlobal);
  }
  MutableArrayRef<Symbol *> getMutableGlobalSymbols() {
    return llvm::MutableArrayRef(symbols.get() + firstGlobal,
                                     numSymbols - firstGlobal);
  }

  template <typename ELFT> typename ELFT::ShdrRange getELFShdrs() const {
    return typename ELFT::ShdrRange(
        reinterpret_cast<const typename ELFT::Shdr *>(elfShdrs), numELFShdrs);
  }
  template <typename ELFT> typename ELFT::SymRange getELFSyms() const {
    return typename ELFT::SymRange(
        reinterpret_cast<const typename ELFT::Sym *>(elfSyms), numELFSyms);
  }
  template <typename ELFT> typename ELFT::SymRange getGlobalELFSyms() const {
    return getELFSyms<ELFT>().slice(firstGlobal);
  }

protected:
  // Initializes this class's member variables.
  template <typename ELFT> void init(InputFile::Kind k);

  StringRef stringTable;
  const void *elfShdrs = nullptr;
  const void *elfSyms = nullptr;
  uint32_t numELFShdrs = 0;
  uint32_t numELFSyms = 0;
  uint32_t firstGlobal = 0;

public:
  uint32_t andFeatures = 0;
  bool hasCommonSyms = false;
  ArrayRef<uint8_t> aarch64PauthAbiCoreInfo;
};

// .o file.
template <class ELFT> class ObjFile : public ELFFileBase {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

public:
  static bool classof(const InputFile *f) { return f->kind() == ObjKind; }

  llvm::object::ELFFile<ELFT> getObj() const {
    return this->ELFFileBase::getObj<ELFT>();
  }

  ObjFile(ELFKind ekind, MemoryBufferRef m, StringRef archiveName)
      : ELFFileBase(ObjKind, ekind, m) {
    this->archiveName = archiveName;
  }

  void parse(bool ignoreComdats = false);
  void parseLazy();

  StringRef getShtGroupSignature(ArrayRef<Elf_Shdr> sections,
                                 const Elf_Shdr &sec);

  uint32_t getSectionIndex(const Elf_Sym &sym) const;

  std::optional<llvm::DILineInfo> getDILineInfo(const InputSectionBase *,
                                                uint64_t);
  std::optional<std::pair<std::string, unsigned>>
  getVariableLoc(StringRef name);

  // Name of source file obtained from STT_FILE symbol value,
  // or empty string if there is no such symbol in object file
  // symbol table.
  StringRef sourceFile;

  // Pointer to this input file's .llvm_addrsig section, if it has one.
  const Elf_Shdr *addrsigSec = nullptr;

  // SHT_LLVM_CALL_GRAPH_PROFILE section index.
  uint32_t cgProfileSectionIndex = 0;

  // MIPS GP0 value defined by this file. This value represents the gp value
  // used to create the relocatable object and required to support
  // R_MIPS_GPREL16 / R_MIPS_GPREL32 relocations.
  uint32_t mipsGp0 = 0;

  // True if the file defines functions compiled with
  // -fsplit-stack. Usually false.
  bool splitStack = false;

  // True if the file defines functions compiled with -fsplit-stack,
  // but had one or more functions with the no_split_stack attribute.
  bool someNoSplitStack = false;

  // Get cached DWARF information.
  DWARFCache *getDwarf();

  void initSectionsAndLocalSyms(bool ignoreComdats);
  void postParse();
  void importCmseSymbols();

private:
  void initializeSections(bool ignoreComdats,
                          const llvm::object::ELFFile<ELFT> &obj);
  void initializeSymbols(const llvm::object::ELFFile<ELFT> &obj);
  void initializeJustSymbols();

  InputSectionBase *getRelocTarget(uint32_t idx, uint32_t info);
  InputSectionBase *createInputSection(uint32_t idx, const Elf_Shdr &sec,
                                       StringRef name);

  bool shouldMerge(const Elf_Shdr &sec, StringRef name);

  // Each ELF symbol contains a section index which the symbol belongs to.
  // However, because the number of bits dedicated for that is limited, a
  // symbol can directly point to a section only when the section index is
  // equal to or smaller than 65280.
  //
  // If an object file contains more than 65280 sections, the file must
  // contain .symtab_shndx section. The section contains an array of
  // 32-bit integers whose size is the same as the number of symbols.
  // Nth symbol's section index is in the Nth entry of .symtab_shndx.
  //
  // The following variable contains the contents of .symtab_shndx.
  // If the section does not exist (which is common), the array is empty.
  ArrayRef<Elf_Word> shndxTable;

  // Debugging information to retrieve source file and line for error
  // reporting. Linker may find reasonable number of errors in a
  // single object file, so we cache debugging information in order to
  // parse it only once for each object file we link.
  std::unique_ptr<DWARFCache> dwarf;
  llvm::once_flag initDwarf;
};

class BitcodeFile : public InputFile {
public:
  BitcodeFile(MemoryBufferRef m, StringRef archiveName,
              uint64_t offsetInArchive, bool lazy);
  static bool classof(const InputFile *f) { return f->kind() == BitcodeKind; }
  void parse();
  void parseLazy();
  void postParse();
  std::unique_ptr<llvm::lto::InputFile> obj;
  std::vector<bool> keptComdats;
};

// .so file.
class SharedFile : public ELFFileBase {
public:
  SharedFile(MemoryBufferRef m, StringRef defaultSoName);

  // This is actually a vector of Elf_Verdef pointers.
  SmallVector<const void *, 0> verdefs;

  // If the output file needs Elf_Verneed data structures for this file, this is
  // a vector of Elf_Vernaux version identifiers that map onto the entries in
  // Verdefs, otherwise it is empty.
  SmallVector<uint32_t, 0> vernauxs;

  static unsigned vernauxNum;

  SmallVector<StringRef, 0> dtNeeded;
  StringRef soName;

  static bool classof(const InputFile *f) { return f->kind() == SharedKind; }

  template <typename ELFT> void parse();

  // Used for --as-needed
  bool isNeeded;

  // Non-weak undefined symbols which are not yet resolved when the SO is
  // parsed. Only filled for `--no-allow-shlib-undefined`.
  SmallVector<Symbol *, 0> requiredSymbols;

private:
  template <typename ELFT>
  std::vector<uint32_t> parseVerneed(const llvm::object::ELFFile<ELFT> &obj,
                                     const typename ELFT::Shdr *sec);
};

class BinaryFile : public InputFile {
public:
  explicit BinaryFile(MemoryBufferRef m) : InputFile(BinaryKind, m) {}
  static bool classof(const InputFile *f) { return f->kind() == BinaryKind; }
  void parse();
};

InputFile *createInternalFile(StringRef name);
ELFFileBase *createObjFile(MemoryBufferRef mb, StringRef archiveName = "",
                           bool lazy = false);

std::string replaceThinLTOSuffix(StringRef path);

} // namespace elf
} // namespace lld

#endif
