//===- Symbols.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Driver.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "OutputSections.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "Writer.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/Compiler.h"
#include <cstring>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

static_assert(sizeof(SymbolUnion) <= 72, "SymbolUnion too large");

template <typename T> struct AssertSymbol {
  static_assert(std::is_trivially_destructible<T>(),
                "Symbol types must be trivially destructible");
  static_assert(sizeof(T) <= sizeof(SymbolUnion), "SymbolUnion too small");
  static_assert(alignof(T) <= alignof(SymbolUnion),
                "SymbolUnion not aligned enough");
};

LLVM_ATTRIBUTE_UNUSED static inline void assertSymbols() {
  AssertSymbol<Defined>();
  AssertSymbol<CommonSymbol>();
  AssertSymbol<Undefined>();
  AssertSymbol<SharedSymbol>();
  AssertSymbol<LazySymbol>();
}

// Returns a symbol for an error message.
static std::string maybeDemangleSymbol(StringRef symName) {
  return elf::config->demangle ? demangle(symName.str()) : symName.str();
}

std::string lld::toString(const elf::Symbol &sym) {
  StringRef name = sym.getName();
  std::string ret = maybeDemangleSymbol(name);

  const char *suffix = sym.getVersionSuffix();
  if (*suffix == '@')
    ret += suffix;
  return ret;
}

Defined *ElfSym::bss;
Defined *ElfSym::data;
Defined *ElfSym::etext1;
Defined *ElfSym::etext2;
Defined *ElfSym::edata1;
Defined *ElfSym::edata2;
Defined *ElfSym::end1;
Defined *ElfSym::end2;
Defined *ElfSym::globalOffsetTable;
Defined *ElfSym::mipsGp;
Defined *ElfSym::mipsGpDisp;
Defined *ElfSym::mipsLocalGp;
Defined *ElfSym::riscvGlobalPointer;
Defined *ElfSym::relaIpltStart;
Defined *ElfSym::relaIpltEnd;
Defined *ElfSym::tlsModuleBase;
SmallVector<SymbolAux, 0> elf::symAux;

static uint64_t getSymVA(const Symbol &sym, int64_t addend) {
  switch (sym.kind()) {
  case Symbol::DefinedKind: {
    auto &d = cast<Defined>(sym);
    SectionBase *isec = d.section;

    // This is an absolute symbol.
    if (!isec)
      return d.value;

    assert(isec != &InputSection::discarded);

    uint64_t offset = d.value;

    // An object in an SHF_MERGE section might be referenced via a
    // section symbol (as a hack for reducing the number of local
    // symbols).
    // Depending on the addend, the reference via a section symbol
    // refers to a different object in the merge section.
    // Since the objects in the merge section are not necessarily
    // contiguous in the output, the addend can thus affect the final
    // VA in a non-linear way.
    // To make this work, we incorporate the addend into the section
    // offset (and zero out the addend for later processing) so that
    // we find the right object in the section.
    if (d.isSection())
      offset += addend;

    // In the typical case, this is actually very simple and boils
    // down to adding together 3 numbers:
    // 1. The address of the output section.
    // 2. The offset of the input section within the output section.
    // 3. The offset within the input section (this addition happens
    //    inside InputSection::getOffset).
    //
    // If you understand the data structures involved with this next
    // line (and how they get built), then you have a pretty good
    // understanding of the linker.
    uint64_t va = isec->getVA(offset);
    if (d.isSection())
      va -= addend;

    // MIPS relocatable files can mix regular and microMIPS code.
    // Linker needs to distinguish such code. To do so microMIPS
    // symbols has the `STO_MIPS_MICROMIPS` flag in the `st_other`
    // field. Unfortunately, the `MIPS::relocate()` method has
    // a symbol value only. To pass type of the symbol (regular/microMIPS)
    // to that routine as well as other places where we write
    // a symbol value as-is (.dynamic section, `Elf_Ehdr::e_entry`
    // field etc) do the same trick as compiler uses to mark microMIPS
    // for CPU - set the less-significant bit.
    if (config->emachine == EM_MIPS && isMicroMips() &&
        ((sym.stOther & STO_MIPS_MICROMIPS) || sym.hasFlag(NEEDS_COPY)))
      va |= 1;

    if (d.isTls() && !config->relocatable) {
      // Use the address of the TLS segment's first section rather than the
      // segment's address, because segment addresses aren't initialized until
      // after sections are finalized. (e.g. Measuring the size of .rela.dyn
      // for Android relocation packing requires knowing TLS symbol addresses
      // during section finalization.)
      if (!Out::tlsPhdr || !Out::tlsPhdr->firstSec)
        fatal(toString(d.file) +
              " has an STT_TLS symbol but doesn't have an SHF_TLS section");
      return va - Out::tlsPhdr->firstSec->addr;
    }
    return va;
  }
  case Symbol::SharedKind:
  case Symbol::UndefinedKind:
    return 0;
  case Symbol::LazyKind:
    llvm_unreachable("lazy symbol reached writer");
  case Symbol::CommonKind:
    llvm_unreachable("common symbol reached writer");
  case Symbol::PlaceholderKind:
    llvm_unreachable("placeholder symbol reached writer");
  }
  llvm_unreachable("invalid symbol kind");
}

