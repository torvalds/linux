//===------- ELF_riscv.cpp -JIT linker implementation for ELF/riscv -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ELF/riscv jit-link implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ELF_riscv.h"
#include "EHFrameSupportImpl.h"
#include "ELFLinkGraphBuilder.h"
#include "JITLinkGeneric.h"
#include "PerGraphGOTAndPLTStubsBuilder.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/ExecutionEngine/JITLink/DWARFRecordSectionSplitter.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/riscv.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Endian.h"

#define DEBUG_TYPE "jitlink"
using namespace llvm;
using namespace llvm::jitlink;
using namespace llvm::jitlink::riscv;

namespace {

class PerGraphGOTAndPLTStubsBuilder_ELF_riscv
    : public PerGraphGOTAndPLTStubsBuilder<
          PerGraphGOTAndPLTStubsBuilder_ELF_riscv> {
public:
  static constexpr size_t StubEntrySize = 16;
  static const uint8_t NullGOTEntryContent[8];
  static const uint8_t RV64StubContent[StubEntrySize];
  static const uint8_t RV32StubContent[StubEntrySize];

  using PerGraphGOTAndPLTStubsBuilder<
      PerGraphGOTAndPLTStubsBuilder_ELF_riscv>::PerGraphGOTAndPLTStubsBuilder;

  bool isRV64() const { return G.getPointerSize() == 8; }

  bool isGOTEdgeToFix(Edge &E) const { return E.getKind() == R_RISCV_GOT_HI20; }

  Symbol &createGOTEntry(Symbol &Target) {
    Block &GOTBlock =
        G.createContentBlock(getGOTSection(), getGOTEntryBlockContent(),
                             orc::ExecutorAddr(), G.getPointerSize(), 0);
    GOTBlock.addEdge(isRV64() ? R_RISCV_64 : R_RISCV_32, 0, Target, 0);
    return G.addAnonymousSymbol(GOTBlock, 0, G.getPointerSize(), false, false);
  }

  Symbol &createPLTStub(Symbol &Target) {
    Block &StubContentBlock = G.createContentBlock(
        getStubsSection(), getStubBlockContent(), orc::ExecutorAddr(), 4, 0);
    auto &GOTEntrySymbol = getGOTEntry(Target);
    StubContentBlock.addEdge(R_RISCV_CALL, 0, GOTEntrySymbol, 0);
    return G.addAnonymousSymbol(StubContentBlock, 0, StubEntrySize, true,
                                false);
  }

  void fixGOTEdge(Edge &E, Symbol &GOTEntry) {
    // Replace the relocation pair (R_RISCV_GOT_HI20, R_RISCV_PCREL_LO12)
    // with (R_RISCV_PCREL_HI20, R_RISCV_PCREL_LO12)
    // Therefore, here just change the R_RISCV_GOT_HI20 to R_RISCV_PCREL_HI20
    E.setKind(R_RISCV_PCREL_HI20);
    E.setTarget(GOTEntry);
  }

  void fixPLTEdge(Edge &E, Symbol &PLTStubs) {
    assert((E.getKind() == R_RISCV_CALL || E.getKind() == R_RISCV_CALL_PLT ||
            E.getKind() == CallRelaxable) &&
           "Not a PLT edge?");
    E.setKind(R_RISCV_CALL);
    E.setTarget(PLTStubs);
  }

  bool isExternalBranchEdge(Edge &E) const {
    return (E.getKind() == R_RISCV_CALL || E.getKind() == R_RISCV_CALL_PLT ||
            E.getKind() == CallRelaxable) &&
           !E.getTarget().isDefined();
  }

private:
  Section &getGOTSection() const {
    if (!GOTSection)
      GOTSection = &G.createSection("$__GOT", orc::MemProt::Read);
    return *GOTSection;
  }

  Section &getStubsSection() const {
    if (!StubsSection)
      StubsSection =
          &G.createSection("$__STUBS", orc::MemProt::Read | orc::MemProt::Exec);
    return *StubsSection;
  }

  ArrayRef<char> getGOTEntryBlockContent() {
    return {reinterpret_cast<const char *>(NullGOTEntryContent),
            G.getPointerSize()};
  }

  ArrayRef<char> getStubBlockContent() {
    auto StubContent = isRV64() ? RV64StubContent : RV32StubContent;
    return {reinterpret_cast<const char *>(StubContent), StubEntrySize};
  }

  mutable Section *GOTSection = nullptr;
  mutable Section *StubsSection = nullptr;
};

const uint8_t PerGraphGOTAndPLTStubsBuilder_ELF_riscv::NullGOTEntryContent[8] =
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const uint8_t
    PerGraphGOTAndPLTStubsBuilder_ELF_riscv::RV64StubContent[StubEntrySize] = {
        0x17, 0x0e, 0x00, 0x00,  // auipc t3, literal
        0x03, 0x3e, 0x0e, 0x00,  // ld    t3, literal(t3)
        0x67, 0x00, 0x0e, 0x00,  // jr    t3
        0x13, 0x00, 0x00, 0x00}; // nop

const uint8_t
    PerGraphGOTAndPLTStubsBuilder_ELF_riscv::RV32StubContent[StubEntrySize] = {
        0x17, 0x0e, 0x00, 0x00,  // auipc t3, literal
        0x03, 0x2e, 0x0e, 0x00,  // lw    t3, literal(t3)
        0x67, 0x00, 0x0e, 0x00,  // jr    t3
        0x13, 0x00, 0x00, 0x00}; // nop
} // namespace
namespace llvm {
namespace jitlink {

static uint32_t extractBits(uint32_t Num, unsigned Low, unsigned Size) {
  return (Num & (((1ULL << Size) - 1) << Low)) >> Low;
}

static inline bool isAlignmentCorrect(uint64_t Value, int N) {
  return (Value & (N - 1)) ? false : true;
}

// Requires 0 < N <= 64.
static inline bool isInRangeForImm(int64_t Value, int N) {
  return Value == llvm::SignExtend64(Value, N);
}

class ELFJITLinker_riscv : public JITLinker<ELFJITLinker_riscv> {
  friend class JITLinker<ELFJITLinker_riscv>;

public:
  ELFJITLinker_riscv(std::unique_ptr<JITLinkContext> Ctx,
                     std::unique_ptr<LinkGraph> G, PassConfiguration PassConfig)
      : JITLinker(std::move(Ctx), std::move(G), std::move(PassConfig)) {
    JITLinkerBase::getPassConfig().PostAllocationPasses.push_back(
        [this](LinkGraph &G) { return gatherRISCVPCRelHi20(G); });
  }

private:
  DenseMap<std::pair<const Block *, orc::ExecutorAddrDiff>, const Edge *>
      RelHi20;

