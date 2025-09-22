//===--- ppc64.h - Generic JITLink ppc64 edge kinds, utilities --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing 64-bit PowerPC objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_PPC64_H
#define LLVM_EXECUTIONENGINE_JITLINK_PPC64_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/TableManager.h"
#include "llvm/Support/Endian.h"

namespace llvm::jitlink::ppc64 {

/// Represents ppc64 fixups and other ppc64-specific edge kinds.
enum EdgeKind_ppc64 : Edge::Kind {
  Pointer64 = Edge::FirstRelocation,
  Pointer32,
  Pointer16,
  Pointer16DS,
  Pointer16HA,
  Pointer16HI,
  Pointer16HIGH,
  Pointer16HIGHA,
  Pointer16HIGHER,
  Pointer16HIGHERA,
  Pointer16HIGHEST,
  Pointer16HIGHESTA,
  Pointer16LO,
  Pointer16LODS,
  Pointer14,
  Delta64,
  Delta34,
  Delta32,
  NegDelta32,
  Delta16,
  Delta16HA,
  Delta16HI,
  Delta16LO,
  TOC,
  TOCDelta16,
  TOCDelta16DS,
  TOCDelta16HA,
  TOCDelta16HI,
  TOCDelta16LO,
  TOCDelta16LODS,
  RequestGOTAndTransformToDelta34,
  CallBranchDelta,
  // Need to restore r2 after the bl, suggesting the bl is followed by a nop.
  CallBranchDeltaRestoreTOC,
  // Request calling function with TOC.
  RequestCall,
  // Request calling function without TOC.
  RequestCallNoTOC,
  RequestTLSDescInGOTAndTransformToTOCDelta16HA,
  RequestTLSDescInGOTAndTransformToTOCDelta16LO,
  RequestTLSDescInGOTAndTransformToDelta34,
};

enum PLTCallStubKind {
  // Setup function entry(r12) and long branch to target using TOC.
  LongBranch,
  // Save TOC pointer, setup function entry and long branch to target using TOC.
  LongBranchSaveR2,
  // Setup function entry(r12) and long branch to target without using TOC.
  LongBranchNoTOC,
};

extern const char NullPointerContent[8];
extern const char PointerJumpStubContent_big[20];
extern const char PointerJumpStubContent_little[20];
extern const char PointerJumpStubNoTOCContent_big[32];
extern const char PointerJumpStubNoTOCContent_little[32];

struct PLTCallStubReloc {
  Edge::Kind K;
  size_t Offset;
  Edge::AddendT A;
};

struct PLTCallStubInfo {
  ArrayRef<char> Content;
  SmallVector<PLTCallStubReloc, 2> Relocs;
};

template <llvm::endianness Endianness>
inline PLTCallStubInfo pickStub(PLTCallStubKind StubKind) {
  constexpr bool isLE = Endianness == llvm::endianness::little;
  switch (StubKind) {
  case LongBranch: {
    ArrayRef<char> Content =
        isLE ? PointerJumpStubContent_little : PointerJumpStubContent_big;
    // Skip save r2.
    Content = Content.slice(4);
    size_t Offset = isLE ? 0 : 2;
    return PLTCallStubInfo{
        Content,
        {{TOCDelta16HA, Offset, 0}, {TOCDelta16LO, Offset + 4, 0}},
    };
  }
  case LongBranchSaveR2: {
    ArrayRef<char> Content =
        isLE ? PointerJumpStubContent_little : PointerJumpStubContent_big;
    size_t Offset = isLE ? 4 : 6;
    return PLTCallStubInfo{
        Content,
        {{TOCDelta16HA, Offset, 0}, {TOCDelta16LO, Offset + 4, 0}},
    };
  }
  case LongBranchNoTOC: {
    ArrayRef<char> Content = isLE ? PointerJumpStubNoTOCContent_little
                                  : PointerJumpStubNoTOCContent_big;
    size_t Offset = isLE ? 16 : 18;
    Edge::AddendT Addend = isLE ? 8 : 10;
    return PLTCallStubInfo{
        Content,
        {{Delta16HA, Offset, Addend}, {Delta16LO, Offset + 4, Addend + 4}},
    };
  }
  }
  llvm_unreachable("Unknown PLTCallStubKind enum");
}

inline Symbol &createAnonymousPointer(LinkGraph &G, Section &PointerSection,
                                      Symbol *InitialTarget = nullptr,
                                      uint64_t InitialAddend = 0) {
  assert(G.getPointerSize() == sizeof(NullPointerContent) &&
         "LinkGraph's pointer size should be consistent with size of "
         "NullPointerContent");
  Block &B = G.createContentBlock(PointerSection, NullPointerContent,
                                  orc::ExecutorAddr(), G.getPointerSize(), 0);
  if (InitialTarget)
    B.addEdge(Pointer64, 0, *InitialTarget, InitialAddend);
  return G.addAnonymousSymbol(B, 0, G.getPointerSize(), false, false);
}

template <llvm::endianness Endianness>
inline Symbol &createAnonymousPointerJumpStub(LinkGraph &G,
                                              Section &StubSection,
                                              Symbol &PointerSymbol,
                                              PLTCallStubKind StubKind) {
  PLTCallStubInfo StubInfo = pickStub<Endianness>(StubKind);
  Block &B = G.createContentBlock(StubSection, StubInfo.Content,
                                  orc::ExecutorAddr(), 4, 0);
  for (auto const &Reloc : StubInfo.Relocs)
    B.addEdge(Reloc.K, Reloc.Offset, PointerSymbol, Reloc.A);
  return G.addAnonymousSymbol(B, 0, StubInfo.Content.size(), true, false);
}

template <llvm::endianness Endianness>
class TOCTableManager : public TableManager<TOCTableManager<Endianness>> {
public:
  // FIXME: `llvm-jitlink -check` relies this name to be $__GOT.
  static StringRef getSectionName() { return "$__GOT"; }

  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    Edge::Kind K = E.getKind();
    switch (K) {
    case TOCDelta16HA:
    case TOCDelta16LO:
    case TOCDelta16DS:
    case TOCDelta16LODS:
    case CallBranchDeltaRestoreTOC:
    case RequestCall:
      // Create TOC section if TOC relocation, PLT or GOT is used.
      getOrCreateTOCSection(G);
      return false;
    case RequestGOTAndTransformToDelta34:
      E.setKind(ppc64::Delta34);
      E.setTarget(createEntry(G, E.getTarget()));
      return true;
    default:
      return false;
    }
  }

  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointer(G, getOrCreateTOCSection(G), &Target);
  }

