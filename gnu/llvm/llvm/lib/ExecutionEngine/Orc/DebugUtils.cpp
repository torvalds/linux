//===---------- DebugUtils.cpp - Utilities for debugging ORC JITs ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/DebugUtils.h"

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "orc"

using namespace llvm;

namespace {

#ifndef NDEBUG

cl::opt<bool> PrintHidden("debug-orc-print-hidden", cl::init(true),
                          cl::desc("debug print hidden symbols defined by "
                                   "materialization units"),
                          cl::Hidden);

cl::opt<bool> PrintCallable("debug-orc-print-callable", cl::init(true),
                            cl::desc("debug print callable symbols defined by "
                                     "materialization units"),
                            cl::Hidden);

cl::opt<bool> PrintData("debug-orc-print-data", cl::init(true),
                        cl::desc("debug print data symbols defined by "
                                 "materialization units"),
                        cl::Hidden);

#endif // NDEBUG

// SetPrinter predicate that prints every element.
template <typename T> struct PrintAll {
  bool operator()(const T &E) { return true; }
};

bool anyPrintSymbolOptionSet() {
#ifndef NDEBUG
  return PrintHidden || PrintCallable || PrintData;
#else
  return false;
#endif // NDEBUG
}

bool flagsMatchCLOpts(const JITSymbolFlags &Flags) {
#ifndef NDEBUG
  // Bail out early if this is a hidden symbol and we're not printing hiddens.
  if (!PrintHidden && !Flags.isExported())
    return false;

  // Return true if this is callable and we're printing callables.
  if (PrintCallable && Flags.isCallable())
    return true;

  // Return true if this is data and we're printing data.
  if (PrintData && !Flags.isCallable())
    return true;

  // otherwise return false.
  return false;
#else
  return false;
#endif // NDEBUG
}

// Prints a sequence of items, filtered by an user-supplied predicate.
template <typename Sequence,
          typename Pred = PrintAll<typename Sequence::value_type>>
class SequencePrinter {
public:
  SequencePrinter(const Sequence &S, char OpenSeq, char CloseSeq,
                  Pred ShouldPrint = Pred())
      : S(S), OpenSeq(OpenSeq), CloseSeq(CloseSeq),
        ShouldPrint(std::move(ShouldPrint)) {}

  void printTo(llvm::raw_ostream &OS) const {
    bool PrintComma = false;
    OS << OpenSeq;
    for (auto &E : S) {
      if (ShouldPrint(E)) {
        if (PrintComma)
          OS << ',';
        OS << ' ' << E;
        PrintComma = true;
      }
    }
    OS << ' ' << CloseSeq;
  }

private:
  const Sequence &S;
  char OpenSeq;
  char CloseSeq;
  mutable Pred ShouldPrint;
};

template <typename Sequence, typename Pred>
SequencePrinter<Sequence, Pred> printSequence(const Sequence &S, char OpenSeq,
                                              char CloseSeq, Pred P = Pred()) {
  return SequencePrinter<Sequence, Pred>(S, OpenSeq, CloseSeq, std::move(P));
}

// Render a SequencePrinter by delegating to its printTo method.
template <typename Sequence, typename Pred>
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                              const SequencePrinter<Sequence, Pred> &Printer) {
  Printer.printTo(OS);
  return OS;
}

struct PrintSymbolFlagsMapElemsMatchingCLOpts {
  bool operator()(const orc::SymbolFlagsMap::value_type &KV) {
    return flagsMatchCLOpts(KV.second);
  }
};

struct PrintSymbolMapElemsMatchingCLOpts {
  bool operator()(const orc::SymbolMap::value_type &KV) {
    return flagsMatchCLOpts(KV.second.getFlags());
  }
};

} // end anonymous namespace

