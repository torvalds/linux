//===- SyntheticSections.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SyntheticSections.h"
#include "ConcatOutputSection.h"
#include "Config.h"
#include "ExportTrie.h"
#include "InputFiles.h"
#include "MachOStructs.h"
#include "ObjC.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "Symbols.h"

#include "lld/Common/CommonLinkerContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/xxhash.h"

#if defined(__APPLE__)
#include <sys/mman.h>

#define COMMON_DIGEST_FOR_OPENSSL
#include <CommonCrypto/CommonDigest.h>
#else
#include "llvm/Support/SHA256.h"
#endif

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::support;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::macho;

// Reads `len` bytes at data and writes the 32-byte SHA256 checksum to `output`.
static void sha256(const uint8_t *data, size_t len, uint8_t *output) {
#if defined(__APPLE__)
  // FIXME: Make LLVM's SHA256 faster and use it unconditionally. See PR56121
  // for some notes on this.
  CC_SHA256(data, len, output);
#else
  ArrayRef<uint8_t> block(data, len);
  std::array<uint8_t, 32> hash = SHA256::hash(block);
  static_assert(hash.size() == CodeSignatureSection::hashSize);
  memcpy(output, hash.data(), hash.size());
#endif
}

InStruct macho::in;
std::vector<SyntheticSection *> macho::syntheticSections;

SyntheticSection::SyntheticSection(const char *segname, const char *name)
    : OutputSection(SyntheticKind, name) {
  std::tie(this->segname, this->name) = maybeRenameSection({segname, name});
  isec = makeSyntheticInputSection(segname, name);
  isec->parent = this;
  syntheticSections.push_back(this);
}

// dyld3's MachOLoaded::getSlide() assumes that the __TEXT segment starts
// from the beginning of the file (i.e. the header).
MachHeaderSection::MachHeaderSection()
    : SyntheticSection(segment_names::text, section_names::header) {
  // XXX: This is a hack. (See D97007)
  // Setting the index to 1 to pretend that this section is the text
  // section.
  index = 1;
  isec->isFinal = true;
}

void MachHeaderSection::addLoadCommand(LoadCommand *lc) {
  loadCommands.push_back(lc);
  sizeOfCmds += lc->getSize();
}

uint64_t MachHeaderSection::getSize() const {
  uint64_t size = target->headerSize + sizeOfCmds + config->headerPad;
  // If we are emitting an encryptable binary, our load commands must have a
  // separate (non-encrypted) page to themselves.
  if (config->emitEncryptionInfo)
    size = alignToPowerOf2(size, target->getPageSize());
  return size;
}

static uint32_t cpuSubtype() {
  uint32_t subtype = target->cpuSubtype;

  if (config->outputType == MH_EXECUTE && !config->staticLink &&
      target->cpuSubtype == CPU_SUBTYPE_X86_64_ALL &&
      config->platform() == PLATFORM_MACOS &&
      config->platformInfo.target.MinDeployment >= VersionTuple(10, 5))
    subtype |= CPU_SUBTYPE_LIB64;

  return subtype;
}

static bool hasWeakBinding() {
  return config->emitChainedFixups ? in.chainedFixups->hasWeakBinding()
                                   : in.weakBinding->hasEntry();
}

static bool hasNonWeakDefinition() {
  return config->emitChainedFixups ? in.chainedFixups->hasNonWeakDefinition()
                                   : in.weakBinding->hasNonWeakDefinition();
}

void MachHeaderSection::writeTo(uint8_t *buf) const {
  auto *hdr = reinterpret_cast<mach_header *>(buf);
  hdr->magic = target->magic;
  hdr->cputype = target->cpuType;
  hdr->cpusubtype = cpuSubtype();
  hdr->filetype = config->outputType;
  hdr->ncmds = loadCommands.size();
  hdr->sizeofcmds = sizeOfCmds;
  hdr->flags = MH_DYLDLINK;

  if (config->namespaceKind == NamespaceKind::twolevel)
    hdr->flags |= MH_NOUNDEFS | MH_TWOLEVEL;

  if (config->outputType == MH_DYLIB && !config->hasReexports)
    hdr->flags |= MH_NO_REEXPORTED_DYLIBS;

  if (config->markDeadStrippableDylib)
    hdr->flags |= MH_DEAD_STRIPPABLE_DYLIB;

  if (config->outputType == MH_EXECUTE && config->isPic)
    hdr->flags |= MH_PIE;

  if (config->outputType == MH_DYLIB && config->applicationExtension)
    hdr->flags |= MH_APP_EXTENSION_SAFE;

  if (in.exports->hasWeakSymbol || hasNonWeakDefinition())
    hdr->flags |= MH_WEAK_DEFINES;

  if (in.exports->hasWeakSymbol || hasWeakBinding())
    hdr->flags |= MH_BINDS_TO_WEAK;

  for (const OutputSegment *seg : outputSegments) {
    for (const OutputSection *osec : seg->getSections()) {
      if (isThreadLocalVariables(osec->flags)) {
        hdr->flags |= MH_HAS_TLV_DESCRIPTORS;
        break;
      }
    }
  }

  uint8_t *p = reinterpret_cast<uint8_t *>(hdr) + target->headerSize;
  for (const LoadCommand *lc : loadCommands) {
    lc->writeTo(p);
    p += lc->getSize();
  }
}

PageZeroSection::PageZeroSection()
    : SyntheticSection(segment_names::pageZero, section_names::pageZero) {}

RebaseSection::RebaseSection()
    : LinkEditSection(segment_names::linkEdit, section_names::rebase) {}

namespace {
struct RebaseState {
  uint64_t sequenceLength;
  uint64_t skipLength;
};
} // namespace

static void emitIncrement(uint64_t incr, raw_svector_ostream &os) {
  assert(incr != 0);

  if ((incr >> target->p2WordSize) <= REBASE_IMMEDIATE_MASK &&
      (incr % target->wordSize) == 0) {
    os << static_cast<uint8_t>(REBASE_OPCODE_ADD_ADDR_IMM_SCALED |
                               (incr >> target->p2WordSize));
  } else {
    os << static_cast<uint8_t>(REBASE_OPCODE_ADD_ADDR_ULEB);
    encodeULEB128(incr, os);
  }
}

static void flushRebase(const RebaseState &state, raw_svector_ostream &os) {
  assert(state.sequenceLength > 0);

  if (state.skipLength == target->wordSize) {
    if (state.sequenceLength <= REBASE_IMMEDIATE_MASK) {
      os << static_cast<uint8_t>(REBASE_OPCODE_DO_REBASE_IMM_TIMES |
                                 state.sequenceLength);
    } else {
      os << static_cast<uint8_t>(REBASE_OPCODE_DO_REBASE_ULEB_TIMES);
      encodeULEB128(state.sequenceLength, os);
    }
  } else if (state.sequenceLength == 1) {
    os << static_cast<uint8_t>(REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB);
    encodeULEB128(state.skipLength - target->wordSize, os);
  } else {
    os << static_cast<uint8_t>(
        REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB);
    encodeULEB128(state.sequenceLength, os);
    encodeULEB128(state.skipLength - target->wordSize, os);
  }
}

// Rebases are communicated to dyld using a bytecode, whose opcodes cause the
// memory location at a specific address to be rebased and/or the address to be
// incremented.
//
// Opcode REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB is the most generic
// one, encoding a series of evenly spaced addresses. This algorithm works by
// splitting up the sorted list of addresses into such chunks. If the locations
// are consecutive or the sequence consists of a single location, flushRebase
// will use a smaller, more specialized encoding.
static void encodeRebases(const OutputSegment *seg,
                          MutableArrayRef<Location> locations,
                          raw_svector_ostream &os) {
  // dyld operates on segments. Translate section offsets into segment offsets.
  for (Location &loc : locations)
    loc.offset =
        loc.isec->parent->getSegmentOffset() + loc.isec->getOffset(loc.offset);
  // The algorithm assumes that locations are unique.
  Location *end =
      llvm::unique(locations, [](const Location &a, const Location &b) {
        return a.offset == b.offset;
      });
  size_t count = end - locations.begin();

  os << static_cast<uint8_t>(REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB |
                             seg->index);
  assert(!locations.empty());
  uint64_t offset = locations[0].offset;
  encodeULEB128(offset, os);

  RebaseState state{1, target->wordSize};

  for (size_t i = 1; i < count; ++i) {
    offset = locations[i].offset;

    uint64_t skip = offset - locations[i - 1].offset;
    assert(skip != 0 && "duplicate locations should have been weeded out");

    if (skip == state.skipLength) {
      ++state.sequenceLength;
    } else if (state.sequenceLength == 1) {
      ++state.sequenceLength;
      state.skipLength = skip;
    } else if (skip < state.skipLength) {
      // The address is lower than what the rebase pointer would be if the last
      // location would be part of a sequence. We start a new sequence from the
      // previous location.
      --state.sequenceLength;
      flushRebase(state, os);

      state.sequenceLength = 2;
      state.skipLength = skip;
    } else {
      // The address is at some positive offset from the rebase pointer. We
      // start a new sequence which begins with the current location.
      flushRebase(state, os);
      emitIncrement(skip - state.skipLength, os);
      state.sequenceLength = 1;
      state.skipLength = target->wordSize;
    }
  }
  flushRebase(state, os);
}

void RebaseSection::finalizeContents() {
  if (locations.empty())
    return;

  raw_svector_ostream os{contents};
  os << static_cast<uint8_t>(REBASE_OPCODE_SET_TYPE_IMM | REBASE_TYPE_POINTER);

  llvm::sort(locations, [](const Location &a, const Location &b) {
    return a.isec->getVA(a.offset) < b.isec->getVA(b.offset);
  });

  for (size_t i = 0, count = locations.size(); i < count;) {
    const OutputSegment *seg = locations[i].isec->parent->parent;
    size_t j = i + 1;
    while (j < count && locations[j].isec->parent->parent == seg)
      ++j;
    encodeRebases(seg, {locations.data() + i, locations.data() + j}, os);
    i = j;
  }
  os << static_cast<uint8_t>(REBASE_OPCODE_DONE);
}

void RebaseSection::writeTo(uint8_t *buf) const {
  memcpy(buf, contents.data(), contents.size());
}

NonLazyPointerSectionBase::NonLazyPointerSectionBase(const char *segname,
                                                     const char *name)
    : SyntheticSection(segname, name) {
  align = target->wordSize;
}

void macho::addNonLazyBindingEntries(const Symbol *sym,
                                     const InputSection *isec, uint64_t offset,
                                     int64_t addend) {
  if (config->emitChainedFixups) {
    if (needsBinding(sym))
      in.chainedFixups->addBinding(sym, isec, offset, addend);
    else if (isa<Defined>(sym))
      in.chainedFixups->addRebase(isec, offset);
    else
      llvm_unreachable("cannot bind to an undefined symbol");
    return;
  }

  if (const auto *dysym = dyn_cast<DylibSymbol>(sym)) {
    in.binding->addEntry(dysym, isec, offset, addend);
    if (dysym->isWeakDef())
      in.weakBinding->addEntry(sym, isec, offset, addend);
  } else if (const auto *defined = dyn_cast<Defined>(sym)) {
    in.rebase->addEntry(isec, offset);
    if (defined->isExternalWeakDef())
      in.weakBinding->addEntry(sym, isec, offset, addend);
    else if (defined->interposable)
      in.binding->addEntry(sym, isec, offset, addend);
  } else {
    // Undefined symbols are filtered out in scanRelocations(); we should never
    // get here
    llvm_unreachable("cannot bind to an undefined symbol");
  }
}

void NonLazyPointerSectionBase::addEntry(Symbol *sym) {
  if (entries.insert(sym)) {
    assert(!sym->isInGot());
    sym->gotIndex = entries.size() - 1;

    addNonLazyBindingEntries(sym, isec, sym->gotIndex * target->wordSize);
  }
}

void macho::writeChainedRebase(uint8_t *buf, uint64_t targetVA) {
  assert(config->emitChainedFixups);
  assert(target->wordSize == 8 && "Only 64-bit platforms are supported");
  auto *rebase = reinterpret_cast<dyld_chained_ptr_64_rebase *>(buf);
  rebase->target = targetVA & 0xf'ffff'ffff;
  rebase->high8 = (targetVA >> 56);
  rebase->reserved = 0;
  rebase->next = 0;
  rebase->bind = 0;

  // The fixup format places a 64 GiB limit on the output's size.
  // Should we handle this gracefully?
  uint64_t encodedVA = rebase->target | ((uint64_t)rebase->high8 << 56);
  if (encodedVA != targetVA)
    error("rebase target address 0x" + Twine::utohexstr(targetVA) +
          " does not fit into chained fixup. Re-link with -no_fixup_chains");
}