  Error gatherRISCVPCRelHi20(LinkGraph &G) {
    for (Block *B : G.blocks())
      for (Edge &E : B->edges())
        if (E.getKind() == R_RISCV_PCREL_HI20)
          RelHi20[{B, E.getOffset()}] = &E;

    return Error::success();
  }

  Expected<const Edge &> getRISCVPCRelHi20(const Edge &E) const {
    using namespace riscv;
    assert((E.getKind() == R_RISCV_PCREL_LO12_I ||
            E.getKind() == R_RISCV_PCREL_LO12_S) &&
           "Can only have high relocation for R_RISCV_PCREL_LO12_I or "
           "R_RISCV_PCREL_LO12_S");

    const Symbol &Sym = E.getTarget();
    const Block &B = Sym.getBlock();
    orc::ExecutorAddrDiff Offset = Sym.getOffset();

    auto It = RelHi20.find({&B, Offset});
    if (It != RelHi20.end())
      return *It->second;

    return make_error<JITLinkError>("No HI20 PCREL relocation type be found "
                                    "for LO12 PCREL relocation type");
  }

  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    using namespace riscv;
    using namespace llvm::support;

    char *BlockWorkingMem = B.getAlreadyMutableContent().data();
    char *FixupPtr = BlockWorkingMem + E.getOffset();
    orc::ExecutorAddr FixupAddress = B.getAddress() + E.getOffset();
    switch (E.getKind()) {
    case R_RISCV_32: {
      int64_t Value = (E.getTarget().getAddress() + E.getAddend()).getValue();
      *(little32_t *)FixupPtr = static_cast<uint32_t>(Value);
      break;
    }
    case R_RISCV_64: {
      int64_t Value = (E.getTarget().getAddress() + E.getAddend()).getValue();
      *(little64_t *)FixupPtr = static_cast<uint64_t>(Value);
      break;
    }
    case R_RISCV_BRANCH: {
      int64_t Value = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
      if (LLVM_UNLIKELY(!isInRangeForImm(Value >> 1, 12)))
        return makeTargetOutOfRangeError(G, B, E);
      if (LLVM_UNLIKELY(!isAlignmentCorrect(Value, 2)))
        return makeAlignmentError(FixupAddress, Value, 2, E);
      uint32_t Imm12 = extractBits(Value, 12, 1) << 31;
      uint32_t Imm10_5 = extractBits(Value, 5, 6) << 25;
      uint32_t Imm4_1 = extractBits(Value, 1, 4) << 8;
      uint32_t Imm11 = extractBits(Value, 11, 1) << 7;
      uint32_t RawInstr = *(little32_t *)FixupPtr;
      *(little32_t *)FixupPtr =
          (RawInstr & 0x1FFF07F) | Imm12 | Imm10_5 | Imm4_1 | Imm11;
      break;
    }
    case R_RISCV_JAL: {
      int64_t Value = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
      if (LLVM_UNLIKELY(!isInRangeForImm(Value >> 1, 20)))
        return makeTargetOutOfRangeError(G, B, E);
      if (LLVM_UNLIKELY(!isAlignmentCorrect(Value, 2)))
        return makeAlignmentError(FixupAddress, Value, 2, E);
      uint32_t Imm20 = extractBits(Value, 20, 1) << 31;
      uint32_t Imm10_1 = extractBits(Value, 1, 10) << 21;
      uint32_t Imm11 = extractBits(Value, 11, 1) << 20;
      uint32_t Imm19_12 = extractBits(Value, 12, 8) << 12;
      uint32_t RawInstr = *(little32_t *)FixupPtr;
      *(little32_t *)FixupPtr =
          (RawInstr & 0xFFF) | Imm20 | Imm10_1 | Imm11 | Imm19_12;
      break;
    }
    case CallRelaxable:
      // Treat as R_RISCV_CALL when the relaxation pass did not run
    case R_RISCV_CALL_PLT:
    case R_RISCV_CALL: {
      int64_t Value = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
      int64_t Hi = Value + 0x800;
      if (LLVM_UNLIKELY(!isInRangeForImm(Hi, 32)))
        return makeTargetOutOfRangeError(G, B, E);
      int32_t Lo = Value & 0xFFF;
      uint32_t RawInstrAuipc = *(little32_t *)FixupPtr;
      uint32_t RawInstrJalr = *(little32_t *)(FixupPtr + 4);
      *(little32_t *)FixupPtr =
          RawInstrAuipc | (static_cast<uint32_t>(Hi & 0xFFFFF000));
      *(little32_t *)(FixupPtr + 4) =
          RawInstrJalr | (static_cast<uint32_t>(Lo) << 20);
      break;
    }
    // The relocations R_RISCV_CALL_PLT and R_RISCV_GOT_HI20 are handled by
    // PerGraphGOTAndPLTStubsBuilder_ELF_riscv and are transformed into
    // R_RISCV_CALL and R_RISCV_PCREL_HI20.
    case R_RISCV_PCREL_HI20: {
      int64_t Value = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
      int64_t Hi = Value + 0x800;
      if (LLVM_UNLIKELY(!isInRangeForImm(Hi, 32)))
        return makeTargetOutOfRangeError(G, B, E);
      uint32_t RawInstr = *(little32_t *)FixupPtr;
      *(little32_t *)FixupPtr =
          (RawInstr & 0xFFF) | (static_cast<uint32_t>(Hi & 0xFFFFF000));
      break;
    }
    case R_RISCV_PCREL_LO12_I: {
      // FIXME: We assume that R_RISCV_PCREL_HI20 is present in object code and
      // pairs with current relocation R_RISCV_PCREL_LO12_I. So here may need a
      // check.
      auto RelHI20 = getRISCVPCRelHi20(E);
      if (!RelHI20)
        return RelHI20.takeError();
      int64_t Value = RelHI20->getTarget().getAddress() +
                      RelHI20->getAddend() - E.getTarget().getAddress();
      int64_t Lo = Value & 0xFFF;
      uint32_t RawInstr = *(little32_t *)FixupPtr;
      *(little32_t *)FixupPtr =
          (RawInstr & 0xFFFFF) | (static_cast<uint32_t>(Lo & 0xFFF) << 20);
      break;
    }
    case R_RISCV_PCREL_LO12_S: {
      // FIXME: We assume that R_RISCV_PCREL_HI20 is present in object code and
      // pairs with current relocation R_RISCV_PCREL_LO12_S. So here may need a
      // check.
      auto RelHI20 = getRISCVPCRelHi20(E);
      if (!RelHI20)
        return RelHI20.takeError();
      int64_t Value = RelHI20->getTarget().getAddress() +
                      RelHI20->getAddend() - E.getTarget().getAddress();
      int64_t Lo = Value & 0xFFF;
      uint32_t Imm11_5 = extractBits(Lo, 5, 7) << 25;
      uint32_t Imm4_0 = extractBits(Lo, 0, 5) << 7;
      uint32_t RawInstr = *(little32_t *)FixupPtr;

      *(little32_t *)FixupPtr = (RawInstr & 0x1FFF07F) | Imm11_5 | Imm4_0;
      break;
    }
    case R_RISCV_HI20: {
      int64_t Value = (E.getTarget().getAddress() + E.getAddend()).getValue();
      int64_t Hi = Value + 0x800;
      if (LLVM_UNLIKELY(!isInRangeForImm(Hi, 32)))
        return makeTargetOutOfRangeError(G, B, E);
      uint32_t RawInstr = *(little32_t *)FixupPtr;
      *(little32_t *)FixupPtr =
          (RawInstr & 0xFFF) | (static_cast<uint32_t>(Hi & 0xFFFFF000));
      break;
    }
    case R_RISCV_LO12_I: {
      // FIXME: We assume that R_RISCV_HI20 is present in object code and pairs
      // with current relocation R_RISCV_LO12_I. So here may need a check.
      int64_t Value = (E.getTarget().getAddress() + E.getAddend()).getValue();
      int32_t Lo = Value & 0xFFF;
      uint32_t RawInstr = *(little32_t *)FixupPtr;
      *(little32_t *)FixupPtr =
          (RawInstr & 0xFFFFF) | (static_cast<uint32_t>(Lo & 0xFFF) << 20);
      break;
    }
    case R_RISCV_LO12_S: {
      // FIXME: We assume that R_RISCV_HI20 is present in object code and pairs
      // with current relocation R_RISCV_LO12_S. So here may need a check.
      int64_t Value = (E.getTarget().getAddress() + E.getAddend()).getValue();
      int64_t Lo = Value & 0xFFF;
      uint32_t Imm11_5 = extractBits(Lo, 5, 7) << 25;
      uint32_t Imm4_0 = extractBits(Lo, 0, 5) << 7;
      uint32_t RawInstr = *(little32_t *)FixupPtr;
      *(little32_t *)FixupPtr = (RawInstr & 0x1FFF07F) | Imm11_5 | Imm4_0;
      break;
    }
    case R_RISCV_ADD8: {
      int64_t Value =
          (E.getTarget().getAddress() +
           *(reinterpret_cast<const uint8_t *>(FixupPtr)) + E.getAddend())
              .getValue();
      *FixupPtr = static_cast<uint8_t>(Value);
      break;
    }
    case R_RISCV_ADD16: {
      int64_t Value = (E.getTarget().getAddress() +
                       support::endian::read16le(FixupPtr) + E.getAddend())
                          .getValue();
      *(little16_t *)FixupPtr = static_cast<uint16_t>(Value);
      break;
    }
    case R_RISCV_ADD32: {
      int64_t Value = (E.getTarget().getAddress() +
                       support::endian::read32le(FixupPtr) + E.getAddend())
                          .getValue();
      *(little32_t *)FixupPtr = static_cast<uint32_t>(Value);
      break;
    }
    case R_RISCV_ADD64: {
      int64_t Value = (E.getTarget().getAddress() +
                       support::endian::read64le(FixupPtr) + E.getAddend())
                          .getValue();
      *(little64_t *)FixupPtr = static_cast<uint64_t>(Value);
      break;
    }
    case R_RISCV_SUB8: {
      int64_t Value = *(reinterpret_cast<const uint8_t *>(FixupPtr)) -
                      E.getTarget().getAddress().getValue() - E.getAddend();
      *FixupPtr = static_cast<uint8_t>(Value);
      break;
    }
    case R_RISCV_SUB16: {
      int64_t Value = support::endian::read16le(FixupPtr) -
                      E.getTarget().getAddress().getValue() - E.getAddend();
      *(little16_t *)FixupPtr = static_cast<uint32_t>(Value);
      break;
    }
    case R_RISCV_SUB32: {
      int64_t Value = support::endian::read32le(FixupPtr) -
                      E.getTarget().getAddress().getValue() - E.getAddend();
      *(little32_t *)FixupPtr = static_cast<uint32_t>(Value);
      break;
    }
    case R_RISCV_SUB64: {
      int64_t Value = support::endian::read64le(FixupPtr) -
                      E.getTarget().getAddress().getValue() - E.getAddend();
      *(little64_t *)FixupPtr = static_cast<uint64_t>(Value);
      break;
    }
    case R_RISCV_RVC_BRANCH: {
      int64_t Value = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
      if (LLVM_UNLIKELY(!isInRangeForImm(Value >> 1, 8)))
        return makeTargetOutOfRangeError(G, B, E);
      if (LLVM_UNLIKELY(!isAlignmentCorrect(Value, 2)))
        return makeAlignmentError(FixupAddress, Value, 2, E);
      uint16_t Imm8 = extractBits(Value, 8, 1) << 12;
      uint16_t Imm4_3 = extractBits(Value, 3, 2) << 10;
      uint16_t Imm7_6 = extractBits(Value, 6, 2) << 5;
      uint16_t Imm2_1 = extractBits(Value, 1, 2) << 3;
      uint16_t Imm5 = extractBits(Value, 5, 1) << 2;
      uint16_t RawInstr = *(little16_t *)FixupPtr;
      *(little16_t *)FixupPtr =
          (RawInstr & 0xE383) | Imm8 | Imm4_3 | Imm7_6 | Imm2_1 | Imm5;
      break;
    }
    case R_RISCV_RVC_JUMP: {
      int64_t Value = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
      if (LLVM_UNLIKELY(!isInRangeForImm(Value >> 1, 11)))
        return makeTargetOutOfRangeError(G, B, E);
      if (LLVM_UNLIKELY(!isAlignmentCorrect(Value, 2)))
        return makeAlignmentError(FixupAddress, Value, 2, E);
      uint16_t Imm11 = extractBits(Value, 11, 1) << 12;
      uint16_t Imm4 = extractBits(Value, 4, 1) << 11;
      uint16_t Imm9_8 = extractBits(Value, 8, 2) << 9;
      uint16_t Imm10 = extractBits(Value, 10, 1) << 8;
      uint16_t Imm6 = extractBits(Value, 6, 1) << 7;
      uint16_t Imm7 = extractBits(Value, 7, 1) << 6;
      uint16_t Imm3_1 = extractBits(Value, 1, 3) << 3;
      uint16_t Imm5 = extractBits(Value, 5, 1) << 2;
      uint16_t RawInstr = *(little16_t *)FixupPtr;
      *(little16_t *)FixupPtr = (RawInstr & 0xE003) | Imm11 | Imm4 | Imm9_8 |
                                Imm10 | Imm6 | Imm7 | Imm3_1 | Imm5;
      break;
    }
    case R_RISCV_SUB6: {
      int64_t Value = *(reinterpret_cast<const uint8_t *>(FixupPtr)) & 0x3f;
      Value -= E.getTarget().getAddress().getValue() - E.getAddend();
      *FixupPtr = (*FixupPtr & 0xc0) | (static_cast<uint8_t>(Value) & 0x3f);
      break;
    }
    case R_RISCV_SET6: {
      int64_t Value = (E.getTarget().getAddress() + E.getAddend()).getValue();
      uint32_t RawData = *(little32_t *)FixupPtr;
      int64_t Word6 = Value & 0x3f;
      *(little32_t *)FixupPtr = (RawData & 0xffffffc0) | Word6;
      break;
    }
    case R_RISCV_SET8: {
      int64_t Value = (E.getTarget().getAddress() + E.getAddend()).getValue();
      uint32_t RawData = *(little32_t *)FixupPtr;
      int64_t Word8 = Value & 0xff;
      *(little32_t *)FixupPtr = (RawData & 0xffffff00) | Word8;
      break;
    }
    case R_RISCV_SET16: {
      int64_t Value = (E.getTarget().getAddress() + E.getAddend()).getValue();
      uint32_t RawData = *(little32_t *)FixupPtr;
      int64_t Word16 = Value & 0xffff;
      *(little32_t *)FixupPtr = (RawData & 0xffff0000) | Word16;
      break;
    }
    case R_RISCV_SET32: {
      int64_t Value = (E.getTarget().getAddress() + E.getAddend()).getValue();
      int64_t Word32 = Value & 0xffffffff;
      *(little32_t *)FixupPtr = Word32;
      break;
    }
    case R_RISCV_32_PCREL: {
      int64_t Value = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
      int64_t Word32 = Value & 0xffffffff;
      *(little32_t *)FixupPtr = Word32;
      break;
    }
    case AlignRelaxable:
      // Ignore when the relaxation pass did not run
      break;
    case NegDelta32: {
      int64_t Value = FixupAddress - E.getTarget().getAddress() + E.getAddend();
      if (LLVM_UNLIKELY(!isInRangeForImm(Value, 32)))
        return makeTargetOutOfRangeError(G, B, E);
      *(little32_t *)FixupPtr = static_cast<uint32_t>(Value);
      break;
    }
    }
    return Error::success();
  }
};

namespace {

struct SymbolAnchor {
  uint64_t Offset;
  Symbol *Sym;
  bool End; // true for the anchor of getOffset() + getSize()
};

struct BlockRelaxAux {
  // This records symbol start and end offsets which will be adjusted according
  // to the nearest RelocDeltas element.
  SmallVector<SymbolAnchor, 0> Anchors;
  // All edges that either 1) are R_RISCV_ALIGN or 2) have a R_RISCV_RELAX edge
  // at the same offset.
  SmallVector<Edge *, 0> RelaxEdges;
  // For RelaxEdges[I], the actual offset is RelaxEdges[I]->getOffset() - (I ?
  // RelocDeltas[I - 1] : 0).
  SmallVector<uint32_t, 0> RelocDeltas;
  // For RelaxEdges[I], the actual type is EdgeKinds[I].
  SmallVector<Edge::Kind, 0> EdgeKinds;
  // List of rewritten instructions. Contains one raw encoded instruction per
  // element in EdgeKinds that isn't Invalid or R_RISCV_ALIGN.
  SmallVector<uint32_t, 0> Writes;
};

struct RelaxConfig {
  bool IsRV32;
  bool HasRVC;
};

struct RelaxAux {
  RelaxConfig Config;
  DenseMap<Block *, BlockRelaxAux> Blocks;
};

} // namespace

static bool shouldRelax(const Section &S) {
  return (S.getMemProt() & orc::MemProt::Exec) != orc::MemProt::None;
}

static bool isRelaxable(const Edge &E) {
  switch (E.getKind()) {
  default:
    return false;
  case CallRelaxable:
  case AlignRelaxable:
    return true;
  }
}

static RelaxAux initRelaxAux(LinkGraph &G) {
  RelaxAux Aux;
  Aux.Config.IsRV32 = G.getTargetTriple().isRISCV32();
  const auto &Features = G.getFeatures().getFeatures();
  Aux.Config.HasRVC = llvm::is_contained(Features, "+c") ||
                      llvm::is_contained(Features, "+zca");

  for (auto &S : G.sections()) {
    if (!shouldRelax(S))
      continue;
    for (auto *B : S.blocks()) {
      auto BlockEmplaceResult = Aux.Blocks.try_emplace(B);
      assert(BlockEmplaceResult.second && "Block encountered twice");
      auto &BlockAux = BlockEmplaceResult.first->second;

      for (auto &E : B->edges())
        if (isRelaxable(E))
          BlockAux.RelaxEdges.push_back(&E);

      if (BlockAux.RelaxEdges.empty()) {
        Aux.Blocks.erase(BlockEmplaceResult.first);
        continue;
      }

      const auto NumEdges = BlockAux.RelaxEdges.size();
      BlockAux.RelocDeltas.resize(NumEdges, 0);
      BlockAux.EdgeKinds.resize_for_overwrite(NumEdges);

      // Store anchors (offset and offset+size) for symbols.
      for (auto *Sym : S.symbols()) {
        if (!Sym->isDefined() || &Sym->getBlock() != B)
          continue;

        BlockAux.Anchors.push_back({Sym->getOffset(), Sym, false});
        BlockAux.Anchors.push_back(
            {Sym->getOffset() + Sym->getSize(), Sym, true});
      }
    }
  }

  // Sort anchors by offset so that we can find the closest relocation
  // efficiently. For a zero size symbol, ensure that its start anchor precedes
  // its end anchor. For two symbols with anchors at the same offset, their
  // order does not matter.
  for (auto &BlockAuxIter : Aux.Blocks) {
    llvm::sort(BlockAuxIter.second.Anchors, [](auto &A, auto &B) {
      return std::make_pair(A.Offset, A.End) < std::make_pair(B.Offset, B.End);
    });
  }

  return Aux;
}

static void relaxAlign(orc::ExecutorAddr Loc, const Edge &E, uint32_t &Remove,
                       Edge::Kind &NewEdgeKind) {
  // E points to the start of the padding bytes.
  // E + Addend points to the instruction to be aligned by removing padding.
  // Alignment is the smallest power of 2 strictly greater than Addend.
  const auto Align = NextPowerOf2(E.getAddend());
  const auto DestLoc = alignTo(Loc.getValue(), Align);
  const auto SrcLoc = Loc.getValue() + E.getAddend();
  Remove = SrcLoc - DestLoc;
  assert(static_cast<int32_t>(Remove) >= 0 &&
         "R_RISCV_ALIGN needs expanding the content");
  NewEdgeKind = AlignRelaxable;
}

static void relaxCall(const Block &B, BlockRelaxAux &Aux,
                      const RelaxConfig &Config, orc::ExecutorAddr Loc,
                      const Edge &E, uint32_t &Remove,
                      Edge::Kind &NewEdgeKind) {
  const auto JALR =
      support::endian::read32le(B.getContent().data() + E.getOffset() + 4);
  const auto RD = extractBits(JALR, 7, 5);
  const auto Dest = E.getTarget().getAddress() + E.getAddend();
  const auto Displace = Dest - Loc;

  if (Config.HasRVC && isInt<12>(Displace) && RD == 0) {
    NewEdgeKind = R_RISCV_RVC_JUMP;
    Aux.Writes.push_back(0xa001); // c.j
    Remove = 6;
  } else if (Config.HasRVC && Config.IsRV32 && isInt<12>(Displace) && RD == 1) {
    NewEdgeKind = R_RISCV_RVC_JUMP;
    Aux.Writes.push_back(0x2001); // c.jal
    Remove = 6;
  } else if (isInt<21>(Displace)) {
    NewEdgeKind = R_RISCV_JAL;
    Aux.Writes.push_back(0x6f | RD << 7); // jal
    Remove = 4;
  } else {
    // Not relaxable
    NewEdgeKind = R_RISCV_CALL_PLT;
    Remove = 0;
  }
}

static bool relaxBlock(LinkGraph &G, Block &Block, BlockRelaxAux &Aux,
                       const RelaxConfig &Config) {
  const auto BlockAddr = Block.getAddress();
  bool Changed = false;
  ArrayRef<SymbolAnchor> SA = ArrayRef(Aux.Anchors);
  uint32_t Delta = 0;

  Aux.EdgeKinds.assign(Aux.EdgeKinds.size(), Edge::Invalid);
  Aux.Writes.clear();

  for (auto [I, E] : llvm::enumerate(Aux.RelaxEdges)) {
    const auto Loc = BlockAddr + E->getOffset() - Delta;
    auto &Cur = Aux.RelocDeltas[I];
    uint32_t Remove = 0;
    switch (E->getKind()) {
    case AlignRelaxable:
      relaxAlign(Loc, *E, Remove, Aux.EdgeKinds[I]);
      break;
    case CallRelaxable:
      relaxCall(Block, Aux, Config, Loc, *E, Remove, Aux.EdgeKinds[I]);
      break;
    default:
      llvm_unreachable("Unexpected relaxable edge kind");
    }

    // For all anchors whose offsets are <= E->getOffset(), they are preceded by
    // the previous relocation whose RelocDeltas value equals Delta.
    // Decrease their offset and update their size.
    for (; SA.size() && SA[0].Offset <= E->getOffset(); SA = SA.slice(1)) {
      if (SA[0].End)
        SA[0].Sym->setSize(SA[0].Offset - Delta - SA[0].Sym->getOffset());
      else
        SA[0].Sym->setOffset(SA[0].Offset - Delta);
    }

    Delta += Remove;
    if (Delta != Cur) {
      Cur = Delta;
      Changed = true;
    }
  }

  for (const SymbolAnchor &A : SA) {
    if (A.End)
      A.Sym->setSize(A.Offset - Delta - A.Sym->getOffset());
    else
      A.Sym->setOffset(A.Offset - Delta);
  }

  return Changed;
}

static bool relaxOnce(LinkGraph &G, RelaxAux &Aux) {
  bool Changed = false;

  for (auto &[B, BlockAux] : Aux.Blocks)
    Changed |= relaxBlock(G, *B, BlockAux, Aux.Config);

  return Changed;
}

static void finalizeBlockRelax(LinkGraph &G, Block &Block, BlockRelaxAux &Aux) {
  auto Contents = Block.getAlreadyMutableContent();
  auto *Dest = Contents.data();
  auto NextWrite = Aux.Writes.begin();
  uint32_t Offset = 0;
  uint32_t Delta = 0;

  // Update section content: remove NOPs for R_RISCV_ALIGN and rewrite
  // instructions for relaxed relocations.
  for (auto [I, E] : llvm::enumerate(Aux.RelaxEdges)) {
    uint32_t Remove = Aux.RelocDeltas[I] - Delta;
    Delta = Aux.RelocDeltas[I];
    if (Remove == 0 && Aux.EdgeKinds[I] == Edge::Invalid)
      continue;

    // Copy from last location to the current relocated location.
    const auto Size = E->getOffset() - Offset;
    std::memmove(Dest, Contents.data() + Offset, Size);
    Dest += Size;

    uint32_t Skip = 0;
    switch (Aux.EdgeKinds[I]) {
    case Edge::Invalid:
      break;
    case AlignRelaxable:
      // For R_RISCV_ALIGN, we will place Offset in a location (among NOPs) to
      // satisfy the alignment requirement. If both Remove and E->getAddend()
      // are multiples of 4, it is as if we have skipped some NOPs. Otherwise we
      // are in the middle of a 4-byte NOP, and we need to rewrite the NOP
      // sequence.
      if (Remove % 4 || E->getAddend() % 4) {
        Skip = E->getAddend() - Remove;
        uint32_t J = 0;
        for (; J + 4 <= Skip; J += 4)
          support::endian::write32le(Dest + J, 0x00000013); // nop
        if (J != Skip) {
          assert(J + 2 == Skip);
          support::endian::write16le(Dest + J, 0x0001); // c.nop
        }
      }
      break;
    case R_RISCV_RVC_JUMP:
      Skip = 2;
      support::endian::write16le(Dest, *NextWrite++);
      break;
    case R_RISCV_JAL:
      Skip = 4;
      support::endian::write32le(Dest, *NextWrite++);
      break;
    }

    Dest += Skip;
    Offset = E->getOffset() + Skip + Remove;
  }

  std::memmove(Dest, Contents.data() + Offset, Contents.size() - Offset);

  // Fixup edge offsets and kinds.
  Delta = 0;
  size_t I = 0;
  for (auto &E : Block.edges()) {
    E.setOffset(E.getOffset() - Delta);

    if (I < Aux.RelaxEdges.size() && Aux.RelaxEdges[I] == &E) {
      if (Aux.EdgeKinds[I] != Edge::Invalid)
        E.setKind(Aux.EdgeKinds[I]);

      Delta = Aux.RelocDeltas[I];
      ++I;
    }
  }

  // Remove AlignRelaxable edges: all other relaxable edges got modified and
  // will be used later while linking. Alignment is entirely handled here so we
  // don't need these edges anymore.
  for (auto IE = Block.edges().begin(); IE != Block.edges().end();) {
    if (IE->getKind() == AlignRelaxable)
      IE = Block.removeEdge(IE);
    else
      ++IE;
  }
}

static void finalizeRelax(LinkGraph &G, RelaxAux &Aux) {
  for (auto &[B, BlockAux] : Aux.Blocks)
    finalizeBlockRelax(G, *B, BlockAux);
}

static Error relax(LinkGraph &G) {
  auto Aux = initRelaxAux(G);
  while (relaxOnce(G, Aux)) {
  }
  finalizeRelax(G, Aux);
  return Error::success();
}

template <typename ELFT>
class ELFLinkGraphBuilder_riscv : public ELFLinkGraphBuilder<ELFT> {
private:
  static Expected<riscv::EdgeKind_riscv>
  getRelocationKind(const uint32_t Type) {
    using namespace riscv;
    switch (Type) {
    case ELF::R_RISCV_32:
      return EdgeKind_riscv::R_RISCV_32;
    case ELF::R_RISCV_64:
      return EdgeKind_riscv::R_RISCV_64;
    case ELF::R_RISCV_BRANCH:
      return EdgeKind_riscv::R_RISCV_BRANCH;
    case ELF::R_RISCV_JAL:
      return EdgeKind_riscv::R_RISCV_JAL;
    case ELF::R_RISCV_CALL:
      return EdgeKind_riscv::R_RISCV_CALL;
    case ELF::R_RISCV_CALL_PLT:
      return EdgeKind_riscv::R_RISCV_CALL_PLT;
    case ELF::R_RISCV_GOT_HI20:
      return EdgeKind_riscv::R_RISCV_GOT_HI20;
    case ELF::R_RISCV_PCREL_HI20:
      return EdgeKind_riscv::R_RISCV_PCREL_HI20;
    case ELF::R_RISCV_PCREL_LO12_I:
      return EdgeKind_riscv::R_RISCV_PCREL_LO12_I;
    case ELF::R_RISCV_PCREL_LO12_S:
      return EdgeKind_riscv::R_RISCV_PCREL_LO12_S;
    case ELF::R_RISCV_HI20:
      return EdgeKind_riscv::R_RISCV_HI20;
    case ELF::R_RISCV_LO12_I:
      return EdgeKind_riscv::R_RISCV_LO12_I;
    case ELF::R_RISCV_LO12_S:
      return EdgeKind_riscv::R_RISCV_LO12_S;
    case ELF::R_RISCV_ADD8:
      return EdgeKind_riscv::R_RISCV_ADD8;
    case ELF::R_RISCV_ADD16:
      return EdgeKind_riscv::R_RISCV_ADD16;
    case ELF::R_RISCV_ADD32:
      return EdgeKind_riscv::R_RISCV_ADD32;
    case ELF::R_RISCV_ADD64:
      return EdgeKind_riscv::R_RISCV_ADD64;
    case ELF::R_RISCV_SUB8:
      return EdgeKind_riscv::R_RISCV_SUB8;
    case ELF::R_RISCV_SUB16:
      return EdgeKind_riscv::R_RISCV_SUB16;
    case ELF::R_RISCV_SUB32:
      return EdgeKind_riscv::R_RISCV_SUB32;
    case ELF::R_RISCV_SUB64:
      return EdgeKind_riscv::R_RISCV_SUB64;
    case ELF::R_RISCV_RVC_BRANCH:
      return EdgeKind_riscv::R_RISCV_RVC_BRANCH;
    case ELF::R_RISCV_RVC_JUMP:
      return EdgeKind_riscv::R_RISCV_RVC_JUMP;
    case ELF::R_RISCV_SUB6:
      return EdgeKind_riscv::R_RISCV_SUB6;
    case ELF::R_RISCV_SET6:
      return EdgeKind_riscv::R_RISCV_SET6;
    case ELF::R_RISCV_SET8:
      return EdgeKind_riscv::R_RISCV_SET8;
    case ELF::R_RISCV_SET16:
      return EdgeKind_riscv::R_RISCV_SET16;
    case ELF::R_RISCV_SET32:
      return EdgeKind_riscv::R_RISCV_SET32;
    case ELF::R_RISCV_32_PCREL:
      return EdgeKind_riscv::R_RISCV_32_PCREL;
    case ELF::R_RISCV_ALIGN:
      return EdgeKind_riscv::AlignRelaxable;
    }

    return make_error<JITLinkError>(
        "Unsupported riscv relocation:" + formatv("{0:d}: ", Type) +
        object::getELFRelocationTypeName(ELF::EM_RISCV, Type));
  }