private:
  Section &getOrCreateTOCSection(LinkGraph &G) {
    TOCSection = G.findSectionByName(getSectionName());
    if (!TOCSection)
      TOCSection = &G.createSection(getSectionName(), orc::MemProt::Read);
    return *TOCSection;
  }

  Section *TOCSection = nullptr;
};

template <llvm::endianness Endianness>
class PLTTableManager : public TableManager<PLTTableManager<Endianness>> {
public:
  PLTTableManager(TOCTableManager<Endianness> &TOC) : TOC(TOC) {}

  static StringRef getSectionName() { return "$__STUBS"; }

  // FIXME: One external symbol can only have one PLT stub in a object file.
  // This is a limitation when we need different PLT stubs for the same symbol.
  // For example, we need two different PLT stubs for `bl __tls_get_addr` and
  // `bl __tls_get_addr@notoc`.
  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    bool isExternal = E.getTarget().isExternal();
    Edge::Kind K = E.getKind();
    if (K == ppc64::RequestCall) {
      if (isExternal) {
        E.setKind(ppc64::CallBranchDeltaRestoreTOC);
        this->StubKind = LongBranchSaveR2;
        // FIXME: We assume the addend to the external target is zero. It's
        // quite unusual that the addend of an external target to be non-zero as
        // if we have known the layout of the external object.
        E.setTarget(this->getEntryForTarget(G, E.getTarget()));
        // Addend to the stub is zero.
        E.setAddend(0);
      } else
        // TODO: There are cases a local function call need a call stub.
        // 1. Caller uses TOC, the callee doesn't, need a r2 save stub.
        // 2. Caller doesn't use TOC, the callee does, need a r12 setup stub.
        // 3. Branching target is out of range.
        E.setKind(ppc64::CallBranchDelta);
      return true;
    }
    if (K == ppc64::RequestCallNoTOC) {
      E.setKind(ppc64::CallBranchDelta);
      this->StubKind = LongBranchNoTOC;
      E.setTarget(this->getEntryForTarget(G, E.getTarget()));
      return true;
    }
    return false;
  }

  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointerJumpStub<Endianness>(
        G, getOrCreateStubsSection(G), TOC.getEntryForTarget(G, Target),
        this->StubKind);
  }

private:
  Section &getOrCreateStubsSection(LinkGraph &G) {
    PLTSection = G.findSectionByName(getSectionName());
    if (!PLTSection)
      PLTSection = &G.createSection(getSectionName(),
                                    orc::MemProt::Read | orc::MemProt::Exec);
    return *PLTSection;
  }

  TOCTableManager<Endianness> &TOC;
  Section *PLTSection = nullptr;
  PLTCallStubKind StubKind;
};