static void writeChainedBind(uint8_t *buf, const Symbol *sym, int64_t addend) {
  assert(config->emitChainedFixups);
  assert(target->wordSize == 8 && "Only 64-bit platforms are supported");
  auto *bind = reinterpret_cast<dyld_chained_ptr_64_bind *>(buf);
  auto [ordinal, inlineAddend] = in.chainedFixups->getBinding(sym, addend);
  bind->ordinal = ordinal;
  bind->addend = inlineAddend;
  bind->reserved = 0;
  bind->next = 0;
  bind->bind = 1;
}

void macho::writeChainedFixup(uint8_t *buf, const Symbol *sym, int64_t addend) {
  if (needsBinding(sym))
    writeChainedBind(buf, sym, addend);
  else
    writeChainedRebase(buf, sym->getVA() + addend);
}

void NonLazyPointerSectionBase::writeTo(uint8_t *buf) const {
  if (config->emitChainedFixups) {
    for (const auto &[i, entry] : llvm::enumerate(entries))
      writeChainedFixup(&buf[i * target->wordSize], entry, 0);
  } else {
    for (const auto &[i, entry] : llvm::enumerate(entries))
      if (auto *defined = dyn_cast<Defined>(entry))
        write64le(&buf[i * target->wordSize], defined->getVA());
  }
}

GotSection::GotSection()
    : NonLazyPointerSectionBase(segment_names::data, section_names::got) {
  flags = S_NON_LAZY_SYMBOL_POINTERS;
}

TlvPointerSection::TlvPointerSection()
    : NonLazyPointerSectionBase(segment_names::data,
                                section_names::threadPtrs) {
  flags = S_THREAD_LOCAL_VARIABLE_POINTERS;
}

BindingSection::BindingSection()
    : LinkEditSection(segment_names::linkEdit, section_names::binding) {}

namespace {
struct Binding {
  OutputSegment *segment = nullptr;
  uint64_t offset = 0;
  int64_t addend = 0;
};
struct BindIR {
  // Default value of 0xF0 is not valid opcode and should make the program
  // scream instead of accidentally writing "valid" values.
  uint8_t opcode = 0xF0;
  uint64_t data = 0;
  uint64_t consecutiveCount = 0;
};
} // namespace

// Encode a sequence of opcodes that tell dyld to write the address of symbol +
// addend at osec->addr + outSecOff.
//
// The bind opcode "interpreter" remembers the values of each binding field, so
// we only need to encode the differences between bindings. Hence the use of
// lastBinding.
static void encodeBinding(const OutputSection *osec, uint64_t outSecOff,
                          int64_t addend, Binding &lastBinding,
                          std::vector<BindIR> &opcodes) {
  OutputSegment *seg = osec->parent;
  uint64_t offset = osec->getSegmentOffset() + outSecOff;
  if (lastBinding.segment != seg) {
    opcodes.push_back(
        {static_cast<uint8_t>(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB |
                              seg->index),
         offset});
    lastBinding.segment = seg;
    lastBinding.offset = offset;
  } else if (lastBinding.offset != offset) {
    opcodes.push_back({BIND_OPCODE_ADD_ADDR_ULEB, offset - lastBinding.offset});
    lastBinding.offset = offset;
  }

  if (lastBinding.addend != addend) {
    opcodes.push_back(
        {BIND_OPCODE_SET_ADDEND_SLEB, static_cast<uint64_t>(addend)});
    lastBinding.addend = addend;
  }

  opcodes.push_back({BIND_OPCODE_DO_BIND, 0});
  // DO_BIND causes dyld to both perform the binding and increment the offset
  lastBinding.offset += target->wordSize;
}

static void optimizeOpcodes(std::vector<BindIR> &opcodes) {
  // Pass 1: Combine bind/add pairs
  size_t i;
  int pWrite = 0;
  for (i = 1; i < opcodes.size(); ++i, ++pWrite) {
    if ((opcodes[i].opcode == BIND_OPCODE_ADD_ADDR_ULEB) &&
        (opcodes[i - 1].opcode == BIND_OPCODE_DO_BIND)) {
      opcodes[pWrite].opcode = BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB;
      opcodes[pWrite].data = opcodes[i].data;
      ++i;
    } else {
      opcodes[pWrite] = opcodes[i - 1];
    }
  }
  if (i == opcodes.size())
    opcodes[pWrite] = opcodes[i - 1];
  opcodes.resize(pWrite + 1);

  // Pass 2: Compress two or more bind_add opcodes
  pWrite = 0;
  for (i = 1; i < opcodes.size(); ++i, ++pWrite) {
    if ((opcodes[i].opcode == BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB) &&
        (opcodes[i - 1].opcode == BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB) &&
        (opcodes[i].data == opcodes[i - 1].data)) {
      opcodes[pWrite].opcode = BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB;
      opcodes[pWrite].consecutiveCount = 2;
      opcodes[pWrite].data = opcodes[i].data;
      ++i;
      while (i < opcodes.size() &&
             (opcodes[i].opcode == BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB) &&
             (opcodes[i].data == opcodes[i - 1].data)) {
        opcodes[pWrite].consecutiveCount++;
        ++i;
      }
    } else {
      opcodes[pWrite] = opcodes[i - 1];
    }
  }
  if (i == opcodes.size())
    opcodes[pWrite] = opcodes[i - 1];
  opcodes.resize(pWrite + 1);

  // Pass 3: Use immediate encodings
  // Every binding is the size of one pointer. If the next binding is a
  // multiple of wordSize away that is within BIND_IMMEDIATE_MASK, the
  // opcode can be scaled by wordSize into a single byte and dyld will
  // expand it to the correct address.
  for (auto &p : opcodes) {
    // It's unclear why the check needs to be less than BIND_IMMEDIATE_MASK,
    // but ld64 currently does this. This could be a potential bug, but
    // for now, perform the same behavior to prevent mysterious bugs.
    if ((p.opcode == BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB) &&
        ((p.data / target->wordSize) < BIND_IMMEDIATE_MASK) &&
        ((p.data % target->wordSize) == 0)) {
      p.opcode = BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED;
      p.data /= target->wordSize;
    }
  }
}

static void flushOpcodes(const BindIR &op, raw_svector_ostream &os) {
  uint8_t opcode = op.opcode & BIND_OPCODE_MASK;
  switch (opcode) {
  case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
  case BIND_OPCODE_ADD_ADDR_ULEB:
  case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
    os << op.opcode;
    encodeULEB128(op.data, os);
    break;
  case BIND_OPCODE_SET_ADDEND_SLEB:
    os << op.opcode;
    encodeSLEB128(static_cast<int64_t>(op.data), os);
    break;
  case BIND_OPCODE_DO_BIND:
    os << op.opcode;
    break;
  case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
    os << op.opcode;
    encodeULEB128(op.consecutiveCount, os);
    encodeULEB128(op.data, os);
    break;
  case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
    os << static_cast<uint8_t>(op.opcode | op.data);
    break;
  default:
    llvm_unreachable("cannot bind to an unrecognized symbol");
  }
}

static bool needsWeakBind(const Symbol &sym) {
  if (auto *dysym = dyn_cast<DylibSymbol>(&sym))
    return dysym->isWeakDef();
  if (auto *defined = dyn_cast<Defined>(&sym))
    return defined->isExternalWeakDef();
  return false;
}

// Non-weak bindings need to have their dylib ordinal encoded as well.
static int16_t ordinalForDylibSymbol(const DylibSymbol &dysym) {
  if (config->namespaceKind == NamespaceKind::flat || dysym.isDynamicLookup())
    return static_cast<int16_t>(BIND_SPECIAL_DYLIB_FLAT_LOOKUP);
  assert(dysym.getFile()->isReferenced());
  return dysym.getFile()->ordinal;
}

static int16_t ordinalForSymbol(const Symbol &sym) {
  if (config->emitChainedFixups && needsWeakBind(sym))
    return BIND_SPECIAL_DYLIB_WEAK_LOOKUP;
  if (const auto *dysym = dyn_cast<DylibSymbol>(&sym))
    return ordinalForDylibSymbol(*dysym);
  assert(cast<Defined>(&sym)->interposable);
  return BIND_SPECIAL_DYLIB_FLAT_LOOKUP;
}

static void encodeDylibOrdinal(int16_t ordinal, raw_svector_ostream &os) {
  if (ordinal <= 0) {
    os << static_cast<uint8_t>(BIND_OPCODE_SET_DYLIB_SPECIAL_IMM |
                               (ordinal & BIND_IMMEDIATE_MASK));
  } else if (ordinal <= BIND_IMMEDIATE_MASK) {
    os << static_cast<uint8_t>(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | ordinal);
  } else {
    os << static_cast<uint8_t>(BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB);
    encodeULEB128(ordinal, os);
  }
}

static void encodeWeakOverride(const Defined *defined,
                               raw_svector_ostream &os) {
  os << static_cast<uint8_t>(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM |
                             BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION)
     << defined->getName() << '\0';
}

// Organize the bindings so we can encoded them with fewer opcodes.
//
// First, all bindings for a given symbol should be grouped together.
// BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM is the largest opcode (since it
// has an associated symbol string), so we only want to emit it once per symbol.
//
// Within each group, we sort the bindings by address. Since bindings are
// delta-encoded, sorting them allows for a more compact result. Note that
// sorting by address alone ensures that bindings for the same segment / section
// are located together, minimizing the number of times we have to emit
// BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB.
//
// Finally, we sort the symbols by the address of their first binding, again
// to facilitate the delta-encoding process.
template <class Sym>
std::vector<std::pair<const Sym *, std::vector<BindingEntry>>>
sortBindings(const BindingsMap<const Sym *> &bindingsMap) {
  std::vector<std::pair<const Sym *, std::vector<BindingEntry>>> bindingsVec(
      bindingsMap.begin(), bindingsMap.end());
  for (auto &p : bindingsVec) {
    std::vector<BindingEntry> &bindings = p.second;
    llvm::sort(bindings, [](const BindingEntry &a, const BindingEntry &b) {
      return a.target.getVA() < b.target.getVA();
    });
  }
  llvm::sort(bindingsVec, [](const auto &a, const auto &b) {
    return a.second[0].target.getVA() < b.second[0].target.getVA();
  });
  return bindingsVec;
}

// Emit bind opcodes, which are a stream of byte-sized opcodes that dyld
// interprets to update a record with the following fields:
//  * segment index (of the segment to write the symbol addresses to, typically
//    the __DATA_CONST segment which contains the GOT)
//  * offset within the segment, indicating the next location to write a binding
//  * symbol type
//  * symbol library ordinal (the index of its library's LC_LOAD_DYLIB command)
//  * symbol name
//  * addend
// When dyld sees BIND_OPCODE_DO_BIND, it uses the current record state to bind
// a symbol in the GOT, and increments the segment offset to point to the next
// entry. It does *not* clear the record state after doing the bind, so
// subsequent opcodes only need to encode the differences between bindings.
void BindingSection::finalizeContents() {
  raw_svector_ostream os{contents};
  Binding lastBinding;
  int16_t lastOrdinal = 0;

  for (auto &p : sortBindings(bindingsMap)) {
    const Symbol *sym = p.first;
    std::vector<BindingEntry> &bindings = p.second;
    uint8_t flags = BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM;
    if (sym->isWeakRef())
      flags |= BIND_SYMBOL_FLAGS_WEAK_IMPORT;
    os << flags << sym->getName() << '\0'
       << static_cast<uint8_t>(BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER);
    int16_t ordinal = ordinalForSymbol(*sym);
    if (ordinal != lastOrdinal) {
      encodeDylibOrdinal(ordinal, os);
      lastOrdinal = ordinal;
    }
    std::vector<BindIR> opcodes;
    for (const BindingEntry &b : bindings)
      encodeBinding(b.target.isec->parent,
                    b.target.isec->getOffset(b.target.offset), b.addend,
                    lastBinding, opcodes);
    if (config->optimize > 1)
      optimizeOpcodes(opcodes);
    for (const auto &op : opcodes)
      flushOpcodes(op, os);
  }
  if (!bindingsMap.empty())
    os << static_cast<uint8_t>(BIND_OPCODE_DONE);
}