uint64_t Symbol::getVA(int64_t addend) const {
  return getSymVA(*this, addend) + addend;
}

uint64_t Symbol::getGotVA() const {
  if (gotInIgot)
    return in.igotPlt->getVA() + getGotPltOffset();
  return in.got->getVA() + getGotOffset();
}

uint64_t Symbol::getGotOffset() const {
  return getGotIdx() * target->gotEntrySize;
}

uint64_t Symbol::getGotPltVA() const {
  if (isInIplt)
    return in.igotPlt->getVA() + getGotPltOffset();
  return in.gotPlt->getVA() + getGotPltOffset();
}

uint64_t Symbol::getGotPltOffset() const {
  if (isInIplt)
    return getPltIdx() * target->gotEntrySize;
  return (getPltIdx() + target->gotPltHeaderEntriesNum) * target->gotEntrySize;
}

uint64_t Symbol::getPltVA() const {
  uint64_t outVA = isInIplt
                       ? in.iplt->getVA() + getPltIdx() * target->ipltEntrySize
                       : in.plt->getVA() + in.plt->headerSize +
                             getPltIdx() * target->pltEntrySize;

  // While linking microMIPS code PLT code are always microMIPS
  // code. Set the less-significant bit to track that fact.
  // See detailed comment in the `getSymVA` function.
  if (config->emachine == EM_MIPS && isMicroMips())
    outVA |= 1;
  return outVA;
}

uint64_t Symbol::getSize() const {
  if (const auto *dr = dyn_cast<Defined>(this))
    return dr->size;
  return cast<SharedSymbol>(this)->size;
}

OutputSection *Symbol::getOutputSection() const {
  if (auto *s = dyn_cast<Defined>(this)) {
    if (auto *sec = s->section)
      return sec->getOutputSection();
    return nullptr;
  }
  return nullptr;
}

// If a symbol name contains '@', the characters after that is
// a symbol version name. This function parses that.
void Symbol::parseSymbolVersion() {
  // Return if localized by a local: pattern in a version script.
  if (versionId == VER_NDX_LOCAL)
    return;
  StringRef s = getName();
  size_t pos = s.find('@');
  if (pos == StringRef::npos)
    return;
  StringRef verstr = s.substr(pos + 1);

  // Truncate the symbol name so that it doesn't include the version string.
  nameSize = pos;

  if (verstr.empty())
    return;

  // If this is not in this DSO, it is not a definition.
  if (!isDefined())
    return;

  // '@@' in a symbol name means the default version.
  // It is usually the most recent one.
  bool isDefault = (verstr[0] == '@');
  if (isDefault)
    verstr = verstr.substr(1);

  for (const VersionDefinition &ver : namedVersionDefs()) {
    if (ver.name != verstr)
      continue;

    if (isDefault)
      versionId = ver.id;
    else
      versionId = ver.id | VERSYM_HIDDEN;
    return;
  }

  // It is an error if the specified version is not defined.
  // Usually version script is not provided when linking executable,
  // but we may still want to override a versioned symbol from DSO,
  // so we do not report error in this case. We also do not error
  // if the symbol has a local version as it won't be in the dynamic
  // symbol table.
  if (config->shared && versionId != VER_NDX_LOCAL)
    error(toString(file) + ": symbol " + s + " has undefined version " +
          verstr);
}

