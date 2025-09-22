//===- Relocations.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_RELOCATIONS_H
#define LLD_ELF_RELOCATIONS_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Object/ELFTypes.h"
#include <vector>

namespace lld::elf {
class Symbol;
class InputSection;
class InputSectionBase;
class OutputSection;
class SectionBase;

// Represents a relocation type, such as R_X86_64_PC32 or R_ARM_THM_CALL.
using RelType = uint32_t;
using JumpModType = uint32_t;

// List of target-independent relocation types. Relocations read
// from files are converted to these types so that the main code
// doesn't have to know about architecture-specific details.
enum RelExpr {
  R_ABS,
  R_ADDEND,
  R_DTPREL,
  R_GOT,
  R_GOT_OFF,
  R_GOT_PC,
  R_GOTONLY_PC,
  R_GOTPLTONLY_PC,
  R_GOTPLT,
  R_GOTPLTREL,
  R_GOTREL,
  R_GOTPLT_GOTREL,
  R_GOTPLT_PC,
  R_NONE,
  R_PC,
  R_PLT,
  R_PLT_PC,
  R_PLT_GOTPLT,
  R_PLT_GOTREL,
  R_RELAX_HINT,
  R_RELAX_GOT_PC,
  R_RELAX_GOT_PC_NOPIC,
  R_RELAX_TLS_GD_TO_IE,
  R_RELAX_TLS_GD_TO_IE_ABS,
  R_RELAX_TLS_GD_TO_IE_GOT_OFF,
  R_RELAX_TLS_GD_TO_IE_GOTPLT,
  R_RELAX_TLS_GD_TO_LE,
  R_RELAX_TLS_GD_TO_LE_NEG,
  R_RELAX_TLS_IE_TO_LE,
  R_RELAX_TLS_LD_TO_LE,
  R_RELAX_TLS_LD_TO_LE_ABS,
  R_SIZE,
  R_TPREL,
  R_TPREL_NEG,
  R_TLSDESC,
  R_TLSDESC_CALL,
  R_TLSDESC_PC,
  R_TLSDESC_GOTPLT,
  R_TLSGD_GOT,
  R_TLSGD_GOTPLT,
  R_TLSGD_PC,
  R_TLSIE_HINT,
  R_TLSLD_GOT,
  R_TLSLD_GOTPLT,
  R_TLSLD_GOT_OFF,
  R_TLSLD_HINT,
  R_TLSLD_PC,

  // The following is abstract relocation types used for only one target.
  //
  // Even though RelExpr is intended to be a target-neutral representation
  // of a relocation type, there are some relocations whose semantics are
  // unique to a target. Such relocation are marked with R_<TARGET_NAME>.
  R_AARCH64_GOT_PAGE_PC,
  R_AARCH64_GOT_PAGE,
  R_AARCH64_PAGE_PC,
  R_AARCH64_RELAX_TLS_GD_TO_IE_PAGE_PC,
  R_AARCH64_TLSDESC_PAGE,
  R_AARCH64_AUTH,
  R_ARM_PCA,
  R_ARM_SBREL,
  R_MIPS_GOTREL,
  R_MIPS_GOT_GP,
  R_MIPS_GOT_GP_PC,
  R_MIPS_GOT_LOCAL_PAGE,
  R_MIPS_GOT_OFF,
  R_MIPS_GOT_OFF32,
  R_MIPS_TLSGD,
  R_MIPS_TLSLD,
  R_PPC32_PLTREL,
  R_PPC64_CALL,
  R_PPC64_CALL_PLT,
  R_PPC64_RELAX_TOC,
  R_PPC64_TOCBASE,
  R_PPC64_RELAX_GOT_PC,
  R_RISCV_ADD,
  R_RISCV_LEB128,
  R_RISCV_PC_INDIRECT,
  // Same as R_PC but with page-aligned semantics.
  R_LOONGARCH_PAGE_PC,
  // Same as R_PLT_PC but with page-aligned semantics.
  R_LOONGARCH_PLT_PAGE_PC,
  // In addition to having page-aligned semantics, LoongArch GOT relocs are
  // also reused for TLS, making the semantics differ from other architectures.
  R_LOONGARCH_GOT,
  R_LOONGARCH_GOT_PAGE_PC,
  R_LOONGARCH_TLSGD_PAGE_PC,
  R_LOONGARCH_TLSDESC_PAGE_PC,
};

// Architecture-neutral representation of relocation.
struct Relocation {
  RelExpr expr;
  RelType type;
  uint64_t offset;
  int64_t addend;
  Symbol *sym;
};

// Manipulate jump instructions with these modifiers.  These are used to relax
// jump instruction opcodes at basic block boundaries and are particularly
// useful when basic block sections are enabled.
struct JumpInstrMod {
  uint64_t offset;
  JumpModType original;
  unsigned size;
};

// This function writes undefined symbol diagnostics to an internal buffer.
// Call reportUndefinedSymbols() after calling scanRelocations() to emit
// the diagnostics.
template <class ELFT> void scanRelocations();
template <class ELFT> void checkNoCrossRefs();
void reportUndefinedSymbols();
void postScanRelocations();
void addGotEntry(Symbol &sym);

void hexagonTLSSymbolUpdate(ArrayRef<OutputSection *> outputSections);
bool hexagonNeedsTLSSymbol(ArrayRef<OutputSection *> outputSections);

class ThunkSection;
class Thunk;
class InputSectionDescription;

class ThunkCreator {
public:
  // Return true if Thunks have been added to OutputSections
  bool createThunks(uint32_t pass, ArrayRef<OutputSection *> outputSections);

private:
  void mergeThunks(ArrayRef<OutputSection *> outputSections);