void BindingSection::writeTo(uint8_t *buf) const {
  memcpy(buf, contents.data(), contents.size());
}

WeakBindingSection::WeakBindingSection()
    : LinkEditSection(segment_names::linkEdit, section_names::weakBinding) {}

void WeakBindingSection::finalizeContents() {
  raw_svector_ostream os{contents};
  Binding lastBinding;

  for (const Defined *defined : definitions)
    encodeWeakOverride(defined, os);

  for (auto &p : sortBindings(bindingsMap)) {
    const Symbol *sym = p.first;
    std::vector<BindingEntry> &bindings = p.second;
    os << static_cast<uint8_t>(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM)
       << sym->getName() << '\0'
       << static_cast<uint8_t>(BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER);
    std::vector<BindIR> opcodes;
    for (const BindingEntry &b : bindings)
      encodeBinding(b.target.isec->parent,
                    b.target.isec->getOffset(b.target.offset), b.addend,
                    lastBinding, opcodes);
    if (config->optimize > 1)
      optimizeOpcodes(opcodes);
    for (const auto &op : opcodes)
      flushOpcodes(op, os);
  }
  if (!bindingsMap.empty() || !definitions.empty())
    os << static_cast<uint8_t>(BIND_OPCODE_DONE);
}

void WeakBindingSection::writeTo(uint8_t *buf) const {
  memcpy(buf, contents.data(), contents.size());
}

StubsSection::StubsSection()
    : SyntheticSection(segment_names::text, section_names::stubs) {
  flags = S_SYMBOL_STUBS | S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS;
  // The stubs section comprises machine instructions, which are aligned to
  // 4 bytes on the archs we care about.
  align = 4;
  reserved2 = target->stubSize;
}

uint64_t StubsSection::getSize() const {
  return entries.size() * target->stubSize;
}

void StubsSection::writeTo(uint8_t *buf) const {
  size_t off = 0;
  for (const Symbol *sym : entries) {
    uint64_t pointerVA =
        config->emitChainedFixups ? sym->getGotVA() : sym->getLazyPtrVA();
    target->writeStub(buf + off, *sym, pointerVA);
    off += target->stubSize;
  }
}

void StubsSection::finalize() { isFinal = true; }

static void addBindingsForStub(Symbol *sym) {
  assert(!config->emitChainedFixups);
  if (auto *dysym = dyn_cast<DylibSymbol>(sym)) {
    if (sym->isWeakDef()) {
      in.binding->addEntry(dysym, in.lazyPointers->isec,
                           sym->stubsIndex * target->wordSize);
      in.weakBinding->addEntry(sym, in.lazyPointers->isec,
                               sym->stubsIndex * target->wordSize);
    } else {
      in.lazyBinding->addEntry(dysym);
    }
  } else if (auto *defined = dyn_cast<Defined>(sym)) {
    if (defined->isExternalWeakDef()) {
      in.rebase->addEntry(in.lazyPointers->isec,
                          sym->stubsIndex * target->wordSize);
      in.weakBinding->addEntry(sym, in.lazyPointers->isec,
                               sym->stubsIndex * target->wordSize);
    } else if (defined->interposable) {
      in.lazyBinding->addEntry(sym);
    } else {
      llvm_unreachable("invalid stub target");
    }
  } else {
    llvm_unreachable("invalid stub target symbol type");
  }
}

void StubsSection::addEntry(Symbol *sym) {
  bool inserted = entries.insert(sym);
  if (inserted) {
    sym->stubsIndex = entries.size() - 1;

    if (config->emitChainedFixups)
      in.got->addEntry(sym);
    else
      addBindingsForStub(sym);
  }
}

StubHelperSection::StubHelperSection()
    : SyntheticSection(segment_names::text, section_names::stubHelper) {
  flags = S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS;
  align = 4; // This section comprises machine instructions
}

uint64_t StubHelperSection::getSize() const {
  return target->stubHelperHeaderSize +
         in.lazyBinding->getEntries().size() * target->stubHelperEntrySize;
}

bool StubHelperSection::isNeeded() const { return in.lazyBinding->isNeeded(); }

void StubHelperSection::writeTo(uint8_t *buf) const {
  target->writeStubHelperHeader(buf);
  size_t off = target->stubHelperHeaderSize;
  for (const Symbol *sym : in.lazyBinding->getEntries()) {
    target->writeStubHelperEntry(buf + off, *sym, addr + off);
    off += target->stubHelperEntrySize;
  }
}

void StubHelperSection::setUp() {
  Symbol *binder = symtab->addUndefined("dyld_stub_binder", /*file=*/nullptr,
                                        /*isWeakRef=*/false);
  if (auto *undefined = dyn_cast<Undefined>(binder))
    treatUndefinedSymbol(*undefined,
                         "lazy binding (normally in libSystem.dylib)");

  // treatUndefinedSymbol() can replace binder with a DylibSymbol; re-check.
  stubBinder = dyn_cast_or_null<DylibSymbol>(binder);
  if (stubBinder == nullptr)
    return;

  in.got->addEntry(stubBinder);

  in.imageLoaderCache->parent =
      ConcatOutputSection::getOrCreateForInput(in.imageLoaderCache);
  addInputSection(in.imageLoaderCache);
  // Since this isn't in the symbol table or in any input file, the noDeadStrip
  // argument doesn't matter.
  dyldPrivate =
      make<Defined>("__dyld_private", nullptr, in.imageLoaderCache, 0, 0,
                    /*isWeakDef=*/false,
                    /*isExternal=*/false, /*isPrivateExtern=*/false,
                    /*includeInSymtab=*/true,
                    /*isReferencedDynamically=*/false,
                    /*noDeadStrip=*/false);
  dyldPrivate->used = true;
}

llvm::DenseMap<llvm::CachedHashStringRef, ConcatInputSection *>
    ObjCSelRefsHelper::methnameToSelref;
void ObjCSelRefsHelper::initialize() {
  // Do not fold selrefs without ICF.
  if (config->icfLevel == ICFLevel::none)
    return;

  // Search methnames already referenced in __objc_selrefs
  // Map the name to the corresponding selref entry
  // which we will reuse when creating objc stubs.
  for (ConcatInputSection *isec : inputSections) {
    if (isec->shouldOmitFromOutput())
      continue;
    if (isec->getName() != section_names::objcSelrefs)
      continue;
    // We expect a single relocation per selref entry to __objc_methname that
    // might be aggregated.
    assert(isec->relocs.size() == 1);
    auto Reloc = isec->relocs[0];
    if (const auto *sym = Reloc.referent.dyn_cast<Symbol *>()) {
      if (const auto *d = dyn_cast<Defined>(sym)) {
        auto *cisec = cast<CStringInputSection>(d->isec());
        auto methname = cisec->getStringRefAtOffset(d->value);
        methnameToSelref[CachedHashStringRef(methname)] = isec;
      }
    }
  }
}

void ObjCSelRefsHelper::cleanup() { methnameToSelref.clear(); }

ConcatInputSection *ObjCSelRefsHelper::makeSelRef(StringRef methname) {
  auto methnameOffset =
      in.objcMethnameSection->getStringOffset(methname).outSecOff;

  size_t wordSize = target->wordSize;
  uint8_t *selrefData = bAlloc().Allocate<uint8_t>(wordSize);
  write64le(selrefData, methnameOffset);
  ConcatInputSection *objcSelref =
      makeSyntheticInputSection(segment_names::data, section_names::objcSelrefs,
                                S_LITERAL_POINTERS | S_ATTR_NO_DEAD_STRIP,
                                ArrayRef<uint8_t>{selrefData, wordSize},
                                /*align=*/wordSize);
  assert(objcSelref->live);
  objcSelref->relocs.push_back({/*type=*/target->unsignedRelocType,
                                /*pcrel=*/false, /*length=*/3,
                                /*offset=*/0,
                                /*addend=*/static_cast<int64_t>(methnameOffset),
                                /*referent=*/in.objcMethnameSection->isec});
  objcSelref->parent = ConcatOutputSection::getOrCreateForInput(objcSelref);
  addInputSection(objcSelref);
  objcSelref->isFinal = true;
  methnameToSelref[CachedHashStringRef(methname)] = objcSelref;
  return objcSelref;
}

ConcatInputSection *ObjCSelRefsHelper::getSelRef(StringRef methname) {
  auto it = methnameToSelref.find(CachedHashStringRef(methname));
  if (it == methnameToSelref.end())
    return nullptr;
  return it->second;
}

ObjCStubsSection::ObjCStubsSection()
    : SyntheticSection(segment_names::text, section_names::objcStubs) {
  flags = S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS;
  align = config->objcStubsMode == ObjCStubsMode::fast
              ? target->objcStubsFastAlignment
              : target->objcStubsSmallAlignment;
}

bool ObjCStubsSection::isObjCStubSymbol(Symbol *sym) {
  return sym->getName().starts_with(symbolPrefix);
}

StringRef ObjCStubsSection::getMethname(Symbol *sym) {
  assert(isObjCStubSymbol(sym) && "not an objc stub");
  auto name = sym->getName();
  StringRef methname = name.drop_front(symbolPrefix.size());
  return methname;
}

void ObjCStubsSection::addEntry(Symbol *sym) {
  StringRef methname = getMethname(sym);
  // We create a selref entry for each unique methname.
  if (!ObjCSelRefsHelper::getSelRef(methname))
    ObjCSelRefsHelper::makeSelRef(methname);

  auto stubSize = config->objcStubsMode == ObjCStubsMode::fast
                      ? target->objcStubsFastSize
                      : target->objcStubsSmallSize;
  Defined *newSym = replaceSymbol<Defined>(
      sym, sym->getName(), nullptr, isec,
      /*value=*/symbols.size() * stubSize,
      /*size=*/stubSize,
      /*isWeakDef=*/false, /*isExternal=*/true, /*isPrivateExtern=*/true,
      /*includeInSymtab=*/true, /*isReferencedDynamically=*/false,
      /*noDeadStrip=*/false);
  symbols.push_back(newSym);
}

void ObjCStubsSection::setUp() {
  objcMsgSend = symtab->addUndefined("_objc_msgSend", /*file=*/nullptr,
                                     /*isWeakRef=*/false);
  if (auto *undefined = dyn_cast<Undefined>(objcMsgSend))
    treatUndefinedSymbol(*undefined,
                         "lazy binding (normally in libobjc.dylib)");
  objcMsgSend->used = true;
  if (config->objcStubsMode == ObjCStubsMode::fast) {
    in.got->addEntry(objcMsgSend);
    assert(objcMsgSend->isInGot());
  } else {
    assert(config->objcStubsMode == ObjCStubsMode::small);
    // In line with ld64's behavior, when objc_msgSend is a direct symbol,
    // we directly reference it.
    // In other cases, typically when binding in libobjc.dylib,
    // we generate a stub to invoke objc_msgSend.
    if (!isa<Defined>(objcMsgSend))
      in.stubs->addEntry(objcMsgSend);
  }
}

uint64_t ObjCStubsSection::getSize() const {
  auto stubSize = config->objcStubsMode == ObjCStubsMode::fast
                      ? target->objcStubsFastSize
                      : target->objcStubsSmallSize;
  return stubSize * symbols.size();
}

void ObjCStubsSection::writeTo(uint8_t *buf) const {
  uint64_t stubOffset = 0;
  for (size_t i = 0, n = symbols.size(); i < n; ++i) {
    Defined *sym = symbols[i];

    auto methname = getMethname(sym);
    InputSection *selRef = ObjCSelRefsHelper::getSelRef(methname);
    assert(selRef != nullptr && "no selref for methname");
    auto selrefAddr = selRef->getVA(0);
    target->writeObjCMsgSendStub(buf + stubOffset, sym, in.objcStubs->addr,
                                 stubOffset, selrefAddr, objcMsgSend);
  }
}

LazyPointerSection::LazyPointerSection()
    : SyntheticSection(segment_names::data, section_names::lazySymbolPtr) {
  align = target->wordSize;
  flags = S_LAZY_SYMBOL_POINTERS;
}