void Symbol::extract() const {
  if (file->lazy) {
    file->lazy = false;
    parseFile(file);
  }
}

uint8_t Symbol::computeBinding() const {
  auto v = visibility();
  if ((v != STV_DEFAULT && v != STV_PROTECTED) || versionId == VER_NDX_LOCAL)
    return STB_LOCAL;
  if (binding == STB_GNU_UNIQUE && !config->gnuUnique)
    return STB_GLOBAL;
  return binding;
}

bool Symbol::includeInDynsym() const {
  if (computeBinding() == STB_LOCAL)
    return false;
  if (!isDefined() && !isCommon())
    // This should unconditionally return true, unfortunately glibc -static-pie
    // expects undefined weak symbols not to exist in .dynsym, e.g.
    // __pthread_mutex_lock reference in _dl_add_to_namespace_list,
    // __pthread_initialize_minimal reference in csu/libc-start.c.
    return !(isUndefWeak() && config->noDynamicLinker);

  return exportDynamic || inDynamicList;
}

// Print out a log message for --trace-symbol.
void elf::printTraceSymbol(const Symbol &sym, StringRef name) {
  std::string s;
  if (sym.isUndefined())
    s = ": reference to ";
  else if (sym.isLazy())
    s = ": lazy definition of ";
  else if (sym.isShared())
    s = ": shared definition of ";
  else if (sym.isCommon())
    s = ": common definition of ";
  else
    s = ": definition of ";

  message(toString(sym.file) + s + name);
}

static void recordWhyExtract(const InputFile *reference,
                             const InputFile &extracted, const Symbol &sym) {
  ctx.whyExtractRecords.emplace_back(toString(reference), &extracted, sym);
}

void elf::maybeWarnUnorderableSymbol(const Symbol *sym) {
  if (!config->warnSymbolOrdering)
    return;

  // If UnresolvedPolicy::Ignore is used, no "undefined symbol" error/warning is
  // emitted. It makes sense to not warn on undefined symbols (excluding those
  // demoted by demoteSymbols).
  //
  // Note, ld.bfd --symbol-ordering-file= does not warn on undefined symbols,
  // but we don't have to be compatible here.
  if (sym->isUndefined() && !cast<Undefined>(sym)->discardedSecIdx &&
      config->unresolvedSymbols == UnresolvedPolicy::Ignore)
    return;

  const InputFile *file = sym->file;
  auto *d = dyn_cast<Defined>(sym);

  auto report = [&](StringRef s) { warn(toString(file) + s + sym->getName()); };

  if (sym->isUndefined()) {
    if (cast<Undefined>(sym)->discardedSecIdx)
      report(": unable to order discarded symbol: ");
    else
      report(": unable to order undefined symbol: ");
  } else if (sym->isShared())
    report(": unable to order shared symbol: ");
  else if (d && !d->section)
    report(": unable to order absolute symbol: ");
  else if (d && isa<OutputSection>(d->section))
    report(": unable to order synthetic symbol: ");
  else if (d && !d->section->isLive())
    report(": unable to order discarded symbol: ");
}

// Returns true if a symbol can be replaced at load-time by a symbol
// with the same name defined in other ELF executable or DSO.
bool elf::computeIsPreemptible(const Symbol &sym) {
  assert(!sym.isLocal() || sym.isPlaceholder());

  // Only symbols with default visibility that appear in dynsym can be
  // preempted. Symbols with protected visibility cannot be preempted.
  if (!sym.includeInDynsym() || sym.visibility() != STV_DEFAULT)
    return false;

  // At this point copy relocations have not been created yet, so any
  // symbol that is not defined locally is preemptible.
  if (!sym.isDefined())
    return true;

  if (!config->shared)
    return false;

  // If -Bsymbolic or --dynamic-list is specified, or -Bsymbolic-functions is
  // specified and the symbol is STT_FUNC, the symbol is preemptible iff it is
  // in the dynamic list. -Bsymbolic-non-weak-functions is a non-weak subset of
  // -Bsymbolic-functions.
  if (config->symbolic ||
      (config->bsymbolic == BsymbolicKind::NonWeak &&
       sym.binding != STB_WEAK) ||
      (config->bsymbolic == BsymbolicKind::Functions && sym.isFunc()) ||
      (config->bsymbolic == BsymbolicKind::NonWeakFunctions && sym.isFunc() &&
       sym.binding != STB_WEAK))
    return sym.inDynamicList;
  return true;
}