  ThunkSection *getISDThunkSec(OutputSection *os, InputSection *isec,
                               InputSectionDescription *isd,
                               const Relocation &rel, uint64_t src);

  ThunkSection *getISThunkSec(InputSection *isec);

  void createInitialThunkSections(ArrayRef<OutputSection *> outputSections);

  std::pair<Thunk *, bool> getThunk(InputSection *isec, Relocation &rel,
                                    uint64_t src);

  ThunkSection *addThunkSection(OutputSection *os, InputSectionDescription *,
                                uint64_t off);

  bool normalizeExistingThunk(Relocation &rel, uint64_t src);

  // Record all the available Thunks for a (Symbol, addend) pair, where Symbol
  // is represented as a (section, offset) pair. There may be multiple
  // relocations sharing the same (section, offset + addend) pair. We may revert
  // a relocation back to its original non-Thunk target, and restore the
  // original addend, so we cannot fold offset + addend. A nested pair is used
  // because DenseMapInfo is not specialized for std::tuple.
  llvm::DenseMap<std::pair<std::pair<SectionBase *, uint64_t>, int64_t>,
                 std::vector<Thunk *>>
      thunkedSymbolsBySectionAndAddend;
  llvm::DenseMap<std::pair<Symbol *, int64_t>, std::vector<Thunk *>>
      thunkedSymbols;

  // Find a Thunk from the Thunks symbol definition, we can use this to find
  // the Thunk from a relocation to the Thunks symbol definition.
  llvm::DenseMap<Symbol *, Thunk *> thunks;

  // Track InputSections that have an inline ThunkSection placed in front
  // an inline ThunkSection may have control fall through to the section below
  // so we need to make sure that there is only one of them.
  // The Mips LA25 Thunk is an example of an inline ThunkSection.
  llvm::DenseMap<InputSection *, ThunkSection *> thunkedSections;