uint64_t LazyPointerSection::getSize() const {
  return in.stubs->getEntries().size() * target->wordSize;
}

bool LazyPointerSection::isNeeded() const {
  return !in.stubs->getEntries().empty();
}

void LazyPointerSection::writeTo(uint8_t *buf) const {
  size_t off = 0;
  for (const Symbol *sym : in.stubs->getEntries()) {
    if (const auto *dysym = dyn_cast<DylibSymbol>(sym)) {
      if (dysym->hasStubsHelper()) {
        uint64_t stubHelperOffset =
            target->stubHelperHeaderSize +
            dysym->stubsHelperIndex * target->stubHelperEntrySize;
        write64le(buf + off, in.stubHelper->addr + stubHelperOffset);
      }
    } else {
      write64le(buf + off, sym->getVA());
    }
    off += target->wordSize;
  }
}

LazyBindingSection::LazyBindingSection()
    : LinkEditSection(segment_names::linkEdit, section_names::lazyBinding) {}

void LazyBindingSection::finalizeContents() {
  // TODO: Just precompute output size here instead of writing to a temporary
  // buffer
  for (Symbol *sym : entries)
    sym->lazyBindOffset = encode(*sym);
}

void LazyBindingSection::writeTo(uint8_t *buf) const {
  memcpy(buf, contents.data(), contents.size());
}

void LazyBindingSection::addEntry(Symbol *sym) {
  assert(!config->emitChainedFixups && "Chained fixups always bind eagerly");
  if (entries.insert(sym)) {
    sym->stubsHelperIndex = entries.size() - 1;
    in.rebase->addEntry(in.lazyPointers->isec,
                        sym->stubsIndex * target->wordSize);
  }
}

// Unlike the non-lazy binding section, the bind opcodes in this section aren't
// interpreted all at once. Rather, dyld will start interpreting opcodes at a
// given offset, typically only binding a single symbol before it finds a
// BIND_OPCODE_DONE terminator. As such, unlike in the non-lazy-binding case,
// we cannot encode just the differences between symbols; we have to emit the
// complete bind information for each symbol.
uint32_t LazyBindingSection::encode(const Symbol &sym) {
  uint32_t opstreamOffset = contents.size();
  OutputSegment *dataSeg = in.lazyPointers->parent;
  os << static_cast<uint8_t>(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB |
                             dataSeg->index);
  uint64_t offset =
      in.lazyPointers->addr - dataSeg->addr + sym.stubsIndex * target->wordSize;
  encodeULEB128(offset, os);
  encodeDylibOrdinal(ordinalForSymbol(sym), os);

  uint8_t flags = BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM;
  if (sym.isWeakRef())
    flags |= BIND_SYMBOL_FLAGS_WEAK_IMPORT;

  os << flags << sym.getName() << '\0'
     << static_cast<uint8_t>(BIND_OPCODE_DO_BIND)
     << static_cast<uint8_t>(BIND_OPCODE_DONE);
  return opstreamOffset;
}

ExportSection::ExportSection()
    : LinkEditSection(segment_names::linkEdit, section_names::export_) {}

void ExportSection::finalizeContents() {
  trieBuilder.setImageBase(in.header->addr);
  for (const Symbol *sym : symtab->getSymbols()) {
    if (const auto *defined = dyn_cast<Defined>(sym)) {
      if (defined->privateExtern || !defined->isLive())
        continue;
      trieBuilder.addSymbol(*defined);
      hasWeakSymbol = hasWeakSymbol || sym->isWeakDef();
    } else if (auto *dysym = dyn_cast<DylibSymbol>(sym)) {
      if (dysym->shouldReexport)
        trieBuilder.addSymbol(*dysym);
    }
  }
  size = trieBuilder.build();
}

void ExportSection::writeTo(uint8_t *buf) const { trieBuilder.writeTo(buf); }

DataInCodeSection::DataInCodeSection()
    : LinkEditSection(segment_names::linkEdit, section_names::dataInCode) {}

template <class LP>
static std::vector<MachO::data_in_code_entry> collectDataInCodeEntries() {
  std::vector<MachO::data_in_code_entry> dataInCodeEntries;
  for (const InputFile *inputFile : inputFiles) {
    if (!isa<ObjFile>(inputFile))
      continue;
    const ObjFile *objFile = cast<ObjFile>(inputFile);
    ArrayRef<MachO::data_in_code_entry> entries = objFile->getDataInCode();
    if (entries.empty())
      continue;

    std::vector<MachO::data_in_code_entry> sortedEntries;
    sortedEntries.assign(entries.begin(), entries.end());
    llvm::sort(sortedEntries, [](const data_in_code_entry &lhs,
                                 const data_in_code_entry &rhs) {
      return lhs.offset < rhs.offset;
    });

    // For each code subsection find 'data in code' entries residing in it.
    // Compute the new offset values as
    // <offset within subsection> + <subsection address> - <__TEXT address>.
    for (const Section *section : objFile->sections) {
      for (const Subsection &subsec : section->subsections) {
        const InputSection *isec = subsec.isec;
        if (!isCodeSection(isec))
          continue;
        if (cast<ConcatInputSection>(isec)->shouldOmitFromOutput())
          continue;
        const uint64_t beginAddr = section->addr + subsec.offset;
        auto it = llvm::lower_bound(
            sortedEntries, beginAddr,
            [](const MachO::data_in_code_entry &entry, uint64_t addr) {
              return entry.offset < addr;
            });
        const uint64_t endAddr = beginAddr + isec->getSize();
        for (const auto end = sortedEntries.end();
             it != end && it->offset + it->length <= endAddr; ++it)
          dataInCodeEntries.push_back(
              {static_cast<uint32_t>(isec->getVA(it->offset - beginAddr) -
                                     in.header->addr),
               it->length, it->kind});
      }
    }
  }

  // ld64 emits the table in sorted order too.
  llvm::sort(dataInCodeEntries,
             [](const data_in_code_entry &lhs, const data_in_code_entry &rhs) {
               return lhs.offset < rhs.offset;
             });
  return dataInCodeEntries;
}

void DataInCodeSection::finalizeContents() {
  entries = target->wordSize == 8 ? collectDataInCodeEntries<LP64>()
                                  : collectDataInCodeEntries<ILP32>();
}

void DataInCodeSection::writeTo(uint8_t *buf) const {
  if (!entries.empty())
    memcpy(buf, entries.data(), getRawSize());
}

FunctionStartsSection::FunctionStartsSection()
    : LinkEditSection(segment_names::linkEdit, section_names::functionStarts) {}

void FunctionStartsSection::finalizeContents() {
  raw_svector_ostream os{contents};
  std::vector<uint64_t> addrs;
  for (const InputFile *file : inputFiles) {
    if (auto *objFile = dyn_cast<ObjFile>(file)) {
      for (const Symbol *sym : objFile->symbols) {
        if (const auto *defined = dyn_cast_or_null<Defined>(sym)) {
          if (!defined->isec() || !isCodeSection(defined->isec()) ||
              !defined->isLive())
            continue;
          addrs.push_back(defined->getVA());
        }
      }
    }
  }
  llvm::sort(addrs);
  uint64_t addr = in.header->addr;
  for (uint64_t nextAddr : addrs) {
    uint64_t delta = nextAddr - addr;
    if (delta == 0)
      continue;
    encodeULEB128(delta, os);
    addr = nextAddr;
  }
  os << '\0';
}

void FunctionStartsSection::writeTo(uint8_t *buf) const {
  memcpy(buf, contents.data(), contents.size());
}

SymtabSection::SymtabSection(StringTableSection &stringTableSection)
    : LinkEditSection(segment_names::linkEdit, section_names::symbolTable),
      stringTableSection(stringTableSection) {}

void SymtabSection::emitBeginSourceStab(StringRef sourceFile) {
  StabsEntry stab(N_SO);
  stab.strx = stringTableSection.addString(saver().save(sourceFile));
  stabs.emplace_back(std::move(stab));
}

void SymtabSection::emitEndSourceStab() {
  StabsEntry stab(N_SO);
  stab.sect = 1;
  stabs.emplace_back(std::move(stab));
}

void SymtabSection::emitObjectFileStab(ObjFile *file) {
  StabsEntry stab(N_OSO);
  stab.sect = target->cpuSubtype;
  SmallString<261> path(!file->archiveName.empty() ? file->archiveName
                                                   : file->getName());
  std::error_code ec = sys::fs::make_absolute(path);
  if (ec)
    fatal("failed to get absolute path for " + path);

  if (!file->archiveName.empty())
    path.append({"(", file->getName(), ")"});

  StringRef adjustedPath = saver().save(path.str());
  adjustedPath.consume_front(config->osoPrefix);

  stab.strx = stringTableSection.addString(adjustedPath);
  stab.desc = 1;
  stab.value = file->modTime;
  stabs.emplace_back(std::move(stab));
}

void SymtabSection::emitEndFunStab(Defined *defined) {
  StabsEntry stab(N_FUN);
  stab.value = defined->size;
  stabs.emplace_back(std::move(stab));
}

void SymtabSection::emitStabs() {
  if (config->omitDebugInfo)
    return;

  for (const std::string &s : config->astPaths) {
    StabsEntry astStab(N_AST);
    astStab.strx = stringTableSection.addString(s);
    stabs.emplace_back(std::move(astStab));
  }

  // Cache the file ID for each symbol in an std::pair for faster sorting.
  using SortingPair = std::pair<Defined *, int>;
  std::vector<SortingPair> symbolsNeedingStabs;
  for (const SymtabEntry &entry :
       concat<SymtabEntry>(localSymbols, externalSymbols)) {
    Symbol *sym = entry.sym;
    assert(sym->isLive() &&
           "dead symbols should not be in localSymbols, externalSymbols");
    if (auto *defined = dyn_cast<Defined>(sym)) {
      // Excluded symbols should have been filtered out in finalizeContents().
      assert(defined->includeInSymtab);

      if (defined->isAbsolute())
        continue;

      // Constant-folded symbols go in the executable's symbol table, but don't
      // get a stabs entry unless --keep-icf-stabs flag is specified
      if (!config->keepICFStabs && defined->wasIdenticalCodeFolded)
        continue;

      ObjFile *file = defined->getObjectFile();
      if (!file || !file->compileUnit)
        continue;

      // We use 'originalIsec' to get the file id of the symbol since 'isec()'
      // might point to the merged ICF symbol's file
      symbolsNeedingStabs.emplace_back(defined,
                                       defined->originalIsec->getFile()->id);
    }
  }

  llvm::stable_sort(symbolsNeedingStabs,
                    [&](const SortingPair &a, const SortingPair &b) {
                      return a.second < b.second;
                    });

  // Emit STABS symbols so that dsymutil and/or the debugger can map address
  // regions in the final binary to the source and object files from which they
  // originated.
  InputFile *lastFile = nullptr;
  for (SortingPair &pair : symbolsNeedingStabs) {
    Defined *defined = pair.first;
    // We use 'originalIsec' of the symbol since we care about the actual origin
    // of the symbol, not the canonical location returned by `isec()`.
    InputSection *isec = defined->originalIsec;
    ObjFile *file = cast<ObjFile>(isec->getFile());

    if (lastFile == nullptr || lastFile != file) {
      if (lastFile != nullptr)
        emitEndSourceStab();
      lastFile = file;

      emitBeginSourceStab(file->sourceFile());
      emitObjectFileStab(file);
    }

    StabsEntry symStab;
    symStab.sect = isec->parent->index;
    symStab.strx = stringTableSection.addString(defined->getName());
    symStab.value = defined->getVA();

    if (isCodeSection(isec)) {
      symStab.type = N_FUN;
      stabs.emplace_back(std::move(symStab));
      emitEndFunStab(defined);
    } else {
      symStab.type = defined->isExternal() ? N_GSYM : N_STSYM;
      stabs.emplace_back(std::move(symStab));
    }
  }

  if (!stabs.empty())
    emitEndSourceStab();
}