// Merge symbol properties.
//
// When we have many symbols of the same name, we choose one of them,
// and that's the result of symbol resolution. However, symbols that
// were not chosen still affect some symbol properties.
void Symbol::mergeProperties(const Symbol &other) {
  if (other.exportDynamic)
    exportDynamic = true;

  // DSO symbols do not affect visibility in the output.
  if (!other.isShared() && other.visibility() != STV_DEFAULT) {
    uint8_t v = visibility(), ov = other.visibility();
    setVisibility(v == STV_DEFAULT ? ov : std::min(v, ov));
  }
}

void Symbol::resolve(const Undefined &other) {
  if (other.visibility() != STV_DEFAULT) {
    uint8_t v = visibility(), ov = other.visibility();
    setVisibility(v == STV_DEFAULT ? ov : std::min(v, ov));
  }
  // An undefined symbol with non default visibility must be satisfied
  // in the same DSO.
  //
  // If this is a non-weak defined symbol in a discarded section, override the
  // existing undefined symbol for better error message later.
  if (isPlaceholder() || (isShared() && other.visibility() != STV_DEFAULT) ||
      (isUndefined() && other.binding != STB_WEAK && other.discardedSecIdx)) {
    other.overwrite(*this);
    return;
  }

  if (traced)
    printTraceSymbol(other, getName());

  if (isLazy()) {
    // An undefined weak will not extract archive members. See comment on Lazy
    // in Symbols.h for the details.
    if (other.binding == STB_WEAK) {
      binding = STB_WEAK;
      type = other.type;
      return;
    }

    // Do extra check for --warn-backrefs.
    //
    // --warn-backrefs is an option to prevent an undefined reference from
    // extracting an archive member written earlier in the command line. It can
    // be used to keep compatibility with GNU linkers to some degree. I'll
    // explain the feature and why you may find it useful in this comment.
    //
    // lld's symbol resolution semantics is more relaxed than traditional Unix
    // linkers. For example,
    //
    //   ld.lld foo.a bar.o
    //
    // succeeds even if bar.o contains an undefined symbol that has to be
    // resolved by some object file in foo.a. Traditional Unix linkers don't
    // allow this kind of backward reference, as they visit each file only once
    // from left to right in the command line while resolving all undefined
    // symbols at the moment of visiting.
    //
    // In the above case, since there's no undefined symbol when a linker visits
    // foo.a, no files are pulled out from foo.a, and because the linker forgets
    // about foo.a after visiting, it can't resolve undefined symbols in bar.o
    // that could have been resolved otherwise.
    //
    // That lld accepts more relaxed form means that (besides it'd make more
    // sense) you can accidentally write a command line or a build file that
    // works only with lld, even if you have a plan to distribute it to wider
    // users who may be using GNU linkers. With --warn-backrefs, you can detect
    // a library order that doesn't work with other Unix linkers.
    //
    // The option is also useful to detect cyclic dependencies between static
    // archives. Again, lld accepts
    //
    //   ld.lld foo.a bar.a
    //
    // even if foo.a and bar.a depend on each other. With --warn-backrefs, it is
    // handled as an error.
    //
    // Here is how the option works. We assign a group ID to each file. A file
    // with a smaller group ID can pull out object files from an archive file
    // with an equal or greater group ID. Otherwise, it is a reverse dependency
    // and an error.
    //
    // A file outside --{start,end}-group gets a fresh ID when instantiated. All
    // files within the same --{start,end}-group get the same group ID. E.g.
    //
    //   ld.lld A B --start-group C D --end-group E
    //
    // A forms group 0. B form group 1. C and D (including their member object
    // files) form group 2. E forms group 3. I think that you can see how this
    // group assignment rule simulates the traditional linker's semantics.
    bool backref = config->warnBackrefs && other.file &&
                   file->groupId < other.file->groupId;
    extract();

    if (!config->whyExtract.empty())
      recordWhyExtract(other.file, *file, *this);

    // We don't report backward references to weak symbols as they can be
    // overridden later.
    //
    // A traditional linker does not error for -ldef1 -lref -ldef2 (linking
    // sandwich), where def2 may or may not be the same as def1. We don't want
    // to warn for this case, so dismiss the warning if we see a subsequent lazy
    // definition. this->file needs to be saved because in the case of LTO it
    // may be reset to nullptr or be replaced with a file named lto.tmp.
    if (backref && !isWeak())
      ctx.backwardReferences.try_emplace(this,
                                         std::make_pair(other.file, file));
    return;
  }

  // Undefined symbols in a SharedFile do not change the binding.
  if (isa_and_nonnull<SharedFile>(other.file))
    return;

  if (isUndefined() || isShared()) {
    // The binding will be weak if there is at least one reference and all are
    // weak. The binding has one opportunity to change to weak: if the first
    // reference is weak.
    if (other.binding != STB_WEAK || !referenced)
      binding = other.binding;
  }
}