  // The number of completed passes of createThunks this permits us
  // to do one time initialization on Pass 0 and put a limit on the
  // number of times it can be called to prevent infinite loops.
  uint32_t pass = 0;
};

// Decode LEB128 without error checking. Only used by performance critical code
// like RelocsCrel.
inline uint64_t readLEB128(const uint8_t *&p, uint64_t leb) {
  uint64_t acc = 0, shift = 0, byte;
  do {
    byte = *p++;
    acc |= (byte - 128 * (byte >= leb)) << shift;
    shift += 7;
  } while (byte >= 128);
  return acc;
}
inline uint64_t readULEB128(const uint8_t *&p) { return readLEB128(p, 128); }
inline int64_t readSLEB128(const uint8_t *&p) { return readLEB128(p, 64); }

// This class implements a CREL iterator that does not allocate extra memory.
template <bool is64> struct RelocsCrel {
  using uint = std::conditional_t<is64, uint64_t, uint32_t>;
  struct const_iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = llvm::object::Elf_Crel_Impl<is64>;
    using difference_type = ptrdiff_t;
    using pointer = value_type *;
    using reference = const value_type &;
    uint32_t count;
    uint8_t flagBits, shift;
    const uint8_t *p;
    llvm::object::Elf_Crel_Impl<is64> crel{};
    const_iterator(size_t hdr, const uint8_t *p)
        : count(hdr / 8), flagBits(hdr & 4 ? 3 : 2), shift(hdr % 4), p(p) {
      if (count)
        step();
    }
    void step() {
      // See object::decodeCrel.
      const uint8_t b = *p++;
      crel.r_offset += b >> flagBits << shift;
      if (b >= 0x80)
        crel.r_offset +=
            ((readULEB128(p) << (7 - flagBits)) - (0x80 >> flagBits)) << shift;
      if (b & 1)
        crel.r_symidx += readSLEB128(p);
      if (b & 2)
        crel.r_type += readSLEB128(p);
      if (b & 4 && flagBits == 3)
        crel.r_addend += static_cast<uint>(readSLEB128(p));
    }
    llvm::object::Elf_Crel_Impl<is64> operator*() const { return crel; };
    const llvm::object::Elf_Crel_Impl<is64> *operator->() const {
      return &crel;
    }
    // For llvm::enumerate.
    bool operator==(const const_iterator &r) const { return count == r.count; }
    bool operator!=(const const_iterator &r) const { return count != r.count; }
    const_iterator &operator++() {
      if (--count)
        step();
      return *this;
    }
    // For RelocationScanner::scanOne.
    void operator+=(size_t n) {
      for (; n; --n)
        operator++();
    }
  };

  size_t hdr = 0;
  const uint8_t *p = nullptr;

  constexpr RelocsCrel() = default;
  RelocsCrel(const uint8_t *p) : hdr(readULEB128(p)) { this->p = p; }
  size_t size() const { return hdr / 8; }
  const_iterator begin() const { return {hdr, p}; }
  const_iterator end() const { return {0, nullptr}; }
};

template <class RelTy> struct Relocs : ArrayRef<RelTy> {
  Relocs() = default;
  Relocs(ArrayRef<RelTy> a) : ArrayRef<RelTy>(a) {}
};

template <bool is64>
struct Relocs<llvm::object::Elf_Crel_Impl<is64>> : RelocsCrel<is64> {
  using RelocsCrel<is64>::RelocsCrel;
};

// Return a int64_t to make sure we get the sign extension out of the way as
// early as possible.
template <class ELFT>
static inline int64_t getAddend(const typename ELFT::Rel &rel) {
  return 0;
}
template <class ELFT>
static inline int64_t getAddend(const typename ELFT::Rela &rel) {
  return rel.r_addend;
}
template <class ELFT>
static inline int64_t getAddend(const typename ELFT::Crel &rel) {
  return rel.r_addend;
}

template <typename RelTy>
inline Relocs<RelTy> sortRels(Relocs<RelTy> rels,
                              SmallVector<RelTy, 0> &storage) {
  auto cmp = [](const RelTy &a, const RelTy &b) {
    return a.r_offset < b.r_offset;
  };
  if (!llvm::is_sorted(rels, cmp)) {
    storage.assign(rels.begin(), rels.end());
    llvm::stable_sort(storage, cmp);
    rels = Relocs<RelTy>(storage);
  }
  return rels;
}

template <bool is64>
inline Relocs<llvm::object::Elf_Crel_Impl<is64>>
sortRels(Relocs<llvm::object::Elf_Crel_Impl<is64>> rels,
         SmallVector<llvm::object::Elf_Crel_Impl<is64>, 0> &storage) {
  return {};
}

// Returns true if Expr refers a GOT entry. Note that this function returns
// false for TLS variables even though they need GOT, because TLS variables uses
// GOT differently than the regular variables.
bool needsGot(RelExpr expr);
} // namespace lld::elf

#endif