void SymtabSection::finalizeContents() {
  auto addSymbol = [&](std::vector<SymtabEntry> &symbols, Symbol *sym) {
    uint32_t strx = stringTableSection.addString(sym->getName());
    symbols.push_back({sym, strx});
  };

  std::function<void(Symbol *)> localSymbolsHandler;
  switch (config->localSymbolsPresence) {
  case SymtabPresence::All:
    localSymbolsHandler = [&](Symbol *sym) { addSymbol(localSymbols, sym); };
    break;
  case SymtabPresence::None:
    localSymbolsHandler = [&](Symbol *) { /* Do nothing*/ };
    break;
  case SymtabPresence::SelectivelyIncluded:
    localSymbolsHandler = [&](Symbol *sym) {
      if (config->localSymbolPatterns.match(sym->getName()))
        addSymbol(localSymbols, sym);
    };
    break;
  case SymtabPresence::SelectivelyExcluded:
    localSymbolsHandler = [&](Symbol *sym) {
      if (!config->localSymbolPatterns.match(sym->getName()))
        addSymbol(localSymbols, sym);
    };
    break;
  }

  // Local symbols aren't in the SymbolTable, so we walk the list of object
  // files to gather them.
  // But if `-x` is set, then we don't need to. localSymbolsHandler() will do
  // the right thing regardless, but this check is a perf optimization because
  // iterating through all the input files and their symbols is expensive.
  if (config->localSymbolsPresence != SymtabPresence::None) {
    for (const InputFile *file : inputFiles) {
      if (auto *objFile = dyn_cast<ObjFile>(file)) {
        for (Symbol *sym : objFile->symbols) {
          if (auto *defined = dyn_cast_or_null<Defined>(sym)) {
            if (defined->isExternal() || !defined->isLive() ||
                !defined->includeInSymtab)
              continue;
            localSymbolsHandler(sym);
          }
        }
      }
    }
  }

  // __dyld_private is a local symbol too. It's linker-created and doesn't
  // exist in any object file.
  if (in.stubHelper && in.stubHelper->dyldPrivate)
    localSymbolsHandler(in.stubHelper->dyldPrivate);

  for (Symbol *sym : symtab->getSymbols()) {
    if (!sym->isLive())
      continue;
    if (auto *defined = dyn_cast<Defined>(sym)) {
      if (!defined->includeInSymtab)
        continue;
      assert(defined->isExternal());
      if (defined->privateExtern)
        localSymbolsHandler(defined);
      else
        addSymbol(externalSymbols, defined);
    } else if (auto *dysym = dyn_cast<DylibSymbol>(sym)) {
      if (dysym->isReferenced())
        addSymbol(undefinedSymbols, sym);
    }
  }

  emitStabs();
  uint32_t symtabIndex = stabs.size();
  for (const SymtabEntry &entry :
       concat<SymtabEntry>(localSymbols, externalSymbols, undefinedSymbols)) {
    entry.sym->symtabIndex = symtabIndex++;
  }
}

uint32_t SymtabSection::getNumSymbols() const {
  return stabs.size() + localSymbols.size() + externalSymbols.size() +
         undefinedSymbols.size();
}

// This serves to hide (type-erase) the template parameter from SymtabSection.
template <class LP> class SymtabSectionImpl final : public SymtabSection {
public:
  SymtabSectionImpl(StringTableSection &stringTableSection)
      : SymtabSection(stringTableSection) {}
  uint64_t getRawSize() const override;
  void writeTo(uint8_t *buf) const override;
};

template <class LP> uint64_t SymtabSectionImpl<LP>::getRawSize() const {
  return getNumSymbols() * sizeof(typename LP::nlist);
}

template <class LP> void SymtabSectionImpl<LP>::writeTo(uint8_t *buf) const {
  auto *nList = reinterpret_cast<typename LP::nlist *>(buf);
  // Emit the stabs entries before the "real" symbols. We cannot emit them
  // after as that would render Symbol::symtabIndex inaccurate.
  for (const StabsEntry &entry : stabs) {
    nList->n_strx = entry.strx;
    nList->n_type = entry.type;
    nList->n_sect = entry.sect;
    nList->n_desc = entry.desc;
    nList->n_value = entry.value;
    ++nList;
  }

  for (const SymtabEntry &entry : concat<const SymtabEntry>(
           localSymbols, externalSymbols, undefinedSymbols)) {
    nList->n_strx = entry.strx;
    // TODO populate n_desc with more flags
    if (auto *defined = dyn_cast<Defined>(entry.sym)) {
      uint8_t scope = 0;
      if (defined->privateExtern) {
        // Private external -- dylib scoped symbol.
        // Promote to non-external at link time.
        scope = N_PEXT;
      } else if (defined->isExternal()) {
        // Normal global symbol.
        scope = N_EXT;
      } else {
        // TU-local symbol from localSymbols.
        scope = 0;
      }

      if (defined->isAbsolute()) {
        nList->n_type = scope | N_ABS;
        nList->n_sect = NO_SECT;
        nList->n_value = defined->value;
      } else {
        nList->n_type = scope | N_SECT;
        nList->n_sect = defined->isec()->parent->index;
        // For the N_SECT symbol type, n_value is the address of the symbol
        nList->n_value = defined->getVA();
      }
      nList->n_desc |= defined->isExternalWeakDef() ? N_WEAK_DEF : 0;
      nList->n_desc |=
          defined->referencedDynamically ? REFERENCED_DYNAMICALLY : 0;
    } else if (auto *dysym = dyn_cast<DylibSymbol>(entry.sym)) {
      uint16_t n_desc = nList->n_desc;
      int16_t ordinal = ordinalForDylibSymbol(*dysym);
      if (ordinal == BIND_SPECIAL_DYLIB_FLAT_LOOKUP)
        SET_LIBRARY_ORDINAL(n_desc, DYNAMIC_LOOKUP_ORDINAL);
      else if (ordinal == BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE)
        SET_LIBRARY_ORDINAL(n_desc, EXECUTABLE_ORDINAL);
      else {
        assert(ordinal > 0);
        SET_LIBRARY_ORDINAL(n_desc, static_cast<uint8_t>(ordinal));
      }

      nList->n_type = N_EXT;
      n_desc |= dysym->isWeakDef() ? N_WEAK_DEF : 0;
      n_desc |= dysym->isWeakRef() ? N_WEAK_REF : 0;
      nList->n_desc = n_desc;
    }
    ++nList;
  }
}

template <class LP>
SymtabSection *
macho::makeSymtabSection(StringTableSection &stringTableSection) {
  return make<SymtabSectionImpl<LP>>(stringTableSection);
}

IndirectSymtabSection::IndirectSymtabSection()
    : LinkEditSection(segment_names::linkEdit,
                      section_names::indirectSymbolTable) {}

uint32_t IndirectSymtabSection::getNumSymbols() const {
  uint32_t size = in.got->getEntries().size() +
                  in.tlvPointers->getEntries().size() +
                  in.stubs->getEntries().size();
  if (!config->emitChainedFixups)
    size += in.stubs->getEntries().size();
  return size;
}

bool IndirectSymtabSection::isNeeded() const {
  return in.got->isNeeded() || in.tlvPointers->isNeeded() ||
         in.stubs->isNeeded();
}

void IndirectSymtabSection::finalizeContents() {
  uint32_t off = 0;
  in.got->reserved1 = off;
  off += in.got->getEntries().size();
  in.tlvPointers->reserved1 = off;
  off += in.tlvPointers->getEntries().size();
  in.stubs->reserved1 = off;
  if (in.lazyPointers) {
    off += in.stubs->getEntries().size();
    in.lazyPointers->reserved1 = off;
  }
}

static uint32_t indirectValue(const Symbol *sym) {
  if (sym->symtabIndex == UINT32_MAX || !needsBinding(sym))
    return INDIRECT_SYMBOL_LOCAL;
  return sym->symtabIndex;
}

void IndirectSymtabSection::writeTo(uint8_t *buf) const {
  uint32_t off = 0;
  for (const Symbol *sym : in.got->getEntries()) {
    write32le(buf + off * sizeof(uint32_t), indirectValue(sym));
    ++off;
  }
  for (const Symbol *sym : in.tlvPointers->getEntries()) {
    write32le(buf + off * sizeof(uint32_t), indirectValue(sym));
    ++off;
  }
  for (const Symbol *sym : in.stubs->getEntries()) {
    write32le(buf + off * sizeof(uint32_t), indirectValue(sym));
    ++off;
  }

  if (in.lazyPointers) {
    // There is a 1:1 correspondence between stubs and LazyPointerSection
    // entries. But giving __stubs and __la_symbol_ptr the same reserved1
    // (the offset into the indirect symbol table) so that they both refer
    // to the same range of offsets confuses `strip`, so write the stubs
    // symbol table offsets a second time.
    for (const Symbol *sym : in.stubs->getEntries()) {
      write32le(buf + off * sizeof(uint32_t), indirectValue(sym));
      ++off;
    }
  }
}

StringTableSection::StringTableSection()
    : LinkEditSection(segment_names::linkEdit, section_names::stringTable) {}

uint32_t StringTableSection::addString(StringRef str) {
  uint32_t strx = size;
  strings.push_back(str); // TODO: consider deduplicating strings
  size += str.size() + 1; // account for null terminator
  return strx;
}

void StringTableSection::writeTo(uint8_t *buf) const {
  uint32_t off = 0;
  for (StringRef str : strings) {
    memcpy(buf + off, str.data(), str.size());
    off += str.size() + 1; // account for null terminator
  }
}

static_assert((CodeSignatureSection::blobHeadersSize % 8) == 0);
static_assert((CodeSignatureSection::fixedHeadersSize % 8) == 0);

CodeSignatureSection::CodeSignatureSection()
    : LinkEditSection(segment_names::linkEdit, section_names::codeSignature) {
  align = 16; // required by libstuff

  // XXX: This mimics LD64, where it uses the install-name as codesign
  // identifier, if available.
  if (!config->installName.empty())
    fileName = config->installName;
  else
    // FIXME: Consider using finalOutput instead of outputFile.
    fileName = config->outputFile;

  size_t slashIndex = fileName.rfind("/");
  if (slashIndex != std::string::npos)
    fileName = fileName.drop_front(slashIndex + 1);

  // NOTE: Any changes to these calculations should be repeated
  // in llvm-objcopy's MachOLayoutBuilder::layoutTail.
  allHeadersSize = alignTo<16>(fixedHeadersSize + fileName.size() + 1);
  fileNamePad = allHeadersSize - fixedHeadersSize - fileName.size();
}

uint32_t CodeSignatureSection::getBlockCount() const {
  return (fileOff + blockSize - 1) / blockSize;
}

uint64_t CodeSignatureSection::getRawSize() const {
  return allHeadersSize + getBlockCount() * hashSize;
}

void CodeSignatureSection::writeHashes(uint8_t *buf) const {
  // NOTE: Changes to this functionality should be repeated in llvm-objcopy's
  // MachOWriter::writeSignatureData.
  uint8_t *hashes = buf + fileOff + allHeadersSize;
  parallelFor(0, getBlockCount(), [&](size_t i) {
    sha256(buf + i * blockSize,
           std::min(static_cast<size_t>(fileOff - i * blockSize), blockSize),
           hashes + i * hashSize);
  });
#if defined(__APPLE__)
  // This is macOS-specific work-around and makes no sense for any
  // other host OS. See https://openradar.appspot.com/FB8914231
  //
  // The macOS kernel maintains a signature-verification cache to
  // quickly validate applications at time of execve(2).  The trouble
  // is that for the kernel creates the cache entry at the time of the
  // mmap(2) call, before we have a chance to write either the code to
  // sign or the signature header+hashes.  The fix is to invalidate
  // all cached data associated with the output file, thus discarding
  // the bogus prematurely-cached signature.
  msync(buf, fileOff + getSize(), MS_INVALIDATE);
#endif
}