namespace llvm {
namespace orc {

raw_ostream &operator<<(raw_ostream &OS, const SymbolStringPtr &Sym) {
  return OS << *Sym;
}

raw_ostream &operator<<(raw_ostream &OS, NonOwningSymbolStringPtr Sym) {
  return OS << *Sym;
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolNameSet &Symbols) {
  return OS << printSequence(Symbols, '{', '}', PrintAll<SymbolStringPtr>());
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolNameVector &Symbols) {
  return OS << printSequence(Symbols, '[', ']', PrintAll<SymbolStringPtr>());
}

raw_ostream &operator<<(raw_ostream &OS, ArrayRef<SymbolStringPtr> Symbols) {
  return OS << printSequence(Symbols, '[', ']', PrintAll<SymbolStringPtr>());
}

raw_ostream &operator<<(raw_ostream &OS, const JITSymbolFlags &Flags) {
  if (Flags.hasError())
    OS << "[*ERROR*]";
  if (Flags.isCallable())
    OS << "[Callable]";
  else
    OS << "[Data]";
  if (Flags.isWeak())
    OS << "[Weak]";
  else if (Flags.isCommon())
    OS << "[Common]";

  if (!Flags.isExported())
    OS << "[Hidden]";

  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const ExecutorSymbolDef &Sym) {
  return OS << Sym.getAddress() << " " << Sym.getFlags();
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolFlagsMap::value_type &KV) {
  return OS << "(\"" << KV.first << "\", " << KV.second << ")";
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolMap::value_type &KV) {
  return OS << "(\"" << KV.first << "\": " << KV.second << ")";
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolFlagsMap &SymbolFlags) {
  return OS << printSequence(SymbolFlags, '{', '}',
                             PrintSymbolFlagsMapElemsMatchingCLOpts());
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolMap &Symbols) {
  return OS << printSequence(Symbols, '{', '}',
                             PrintSymbolMapElemsMatchingCLOpts());
}

raw_ostream &operator<<(raw_ostream &OS,
                        const SymbolDependenceMap::value_type &KV) {
  return OS << "(" << KV.first->getName() << ", " << KV.second << ")";
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolDependenceMap &Deps) {
  return OS << printSequence(Deps, '{', '}',
                             PrintAll<SymbolDependenceMap::value_type>());
}

raw_ostream &operator<<(raw_ostream &OS, const MaterializationUnit &MU) {
  OS << "MU@" << &MU << " (\"" << MU.getName() << "\"";
  if (anyPrintSymbolOptionSet())
    OS << ", " << MU.getSymbols();
  return OS << ")";
}

raw_ostream &operator<<(raw_ostream &OS, const LookupKind &K) {
  switch (K) {
  case LookupKind::Static:
    return OS << "Static";
  case LookupKind::DLSym:
    return OS << "DLSym";
  }
  llvm_unreachable("Invalid lookup kind");
}

raw_ostream &operator<<(raw_ostream &OS,
                        const JITDylibLookupFlags &JDLookupFlags) {
  switch (JDLookupFlags) {
  case JITDylibLookupFlags::MatchExportedSymbolsOnly:
    return OS << "MatchExportedSymbolsOnly";
  case JITDylibLookupFlags::MatchAllSymbols:
    return OS << "MatchAllSymbols";
  }
  llvm_unreachable("Invalid JITDylib lookup flags");
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolLookupFlags &LookupFlags) {
  switch (LookupFlags) {
  case SymbolLookupFlags::RequiredSymbol:
    return OS << "RequiredSymbol";
  case SymbolLookupFlags::WeaklyReferencedSymbol:
    return OS << "WeaklyReferencedSymbol";
  }
  llvm_unreachable("Invalid symbol lookup flags");
}

raw_ostream &operator<<(raw_ostream &OS,
                        const SymbolLookupSet::value_type &KV) {
  return OS << "(" << KV.first << ", " << KV.second << ")";
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolLookupSet &LookupSet) {
  return OS << printSequence(LookupSet, '{', '}',
                             PrintAll<SymbolLookupSet::value_type>());
}

raw_ostream &operator<<(raw_ostream &OS,
                        const JITDylibSearchOrder &SearchOrder) {
  OS << "[";
  if (!SearchOrder.empty()) {
    assert(SearchOrder.front().first &&
           "JITDylibList entries must not be null");
    OS << " (\"" << SearchOrder.front().first->getName() << "\", "
       << SearchOrder.begin()->second << ")";
    for (auto &KV : llvm::drop_begin(SearchOrder)) {
      assert(KV.first && "JITDylibList entries must not be null");
      OS << ", (\"" << KV.first->getName() << "\", " << KV.second << ")";
    }
  }
  OS << " ]";
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolAliasMap &Aliases) {
  OS << "{";
  for (auto &KV : Aliases)
    OS << " " << *KV.first << ": " << KV.second.Aliasee << " "
       << KV.second.AliasFlags;
  OS << " }";
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolState &S) {
  switch (S) {
  case SymbolState::Invalid:
    return OS << "Invalid";
  case SymbolState::NeverSearched:
    return OS << "Never-Searched";
  case SymbolState::Materializing:
    return OS << "Materializing";
  case SymbolState::Resolved:
    return OS << "Resolved";
  case SymbolState::Emitted:
    return OS << "Emitted";
  case SymbolState::Ready:
    return OS << "Ready";
  }
  llvm_unreachable("Invalid state");
}

raw_ostream &operator<<(raw_ostream &OS, const SymbolStringPool &SSP) {
  std::lock_guard<std::mutex> Lock(SSP.PoolMutex);
  SmallVector<std::pair<StringRef, int>, 0> Vec;
  for (auto &KV : SSP.Pool)
    Vec.emplace_back(KV.first(), KV.second);
  llvm::sort(Vec, less_first());
  for (auto &[K, V] : Vec)
    OS << K << ": " << V << "\n";
  return OS;
}

DumpObjects::DumpObjects(std::string DumpDir, std::string IdentifierOverride)
    : DumpDir(std::move(DumpDir)),
      IdentifierOverride(std::move(IdentifierOverride)) {

  /// Discard any trailing separators.
  while (!this->DumpDir.empty() &&
         sys::path::is_separator(this->DumpDir.back()))
    this->DumpDir.pop_back();
}

Expected<std::unique_ptr<MemoryBuffer>>
DumpObjects::operator()(std::unique_ptr<MemoryBuffer> Obj) {
  size_t Idx = 1;

  std::string DumpPathStem;
  raw_string_ostream(DumpPathStem)
      << DumpDir << (DumpDir.empty() ? "" : "/") << getBufferIdentifier(*Obj);

  std::string DumpPath = DumpPathStem + ".o";
  while (sys::fs::exists(DumpPath)) {
    DumpPath.clear();
    raw_string_ostream(DumpPath) << DumpPathStem << "." << (++Idx) << ".o";
  }

  LLVM_DEBUG({
    dbgs() << "Dumping object buffer [ " << (const void *)Obj->getBufferStart()
           << " -- " << (const void *)(Obj->getBufferEnd() - 1) << " ] to "
           << DumpPath << "\n";
  });

  std::error_code EC;
  raw_fd_ostream DumpStream(DumpPath, EC);
  if (EC)
    return errorCodeToError(EC);
  DumpStream.write(Obj->getBufferStart(), Obj->getBufferSize());

  return std::move(Obj);
}

StringRef DumpObjects::getBufferIdentifier(MemoryBuffer &B) {
  if (!IdentifierOverride.empty())
    return IdentifierOverride;
  StringRef Identifier = B.getBufferIdentifier();
  Identifier.consume_back(".o");
  return Identifier;
}

} // End namespace orc.
} // End namespace llvm.