// Compare two symbols. Return true if the new symbol should win.
bool Symbol::shouldReplace(const Defined &other) const {
  if (LLVM_UNLIKELY(isCommon())) {
    if (config->warnCommon)
      warn("common " + getName() + " is overridden");
    return !other.isWeak();
  }
  if (!isDefined())
    return true;

  // Incoming STB_GLOBAL overrides STB_WEAK/STB_GNU_UNIQUE. -fgnu-unique changes
  // some vague linkage data in COMDAT from STB_WEAK to STB_GNU_UNIQUE. Treat
  // STB_GNU_UNIQUE like STB_WEAK so that we prefer the first among all
  // STB_WEAK/STB_GNU_UNIQUE copies. If we prefer an incoming STB_GNU_UNIQUE to
  // an existing STB_WEAK, there may be discarded section errors because the
  // selected copy may be in a non-prevailing COMDAT.
  return !isGlobal() && other.isGlobal();
}

void elf::reportDuplicate(const Symbol &sym, const InputFile *newFile,
                          InputSectionBase *errSec, uint64_t errOffset) {
  if (config->allowMultipleDefinition)
    return;
  // In glibc<2.32, crti.o has .gnu.linkonce.t.__x86.get_pc_thunk.bx, which
  // is sort of proto-comdat. There is actually no duplicate if we have
  // full support for .gnu.linkonce.
  const Defined *d = dyn_cast<Defined>(&sym);
  if (!d || d->getName() == "__x86.get_pc_thunk.bx")
    return;
  // Allow absolute symbols with the same value for GNU ld compatibility.
  if (!d->section && !errSec && errOffset && d->value == errOffset)
    return;
  if (!d->section || !errSec) {
    errorOrWarn("duplicate symbol: " + toString(sym) + "\n>>> defined in " +
                toString(sym.file) + "\n>>> defined in " + toString(newFile));
    return;
  }

  // Construct and print an error message in the form of:
  //
  //   ld.lld: error: duplicate symbol: foo
  //   >>> defined at bar.c:30
  //   >>>            bar.o (/home/alice/src/bar.o)
  //   >>> defined at baz.c:563
  //   >>>            baz.o in archive libbaz.a
  auto *sec1 = cast<InputSectionBase>(d->section);
  std::string src1 = sec1->getSrcMsg(sym, d->value);
  std::string obj1 = sec1->getObjMsg(d->value);
  std::string src2 = errSec->getSrcMsg(sym, errOffset);
  std::string obj2 = errSec->getObjMsg(errOffset);

  std::string msg = "duplicate symbol: " + toString(sym) + "\n>>> defined at ";
  if (!src1.empty())
    msg += src1 + "\n>>>            ";
  msg += obj1 + "\n>>> defined at ";
  if (!src2.empty())
    msg += src2 + "\n>>>            ";
  msg += obj2;
  errorOrWarn(msg);
}