void CodeSignatureSection::writeTo(uint8_t *buf) const {
  // NOTE: Changes to this functionality should be repeated in llvm-objcopy's
  // MachOWriter::writeSignatureData.
  uint32_t signatureSize = static_cast<uint32_t>(getSize());
  auto *superBlob = reinterpret_cast<CS_SuperBlob *>(buf);
  write32be(&superBlob->magic, CSMAGIC_EMBEDDED_SIGNATURE);
  write32be(&superBlob->length, signatureSize);
  write32be(&superBlob->count, 1);
  auto *blobIndex = reinterpret_cast<CS_BlobIndex *>(&superBlob[1]);
  write32be(&blobIndex->type, CSSLOT_CODEDIRECTORY);
  write32be(&blobIndex->offset, blobHeadersSize);
  auto *codeDirectory =
      reinterpret_cast<CS_CodeDirectory *>(buf + blobHeadersSize);
  write32be(&codeDirectory->magic, CSMAGIC_CODEDIRECTORY);
  write32be(&codeDirectory->length, signatureSize - blobHeadersSize);
  write32be(&codeDirectory->version, CS_SUPPORTSEXECSEG);
  write32be(&codeDirectory->flags, CS_ADHOC | CS_LINKER_SIGNED);
  write32be(&codeDirectory->hashOffset,
            sizeof(CS_CodeDirectory) + fileName.size() + fileNamePad);
  write32be(&codeDirectory->identOffset, sizeof(CS_CodeDirectory));
  codeDirectory->nSpecialSlots = 0;
  write32be(&codeDirectory->nCodeSlots, getBlockCount());
  write32be(&codeDirectory->codeLimit, fileOff);
  codeDirectory->hashSize = static_cast<uint8_t>(hashSize);
  codeDirectory->hashType = kSecCodeSignatureHashSHA256;
  codeDirectory->platform = 0;
  codeDirectory->pageSize = blockSizeShift;
  codeDirectory->spare2 = 0;
  codeDirectory->scatterOffset = 0;
  codeDirectory->teamOffset = 0;
  codeDirectory->spare3 = 0;
  codeDirectory->codeLimit64 = 0;
  OutputSegment *textSeg = getOrCreateOutputSegment(segment_names::text);
  write64be(&codeDirectory->execSegBase, textSeg->fileOff);
  write64be(&codeDirectory->execSegLimit, textSeg->fileSize);
  write64be(&codeDirectory->execSegFlags,
            config->outputType == MH_EXECUTE ? CS_EXECSEG_MAIN_BINARY : 0);
  auto *id = reinterpret_cast<char *>(&codeDirectory[1]);
  memcpy(id, fileName.begin(), fileName.size());
  memset(id + fileName.size(), 0, fileNamePad);
}

CStringSection::CStringSection(const char *name)
    : SyntheticSection(segment_names::text, name) {
  flags = S_CSTRING_LITERALS;
}

void CStringSection::addInput(CStringInputSection *isec) {
  isec->parent = this;
  inputs.push_back(isec);
  if (isec->align > align)
    align = isec->align;
}

void CStringSection::writeTo(uint8_t *buf) const {
  for (const CStringInputSection *isec : inputs) {
    for (const auto &[i, piece] : llvm::enumerate(isec->pieces)) {
      if (!piece.live)
        continue;
      StringRef string = isec->getStringRef(i);
      memcpy(buf + piece.outSecOff, string.data(), string.size());
    }
  }
}

void CStringSection::finalizeContents() {
  uint64_t offset = 0;
  for (CStringInputSection *isec : inputs) {
    for (const auto &[i, piece] : llvm::enumerate(isec->pieces)) {
      if (!piece.live)
        continue;
      // See comment above DeduplicatedCStringSection for how alignment is
      // handled.
      uint32_t pieceAlign = 1
                            << llvm::countr_zero(isec->align | piece.inSecOff);
      offset = alignToPowerOf2(offset, pieceAlign);
      piece.outSecOff = offset;
      isec->isFinal = true;
      StringRef string = isec->getStringRef(i);
      offset += string.size() + 1; // account for null terminator
    }
  }
  size = offset;
}

// Mergeable cstring literals are found under the __TEXT,__cstring section. In
// contrast to ELF, which puts strings that need different alignments into
// different sections, clang's Mach-O backend puts them all in one section.
// Strings that need to be aligned have the .p2align directive emitted before
// them, which simply translates into zero padding in the object file. In other
// words, we have to infer the desired alignment of these cstrings from their
// addresses.
//
// We differ slightly from ld64 in how we've chosen to align these cstrings.
// Both LLD and ld64 preserve the number of trailing zeros in each cstring's
// address in the input object files. When deduplicating identical cstrings,
// both linkers pick the cstring whose address has more trailing zeros, and
// preserve the alignment of that address in the final binary. However, ld64
// goes a step further and also preserves the offset of the cstring from the
// last section-aligned address.  I.e. if a cstring is at offset 18 in the
// input, with a section alignment of 16, then both LLD and ld64 will ensure the
// final address is 2-byte aligned (since 18 == 16 + 2). But ld64 will also
// ensure that the final address is of the form 16 * k + 2 for some k.
//
// Note that ld64's heuristic means that a dedup'ed cstring's final address is
// dependent on the order of the input object files. E.g. if in addition to the
// cstring at offset 18 above, we have a duplicate one in another file with a
// `.cstring` section alignment of 2 and an offset of zero, then ld64 will pick
// the cstring from the object file earlier on the command line (since both have
// the same number of trailing zeros in their address). So the final cstring may
// either be at some address `16 * k + 2` or at some address `2 * k`.
//
// I've opted not to follow this behavior primarily for implementation
// simplicity, and secondarily to save a few more bytes. It's not clear to me
// that preserving the section alignment + offset is ever necessary, and there
// are many cases that are clearly redundant. In particular, if an x86_64 object
// file contains some strings that are accessed via SIMD instructions, then the
// .cstring section in the object file will be 16-byte-aligned (since SIMD
// requires its operand addresses to be 16-byte aligned). However, there will
// typically also be other cstrings in the same file that aren't used via SIMD
// and don't need this alignment. They will be emitted at some arbitrary address
// `A`, but ld64 will treat them as being 16-byte aligned with an offset of `16
// % A`.
void DeduplicatedCStringSection::finalizeContents() {
  // Find the largest alignment required for each string.
  for (const CStringInputSection *isec : inputs) {
    for (const auto &[i, piece] : llvm::enumerate(isec->pieces)) {
      if (!piece.live)
        continue;
      auto s = isec->getCachedHashStringRef(i);
      assert(isec->align != 0);
      uint8_t trailingZeros = llvm::countr_zero(isec->align | piece.inSecOff);
      auto it = stringOffsetMap.insert(
          std::make_pair(s, StringOffset(trailingZeros)));
      if (!it.second && it.first->second.trailingZeros < trailingZeros)
        it.first->second.trailingZeros = trailingZeros;
    }
  }

  // Assign an offset for each string and save it to the corresponding
  // StringPieces for easy access.
  for (CStringInputSection *isec : inputs) {
    for (const auto &[i, piece] : llvm::enumerate(isec->pieces)) {
      if (!piece.live)
        continue;
      auto s = isec->getCachedHashStringRef(i);
      auto it = stringOffsetMap.find(s);
      assert(it != stringOffsetMap.end());
      StringOffset &offsetInfo = it->second;
      if (offsetInfo.outSecOff == UINT64_MAX) {
        offsetInfo.outSecOff =
            alignToPowerOf2(size, 1ULL << offsetInfo.trailingZeros);
        size =
            offsetInfo.outSecOff + s.size() + 1; // account for null terminator
      }
      piece.outSecOff = offsetInfo.outSecOff;
    }
    isec->isFinal = true;
  }
}

void DeduplicatedCStringSection::writeTo(uint8_t *buf) const {
  for (const auto &p : stringOffsetMap) {
    StringRef data = p.first.val();
    uint64_t off = p.second.outSecOff;
    if (!data.empty())
      memcpy(buf + off, data.data(), data.size());
  }
}

DeduplicatedCStringSection::StringOffset
DeduplicatedCStringSection::getStringOffset(StringRef str) const {
  // StringPiece uses 31 bits to store the hashes, so we replicate that
  uint32_t hash = xxh3_64bits(str) & 0x7fffffff;
  auto offset = stringOffsetMap.find(CachedHashStringRef(str, hash));
  assert(offset != stringOffsetMap.end() &&
         "Looked-up strings should always exist in section");
  return offset->second;
}

// This section is actually emitted as __TEXT,__const by ld64, but clang may
// emit input sections of that name, and LLD doesn't currently support mixing
// synthetic and concat-type OutputSections. To work around this, I've given
// our merged-literals section a different name.
WordLiteralSection::WordLiteralSection()
    : SyntheticSection(segment_names::text, section_names::literals) {
  align = 16;
}

void WordLiteralSection::addInput(WordLiteralInputSection *isec) {
  isec->parent = this;
  inputs.push_back(isec);
}

void WordLiteralSection::finalizeContents() {
  for (WordLiteralInputSection *isec : inputs) {
    // We do all processing of the InputSection here, so it will be effectively
    // finalized.
    isec->isFinal = true;
    const uint8_t *buf = isec->data.data();
    switch (sectionType(isec->getFlags())) {
    case S_4BYTE_LITERALS: {
      for (size_t off = 0, e = isec->data.size(); off < e; off += 4) {
        if (!isec->isLive(off))
          continue;
        uint32_t value = *reinterpret_cast<const uint32_t *>(buf + off);
        literal4Map.emplace(value, literal4Map.size());
      }
      break;
    }
    case S_8BYTE_LITERALS: {
      for (size_t off = 0, e = isec->data.size(); off < e; off += 8) {
        if (!isec->isLive(off))
          continue;
        uint64_t value = *reinterpret_cast<const uint64_t *>(buf + off);
        literal8Map.emplace(value, literal8Map.size());
      }
      break;
    }
    case S_16BYTE_LITERALS: {
      for (size_t off = 0, e = isec->data.size(); off < e; off += 16) {
        if (!isec->isLive(off))
          continue;
        UInt128 value = *reinterpret_cast<const UInt128 *>(buf + off);
        literal16Map.emplace(value, literal16Map.size());
      }
      break;
    }
    default:
      llvm_unreachable("invalid literal section type");
    }
  }
}

void WordLiteralSection::writeTo(uint8_t *buf) const {
  // Note that we don't attempt to do any endianness conversion in addInput(),
  // so we don't do it here either -- just write out the original value,
  // byte-for-byte.
  for (const auto &p : literal16Map)
    memcpy(buf + p.second * 16, &p.first, 16);
  buf += literal16Map.size() * 16;

  for (const auto &p : literal8Map)
    memcpy(buf + p.second * 8, &p.first, 8);
  buf += literal8Map.size() * 8;

  for (const auto &p : literal4Map)
    memcpy(buf + p.second * 4, &p.first, 4);
}

ObjCImageInfoSection::ObjCImageInfoSection()
    : SyntheticSection(segment_names::data, section_names::objCImageInfo) {}

ObjCImageInfoSection::ImageInfo
ObjCImageInfoSection::parseImageInfo(const InputFile *file) {
  ImageInfo info;
  ArrayRef<uint8_t> data = file->objCImageInfo;
  // The image info struct has the following layout:
  // struct {
  //   uint32_t version;
  //   uint32_t flags;
  // };
  if (data.size() < 8) {
    warn(toString(file) + ": invalid __objc_imageinfo size");
    return info;
  }

  auto *buf = reinterpret_cast<const uint32_t *>(data.data());
  if (read32le(buf) != 0) {
    warn(toString(file) + ": invalid __objc_imageinfo version");
    return info;
  }

  uint32_t flags = read32le(buf + 1);
  info.swiftVersion = (flags >> 8) & 0xff;
  info.hasCategoryClassProperties = flags & 0x40;
  return info;
}

static std::string swiftVersionString(uint8_t version) {
  switch (version) {
    case 1:
      return "1.0";
    case 2:
      return "1.1";
    case 3:
      return "2.0";
    case 4:
      return "3.0";
    case 5:
      return "4.0";
    default:
      return ("0x" + Twine::utohexstr(version)).str();
  }
}

// Validate each object file's __objc_imageinfo and use them to generate the
// image info for the output binary. Only two pieces of info are relevant:
// 1. The Swift version (should be identical across inputs)
// 2. `bool hasCategoryClassProperties` (true only if true for all inputs)
void ObjCImageInfoSection::finalizeContents() {
  assert(files.size() != 0); // should have already been checked via isNeeded()

  info.hasCategoryClassProperties = true;
  const InputFile *firstFile;
  for (const InputFile *file : files) {
    ImageInfo inputInfo = parseImageInfo(file);
    info.hasCategoryClassProperties &= inputInfo.hasCategoryClassProperties;

    // swiftVersion 0 means no Swift is present, so no version checking required
    if (inputInfo.swiftVersion == 0)
      continue;

    if (info.swiftVersion != 0 && info.swiftVersion != inputInfo.swiftVersion) {
      error("Swift version mismatch: " + toString(firstFile) + " has version " +
            swiftVersionString(info.swiftVersion) + " but " + toString(file) +
            " has version " + swiftVersionString(inputInfo.swiftVersion));
    } else {
      info.swiftVersion = inputInfo.swiftVersion;
      firstFile = file;
    }
  }
}