  EdgeKind_riscv getRelaxableRelocationKind(EdgeKind_riscv Kind) {
    switch (Kind) {
    default:
      // Just ignore unsupported relaxations
      return Kind;
    case R_RISCV_CALL:
    case R_RISCV_CALL_PLT:
      return CallRelaxable;
    }
  }

  Error addRelocations() override {
    LLVM_DEBUG(dbgs() << "Processing relocations:\n");

    using Base = ELFLinkGraphBuilder<ELFT>;
    using Self = ELFLinkGraphBuilder_riscv<ELFT>;
    for (const auto &RelSect : Base::Sections)
      if (Error Err = Base::forEachRelaRelocation(RelSect, this,
                                                  &Self::addSingleRelocation))
        return Err;

    return Error::success();
  }

  Error addSingleRelocation(const typename ELFT::Rela &Rel,
                            const typename ELFT::Shdr &FixupSect,
                            Block &BlockToFix) {
    using Base = ELFLinkGraphBuilder<ELFT>;

    uint32_t Type = Rel.getType(false);
    int64_t Addend = Rel.r_addend;

    if (Type == ELF::R_RISCV_RELAX) {
      if (BlockToFix.edges_empty())
        return make_error<StringError>(
            "R_RISCV_RELAX without preceding relocation",
            inconvertibleErrorCode());

      auto &PrevEdge = *std::prev(BlockToFix.edges().end());
      auto Kind = static_cast<EdgeKind_riscv>(PrevEdge.getKind());
      PrevEdge.setKind(getRelaxableRelocationKind(Kind));
      return Error::success();
    }

    Expected<riscv::EdgeKind_riscv> Kind = getRelocationKind(Type);
    if (!Kind)
      return Kind.takeError();

    uint32_t SymbolIndex = Rel.getSymbol(false);
    auto ObjSymbol = Base::Obj.getRelocationSymbol(Rel, Base::SymTabSec);
    if (!ObjSymbol)
      return ObjSymbol.takeError();

    Symbol *GraphSymbol = Base::getGraphSymbol(SymbolIndex);
    if (!GraphSymbol)
      return make_error<StringError>(
          formatv("Could not find symbol at given index, did you add it to "
                  "JITSymbolTable? index: {0}, shndx: {1} Size of table: {2}",
                  SymbolIndex, (*ObjSymbol)->st_shndx,
                  Base::GraphSymbols.size()),
          inconvertibleErrorCode());

    auto FixupAddress = orc::ExecutorAddr(FixupSect.sh_addr) + Rel.r_offset;
    Edge::OffsetT Offset = FixupAddress - BlockToFix.getAddress();
    Edge GE(*Kind, Offset, *GraphSymbol, Addend);
    LLVM_DEBUG({
      dbgs() << "    ";
      printEdge(dbgs(), BlockToFix, GE, riscv::getEdgeKindName(*Kind));
      dbgs() << "\n";
    });

    BlockToFix.addEdge(std::move(GE));
    return Error::success();
  }

public:
  ELFLinkGraphBuilder_riscv(StringRef FileName,
                            const object::ELFFile<ELFT> &Obj, Triple TT,
                            SubtargetFeatures Features)
      : ELFLinkGraphBuilder<ELFT>(Obj, std::move(TT), std::move(Features),
                                  FileName, riscv::getEdgeKindName) {}
};

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_riscv(MemoryBufferRef ObjectBuffer) {
  LLVM_DEBUG({
    dbgs() << "Building jitlink graph for new input "
           << ObjectBuffer.getBufferIdentifier() << "...\n";
  });

  auto ELFObj = object::ObjectFile::createELFObjectFile(ObjectBuffer);
  if (!ELFObj)
    return ELFObj.takeError();

  auto Features = (*ELFObj)->getFeatures();
  if (!Features)
    return Features.takeError();

  if ((*ELFObj)->getArch() == Triple::riscv64) {
    auto &ELFObjFile = cast<object::ELFObjectFile<object::ELF64LE>>(**ELFObj);
    return ELFLinkGraphBuilder_riscv<object::ELF64LE>(
               (*ELFObj)->getFileName(), ELFObjFile.getELFFile(),
               (*ELFObj)->makeTriple(), std::move(*Features))
        .buildGraph();
  } else {
    assert((*ELFObj)->getArch() == Triple::riscv32 &&
           "Invalid triple for RISCV ELF object file");
    auto &ELFObjFile = cast<object::ELFObjectFile<object::ELF32LE>>(**ELFObj);
    return ELFLinkGraphBuilder_riscv<object::ELF32LE>(
               (*ELFObj)->getFileName(), ELFObjFile.getELFFile(),
               (*ELFObj)->makeTriple(), std::move(*Features))
        .buildGraph();
  }
}

void link_ELF_riscv(std::unique_ptr<LinkGraph> G,
                    std::unique_ptr<JITLinkContext> Ctx) {
  PassConfiguration Config;
  const Triple &TT = G->getTargetTriple();
  if (Ctx->shouldAddDefaultTargetPasses(TT)) {

    Config.PrePrunePasses.push_back(DWARFRecordSectionSplitter(".eh_frame"));
    Config.PrePrunePasses.push_back(EHFrameEdgeFixer(
        ".eh_frame", G->getPointerSize(), Edge::Invalid, Edge::Invalid,
        Edge::Invalid, Edge::Invalid, NegDelta32));
    Config.PrePrunePasses.push_back(EHFrameNullTerminator(".eh_frame"));

    if (auto MarkLive = Ctx->getMarkLivePass(TT))
      Config.PrePrunePasses.push_back(std::move(MarkLive));
    else
      Config.PrePrunePasses.push_back(markAllSymbolsLive);
    Config.PostPrunePasses.push_back(
        PerGraphGOTAndPLTStubsBuilder_ELF_riscv::asPass);
    Config.PostAllocationPasses.push_back(relax);
  }
  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  ELFJITLinker_riscv::link(std::move(Ctx), std::move(G), std::move(Config));
}

LinkGraphPassFunction createRelaxationPass_ELF_riscv() { return relax; }

} // namespace jitlink
} // namespace llvm