/// Returns a string name for the given ppc64 edge. For debugging purposes
/// only.
const char *getEdgeKindName(Edge::Kind K);

inline static uint16_t ha(uint64_t x) { return (x + 0x8000) >> 16; }
inline static uint64_t lo(uint64_t x) { return x & 0xffff; }
inline static uint16_t hi(uint64_t x) { return x >> 16; }
inline static uint64_t high(uint64_t x) { return (x >> 16) & 0xffff; }
inline static uint64_t higha(uint64_t x) {
  return ((x + 0x8000) >> 16) & 0xffff;
}
inline static uint64_t higher(uint64_t x) { return (x >> 32) & 0xffff; }
inline static uint64_t highera(uint64_t x) {
  return ((x + 0x8000) >> 32) & 0xffff;
}
inline static uint16_t highest(uint64_t x) { return x >> 48; }
inline static uint16_t highesta(uint64_t x) { return (x + 0x8000) >> 48; }

// Prefixed instruction introduced in ISAv3.1 consists of two 32-bit words,
// prefix word and suffix word, i.e., prefixed_instruction = concat(prefix_word,
// suffix_word). That's to say, for a prefixed instruction encoded in uint64_t,
// the most significant 32 bits belong to the prefix word. The prefix word is at
// low address for both big/little endian. Byte order in each word still follows
// its endian.
template <llvm::endianness Endianness>
inline static uint64_t readPrefixedInstruction(const char *Loc) {
  constexpr bool isLE = Endianness == llvm::endianness::little;
  uint64_t Inst = support::endian::read64<Endianness>(Loc);
  return isLE ? (Inst << 32) | (Inst >> 32) : Inst;
}

template <llvm::endianness Endianness>
inline static void writePrefixedInstruction(char *Loc, uint64_t Inst) {
  constexpr bool isLE = Endianness == llvm::endianness::little;
  Inst = isLE ? (Inst << 32) | (Inst >> 32) : Inst;
  support::endian::write64<Endianness>(Loc, Inst);
}

template <llvm::endianness Endianness>
inline Error relocateHalf16(char *FixupPtr, int64_t Value, Edge::Kind K) {
  switch (K) {
  case Delta16:
  case Pointer16:
  case TOCDelta16:
    support::endian::write16<Endianness>(FixupPtr, Value);
    break;
  case Pointer16DS:
  case TOCDelta16DS:
    support::endian::write16<Endianness>(FixupPtr, Value & ~3);
    break;
  case Delta16HA:
  case Pointer16HA:
  case TOCDelta16HA:
    support::endian::write16<Endianness>(FixupPtr, ha(Value));
    break;
  case Delta16HI:
  case Pointer16HI:
  case TOCDelta16HI:
    support::endian::write16<Endianness>(FixupPtr, hi(Value));
    break;
  case Pointer16HIGH:
    support::endian::write16<Endianness>(FixupPtr, high(Value));
    break;
  case Pointer16HIGHA:
    support::endian::write16<Endianness>(FixupPtr, higha(Value));
    break;
  case Pointer16HIGHER:
    support::endian::write16<Endianness>(FixupPtr, higher(Value));
    break;
  case Pointer16HIGHERA:
    support::endian::write16<Endianness>(FixupPtr, highera(Value));
    break;
  case Pointer16HIGHEST:
    support::endian::write16<Endianness>(FixupPtr, highest(Value));
    break;
  case Pointer16HIGHESTA:
    support::endian::write16<Endianness>(FixupPtr, highesta(Value));
    break;
  case Delta16LO:
  case Pointer16LO:
  case TOCDelta16LO:
    support::endian::write16<Endianness>(FixupPtr, lo(Value));
    break;
  case Pointer16LODS:
  case TOCDelta16LODS:
    support::endian::write16<Endianness>(FixupPtr, lo(Value) & ~3);
    break;
  default:
    return make_error<JITLinkError>(
        StringRef(getEdgeKindName(K)) +
        " relocation does not write at half16 field");
  }
  return Error::success();
}