void ObjCImageInfoSection::writeTo(uint8_t *buf) const {
  uint32_t flags = info.hasCategoryClassProperties ? 0x40 : 0x0;
  flags |= info.swiftVersion << 8;
  write32le(buf + 4, flags);
}

InitOffsetsSection::InitOffsetsSection()
    : SyntheticSection(segment_names::text, section_names::initOffsets) {
  flags = S_INIT_FUNC_OFFSETS;
  align = 4; // This section contains 32-bit integers.
}

uint64_t InitOffsetsSection::getSize() const {
  size_t count = 0;
  for (const ConcatInputSection *isec : sections)
    count += isec->relocs.size();
  return count * sizeof(uint32_t);
}

void InitOffsetsSection::writeTo(uint8_t *buf) const {
  // FIXME: Add function specified by -init when that argument is implemented.
  for (ConcatInputSection *isec : sections) {
    for (const Reloc &rel : isec->relocs) {
      const Symbol *referent = rel.referent.dyn_cast<Symbol *>();
      assert(referent && "section relocation should have been rejected");
      uint64_t offset = referent->getVA() - in.header->addr;
      // FIXME: Can we handle this gracefully?
      if (offset > UINT32_MAX)
        fatal(isec->getLocation(rel.offset) + ": offset to initializer " +
              referent->getName() + " (" + utohexstr(offset) +
              ") does not fit in 32 bits");

      // Entries need to be added in the order they appear in the section, but
      // relocations aren't guaranteed to be sorted.
      size_t index = rel.offset >> target->p2WordSize;
      write32le(&buf[index * sizeof(uint32_t)], offset);
    }
    buf += isec->relocs.size() * sizeof(uint32_t);
  }
}

// The inputs are __mod_init_func sections, which contain pointers to
// initializer functions, therefore all relocations should be of the UNSIGNED
// type. InitOffsetsSection stores offsets, so if the initializer's address is
// not known at link time, stub-indirection has to be used.
void InitOffsetsSection::setUp() {
  for (const ConcatInputSection *isec : sections) {
    for (const Reloc &rel : isec->relocs) {
      RelocAttrs attrs = target->getRelocAttrs(rel.type);
      if (!attrs.hasAttr(RelocAttrBits::UNSIGNED))
        error(isec->getLocation(rel.offset) +
              ": unsupported relocation type: " + attrs.name);
      if (rel.addend != 0)
        error(isec->getLocation(rel.offset) +
              ": relocation addend is not representable in __init_offsets");
      if (rel.referent.is<InputSection *>())
        error(isec->getLocation(rel.offset) +
              ": unexpected section relocation");

      Symbol *sym = rel.referent.dyn_cast<Symbol *>();
      if (auto *undefined = dyn_cast<Undefined>(sym))
        treatUndefinedSymbol(*undefined, isec, rel.offset);
      if (needsBinding(sym))
        in.stubs->addEntry(sym);
    }
  }
}

ObjCMethListSection::ObjCMethListSection()
    : SyntheticSection(segment_names::text, section_names::objcMethList) {
  flags = S_ATTR_NO_DEAD_STRIP;
  align = relativeOffsetSize;
}

// Go through all input method lists and ensure that we have selrefs for all
// their method names. The selrefs will be needed later by ::writeTo. We need to
// create them early on here to ensure they are processed correctly by the lld
// pipeline.
void ObjCMethListSection::setUp() {
  for (const ConcatInputSection *isec : inputs) {
    uint32_t structSizeAndFlags = 0, structCount = 0;
    readMethodListHeader(isec->data.data(), structSizeAndFlags, structCount);
    uint32_t originalStructSize = structSizeAndFlags & structSizeMask;
    // Method name is immediately after header
    uint32_t methodNameOff = methodListHeaderSize;

    // Loop through all methods, and ensure a selref for each of them exists.
    while (methodNameOff < isec->data.size()) {
      const Reloc *reloc = isec->getRelocAt(methodNameOff);
      assert(reloc && "Relocation expected at method list name slot");
      auto *def = dyn_cast_or_null<Defined>(reloc->referent.get<Symbol *>());
      assert(def && "Expected valid Defined at method list name slot");
      auto *cisec = cast<CStringInputSection>(def->isec());
      assert(cisec && "Expected method name to be in a CStringInputSection");
      auto methname = cisec->getStringRefAtOffset(def->value);
      if (!ObjCSelRefsHelper::getSelRef(methname))
        ObjCSelRefsHelper::makeSelRef(methname);

      // Jump to method name offset in next struct
      methodNameOff += originalStructSize;
    }
  }
}

// Calculate section size and final offsets for where InputSection's need to be
// written.
void ObjCMethListSection::finalize() {
  // sectionSize will be the total size of the __objc_methlist section
  sectionSize = 0;
  for (ConcatInputSection *isec : inputs) {
    // We can also use sectionSize as write offset for isec
    assert(sectionSize == alignToPowerOf2(sectionSize, relativeOffsetSize) &&
           "expected __objc_methlist to be aligned by default with the "
           "required section alignment");
    isec->outSecOff = sectionSize;

    isec->isFinal = true;
    uint32_t relativeListSize =
        computeRelativeMethodListSize(isec->data.size());
    sectionSize += relativeListSize;

    // If encoding the method list in relative offset format shrinks the size,
    // then we also need to adjust symbol sizes to match the new size. Note that
    // on 32bit platforms the size of the method list will remain the same when
    // encoded in relative offset format.
    if (relativeListSize != isec->data.size()) {
      for (Symbol *sym : isec->symbols) {
        assert(isa<Defined>(sym) &&
               "Unexpected undefined symbol in ObjC method list");
        auto *def = cast<Defined>(sym);
        // There can be 0-size symbols, check if this is the case and ignore
        // them.
        if (def->size) {
          assert(
              def->size == isec->data.size() &&
              "Invalid ObjC method list symbol size: expected symbol size to "
              "match isec size");
          def->size = relativeListSize;
        }
      }
    }
  }
}

void ObjCMethListSection::writeTo(uint8_t *bufStart) const {
  uint8_t *buf = bufStart;
  for (const ConcatInputSection *isec : inputs) {
    assert(buf - bufStart == long(isec->outSecOff) &&
           "Writing at unexpected offset");
    uint32_t writtenSize = writeRelativeMethodList(isec, buf);
    buf += writtenSize;
  }
  assert(buf - bufStart == sectionSize &&
         "Written size does not match expected section size");
}

// Check if an InputSection is a method list. To do this we scan the
// InputSection for any symbols who's names match the patterns we expect clang
// to generate for method lists.
bool ObjCMethListSection::isMethodList(const ConcatInputSection *isec) {
  const char *symPrefixes[] = {objc::symbol_names::classMethods,
                               objc::symbol_names::instanceMethods,
                               objc::symbol_names::categoryInstanceMethods,
                               objc::symbol_names::categoryClassMethods};
  if (!isec)
    return false;
  for (const Symbol *sym : isec->symbols) {
    auto *def = dyn_cast_or_null<Defined>(sym);
    if (!def)
      continue;
    for (const char *prefix : symPrefixes) {
      if (def->getName().starts_with(prefix)) {
        assert(def->size == isec->data.size() &&
               "Invalid ObjC method list symbol size: expected symbol size to "
               "match isec size");
        assert(def->value == 0 &&
               "Offset of ObjC method list symbol must be 0");
        return true;
      }
    }
  }

  return false;
}

// Encode a single relative offset value. The input is the data/symbol at
// (&isec->data[inSecOff]). The output is written to (&buf[outSecOff]).
// 'createSelRef' indicates that we should not directly use the specified
// symbol, but instead get the selRef for the symbol and use that instead.
void ObjCMethListSection::writeRelativeOffsetForIsec(
    const ConcatInputSection *isec, uint8_t *buf, uint32_t &inSecOff,
    uint32_t &outSecOff, bool useSelRef) const {
  const Reloc *reloc = isec->getRelocAt(inSecOff);
  assert(reloc && "Relocation expected at __objc_methlist Offset");
  auto *def = dyn_cast_or_null<Defined>(reloc->referent.get<Symbol *>());
  assert(def && "Expected all syms in __objc_methlist to be defined");
  uint32_t symVA = def->getVA();

  if (useSelRef) {
    auto *cisec = cast<CStringInputSection>(def->isec());
    auto methname = cisec->getStringRefAtOffset(def->value);
    ConcatInputSection *selRef = ObjCSelRefsHelper::getSelRef(methname);
    assert(selRef && "Expected all selector names to already be already be "
                     "present in __objc_selrefs");
    symVA = selRef->getVA();
    assert(selRef->data.size() == sizeof(target->wordSize) &&
           "Expected one selref per ConcatInputSection");
  }

  uint32_t currentVA = isec->getVA() + outSecOff;
  uint32_t delta = symVA - currentVA;
  write32le(buf + outSecOff, delta);

  // Move one pointer forward in the absolute method list
  inSecOff += target->wordSize;
  // Move one relative offset forward in the relative method list (32 bits)
  outSecOff += relativeOffsetSize;
}

// Write a relative method list to buf, return the size of the written
// information
uint32_t
ObjCMethListSection::writeRelativeMethodList(const ConcatInputSection *isec,
                                             uint8_t *buf) const {
  // Copy over the header, and add the "this is a relative method list" magic
  // value flag
  uint32_t structSizeAndFlags = 0, structCount = 0;
  readMethodListHeader(isec->data.data(), structSizeAndFlags, structCount);
  // Set the struct size for the relative method list
  uint32_t relativeStructSizeAndFlags =
      (relativeOffsetSize * pointersPerStruct) & structSizeMask;
  // Carry over the old flags from the input struct
  relativeStructSizeAndFlags |= structSizeAndFlags & structFlagsMask;
  // Set the relative method list flag
  relativeStructSizeAndFlags |= relMethodHeaderFlag;

  writeMethodListHeader(buf, relativeStructSizeAndFlags, structCount);

  assert(methodListHeaderSize +
                 (structCount * pointersPerStruct * target->wordSize) ==
             isec->data.size() &&
         "Invalid computed ObjC method list size");

  uint32_t inSecOff = methodListHeaderSize;
  uint32_t outSecOff = methodListHeaderSize;

  // Go through the method list and encode input absolute pointers as relative
  // offsets. writeRelativeOffsetForIsec will be incrementing inSecOff and
  // outSecOff
  for (uint32_t i = 0; i < structCount; i++) {
    // Write the name of the method
    writeRelativeOffsetForIsec(isec, buf, inSecOff, outSecOff, true);
    // Write the type of the method
    writeRelativeOffsetForIsec(isec, buf, inSecOff, outSecOff, false);
    // Write reference to the selector of the method
    writeRelativeOffsetForIsec(isec, buf, inSecOff, outSecOff, false);
  }

  // Expecting to have read all the data in the isec
  assert(inSecOff == isec->data.size() &&
         "Invalid actual ObjC method list size");
  assert(
      outSecOff == computeRelativeMethodListSize(inSecOff) &&
      "Mismatch between input & output size when writing relative method list");
  return outSecOff;
}

// Given the size of an ObjC method list InputSection, return the size of the
// method list when encoded in relative offsets format. We can do this without
// decoding the actual data, as it can be directly inferred from the size of the
// isec.
uint32_t ObjCMethListSection::computeRelativeMethodListSize(
    uint32_t absoluteMethodListSize) const {
  uint32_t oldPointersSize = absoluteMethodListSize - methodListHeaderSize;
  uint32_t pointerCount = oldPointersSize / target->wordSize;
  assert(((pointerCount % pointersPerStruct) == 0) &&
         "__objc_methlist expects method lists to have multiple-of-3 pointers");

  uint32_t newPointersSize = pointerCount * relativeOffsetSize;
  uint32_t newTotalSize = methodListHeaderSize + newPointersSize;

  assert((newTotalSize <= absoluteMethodListSize) &&
         "Expected relative method list size to be smaller or equal than "
         "original size");
  return newTotalSize;
}

// Read a method list header from buf
void ObjCMethListSection::readMethodListHeader(const uint8_t *buf,
                                               uint32_t &structSizeAndFlags,
                                               uint32_t &structCount) const {
  structSizeAndFlags = read32le(buf);
  structCount = read32le(buf + sizeof(uint32_t));
}

