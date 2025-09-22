//===------------- JITLink.cpp - Core Run-time JIT linker APIs ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/ExecutionEngine/JITLink/COFF.h"
#include "llvm/ExecutionEngine/JITLink/ELF.h"
#include "llvm/ExecutionEngine/JITLink/MachO.h"
#include "llvm/ExecutionEngine/JITLink/aarch64.h"
#include "llvm/ExecutionEngine/JITLink/i386.h"
#include "llvm/ExecutionEngine/JITLink/loongarch.h"
#include "llvm/ExecutionEngine/JITLink/x86_64.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;

#define DEBUG_TYPE "jitlink"

namespace {

enum JITLinkErrorCode { GenericJITLinkError = 1 };

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class JITLinkerErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "runtimedyld"; }

  std::string message(int Condition) const override {
    switch (static_cast<JITLinkErrorCode>(Condition)) {
    case GenericJITLinkError:
      return "Generic JITLink error";
    }
    llvm_unreachable("Unrecognized JITLinkErrorCode");
  }
};

} // namespace

namespace llvm {
namespace jitlink {

char JITLinkError::ID = 0;

void JITLinkError::log(raw_ostream &OS) const { OS << ErrMsg; }

std::error_code JITLinkError::convertToErrorCode() const {
  static JITLinkerErrorCategory TheJITLinkerErrorCategory;
  return std::error_code(GenericJITLinkError, TheJITLinkerErrorCategory);
}

const char *getGenericEdgeKindName(Edge::Kind K) {
  switch (K) {
  case Edge::Invalid:
    return "INVALID RELOCATION";
  case Edge::KeepAlive:
    return "Keep-Alive";
  default:
    return "<Unrecognized edge kind>";
  }
}

const char *getLinkageName(Linkage L) {
  switch (L) {
  case Linkage::Strong:
    return "strong";
  case Linkage::Weak:
    return "weak";
  }
  llvm_unreachable("Unrecognized llvm.jitlink.Linkage enum");
}

const char *getScopeName(Scope S) {
  switch (S) {
  case Scope::Default:
    return "default";
  case Scope::Hidden:
    return "hidden";
  case Scope::Local:
    return "local";
  }
  llvm_unreachable("Unrecognized llvm.jitlink.Scope enum");
}

bool isCStringBlock(Block &B) {
  if (B.getSize() == 0) // Empty blocks are not valid C-strings.
    return false;

  // Zero-fill blocks of size one are valid empty strings.
  if (B.isZeroFill())
    return B.getSize() == 1;

  for (size_t I = 0; I != B.getSize() - 1; ++I)
    if (B.getContent()[I] == '\0')
      return false;

  return B.getContent()[B.getSize() - 1] == '\0';
}

raw_ostream &operator<<(raw_ostream &OS, const Block &B) {
  return OS << B.getAddress() << " -- " << (B.getAddress() + B.getSize())
            << ": "
            << "size = " << formatv("{0:x8}", B.getSize()) << ", "
            << (B.isZeroFill() ? "zero-fill" : "content")
            << ", align = " << B.getAlignment()
            << ", align-ofs = " << B.getAlignmentOffset()
            << ", section = " << B.getSection().getName();
}

raw_ostream &operator<<(raw_ostream &OS, const Symbol &Sym) {
  OS << Sym.getAddress() << " (" << (Sym.isDefined() ? "block" : "addressable")
     << " + " << formatv("{0:x8}", Sym.getOffset())
     << "): size: " << formatv("{0:x8}", Sym.getSize())
     << ", linkage: " << formatv("{0:6}", getLinkageName(Sym.getLinkage()))
     << ", scope: " << formatv("{0:8}", getScopeName(Sym.getScope())) << ", "
     << (Sym.isLive() ? "live" : "dead") << "  -   "
     << (Sym.hasName() ? Sym.getName() : "<anonymous symbol>");
  return OS;
}

void printEdge(raw_ostream &OS, const Block &B, const Edge &E,
               StringRef EdgeKindName) {
  OS << "edge@" << B.getAddress() + E.getOffset() << ": " << B.getAddress()
     << " + " << formatv("{0:x}", E.getOffset()) << " -- " << EdgeKindName
     << " -> ";

  auto &TargetSym = E.getTarget();
  if (TargetSym.hasName())
    OS << TargetSym.getName();
  else {
    auto &TargetBlock = TargetSym.getBlock();
    auto &TargetSec = TargetBlock.getSection();
    orc::ExecutorAddr SecAddress(~uint64_t(0));
    for (auto *B : TargetSec.blocks())
      if (B->getAddress() < SecAddress)
        SecAddress = B->getAddress();

    orc::ExecutorAddrDiff SecDelta = TargetSym.getAddress() - SecAddress;
    OS << TargetSym.getAddress() << " (section " << TargetSec.getName();
    if (SecDelta)
      OS << " + " << formatv("{0:x}", SecDelta);
    OS << " / block " << TargetBlock.getAddress();
    if (TargetSym.getOffset())
      OS << " + " << formatv("{0:x}", TargetSym.getOffset());
    OS << ")";
  }

  if (E.getAddend() != 0)
    OS << " + " << E.getAddend();
}

Section::~Section() {
  for (auto *Sym : Symbols)
    Sym->~Symbol();
  for (auto *B : Blocks)
    B->~Block();
}

Block &LinkGraph::splitBlock(Block &B, size_t SplitIndex,
                             SplitBlockCache *Cache) {

  assert(SplitIndex > 0 && "splitBlock can not be called with SplitIndex == 0");

  // If the split point covers all of B then just return B.
  if (SplitIndex == B.getSize())
    return B;

  assert(SplitIndex < B.getSize() && "SplitIndex out of range");

  // Create the new block covering [ 0, SplitIndex ).
  auto &NewBlock =
      B.isZeroFill()
          ? createZeroFillBlock(B.getSection(), SplitIndex, B.getAddress(),
                                B.getAlignment(), B.getAlignmentOffset())
          : createContentBlock(
                B.getSection(), B.getContent().slice(0, SplitIndex),
                B.getAddress(), B.getAlignment(), B.getAlignmentOffset());

  // Modify B to cover [ SplitIndex, B.size() ).
  B.setAddress(B.getAddress() + SplitIndex);
  B.setContent(B.getContent().slice(SplitIndex));
  B.setAlignmentOffset((B.getAlignmentOffset() + SplitIndex) %
                       B.getAlignment());

  // Handle edge transfer/update.
  {
    // Copy edges to NewBlock (recording their iterators so that we can remove
    // them from B), and update of Edges remaining on B.
    std::vector<Block::edge_iterator> EdgesToRemove;
    for (auto I = B.edges().begin(); I != B.edges().end();) {
      if (I->getOffset() < SplitIndex) {
        NewBlock.addEdge(*I);
        I = B.removeEdge(I);
      } else {
        I->setOffset(I->getOffset() - SplitIndex);
        ++I;
      }
    }
  }

  // Handle symbol transfer/update.
  {
    // Initialize the symbols cache if necessary.
    SplitBlockCache LocalBlockSymbolsCache;
    if (!Cache)
      Cache = &LocalBlockSymbolsCache;
    if (*Cache == std::nullopt) {
      *Cache = SplitBlockCache::value_type();
      for (auto *Sym : B.getSection().symbols())
        if (&Sym->getBlock() == &B)
          (*Cache)->push_back(Sym);

      llvm::sort(**Cache, [](const Symbol *LHS, const Symbol *RHS) {
        return LHS->getOffset() > RHS->getOffset();
      });
    }
    auto &BlockSymbols = **Cache;

    // Transfer all symbols with offset less than SplitIndex to NewBlock.
    while (!BlockSymbols.empty() &&
           BlockSymbols.back()->getOffset() < SplitIndex) {
      auto *Sym = BlockSymbols.back();
      // If the symbol extends beyond the split, update the size to be within
      // the new block.
      if (Sym->getOffset() + Sym->getSize() > SplitIndex)
        Sym->setSize(SplitIndex - Sym->getOffset());
      Sym->setBlock(NewBlock);
      BlockSymbols.pop_back();
    }

    // Update offsets for all remaining symbols in B.
    for (auto *Sym : BlockSymbols)
      Sym->setOffset(Sym->getOffset() - SplitIndex);
  }

  return NewBlock;
}

void LinkGraph::dump(raw_ostream &OS) {
  DenseMap<Block *, std::vector<Symbol *>> BlockSymbols;

  // Map from blocks to the symbols pointing at them.
  for (auto *Sym : defined_symbols())
    BlockSymbols[&Sym->getBlock()].push_back(Sym);

  // For each block, sort its symbols by something approximating
  // relevance.
  for (auto &KV : BlockSymbols)
    llvm::sort(KV.second, [](const Symbol *LHS, const Symbol *RHS) {
      if (LHS->getOffset() != RHS->getOffset())
        return LHS->getOffset() < RHS->getOffset();
      if (LHS->getLinkage() != RHS->getLinkage())
        return LHS->getLinkage() < RHS->getLinkage();
      if (LHS->getScope() != RHS->getScope())
        return LHS->getScope() < RHS->getScope();
      if (LHS->hasName()) {
        if (!RHS->hasName())
          return true;
        return LHS->getName() < RHS->getName();
      }
      return false;
    });

  for (auto &Sec : sections()) {
    OS << "section " << Sec.getName() << ":\n\n";

    std::vector<Block *> SortedBlocks;
    llvm::copy(Sec.blocks(), std::back_inserter(SortedBlocks));
    llvm::sort(SortedBlocks, [](const Block *LHS, const Block *RHS) {
      return LHS->getAddress() < RHS->getAddress();
    });

    for (auto *B : SortedBlocks) {
      OS << "  block " << B->getAddress()
         << " size = " << formatv("{0:x8}", B->getSize())
         << ", align = " << B->getAlignment()
         << ", alignment-offset = " << B->getAlignmentOffset();
      if (B->isZeroFill())
        OS << ", zero-fill";
      OS << "\n";

      auto BlockSymsI = BlockSymbols.find(B);
      if (BlockSymsI != BlockSymbols.end()) {
        OS << "    symbols:\n";
        auto &Syms = BlockSymsI->second;
        for (auto *Sym : Syms)
          OS << "      " << *Sym << "\n";
      } else
        OS << "    no symbols\n";

      if (!B->edges_empty()) {
        OS << "    edges:\n";
        std::vector<Edge> SortedEdges;
        llvm::copy(B->edges(), std::back_inserter(SortedEdges));
        llvm::sort(SortedEdges, [](const Edge &LHS, const Edge &RHS) {
          return LHS.getOffset() < RHS.getOffset();
        });
        for (auto &E : SortedEdges) {
          OS << "      " << B->getFixupAddress(E) << " (block + "
             << formatv("{0:x8}", E.getOffset()) << "), addend = ";
          if (E.getAddend() >= 0)
            OS << formatv("+{0:x8}", E.getAddend());
          else
            OS << formatv("-{0:x8}", -E.getAddend());
          OS << ", kind = " << getEdgeKindName(E.getKind()) << ", target = ";
          if (E.getTarget().hasName())
            OS << E.getTarget().getName();
          else
            OS << "addressable@"
               << formatv("{0:x16}", E.getTarget().getAddress()) << "+"
               << formatv("{0:x8}", E.getTarget().getOffset());
          OS << "\n";
        }
      } else
        OS << "    no edges\n";
      OS << "\n";
    }
  }

  OS << "Absolute symbols:\n";
  if (!absolute_symbols().empty()) {
    for (auto *Sym : absolute_symbols())
      OS << "  " << Sym->getAddress() << ": " << *Sym << "\n";
  } else
    OS << "  none\n";

  OS << "\nExternal symbols:\n";
  if (!external_symbols().empty()) {
    for (auto *Sym : external_symbols())
      OS << "  " << Sym->getAddress() << ": " << *Sym
         << (Sym->isWeaklyReferenced() ? " (weakly referenced)" : "") << "\n";
  } else
    OS << "  none\n";
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolLookupFlags &LF) {
  switch (LF) {
  case SymbolLookupFlags::RequiredSymbol:
    return OS << "RequiredSymbol";
  case SymbolLookupFlags::WeaklyReferencedSymbol:
    return OS << "WeaklyReferencedSymbol";
  }
  llvm_unreachable("Unrecognized lookup flags");
}

void JITLinkAsyncLookupContinuation::anchor() {}

JITLinkContext::~JITLinkContext() = default;

bool JITLinkContext::shouldAddDefaultTargetPasses(const Triple &TT) const {
  return true;
}

LinkGraphPassFunction JITLinkContext::getMarkLivePass(const Triple &TT) const {
  return LinkGraphPassFunction();
}

Error JITLinkContext::modifyPassConfig(LinkGraph &G,
                                       PassConfiguration &Config) {
  return Error::success();
}

Error markAllSymbolsLive(LinkGraph &G) {
  for (auto *Sym : G.defined_symbols())
    Sym->setLive(true);
  return Error::success();
}

Error makeTargetOutOfRangeError(const LinkGraph &G, const Block &B,
                                const Edge &E) {
  std::string ErrMsg;
  {
    raw_string_ostream ErrStream(ErrMsg);
    Section &Sec = B.getSection();
    ErrStream << "In graph " << G.getName() << ", section " << Sec.getName()
              << ": relocation target ";
    if (E.getTarget().hasName()) {
      ErrStream << "\"" << E.getTarget().getName() << "\"";
    } else
      ErrStream << E.getTarget().getBlock().getSection().getName() << " + "
                << formatv("{0:x}", E.getOffset());
    ErrStream << " at address " << formatv("{0:x}", E.getTarget().getAddress())
              << " is out of range of " << G.getEdgeKindName(E.getKind())
              << " fixup at " << formatv("{0:x}", B.getFixupAddress(E)) << " (";

    Symbol *BestSymbolForBlock = nullptr;
    for (auto *Sym : Sec.symbols())
      if (&Sym->getBlock() == &B && Sym->hasName() && Sym->getOffset() == 0 &&
          (!BestSymbolForBlock ||
           Sym->getScope() < BestSymbolForBlock->getScope() ||
           Sym->getLinkage() < BestSymbolForBlock->getLinkage()))
        BestSymbolForBlock = Sym;

    if (BestSymbolForBlock)
      ErrStream << BestSymbolForBlock->getName() << ", ";
    else
      ErrStream << "<anonymous block> @ ";

    ErrStream << formatv("{0:x}", B.getAddress()) << " + "
              << formatv("{0:x}", E.getOffset()) << ")";
  }
  return make_error<JITLinkError>(std::move(ErrMsg));
}

Error makeAlignmentError(llvm::orc::ExecutorAddr Loc, uint64_t Value, int N,
                         const Edge &E) {
  return make_error<JITLinkError>("0x" + llvm::utohexstr(Loc.getValue()) +
                                  " improper alignment for relocation " +
                                  formatv("{0:d}", E.getKind()) + ": 0x" +
                                  llvm::utohexstr(Value) +
                                  " is not aligned to " + Twine(N) + " bytes");
}

AnonymousPointerCreator getAnonymousPointerCreator(const Triple &TT) {
  switch (TT.getArch()) {
  case Triple::aarch64:
    return aarch64::createAnonymousPointer;
  case Triple::x86_64:
    return x86_64::createAnonymousPointer;
  case Triple::x86:
    return i386::createAnonymousPointer;
  case Triple::loongarch32:
  case Triple::loongarch64:
    return loongarch::createAnonymousPointer;
  default:
    return nullptr;
  }
}

PointerJumpStubCreator getPointerJumpStubCreator(const Triple &TT) {
  switch (TT.getArch()) {
  case Triple::aarch64:
    return aarch64::createAnonymousPointerJumpStub;
  case Triple::x86_64:
    return x86_64::createAnonymousPointerJumpStub;
  case Triple::x86:
    return i386::createAnonymousPointerJumpStub;
  case Triple::loongarch32:
  case Triple::loongarch64:
    return loongarch::createAnonymousPointerJumpStub;
  default:
    return nullptr;
  }
}

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromObject(MemoryBufferRef ObjectBuffer) {
  auto Magic = identify_magic(ObjectBuffer.getBuffer());
  switch (Magic) {
  case file_magic::macho_object:
    return createLinkGraphFromMachOObject(ObjectBuffer);
  case file_magic::elf_relocatable:
    return createLinkGraphFromELFObject(ObjectBuffer);
  case file_magic::coff_object:
    return createLinkGraphFromCOFFObject(ObjectBuffer);
  default:
    return make_error<JITLinkError>("Unsupported file format");
  };
}

std::unique_ptr<LinkGraph> absoluteSymbolsLinkGraph(const Triple &TT,
                                                    orc::SymbolMap Symbols) {
  unsigned PointerSize;
  endianness Endianness =
      TT.isLittleEndian() ? endianness::little : endianness::big;
  switch (TT.getArch()) {
  case Triple::aarch64:
  case llvm::Triple::riscv64:
  case Triple::x86_64:
    PointerSize = 8;
    break;
  case llvm::Triple::arm:
  case llvm::Triple::riscv32:
  case llvm::Triple::x86:
    PointerSize = 4;
    break;
  default:
    llvm::report_fatal_error("unhandled target architecture");
  }

  static std::atomic<uint64_t> Counter = {0};
  auto Index = Counter.fetch_add(1, std::memory_order_relaxed);
  auto G = std::make_unique<LinkGraph>(
      "<Absolute Symbols " + std::to_string(Index) + ">", TT, PointerSize,
      Endianness, /*GetEdgeKindName=*/nullptr);
  for (auto &[Name, Def] : Symbols) {
    auto &Sym =
        G->addAbsoluteSymbol(*Name, Def.getAddress(), /*Size=*/0,
                             Linkage::Strong, Scope::Default, /*IsLive=*/true);
    Sym.setCallable(Def.getFlags().isCallable());
  }

  return G;
}

void link(std::unique_ptr<LinkGraph> G, std::unique_ptr<JITLinkContext> Ctx) {
  switch (G->getTargetTriple().getObjectFormat()) {
  case Triple::MachO:
    return link_MachO(std::move(G), std::move(Ctx));
  case Triple::ELF:
    return link_ELF(std::move(G), std::move(Ctx));
  case Triple::COFF:
    return link_COFF(std::move(G), std::move(Ctx));
  default:
    Ctx->notifyFailed(make_error<JITLinkError>("Unsupported object format"));
  };
}

} // end namespace jitlink
} // end namespace llvm