/// Apply fixup expression for edge to block content.
template <llvm::endianness Endianness>
inline Error applyFixup(LinkGraph &G, Block &B, const Edge &E,
                        const Symbol *TOCSymbol) {
  char *BlockWorkingMem = B.getAlreadyMutableContent().data();
  char *FixupPtr = BlockWorkingMem + E.getOffset();
  orc::ExecutorAddr FixupAddress = B.getAddress() + E.getOffset();
  int64_t S = E.getTarget().getAddress().getValue();
  int64_t A = E.getAddend();
  int64_t P = FixupAddress.getValue();
  int64_t TOCBase = TOCSymbol ? TOCSymbol->getAddress().getValue() : 0;
  Edge::Kind K = E.getKind();

  DEBUG_WITH_TYPE("jitlink", {
    dbgs() << "    Applying fixup on " << G.getEdgeKindName(K)
           << " edge, (S, A, P, .TOC.) = (" << formatv("{0:x}", S) << ", "
           << formatv("{0:x}", A) << ", " << formatv("{0:x}", P) << ", "
           << formatv("{0:x}", TOCBase) << ")\n";
  });

  switch (K) {
  case Pointer64: {
    uint64_t Value = S + A;
    support::endian::write64<Endianness>(FixupPtr, Value);
    break;
  }
  case Delta16:
  case Delta16HA:
  case Delta16HI:
  case Delta16LO: {
    int64_t Value = S + A - P;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    return relocateHalf16<Endianness>(FixupPtr, Value, K);
  }
  case TOC:
    support::endian::write64<Endianness>(FixupPtr, TOCBase);
    break;
  case Pointer16:
  case Pointer16DS:
  case Pointer16HA:
  case Pointer16HI:
  case Pointer16HIGH:
  case Pointer16HIGHA:
  case Pointer16HIGHER:
  case Pointer16HIGHERA:
  case Pointer16HIGHEST:
  case Pointer16HIGHESTA:
  case Pointer16LO:
  case Pointer16LODS: {
    uint64_t Value = S + A;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    return relocateHalf16<Endianness>(FixupPtr, Value, K);
  }
  case Pointer14: {
    static const uint32_t Low14Mask = 0xfffc;
    uint64_t Value = S + A;
    assert((Value & 3) == 0 && "Pointer14 requires 4-byte alignment");
    if (LLVM_UNLIKELY(!isInt<16>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    uint32_t Inst = support::endian::read32<Endianness>(FixupPtr);
    support::endian::write32<Endianness>(FixupPtr, (Inst & ~Low14Mask) |
                                                       (Value & Low14Mask));
    break;
  }
  case TOCDelta16:
  case TOCDelta16DS:
  case TOCDelta16HA:
  case TOCDelta16HI:
  case TOCDelta16LO:
  case TOCDelta16LODS: {
    int64_t Value = S + A - TOCBase;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    return relocateHalf16<Endianness>(FixupPtr, Value, K);
  }
  case CallBranchDeltaRestoreTOC:
  case CallBranchDelta: {
    int64_t Value = S + A - P;
    if (LLVM_UNLIKELY(!isInt<26>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    uint32_t Inst = support::endian::read32<Endianness>(FixupPtr);
    support::endian::write32<Endianness>(FixupPtr, (Inst & 0xfc000003) |
                                                       (Value & 0x03fffffc));
    if (K == CallBranchDeltaRestoreTOC) {
      uint32_t NopInst = support::endian::read32<Endianness>(FixupPtr + 4);
      assert(NopInst == 0x60000000 &&
             "NOP should be placed here for restoring r2");
      (void)NopInst;
      // Restore r2 by instruction 0xe8410018 which is `ld r2, 24(r1)`.
      support::endian::write32<Endianness>(FixupPtr + 4, 0xe8410018);
    }
    break;
  }
  case Delta64: {
    int64_t Value = S + A - P;
    support::endian::write64<Endianness>(FixupPtr, Value);
    break;
  }
  case Delta34: {
    int64_t Value = S + A - P;
    if (!LLVM_UNLIKELY(isInt<34>(Value)))
      return makeTargetOutOfRangeError(G, B, E);
    static const uint64_t SI0Mask = 0x00000003ffff0000;
    static const uint64_t SI1Mask = 0x000000000000ffff;
    static const uint64_t FullMask = 0x0003ffff0000ffff;
    uint64_t Inst = readPrefixedInstruction<Endianness>(FixupPtr) & ~FullMask;
    writePrefixedInstruction<Endianness>(
        FixupPtr, Inst | ((Value & SI0Mask) << 16) | (Value & SI1Mask));
    break;
  }
  case Delta32: {
    int64_t Value = S + A - P;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    support::endian::write32<Endianness>(FixupPtr, Value);
    break;
  }
  case NegDelta32: {
    int64_t Value = P - S + A;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    support::endian::write32<Endianness>(FixupPtr, Value);
    break;
  }
  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " unsupported edge kind " + getEdgeKindName(E.getKind()));
  }
  return Error::success();
}

} // end namespace llvm::jitlink::ppc64

#endif // LLVM_EXECUTIONENGINE_JITLINK_PPC64_H