// Write a method list header to buf
void ObjCMethListSection::writeMethodListHeader(uint8_t *buf,
                                                uint32_t structSizeAndFlags,
                                                uint32_t structCount) const {
  write32le(buf, structSizeAndFlags);
  write32le(buf + sizeof(structSizeAndFlags), structCount);
}

void macho::createSyntheticSymbols() {
  auto addHeaderSymbol = [](const char *name) {
    symtab->addSynthetic(name, in.header->isec, /*value=*/0,
                         /*isPrivateExtern=*/true, /*includeInSymtab=*/false,
                         /*referencedDynamically=*/false);
  };

  switch (config->outputType) {
    // FIXME: Assign the right address value for these symbols
    // (rather than 0). But we need to do that after assignAddresses().
  case MH_EXECUTE:
    // If linking PIE, __mh_execute_header is a defined symbol in
    //  __TEXT, __text)
    // Otherwise, it's an absolute symbol.
    if (config->isPic)
      symtab->addSynthetic("__mh_execute_header", in.header->isec, /*value=*/0,
                           /*isPrivateExtern=*/false, /*includeInSymtab=*/true,
                           /*referencedDynamically=*/true);
    else
      symtab->addSynthetic("__mh_execute_header", /*isec=*/nullptr, /*value=*/0,
                           /*isPrivateExtern=*/false, /*includeInSymtab=*/true,
                           /*referencedDynamically=*/true);
    break;

    // The following symbols are N_SECT symbols, even though the header is not
    // part of any section and that they are private to the bundle/dylib/object
    // they are part of.
  case MH_BUNDLE:
    addHeaderSymbol("__mh_bundle_header");
    break;
  case MH_DYLIB:
    addHeaderSymbol("__mh_dylib_header");
    break;
  case MH_DYLINKER:
    addHeaderSymbol("__mh_dylinker_header");
    break;
  case MH_OBJECT:
    addHeaderSymbol("__mh_object_header");
    break;
  default:
    llvm_unreachable("unexpected outputType");
    break;
  }

  // The Itanium C++ ABI requires dylibs to pass a pointer to __cxa_atexit
  // which does e.g. cleanup of static global variables. The ABI document
  // says that the pointer can point to any address in one of the dylib's
  // segments, but in practice ld64 seems to set it to point to the header,
  // so that's what's implemented here.
  addHeaderSymbol("___dso_handle");
}

ChainedFixupsSection::ChainedFixupsSection()
    : LinkEditSection(segment_names::linkEdit, section_names::chainFixups) {}

bool ChainedFixupsSection::isNeeded() const {
  assert(config->emitChainedFixups);
  // dyld always expects LC_DYLD_CHAINED_FIXUPS to point to a valid
  // dyld_chained_fixups_header, so we create this section even if there aren't
  // any fixups.
  return true;
}

void ChainedFixupsSection::addBinding(const Symbol *sym,
                                      const InputSection *isec, uint64_t offset,
                                      int64_t addend) {
  locations.emplace_back(isec, offset);
  int64_t outlineAddend = (addend < 0 || addend > 0xFF) ? addend : 0;
  auto [it, inserted] = bindings.insert(
      {{sym, outlineAddend}, static_cast<uint32_t>(bindings.size())});

  if (inserted) {
    symtabSize += sym->getName().size() + 1;
    hasWeakBind = hasWeakBind || needsWeakBind(*sym);
    if (!isInt<23>(outlineAddend))
      needsLargeAddend = true;
    else if (outlineAddend != 0)
      needsAddend = true;
  }
}

std::pair<uint32_t, uint8_t>
ChainedFixupsSection::getBinding(const Symbol *sym, int64_t addend) const {
  int64_t outlineAddend = (addend < 0 || addend > 0xFF) ? addend : 0;
  auto it = bindings.find({sym, outlineAddend});
  assert(it != bindings.end() && "binding not found in the imports table");
  if (outlineAddend == 0)
    return {it->second, addend};
  return {it->second, 0};
}

static size_t writeImport(uint8_t *buf, int format, int16_t libOrdinal,
                          bool weakRef, uint32_t nameOffset, int64_t addend) {
  switch (format) {
  case DYLD_CHAINED_IMPORT: {
    auto *import = reinterpret_cast<dyld_chained_import *>(buf);
    import->lib_ordinal = libOrdinal;
    import->weak_import = weakRef;
    import->name_offset = nameOffset;
    return sizeof(dyld_chained_import);
  }
  case DYLD_CHAINED_IMPORT_ADDEND: {
    auto *import = reinterpret_cast<dyld_chained_import_addend *>(buf);
    import->lib_ordinal = libOrdinal;
    import->weak_import = weakRef;
    import->name_offset = nameOffset;
    import->addend = addend;
    return sizeof(dyld_chained_import_addend);
  }
  case DYLD_CHAINED_IMPORT_ADDEND64: {
    auto *import = reinterpret_cast<dyld_chained_import_addend64 *>(buf);
    import->lib_ordinal = libOrdinal;
    import->weak_import = weakRef;
    import->name_offset = nameOffset;
    import->addend = addend;
    return sizeof(dyld_chained_import_addend64);
  }
  default:
    llvm_unreachable("Unknown import format");
  }
}

size_t ChainedFixupsSection::SegmentInfo::getSize() const {
  assert(pageStarts.size() > 0 && "SegmentInfo for segment with no fixups?");
  return alignTo<8>(sizeof(dyld_chained_starts_in_segment) +
                    pageStarts.back().first * sizeof(uint16_t));
}

size_t ChainedFixupsSection::SegmentInfo::writeTo(uint8_t *buf) const {
  auto *segInfo = reinterpret_cast<dyld_chained_starts_in_segment *>(buf);
  segInfo->size = getSize();
  segInfo->page_size = target->getPageSize();
  // FIXME: Use DYLD_CHAINED_PTR_64_OFFSET on newer OS versions.
  segInfo->pointer_format = DYLD_CHAINED_PTR_64;
  segInfo->segment_offset = oseg->addr - in.header->addr;
  segInfo->max_valid_pointer = 0; // not used on 64-bit
  segInfo->page_count = pageStarts.back().first + 1;

  uint16_t *starts = segInfo->page_start;
  for (size_t i = 0; i < segInfo->page_count; ++i)
    starts[i] = DYLD_CHAINED_PTR_START_NONE;

  for (auto [pageIdx, startAddr] : pageStarts)
    starts[pageIdx] = startAddr;
  return segInfo->size;
}

static size_t importEntrySize(int format) {
  switch (format) {
  case DYLD_CHAINED_IMPORT:
    return sizeof(dyld_chained_import);
  case DYLD_CHAINED_IMPORT_ADDEND:
    return sizeof(dyld_chained_import_addend);
  case DYLD_CHAINED_IMPORT_ADDEND64:
    return sizeof(dyld_chained_import_addend64);
  default:
    llvm_unreachable("Unknown import format");
  }
}

// This is step 3 of the algorithm described in the class comment of
// ChainedFixupsSection.
//
// LC_DYLD_CHAINED_FIXUPS data consists of (in this order):
// * A dyld_chained_fixups_header
// * A dyld_chained_starts_in_image
// * One dyld_chained_starts_in_segment per segment
// * List of all imports (dyld_chained_import, dyld_chained_import_addend, or
//   dyld_chained_import_addend64)
// * Names of imported symbols
void ChainedFixupsSection::writeTo(uint8_t *buf) const {
  auto *header = reinterpret_cast<dyld_chained_fixups_header *>(buf);
  header->fixups_version = 0;
  header->imports_count = bindings.size();
  header->imports_format = importFormat;
  header->symbols_format = 0;

  buf += alignTo<8>(sizeof(*header));

  auto curOffset = [&buf, &header]() -> uint32_t {
    return buf - reinterpret_cast<uint8_t *>(header);
  };

  header->starts_offset = curOffset();

  auto *imageInfo = reinterpret_cast<dyld_chained_starts_in_image *>(buf);
  imageInfo->seg_count = outputSegments.size();
  uint32_t *segStarts = imageInfo->seg_info_offset;

  // dyld_chained_starts_in_image ends in a flexible array member containing an
  // uint32_t for each segment. Leave room for it, and fill it via segStarts.
  buf += alignTo<8>(offsetof(dyld_chained_starts_in_image, seg_info_offset) +
                    outputSegments.size() * sizeof(uint32_t));

  // Initialize all offsets to 0, which indicates that the segment does not have
  // fixups. Those that do have them will be filled in below.
  for (size_t i = 0; i < outputSegments.size(); ++i)
    segStarts[i] = 0;

  for (const SegmentInfo &seg : fixupSegments) {
    segStarts[seg.oseg->index] = curOffset() - header->starts_offset;
    buf += seg.writeTo(buf);
  }

  // Write imports table.
  header->imports_offset = curOffset();
  uint64_t nameOffset = 0;
  for (auto [import, idx] : bindings) {
    const Symbol &sym = *import.first;
    buf += writeImport(buf, importFormat, ordinalForSymbol(sym),
                       sym.isWeakRef(), nameOffset, import.second);
    nameOffset += sym.getName().size() + 1;
  }

  // Write imported symbol names.
  header->symbols_offset = curOffset();
  for (auto [import, idx] : bindings) {
    StringRef name = import.first->getName();
    memcpy(buf, name.data(), name.size());
    buf += name.size() + 1; // account for null terminator
  }

  assert(curOffset() == getRawSize());
}

// This is step 2 of the algorithm described in the class comment of
// ChainedFixupsSection.
void ChainedFixupsSection::finalizeContents() {
  assert(target->wordSize == 8 && "Only 64-bit platforms are supported");
  assert(config->emitChainedFixups);

  if (!isUInt<32>(symtabSize))
    error("cannot encode chained fixups: imported symbols table size " +
          Twine(symtabSize) + " exceeds 4 GiB");

  bool needsLargeOrdinal = any_of(bindings, [](const auto &p) {
    // 0xF1 - 0xFF are reserved for special ordinals in the 8-bit encoding.
    return ordinalForSymbol(*p.first.first) > 0xF0;
  });

  if (needsLargeAddend || !isUInt<23>(symtabSize) || needsLargeOrdinal)
    importFormat = DYLD_CHAINED_IMPORT_ADDEND64;
  else if (needsAddend)
    importFormat = DYLD_CHAINED_IMPORT_ADDEND;
  else
    importFormat = DYLD_CHAINED_IMPORT;

  for (Location &loc : locations)
    loc.offset =
        loc.isec->parent->getSegmentOffset() + loc.isec->getOffset(loc.offset);

  llvm::sort(locations, [](const Location &a, const Location &b) {
    const OutputSegment *segA = a.isec->parent->parent;
    const OutputSegment *segB = b.isec->parent->parent;
    if (segA == segB)
      return a.offset < b.offset;
    return segA->addr < segB->addr;
  });

  auto sameSegment = [](const Location &a, const Location &b) {
    return a.isec->parent->parent == b.isec->parent->parent;
  };

  const uint64_t pageSize = target->getPageSize();
  for (size_t i = 0, count = locations.size(); i < count;) {
    const Location &firstLoc = locations[i];
    fixupSegments.emplace_back(firstLoc.isec->parent->parent);
    while (i < count && sameSegment(locations[i], firstLoc)) {
      uint32_t pageIdx = locations[i].offset / pageSize;
      fixupSegments.back().pageStarts.emplace_back(
          pageIdx, locations[i].offset % pageSize);
      ++i;
      while (i < count && sameSegment(locations[i], firstLoc) &&
             locations[i].offset / pageSize == pageIdx)
        ++i;
    }
  }

  // Compute expected encoded size.
  size = alignTo<8>(sizeof(dyld_chained_fixups_header));
  size += alignTo<8>(offsetof(dyld_chained_starts_in_image, seg_info_offset) +
                     outputSegments.size() * sizeof(uint32_t));
  for (const SegmentInfo &seg : fixupSegments)
    size += seg.getSize();
  size += importEntrySize(importFormat) * bindings.size();
  size += symtabSize;
}

template SymtabSection *macho::makeSymtabSection<LP64>(StringTableSection &);
template SymtabSection *macho::makeSymtabSection<ILP32>(StringTableSection &);