void Symbol::checkDuplicate(const Defined &other) const {
  if (isDefined() && !isWeak() && !other.isWeak())
    reportDuplicate(*this, other.file,
                    dyn_cast_or_null<InputSectionBase>(other.section),
                    other.value);
}

void Symbol::resolve(const CommonSymbol &other) {
  if (other.exportDynamic)
    exportDynamic = true;
  if (other.visibility() != STV_DEFAULT) {
    uint8_t v = visibility(), ov = other.visibility();
    setVisibility(v == STV_DEFAULT ? ov : std::min(v, ov));
  }
  if (isDefined() && !isWeak()) {
    if (config->warnCommon)
      warn("common " + getName() + " is overridden");
    return;
  }

  if (CommonSymbol *oldSym = dyn_cast<CommonSymbol>(this)) {
    if (config->warnCommon)
      warn("multiple common of " + getName());
    oldSym->alignment = std::max(oldSym->alignment, other.alignment);
    if (oldSym->size < other.size) {
      oldSym->file = other.file;
      oldSym->size = other.size;
    }
    return;
  }

  if (auto *s = dyn_cast<SharedSymbol>(this)) {
    // Increase st_size if the shared symbol has a larger st_size. The shared
    // symbol may be created from common symbols. The fact that some object
    // files were linked into a shared object first should not change the
    // regular rule that picks the largest st_size.
    uint64_t size = s->size;
    other.overwrite(*this);
    if (size > cast<CommonSymbol>(this)->size)
      cast<CommonSymbol>(this)->size = size;
  } else {
    other.overwrite(*this);
  }
}

void Symbol::resolve(const Defined &other) {
  if (other.exportDynamic)
    exportDynamic = true;
  if (other.visibility() != STV_DEFAULT) {
    uint8_t v = visibility(), ov = other.visibility();
    setVisibility(v == STV_DEFAULT ? ov : std::min(v, ov));
  }
  if (shouldReplace(other))
    other.overwrite(*this);
}

void Symbol::resolve(const LazySymbol &other) {
  if (isPlaceholder()) {
    other.overwrite(*this);
    return;
  }

  // For common objects, we want to look for global or weak definitions that
  // should be extracted as the canonical definition instead.
  if (LLVM_UNLIKELY(isCommon()) && elf::config->fortranCommon &&
      other.file->shouldExtractForCommon(getName())) {
    ctx.backwardReferences.erase(this);
    other.overwrite(*this);
    other.extract();
    return;
  }

  if (!isUndefined()) {
    // See the comment in resolveUndefined().
    if (isDefined())
      ctx.backwardReferences.erase(this);
    return;
  }

  // An undefined weak will not extract archive members. See comment on Lazy in
  // Symbols.h for the details.
  if (isWeak()) {
    uint8_t ty = type;
    other.overwrite(*this);
    type = ty;
    binding = STB_WEAK;
    return;
  }

  const InputFile *oldFile = file;
  other.extract();
  if (!config->whyExtract.empty())
    recordWhyExtract(oldFile, *file, *this);
}

void Symbol::resolve(const SharedSymbol &other) {
  exportDynamic = true;
  if (isPlaceholder()) {
    other.overwrite(*this);
    return;
  }
  if (isCommon()) {
    // See the comment in resolveCommon() above.
    if (other.size > cast<CommonSymbol>(this)->size)
      cast<CommonSymbol>(this)->size = other.size;
    return;
  }
  if (visibility() == STV_DEFAULT && (isUndefined() || isLazy())) {
    // An undefined symbol with non default visibility must be satisfied
    // in the same DSO.
    uint8_t bind = binding;
    other.overwrite(*this);
    binding = bind;
  } else if (traced)
    printTraceSymbol(other, getName());
}

void Defined::overwrite(Symbol &sym) const {
  if (isa_and_nonnull<SharedFile>(sym.file))
    sym.versionId = VER_NDX_GLOBAL;
  Symbol::overwrite(sym, DefinedKind);
  auto &s = static_cast<Defined &>(sym);
  s.value = value;
  s.size = size;
  s.section = section;
}
