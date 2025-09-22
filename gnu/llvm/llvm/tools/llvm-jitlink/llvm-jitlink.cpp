//===- llvm-jitlink.cpp -- Command line interface/tester for llvm-jitlink -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility provides a simple command line interface to the llvm jitlink
// library, which makes relocatable object files executable in memory. Its
// primary function is as a testing utility for the jitlink library.
//
//===----------------------------------------------------------------------===//

#include "llvm-jitlink.h"

#include "llvm/BinaryFormat/Magic.h"
#include "llvm/ExecutionEngine/Orc/COFFPlatform.h"
#include "llvm/ExecutionEngine/Orc/COFFVCRuntimeSupport.h"
#include "llvm/ExecutionEngine/Orc/DebugObjectManagerPlugin.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebugInfoSupport.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebuggerSupportPlugin.h"
#include "llvm/ExecutionEngine/Orc/Debugging/PerfSupportPlugin.h"
#include "llvm/ExecutionEngine/Orc/Debugging/VTuneSupportPlugin.h"
#include "llvm/ExecutionEngine/Orc/ELFNixPlatform.h"
#include "llvm/ExecutionEngine/Orc/EPCDebugObjectRegistrar.h"
#include "llvm/ExecutionEngine/Orc/EPCDynamicLibrarySearchGenerator.h"
#include "llvm/ExecutionEngine/Orc/EPCEHFrameRegistrar.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/MachOPlatform.h"
#include "llvm/ExecutionEngine/Orc/MapperJITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/ObjectFileInterface.h"
#include "llvm/ExecutionEngine/Orc/SectCreate.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderGDB.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderPerf.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderVTune.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"

#include <cstring>
#include <deque>
#include <string>

#ifdef LLVM_ON_UNIX
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif // LLVM_ON_UNIX

#define DEBUG_TYPE "llvm_jitlink"

using namespace llvm;
using namespace llvm::jitlink;
using namespace llvm::orc;

static cl::OptionCategory JITLinkCategory("JITLink Options");

static cl::list<std::string> InputFiles(cl::Positional, cl::OneOrMore,
                                        cl::desc("input files"),
                                        cl::cat(JITLinkCategory));

static cl::list<std::string>
    LibrarySearchPaths("L",
                       cl::desc("Add dir to the list of library search paths"),
                       cl::Prefix, cl::cat(JITLinkCategory));

static cl::list<std::string>
    Libraries("l",
              cl::desc("Link against library X in the library search paths"),
              cl::Prefix, cl::cat(JITLinkCategory));

static cl::list<std::string>
    LibrariesHidden("hidden-l",
                    cl::desc("Link against library X in the library search "
                             "paths with hidden visibility"),
                    cl::Prefix, cl::cat(JITLinkCategory));

static cl::list<std::string>
    LoadHidden("load_hidden",
               cl::desc("Link against library X with hidden visibility"),
               cl::cat(JITLinkCategory));

static cl::opt<bool> SearchSystemLibrary(
    "search-sys-lib",
    cl::desc("Add system library paths to library search paths"),
    cl::init(false), cl::cat(JITLinkCategory));

static cl::opt<bool> NoExec("noexec", cl::desc("Do not execute loaded code"),
                            cl::init(false), cl::cat(JITLinkCategory));

static cl::list<std::string>
    CheckFiles("check", cl::desc("File containing verifier checks"),
               cl::cat(JITLinkCategory));

static cl::opt<std::string>
    CheckName("check-name", cl::desc("Name of checks to match against"),
              cl::init("jitlink-check"), cl::cat(JITLinkCategory));

static cl::opt<std::string>
    EntryPointName("entry", cl::desc("Symbol to call as main entry point"),
                   cl::init(""), cl::cat(JITLinkCategory));

static cl::list<std::string> JITDylibs(
    "jd",
    cl::desc("Specifies the JITDylib to be used for any subsequent "
             "input file, -L<seacrh-path>, and -l<library> arguments"),
    cl::cat(JITLinkCategory));

static cl::list<std::string>
    Dylibs("preload",
           cl::desc("Pre-load dynamic libraries (e.g. language runtimes "
                    "required by the ORC runtime)"),
           cl::cat(JITLinkCategory));

static cl::list<std::string> InputArgv("args", cl::Positional,
                                       cl::desc("<program arguments>..."),
                                       cl::PositionalEatsArgs,
                                       cl::cat(JITLinkCategory));

static cl::opt<bool>
    DebuggerSupport("debugger-support",
                    cl::desc("Enable debugger suppport (default = !-noexec)"),
                    cl::init(true), cl::Hidden, cl::cat(JITLinkCategory));

static cl::opt<bool> PerfSupport("perf-support",
                                 cl::desc("Enable perf profiling support"),
                                 cl::init(false), cl::Hidden,
                                 cl::cat(JITLinkCategory));

static cl::opt<bool> VTuneSupport("vtune-support",
                                  cl::desc("Enable vtune profiling support"),
                                  cl::init(false), cl::Hidden,
                                  cl::cat(JITLinkCategory));
static cl::opt<bool>
    NoProcessSymbols("no-process-syms",
                     cl::desc("Do not resolve to llvm-jitlink process symbols"),
                     cl::init(false), cl::cat(JITLinkCategory));

static cl::list<std::string> AbsoluteDefs(
    "abs",
    cl::desc("Inject absolute symbol definitions (syntax: <name>=<addr>)"),
    cl::cat(JITLinkCategory));

static cl::list<std::string>
    Aliases("alias",
            cl::desc("Inject symbol aliases (syntax: <alias-name>=<aliasee>)"),
            cl::cat(JITLinkCategory));

static cl::list<std::string>
    SectCreate("sectcreate",
               cl::desc("given <sectname>,<filename>[@<sym>=<offset>,...]  "
                        "add the content of <filename> to <sectname>"),
               cl::cat(JITLinkCategory));

static cl::list<std::string> TestHarnesses("harness", cl::Positional,
                                           cl::desc("Test harness files"),
                                           cl::PositionalEatsArgs,
                                           cl::cat(JITLinkCategory));

static cl::opt<bool> ShowInitialExecutionSessionState(
    "show-init-es",
    cl::desc("Print ExecutionSession state before resolving entry point"),
    cl::init(false), cl::cat(JITLinkCategory));

static cl::opt<bool> ShowEntryExecutionSessionState(
    "show-entry-es",
    cl::desc("Print ExecutionSession state after resolving entry point"),
    cl::init(false), cl::cat(JITLinkCategory));

static cl::opt<bool> ShowAddrs(
    "show-addrs",
    cl::desc("Print registered symbol, section, got and stub addresses"),
    cl::init(false), cl::cat(JITLinkCategory));

static cl::opt<std::string> ShowLinkGraphs(
    "show-graphs",
    cl::desc("Takes a posix regex and prints the link graphs of all files "
             "matching that regex after fixups have been applied"),
    cl::Optional, cl::cat(JITLinkCategory));

static cl::opt<bool> ShowTimes("show-times",
                               cl::desc("Show times for llvm-jitlink phases"),
                               cl::init(false), cl::cat(JITLinkCategory));

static cl::opt<std::string> SlabAllocateSizeString(
    "slab-allocate",
    cl::desc("Allocate from a slab of the given size "
             "(allowable suffixes: Kb, Mb, Gb. default = "
             "Kb)"),
    cl::init(""), cl::cat(JITLinkCategory));

static cl::opt<uint64_t> SlabAddress(
    "slab-address",
    cl::desc("Set slab target address (requires -slab-allocate and -noexec)"),
    cl::init(~0ULL), cl::cat(JITLinkCategory));

static cl::opt<uint64_t> SlabPageSize(
    "slab-page-size",
    cl::desc("Set page size for slab (requires -slab-allocate and -noexec)"),
    cl::init(0), cl::cat(JITLinkCategory));

static cl::opt<bool> ShowRelocatedSectionContents(
    "show-relocated-section-contents",
    cl::desc("show section contents after fixups have been applied"),
    cl::init(false), cl::cat(JITLinkCategory));

static cl::opt<bool> PhonyExternals(
    "phony-externals",
    cl::desc("resolve all otherwise unresolved externals to null"),
    cl::init(false), cl::cat(JITLinkCategory));

static cl::opt<std::string> OutOfProcessExecutor(
    "oop-executor", cl::desc("Launch an out-of-process executor to run code"),
    cl::ValueOptional, cl::cat(JITLinkCategory));

static cl::opt<std::string> OutOfProcessExecutorConnect(
    "oop-executor-connect",
    cl::desc("Connect to an out-of-process executor via TCP"),
    cl::cat(JITLinkCategory));

static cl::opt<std::string>
    OrcRuntime("orc-runtime", cl::desc("Use ORC runtime from given path"),
               cl::init(""), cl::cat(JITLinkCategory));

static cl::opt<bool> AddSelfRelocations(
    "add-self-relocations",
    cl::desc("Add relocations to function pointers to the current function"),
    cl::init(false), cl::cat(JITLinkCategory));

static cl::opt<bool>
    ShowErrFailedToMaterialize("show-err-failed-to-materialize",
                               cl::desc("Show FailedToMaterialize errors"),
                               cl::init(false), cl::cat(JITLinkCategory));

static cl::opt<bool> UseSharedMemory(
    "use-shared-memory",
    cl::desc("Use shared memory to transfer generated code and data"),
    cl::init(false), cl::cat(JITLinkCategory));

static ExitOnError ExitOnErr;

static LLVM_ATTRIBUTE_USED void linkComponents() {
  errs() << "Linking in runtime functions\n"
         << (void *)&llvm_orc_registerEHFrameSectionWrapper << '\n'
         << (void *)&llvm_orc_deregisterEHFrameSectionWrapper << '\n'
         << (void *)&llvm_orc_registerJITLoaderGDBWrapper << '\n'
         << (void *)&llvm_orc_registerJITLoaderGDBAllocAction << '\n'
         << (void *)&llvm_orc_registerJITLoaderPerfStart << '\n'
         << (void *)&llvm_orc_registerJITLoaderPerfEnd << '\n'
         << (void *)&llvm_orc_registerJITLoaderPerfImpl << '\n'
         << (void *)&llvm_orc_registerVTuneImpl << '\n'
         << (void *)&llvm_orc_unregisterVTuneImpl << '\n'
         << (void *)&llvm_orc_test_registerVTuneImpl << '\n';
}

static bool UseTestResultOverride = false;
static int64_t TestResultOverride = 0;

extern "C" LLVM_ATTRIBUTE_USED void
llvm_jitlink_setTestResultOverride(int64_t Value) {
  TestResultOverride = Value;
  UseTestResultOverride = true;
}

static Error addSelfRelocations(LinkGraph &G);

namespace {

template <typename ErrT>

class ConditionalPrintErr {
public:
  ConditionalPrintErr(bool C) : C(C) {}
  void operator()(ErrT &EI) {
    if (C) {
      errs() << "llvm-jitlink error: ";
      EI.log(errs());
      errs() << "\n";
    }
  }

private:
  bool C;
};

Expected<std::unique_ptr<MemoryBuffer>> getFile(const Twine &FileName) {
  if (auto F = MemoryBuffer::getFile(FileName))
    return std::move(*F);
  else
    return createFileError(FileName, F.getError());
}

void reportLLVMJITLinkError(Error Err) {
  handleAllErrors(
      std::move(Err),
      ConditionalPrintErr<orc::FailedToMaterialize>(ShowErrFailedToMaterialize),
      ConditionalPrintErr<ErrorInfoBase>(true));
}

} // end anonymous namespace

namespace llvm {

static raw_ostream &
operator<<(raw_ostream &OS, const Session::MemoryRegionInfo &MRI) {
  return OS << "target addr = "
            << format("0x%016" PRIx64, MRI.getTargetAddress())
            << ", content: " << (const void *)MRI.getContent().data() << " -- "
            << (const void *)(MRI.getContent().data() + MRI.getContent().size())
            << " (" << MRI.getContent().size() << " bytes)";
}

static raw_ostream &
operator<<(raw_ostream &OS, const Session::SymbolInfoMap &SIM) {
  OS << "Symbols:\n";
  for (auto &SKV : SIM)
    OS << "  \"" << SKV.first() << "\" " << SKV.second << "\n";
  return OS;
}

static raw_ostream &
operator<<(raw_ostream &OS, const Session::FileInfo &FI) {
  for (auto &SIKV : FI.SectionInfos)
    OS << "  Section \"" << SIKV.first() << "\": " << SIKV.second << "\n";
  for (auto &GOTKV : FI.GOTEntryInfos)
    OS << "  GOT \"" << GOTKV.first() << "\": " << GOTKV.second << "\n";
  for (auto &StubKVs : FI.StubInfos) {
    OS << "  Stubs \"" << StubKVs.first() << "\":";
    for (auto MemRegion : StubKVs.second)
      OS << " " << MemRegion;
    OS << "\n";
  }
  return OS;
}

static raw_ostream &
operator<<(raw_ostream &OS, const Session::FileInfoMap &FIM) {
  for (auto &FIKV : FIM)
    OS << "File \"" << FIKV.first() << "\":\n" << FIKV.second;
  return OS;
}

static Error applyHarnessPromotions(Session &S, LinkGraph &G) {

  // If this graph is part of the test harness there's nothing to do.
  if (S.HarnessFiles.empty() || S.HarnessFiles.count(G.getName()))
    return Error::success();

  LLVM_DEBUG(dbgs() << "Applying promotions to graph " << G.getName() << "\n");

  // If this graph is part of the test then promote any symbols referenced by
  // the harness to default scope, remove all symbols that clash with harness
  // definitions.
  std::vector<Symbol *> DefinitionsToRemove;
  for (auto *Sym : G.defined_symbols()) {

    if (!Sym->hasName())
      continue;

    if (Sym->getLinkage() == Linkage::Weak) {
      if (!S.CanonicalWeakDefs.count(Sym->getName()) ||
          S.CanonicalWeakDefs[Sym->getName()] != G.getName()) {
        LLVM_DEBUG({
          dbgs() << "  Externalizing weak symbol " << Sym->getName() << "\n";
        });
        DefinitionsToRemove.push_back(Sym);
      } else {
        LLVM_DEBUG({
          dbgs() << "  Making weak symbol " << Sym->getName() << " strong\n";
        });
        if (S.HarnessExternals.count(Sym->getName()))
          Sym->setScope(Scope::Default);
        else
          Sym->setScope(Scope::Hidden);
        Sym->setLinkage(Linkage::Strong);
      }
    } else if (S.HarnessExternals.count(Sym->getName())) {
      LLVM_DEBUG(dbgs() << "  Promoting " << Sym->getName() << "\n");
      Sym->setScope(Scope::Default);
      Sym->setLive(true);
      continue;
    } else if (S.HarnessDefinitions.count(Sym->getName())) {
      LLVM_DEBUG(dbgs() << "  Externalizing " << Sym->getName() << "\n");
      DefinitionsToRemove.push_back(Sym);
    }
  }

  for (auto *Sym : DefinitionsToRemove)
    G.makeExternal(*Sym);

  return Error::success();
}

static void dumpSectionContents(raw_ostream &OS, LinkGraph &G) {
  constexpr orc::ExecutorAddrDiff DumpWidth = 16;
  static_assert(isPowerOf2_64(DumpWidth), "DumpWidth must be a power of two");

  // Put sections in address order.
  std::vector<Section *> Sections;
  for (auto &S : G.sections())
    Sections.push_back(&S);

  llvm::sort(Sections, [](const Section *LHS, const Section *RHS) {
    if (LHS->symbols().empty() && RHS->symbols().empty())
      return false;
    if (LHS->symbols().empty())
      return false;
    if (RHS->symbols().empty())
      return true;
    SectionRange LHSRange(*LHS);
    SectionRange RHSRange(*RHS);
    return LHSRange.getStart() < RHSRange.getStart();
  });

  for (auto *S : Sections) {
    OS << S->getName() << " content:";
    if (S->symbols().empty()) {
      OS << "\n  section empty\n";
      continue;
    }

    // Sort symbols into order, then render.
    std::vector<Symbol *> Syms(S->symbols().begin(), S->symbols().end());
    llvm::sort(Syms, [](const Symbol *LHS, const Symbol *RHS) {
      return LHS->getAddress() < RHS->getAddress();
    });

    orc::ExecutorAddr NextAddr(Syms.front()->getAddress().getValue() &
                               ~(DumpWidth - 1));
    for (auto *Sym : Syms) {
      bool IsZeroFill = Sym->getBlock().isZeroFill();
      auto SymStart = Sym->getAddress();
      auto SymSize = Sym->getSize();
      auto SymEnd = SymStart + SymSize;
      const uint8_t *SymData = IsZeroFill ? nullptr
                                          : reinterpret_cast<const uint8_t *>(
                                                Sym->getSymbolContent().data());

      // Pad any space before the symbol starts.
      while (NextAddr != SymStart) {
        if (NextAddr % DumpWidth == 0)
          OS << formatv("\n{0:x16}:", NextAddr);
        OS << "   ";
        ++NextAddr;
      }

      // Render the symbol content.
      while (NextAddr != SymEnd) {
        if (NextAddr % DumpWidth == 0)
          OS << formatv("\n{0:x16}:", NextAddr);
        if (IsZeroFill)
          OS << " 00";
        else
          OS << formatv(" {0:x-2}", SymData[NextAddr - SymStart]);
        ++NextAddr;
      }
    }
    OS << "\n";
  }
}

// A memory mapper with a fake offset applied only used for -noexec testing
class InProcessDeltaMapper final : public InProcessMemoryMapper {
public:
  InProcessDeltaMapper(size_t PageSize, uint64_t TargetAddr)
      : InProcessMemoryMapper(PageSize), TargetMapAddr(TargetAddr),
        DeltaAddr(0) {}

  static Expected<std::unique_ptr<InProcessDeltaMapper>> Create() {
    size_t PageSize = SlabPageSize;
    if (!PageSize) {
      if (auto PageSizeOrErr = sys::Process::getPageSize())
        PageSize = *PageSizeOrErr;
      else
        return PageSizeOrErr.takeError();
    }

    if (PageSize == 0)
      return make_error<StringError>("Page size is zero",
                                     inconvertibleErrorCode());

    return std::make_unique<InProcessDeltaMapper>(PageSize, SlabAddress);
  }

  void reserve(size_t NumBytes, OnReservedFunction OnReserved) override {
    InProcessMemoryMapper::reserve(
        NumBytes, [this, OnReserved = std::move(OnReserved)](
                      Expected<ExecutorAddrRange> Result) mutable {
          if (!Result)
            return OnReserved(Result.takeError());

          assert(DeltaAddr == 0 && "Overwriting previous offset");
          if (TargetMapAddr != ~0ULL)
            DeltaAddr = TargetMapAddr - Result->Start.getValue();
          auto OffsetRange = ExecutorAddrRange(Result->Start + DeltaAddr,
                                               Result->End + DeltaAddr);

          OnReserved(OffsetRange);
        });
  }

  char *prepare(ExecutorAddr Addr, size_t ContentSize) override {
    return InProcessMemoryMapper::prepare(Addr - DeltaAddr, ContentSize);
  }

  void initialize(AllocInfo &AI, OnInitializedFunction OnInitialized) override {
    // Slide mapping based on delta, make all segments read-writable, and
    // discard allocation actions.
    auto FixedAI = std::move(AI);
    FixedAI.MappingBase -= DeltaAddr;
    for (auto &Seg : FixedAI.Segments)
      Seg.AG = {MemProt::Read | MemProt::Write, Seg.AG.getMemLifetime()};
    FixedAI.Actions.clear();
    InProcessMemoryMapper::initialize(
        FixedAI, [this, OnInitialized = std::move(OnInitialized)](
                     Expected<ExecutorAddr> Result) mutable {
          if (!Result)
            return OnInitialized(Result.takeError());

          OnInitialized(ExecutorAddr(Result->getValue() + DeltaAddr));
        });
  }

  void deinitialize(ArrayRef<ExecutorAddr> Allocations,
                    OnDeinitializedFunction OnDeInitialized) override {
    std::vector<ExecutorAddr> Addrs(Allocations.size());
    for (const auto Base : Allocations) {
      Addrs.push_back(Base - DeltaAddr);
    }

    InProcessMemoryMapper::deinitialize(Addrs, std::move(OnDeInitialized));
  }

  void release(ArrayRef<ExecutorAddr> Reservations,
               OnReleasedFunction OnRelease) override {
    std::vector<ExecutorAddr> Addrs(Reservations.size());
    for (const auto Base : Reservations) {
      Addrs.push_back(Base - DeltaAddr);
    }
    InProcessMemoryMapper::release(Addrs, std::move(OnRelease));
  }

private:
  uint64_t TargetMapAddr;
  uint64_t DeltaAddr;
};

Expected<uint64_t> getSlabAllocSize(StringRef SizeString) {
  SizeString = SizeString.trim();

  uint64_t Units = 1024;

  if (SizeString.ends_with_insensitive("kb"))
    SizeString = SizeString.drop_back(2).rtrim();
  else if (SizeString.ends_with_insensitive("mb")) {
    Units = 1024 * 1024;
    SizeString = SizeString.drop_back(2).rtrim();
  } else if (SizeString.ends_with_insensitive("gb")) {
    Units = 1024 * 1024 * 1024;
    SizeString = SizeString.drop_back(2).rtrim();
  }

  uint64_t SlabSize = 0;
  if (SizeString.getAsInteger(10, SlabSize))
    return make_error<StringError>("Invalid numeric format for slab size",
                                   inconvertibleErrorCode());

  return SlabSize * Units;
}

static std::unique_ptr<JITLinkMemoryManager> createInProcessMemoryManager() {
  uint64_t SlabSize;
#ifdef _WIN32
  SlabSize = 1024 * 1024;
#else
  SlabSize = 1024 * 1024 * 1024;
#endif

  if (!SlabAllocateSizeString.empty())
    SlabSize = ExitOnErr(getSlabAllocSize(SlabAllocateSizeString));

  // If this is a -no-exec case and we're tweaking the slab address or size then
  // use the delta mapper.
  if (NoExec && (SlabAddress || SlabPageSize))
    return ExitOnErr(
        MapperJITLinkMemoryManager::CreateWithMapper<InProcessDeltaMapper>(
            SlabSize));

  // Otherwise use the standard in-process mapper.
  return ExitOnErr(
      MapperJITLinkMemoryManager::CreateWithMapper<InProcessMemoryMapper>(
          SlabSize));
}

Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
createSharedMemoryManager(SimpleRemoteEPC &SREPC) {
  SharedMemoryMapper::SymbolAddrs SAs;
  if (auto Err = SREPC.getBootstrapSymbols(
          {{SAs.Instance, rt::ExecutorSharedMemoryMapperServiceInstanceName},
           {SAs.Reserve,
            rt::ExecutorSharedMemoryMapperServiceReserveWrapperName},
           {SAs.Initialize,
            rt::ExecutorSharedMemoryMapperServiceInitializeWrapperName},
           {SAs.Deinitialize,
            rt::ExecutorSharedMemoryMapperServiceDeinitializeWrapperName},
           {SAs.Release,
            rt::ExecutorSharedMemoryMapperServiceReleaseWrapperName}}))
    return std::move(Err);

#ifdef _WIN32
  size_t SlabSize = 1024 * 1024;
#else
  size_t SlabSize = 1024 * 1024 * 1024;
#endif

  if (!SlabAllocateSizeString.empty())
    SlabSize = ExitOnErr(getSlabAllocSize(SlabAllocateSizeString));

  return MapperJITLinkMemoryManager::CreateWithMapper<SharedMemoryMapper>(
      SlabSize, SREPC, SAs);
}


static Expected<MaterializationUnit::Interface>
getTestObjectFileInterface(Session &S, MemoryBufferRef O) {

  // Get the standard interface for this object, but ignore the symbols field.
  // We'll handle that manually to include promotion.
  auto I = getObjectFileInterface(S.ES, O);
  if (!I)
    return I.takeError();
  I->SymbolFlags.clear();

  // If creating an object file was going to fail it would have happened above,
  // so we can 'cantFail' this.
  auto Obj = cantFail(object::ObjectFile::createObjectFile(O));

  // The init symbol must be included in the SymbolFlags map if present.
  if (I->InitSymbol)
    I->SymbolFlags[I->InitSymbol] =
        JITSymbolFlags::MaterializationSideEffectsOnly;

  for (auto &Sym : Obj->symbols()) {
    Expected<uint32_t> SymFlagsOrErr = Sym.getFlags();
    if (!SymFlagsOrErr)
      // TODO: Test this error.
      return SymFlagsOrErr.takeError();

    // Skip symbols not defined in this object file.
    if ((*SymFlagsOrErr & object::BasicSymbolRef::SF_Undefined))
      continue;

    auto Name = Sym.getName();
    if (!Name)
      return Name.takeError();

    // Skip symbols that have type SF_File.
    if (auto SymType = Sym.getType()) {
      if (*SymType == object::SymbolRef::ST_File)
        continue;
    } else
      return SymType.takeError();

    auto SymFlags = JITSymbolFlags::fromObjectSymbol(Sym);
    if (!SymFlags)
      return SymFlags.takeError();

    if (SymFlags->isWeak()) {
      // If this is a weak symbol that's not defined in the harness then we
      // need to either mark it as strong (if this is the first definition
      // that we've seen) or discard it.
      if (S.HarnessDefinitions.count(*Name) || S.CanonicalWeakDefs.count(*Name))
        continue;
      S.CanonicalWeakDefs[*Name] = O.getBufferIdentifier();
      *SymFlags &= ~JITSymbolFlags::Weak;
      if (!S.HarnessExternals.count(*Name))
        *SymFlags &= ~JITSymbolFlags::Exported;
    } else if (S.HarnessExternals.count(*Name)) {
      *SymFlags |= JITSymbolFlags::Exported;
    } else if (S.HarnessDefinitions.count(*Name) ||
               !(*SymFlagsOrErr & object::BasicSymbolRef::SF_Global))
      continue;

    auto InternedName = S.ES.intern(*Name);
    I->SymbolFlags[InternedName] = std::move(*SymFlags);
  }

  return I;
}

static Error loadProcessSymbols(Session &S) {
  S.ProcessSymsJD = &S.ES.createBareJITDylib("Process");
  auto FilterMainEntryPoint =
      [EPName = S.ES.intern(EntryPointName)](SymbolStringPtr Name) {
        return Name != EPName;
      };
  S.ProcessSymsJD->addGenerator(
      ExitOnErr(orc::EPCDynamicLibrarySearchGenerator::GetForTargetProcess(
          S.ES, std::move(FilterMainEntryPoint))));

  return Error::success();
}

static Error loadDylibs(Session &S) {
  LLVM_DEBUG(dbgs() << "Loading dylibs...\n");
  for (const auto &Dylib : Dylibs) {
    LLVM_DEBUG(dbgs() << "  " << Dylib << "\n");
    auto DL = S.getOrLoadDynamicLibrary(Dylib);
    if (!DL)
      return DL.takeError();
  }

  return Error::success();
}

static Expected<std::unique_ptr<ExecutorProcessControl>> launchExecutor() {
#ifndef LLVM_ON_UNIX
  // FIXME: Add support for Windows.
  return make_error<StringError>("-" + OutOfProcessExecutor.ArgStr +
                                     " not supported on non-unix platforms",
                                 inconvertibleErrorCode());
#elif !LLVM_ENABLE_THREADS
  // Out of process mode using SimpleRemoteEPC depends on threads.
  return make_error<StringError>(
      "-" + OutOfProcessExecutor.ArgStr +
          " requires threads, but LLVM was built with "
          "LLVM_ENABLE_THREADS=Off",
      inconvertibleErrorCode());
#else

  constexpr int ReadEnd = 0;
  constexpr int WriteEnd = 1;

  // Pipe FDs.
  int ToExecutor[2];
  int FromExecutor[2];

  pid_t ChildPID;

  // Create pipes to/from the executor..
  if (pipe(ToExecutor) != 0 || pipe(FromExecutor) != 0)
    return make_error<StringError>("Unable to create pipe for executor",
                                   inconvertibleErrorCode());

  ChildPID = fork();

  if (ChildPID == 0) {
    // In the child...

    // Close the parent ends of the pipes
    close(ToExecutor[WriteEnd]);
    close(FromExecutor[ReadEnd]);

    // Execute the child process.
    std::unique_ptr<char[]> ExecutorPath, FDSpecifier;
    {
      ExecutorPath = std::make_unique<char[]>(OutOfProcessExecutor.size() + 1);
      strcpy(ExecutorPath.get(), OutOfProcessExecutor.data());

      std::string FDSpecifierStr("filedescs=");
      FDSpecifierStr += utostr(ToExecutor[ReadEnd]);
      FDSpecifierStr += ',';
      FDSpecifierStr += utostr(FromExecutor[WriteEnd]);
      FDSpecifier = std::make_unique<char[]>(FDSpecifierStr.size() + 1);
      strcpy(FDSpecifier.get(), FDSpecifierStr.c_str());
    }

    char *const Args[] = {ExecutorPath.get(), FDSpecifier.get(), nullptr};
    int RC = execvp(ExecutorPath.get(), Args);
    if (RC != 0) {
      errs() << "unable to launch out-of-process executor \""
             << ExecutorPath.get() << "\"\n";
      exit(1);
    }
  }
  // else we're the parent...

  // Close the child ends of the pipes
  close(ToExecutor[ReadEnd]);
  close(FromExecutor[WriteEnd]);

  auto S = SimpleRemoteEPC::Setup();
  if (UseSharedMemory)
    S.CreateMemoryManager = createSharedMemoryManager;

  return SimpleRemoteEPC::Create<FDSimpleRemoteEPCTransport>(
      std::make_unique<DynamicThreadPoolTaskDispatcher>(std::nullopt),
      std::move(S), FromExecutor[ReadEnd], ToExecutor[WriteEnd]);
#endif
}

#if LLVM_ON_UNIX && LLVM_ENABLE_THREADS
static Error createTCPSocketError(Twine Details) {
  return make_error<StringError>(
      formatv("Failed to connect TCP socket '{0}': {1}",
              OutOfProcessExecutorConnect, Details),
      inconvertibleErrorCode());
}

static Expected<int> connectTCPSocket(std::string Host, std::string PortStr) {
  addrinfo *AI;
  addrinfo Hints{};
  Hints.ai_family = AF_INET;
  Hints.ai_socktype = SOCK_STREAM;
  Hints.ai_flags = AI_NUMERICSERV;

  if (int EC = getaddrinfo(Host.c_str(), PortStr.c_str(), &Hints, &AI))
    return createTCPSocketError("Address resolution failed (" +
                                StringRef(gai_strerror(EC)) + ")");

  // Cycle through the returned addrinfo structures and connect to the first
  // reachable endpoint.
  int SockFD;
  addrinfo *Server;
  for (Server = AI; Server != nullptr; Server = Server->ai_next) {
    // socket might fail, e.g. if the address family is not supported. Skip to
    // the next addrinfo structure in such a case.
    if ((SockFD = socket(AI->ai_family, AI->ai_socktype, AI->ai_protocol)) < 0)
      continue;

    // If connect returns null, we exit the loop with a working socket.
    if (connect(SockFD, Server->ai_addr, Server->ai_addrlen) == 0)
      break;

    close(SockFD);
  }
  freeaddrinfo(AI);

  // If we reached the end of the loop without connecting to a valid endpoint,
  // dump the last error that was logged in socket() or connect().
  if (Server == nullptr)
    return createTCPSocketError(std::strerror(errno));

  return SockFD;
}
#endif

static Expected<std::unique_ptr<ExecutorProcessControl>> connectToExecutor() {
#ifndef LLVM_ON_UNIX
  // FIXME: Add TCP support for Windows.
  return make_error<StringError>("-" + OutOfProcessExecutorConnect.ArgStr +
                                     " not supported on non-unix platforms",
                                 inconvertibleErrorCode());
#elif !LLVM_ENABLE_THREADS
  // Out of process mode using SimpleRemoteEPC depends on threads.
  return make_error<StringError>(
      "-" + OutOfProcessExecutorConnect.ArgStr +
          " requires threads, but LLVM was built with "
          "LLVM_ENABLE_THREADS=Off",
      inconvertibleErrorCode());
#else

  StringRef Host, PortStr;
  std::tie(Host, PortStr) = StringRef(OutOfProcessExecutorConnect).split(':');
  if (Host.empty())
    return createTCPSocketError("Host name for -" +
                                OutOfProcessExecutorConnect.ArgStr +
                                " can not be empty");
  if (PortStr.empty())
    return createTCPSocketError("Port number in -" +
                                OutOfProcessExecutorConnect.ArgStr +
                                " can not be empty");
  int Port = 0;
  if (PortStr.getAsInteger(10, Port))
    return createTCPSocketError("Port number '" + PortStr +
                                "' is not a valid integer");

  Expected<int> SockFD = connectTCPSocket(Host.str(), PortStr.str());
  if (!SockFD)
    return SockFD.takeError();

  auto S = SimpleRemoteEPC::Setup();
  if (UseSharedMemory)
    S.CreateMemoryManager = createSharedMemoryManager;

  return SimpleRemoteEPC::Create<FDSimpleRemoteEPCTransport>(
      std::make_unique<DynamicThreadPoolTaskDispatcher>(std::nullopt),
      std::move(S), *SockFD, *SockFD);
#endif
}

class PhonyExternalsGenerator : public DefinitionGenerator {
public:
  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &LookupSet) override {
    SymbolMap PhonySymbols;
    for (auto &KV : LookupSet)
      PhonySymbols[KV.first] = {ExecutorAddr(), JITSymbolFlags::Exported};
    return JD.define(absoluteSymbols(std::move(PhonySymbols)));
  }
};

Expected<std::unique_ptr<Session>> Session::Create(Triple TT,
                                                   SubtargetFeatures Features) {

  std::unique_ptr<ExecutorProcessControl> EPC;
  if (OutOfProcessExecutor.getNumOccurrences()) {
    /// If -oop-executor is passed then launch the executor.
    if (auto REPC = launchExecutor())
      EPC = std::move(*REPC);
    else
      return REPC.takeError();
  } else if (OutOfProcessExecutorConnect.getNumOccurrences()) {
    /// If -oop-executor-connect is passed then connect to the executor.
    if (auto REPC = connectToExecutor())
      EPC = std::move(*REPC);
    else
      return REPC.takeError();
  } else {
    /// Otherwise use SelfExecutorProcessControl to target the current process.
    auto PageSize = sys::Process::getPageSize();
    if (!PageSize)
      return PageSize.takeError();
    EPC = std::make_unique<SelfExecutorProcessControl>(
        std::make_shared<SymbolStringPool>(),
        std::make_unique<InPlaceTaskDispatcher>(), std::move(TT), *PageSize,
        createInProcessMemoryManager());
  }

  Error Err = Error::success();
  std::unique_ptr<Session> S(new Session(std::move(EPC), Err));
  if (Err)
    return std::move(Err);
  S->Features = std::move(Features);
  return std::move(S);
}

Session::~Session() {
  if (auto Err = ES.endSession())
    ES.reportError(std::move(Err));
}

Session::Session(std::unique_ptr<ExecutorProcessControl> EPC, Error &Err)
    : ES(std::move(EPC)),
      ObjLayer(ES, ES.getExecutorProcessControl().getMemMgr()) {

  /// Local ObjectLinkingLayer::Plugin class to forward modifyPassConfig to the
  /// Session.
  class JITLinkSessionPlugin : public ObjectLinkingLayer::Plugin {
  public:
    JITLinkSessionPlugin(Session &S) : S(S) {}
    void modifyPassConfig(MaterializationResponsibility &MR, LinkGraph &G,
                          PassConfiguration &PassConfig) override {
      S.modifyPassConfig(G.getTargetTriple(), PassConfig);
    }

    Error notifyFailed(MaterializationResponsibility &MR) override {
      return Error::success();
    }
    Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
      return Error::success();
    }
    void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                     ResourceKey SrcKey) override {}

  private:
    Session &S;
  };

  ErrorAsOutParameter _(&Err);

  ES.setErrorReporter(reportLLVMJITLinkError);

  if (!NoProcessSymbols)
    ExitOnErr(loadProcessSymbols(*this));

  ExitOnErr(loadDylibs(*this));

  auto &TT = ES.getTargetTriple();

  if (DebuggerSupport && TT.isOSBinFormatMachO()) {
    if (!ProcessSymsJD) {
      Err = make_error<StringError>("MachO debugging requires process symbols",
                                    inconvertibleErrorCode());
      return;
    }
    ObjLayer.addPlugin(ExitOnErr(GDBJITDebugInfoRegistrationPlugin::Create(
        this->ES, *ProcessSymsJD, TT)));
  }

  if (PerfSupport && TT.isOSBinFormatELF()) {
    if (!ProcessSymsJD) {
      Err = make_error<StringError>("MachO debugging requires process symbols",
                                    inconvertibleErrorCode());
      return;
    }
    ObjLayer.addPlugin(ExitOnErr(DebugInfoPreservationPlugin::Create()));
    ObjLayer.addPlugin(ExitOnErr(PerfSupportPlugin::Create(
        this->ES.getExecutorProcessControl(), *ProcessSymsJD, true, true)));
  }

  if (VTuneSupport && TT.isOSBinFormatELF()) {
    ObjLayer.addPlugin(ExitOnErr(DebugInfoPreservationPlugin::Create()));
    ObjLayer.addPlugin(ExitOnErr(
        VTuneSupportPlugin::Create(this->ES.getExecutorProcessControl(),
                                   *ProcessSymsJD, /*EmitDebugInfo=*/true,
                                   /*TestMode=*/true)));
  }

  // Set up the platform.
  if (!OrcRuntime.empty()) {
    assert(ProcessSymsJD && "ProcessSymsJD should have been set");
    PlatformJD = &ES.createBareJITDylib("Platform");
    PlatformJD->addToLinkOrder(*ProcessSymsJD);

    if (TT.isOSBinFormatMachO()) {
      if (auto P = MachOPlatform::Create(ES, ObjLayer, *PlatformJD,
                                         OrcRuntime.c_str()))
        ES.setPlatform(std::move(*P));
      else {
        Err = P.takeError();
        return;
      }
    } else if (TT.isOSBinFormatELF()) {
      if (auto P = ELFNixPlatform::Create(ES, ObjLayer, *PlatformJD,
                                          OrcRuntime.c_str()))
        ES.setPlatform(std::move(*P));
      else {
        Err = P.takeError();
        return;
      }
    } else if (TT.isOSBinFormatCOFF()) {
      auto LoadDynLibrary = [&, this](JITDylib &JD,
                                      StringRef DLLName) -> Error {
        if (!DLLName.ends_with_insensitive(".dll"))
          return make_error<StringError>("DLLName not ending with .dll",
                                         inconvertibleErrorCode());
        return loadAndLinkDynamicLibrary(JD, DLLName);
      };

      if (auto P = COFFPlatform::Create(ES, ObjLayer, *PlatformJD,
                                        OrcRuntime.c_str(),
                                        std::move(LoadDynLibrary)))
        ES.setPlatform(std::move(*P));
      else {
        Err = P.takeError();
        return;
      }
    } else {
      Err = make_error<StringError>(
          "-" + OrcRuntime.ArgStr + " specified, but format " +
              Triple::getObjectFormatTypeName(TT.getObjectFormat()) +
              " not supported",
          inconvertibleErrorCode());
      return;
    }
  } else if (TT.isOSBinFormatELF()) {
    if (!NoExec)
      ObjLayer.addPlugin(std::make_unique<EHFrameRegistrationPlugin>(
          ES, ExitOnErr(EPCEHFrameRegistrar::Create(this->ES))));
    if (DebuggerSupport)
      ObjLayer.addPlugin(std::make_unique<DebugObjectManagerPlugin>(
          ES, ExitOnErr(createJITLoaderGDBRegistrar(this->ES)), true, true));
  }

  if (auto MainJDOrErr = ES.createJITDylib("main"))
    MainJD = &*MainJDOrErr;
  else {
    Err = MainJDOrErr.takeError();
    return;
  }

  if (NoProcessSymbols) {
    // This symbol is used in testcases, but we're not reflecting process
    // symbols so we'll need to make it available some other way.
    auto &TestResultJD = ES.createBareJITDylib("<TestResultJD>");
    ExitOnErr(TestResultJD.define(absoluteSymbols(
        {{ES.intern("llvm_jitlink_setTestResultOverride"),
          {ExecutorAddr::fromPtr(llvm_jitlink_setTestResultOverride),
           JITSymbolFlags::Exported}}})));
    MainJD->addToLinkOrder(TestResultJD);
  }

  ObjLayer.addPlugin(std::make_unique<JITLinkSessionPlugin>(*this));

  // Process any harness files.
  for (auto &HarnessFile : TestHarnesses) {
    HarnessFiles.insert(HarnessFile);

    auto ObjBuffer = ExitOnErr(getFile(HarnessFile));

    auto ObjInterface =
        ExitOnErr(getObjectFileInterface(ES, ObjBuffer->getMemBufferRef()));

    for (auto &KV : ObjInterface.SymbolFlags)
      HarnessDefinitions.insert(*KV.first);

    auto Obj = ExitOnErr(
        object::ObjectFile::createObjectFile(ObjBuffer->getMemBufferRef()));

    for (auto &Sym : Obj->symbols()) {
      uint32_t SymFlags = ExitOnErr(Sym.getFlags());
      auto Name = ExitOnErr(Sym.getName());

      if (Name.empty())
        continue;

      if (SymFlags & object::BasicSymbolRef::SF_Undefined)
        HarnessExternals.insert(Name);
    }
  }

  // If a name is defined by some harness file then it's a definition, not an
  // external.
  for (auto &DefName : HarnessDefinitions)
    HarnessExternals.erase(DefName.getKey());

  if (!ShowLinkGraphs.empty())
    ShowGraphsRegex = Regex(ShowLinkGraphs);
}

void Session::dumpSessionInfo(raw_ostream &OS) {
  OS << "Registered addresses:\n" << SymbolInfos << FileInfos;
}

void Session::modifyPassConfig(const Triple &TT,
                               PassConfiguration &PassConfig) {
  if (!CheckFiles.empty())
    PassConfig.PostFixupPasses.push_back([this](LinkGraph &G) {
      if (ES.getTargetTriple().getObjectFormat() == Triple::ELF)
        return registerELFGraphInfo(*this, G);

      if (ES.getTargetTriple().getObjectFormat() == Triple::MachO)
        return registerMachOGraphInfo(*this, G);

      if (ES.getTargetTriple().getObjectFormat() == Triple::COFF)
        return registerCOFFGraphInfo(*this, G);

      return make_error<StringError>("Unsupported object format for GOT/stub "
                                     "registration",
                                     inconvertibleErrorCode());
    });

  if (ShowGraphsRegex)
    PassConfig.PostFixupPasses.push_back([this](LinkGraph &G) -> Error {
      // Print graph if ShowLinkGraphs is specified-but-empty, or if
      // it contains the given graph.
      if (ShowGraphsRegex->match(G.getName())) {
        outs() << "Link graph \"" << G.getName() << "\" post-fixup:\n";
        G.dump(outs());
      }
      return Error::success();
    });

  PassConfig.PrePrunePasses.push_back(
      [this](LinkGraph &G) { return applyHarnessPromotions(*this, G); });

  if (ShowRelocatedSectionContents)
    PassConfig.PostFixupPasses.push_back([](LinkGraph &G) -> Error {
      outs() << "Relocated section contents for " << G.getName() << ":\n";
      dumpSectionContents(outs(), G);
      return Error::success();
    });

  if (AddSelfRelocations)
    PassConfig.PostPrunePasses.push_back(addSelfRelocations);
}

Expected<JITDylib *> Session::getOrLoadDynamicLibrary(StringRef LibPath) {
  auto It = DynLibJDs.find(LibPath.str());
  if (It != DynLibJDs.end()) {
    return It->second;
  }
  auto G = EPCDynamicLibrarySearchGenerator::Load(ES, LibPath.data());
  if (!G)
    return G.takeError();
  auto JD = &ES.createBareJITDylib(LibPath.str());

  JD->addGenerator(std::move(*G));
  DynLibJDs.emplace(LibPath.str(), JD);
  LLVM_DEBUG({
    dbgs() << "Loaded dynamic library " << LibPath.data() << " for " << LibPath
           << "\n";
  });
  return JD;
}

Error Session::loadAndLinkDynamicLibrary(JITDylib &JD, StringRef LibPath) {
  auto DL = getOrLoadDynamicLibrary(LibPath);
  if (!DL)
    return DL.takeError();
  JD.addToLinkOrder(**DL);
  LLVM_DEBUG({
    dbgs() << "Linking dynamic library " << LibPath << " to " << JD.getName()
           << "\n";
  });
  return Error::success();
}

Error Session::FileInfo::registerGOTEntry(
    LinkGraph &G, Symbol &Sym, GetSymbolTargetFunction GetSymbolTarget) {
  if (Sym.isSymbolZeroFill())
    return make_error<StringError>("Unexpected zero-fill symbol in section " +
                                       Sym.getBlock().getSection().getName(),
                                   inconvertibleErrorCode());
  auto TS = GetSymbolTarget(G, Sym.getBlock());
  if (!TS)
    return TS.takeError();
  GOTEntryInfos[TS->getName()] = {Sym.getSymbolContent(),
                                  Sym.getAddress().getValue(),
                                  Sym.getTargetFlags()};
  return Error::success();
}

Error Session::FileInfo::registerStubEntry(
    LinkGraph &G, Symbol &Sym, GetSymbolTargetFunction GetSymbolTarget) {
  if (Sym.isSymbolZeroFill())
    return make_error<StringError>("Unexpected zero-fill symbol in section " +
                                       Sym.getBlock().getSection().getName(),
                                   inconvertibleErrorCode());
  auto TS = GetSymbolTarget(G, Sym.getBlock());
  if (!TS)
    return TS.takeError();

  SmallVectorImpl<MemoryRegionInfo> &Entry = StubInfos[TS->getName()];
  Entry.insert(Entry.begin(),
               {Sym.getSymbolContent(), Sym.getAddress().getValue(),
                Sym.getTargetFlags()});
  return Error::success();
}

Error Session::FileInfo::registerMultiStubEntry(
    LinkGraph &G, Symbol &Sym, GetSymbolTargetFunction GetSymbolTarget) {
  if (Sym.isSymbolZeroFill())
    return make_error<StringError>("Unexpected zero-fill symbol in section " +
                                       Sym.getBlock().getSection().getName(),
                                   inconvertibleErrorCode());

  auto Target = GetSymbolTarget(G, Sym.getBlock());
  if (!Target)
    return Target.takeError();

  SmallVectorImpl<MemoryRegionInfo> &Entry = StubInfos[Target->getName()];
  Entry.emplace_back(Sym.getSymbolContent(), Sym.getAddress().getValue(),
                     Sym.getTargetFlags());

  // Let's keep stubs ordered by ascending address.
  std::sort(Entry.begin(), Entry.end(),
            [](const MemoryRegionInfo &L, const MemoryRegionInfo &R) {
              return L.getTargetAddress() < R.getTargetAddress();
            });

  return Error::success();
}

Expected<Session::FileInfo &> Session::findFileInfo(StringRef FileName) {
  auto FileInfoItr = FileInfos.find(FileName);
  if (FileInfoItr == FileInfos.end())
    return make_error<StringError>("file \"" + FileName + "\" not recognized",
                                   inconvertibleErrorCode());
  return FileInfoItr->second;
}

Expected<Session::MemoryRegionInfo &>
Session::findSectionInfo(StringRef FileName, StringRef SectionName) {
  auto FI = findFileInfo(FileName);
  if (!FI)
    return FI.takeError();
  auto SecInfoItr = FI->SectionInfos.find(SectionName);
  if (SecInfoItr == FI->SectionInfos.end())
    return make_error<StringError>("no section \"" + SectionName +
                                       "\" registered for file \"" + FileName +
                                       "\"",
                                   inconvertibleErrorCode());
  return SecInfoItr->second;
}

class MemoryMatcher {
public:
  MemoryMatcher(ArrayRef<char> Content)
      : Pos(Content.data()), End(Pos + Content.size()) {}

  template <typename MaskType> bool matchMask(MaskType Mask) {
    if (Mask == (Mask & *reinterpret_cast<const MaskType *>(Pos))) {
      Pos += sizeof(MaskType);
      return true;
    }
    return false;
  }

  template <typename ValueType> bool matchEqual(ValueType Value) {
    if (Value == *reinterpret_cast<const ValueType *>(Pos)) {
      Pos += sizeof(ValueType);
      return true;
    }
    return false;
  }

  bool done() const { return Pos == End; }

private:
  const char *Pos;
  const char *End;
};

static StringRef detectStubKind(const Session::MemoryRegionInfo &Stub) {
  using namespace support::endian;
  auto Armv7MovWTle = byte_swap<uint32_t, endianness::little>(0xe300c000);
  auto Armv7BxR12le = byte_swap<uint32_t, endianness::little>(0xe12fff1c);
  auto Thumbv7MovWTle = byte_swap<uint32_t, endianness::little>(0x0c00f240);
  auto Thumbv7BxR12le = byte_swap<uint16_t, endianness::little>(0x4760);

  MemoryMatcher M(Stub.getContent());
  if (M.matchMask(Thumbv7MovWTle)) {
    if (M.matchMask(Thumbv7MovWTle))
      if (M.matchEqual(Thumbv7BxR12le))
        if (M.done())
          return "thumbv7_abs_le";
  } else if (M.matchMask(Armv7MovWTle)) {
    if (M.matchMask(Armv7MovWTle))
      if (M.matchEqual(Armv7BxR12le))
        if (M.done())
          return "armv7_abs_le";
  }
  return "";
}

Expected<Session::MemoryRegionInfo &>
Session::findStubInfo(StringRef FileName, StringRef TargetName,
                      StringRef KindNameFilter) {
  auto FI = findFileInfo(FileName);
  if (!FI)
    return FI.takeError();
  auto StubInfoItr = FI->StubInfos.find(TargetName);
  if (StubInfoItr == FI->StubInfos.end())
    return make_error<StringError>("no stub for \"" + TargetName +
                                       "\" registered for file \"" + FileName +
                                       "\"",
                                   inconvertibleErrorCode());
  auto &StubsForTarget = StubInfoItr->second;
  assert(!StubsForTarget.empty() && "At least 1 stub in each entry");
  if (KindNameFilter.empty() && StubsForTarget.size() == 1)
    return StubsForTarget[0]; // Regular single-stub match

  std::string KindsStr;
  SmallVector<MemoryRegionInfo *, 1> Matches;
  Regex KindNameMatcher(KindNameFilter.empty() ? ".*" : KindNameFilter);
  for (MemoryRegionInfo &Stub : StubsForTarget) {
    StringRef Kind = detectStubKind(Stub);
    if (KindNameMatcher.match(Kind))
      Matches.push_back(&Stub);
    KindsStr += "\"" + (Kind.empty() ? "<unknown>" : Kind.str()) + "\", ";
  }
  if (Matches.empty())
    return make_error<StringError>(
        "\"" + TargetName + "\" has " + Twine(StubsForTarget.size()) +
            " stubs in file \"" + FileName +
            "\", but none of them matches the stub-kind filter \"" +
            KindNameFilter + "\" (all encountered kinds are " +
            StringRef(KindsStr.data(), KindsStr.size() - 2) + ").",
        inconvertibleErrorCode());
  if (Matches.size() > 1)
    return make_error<StringError>(
        "\"" + TargetName + "\" has " + Twine(Matches.size()) +
            " candidate stubs in file \"" + FileName +
            "\". Please refine stub-kind filter \"" + KindNameFilter +
            "\" for disambiguation (encountered kinds are " +
            StringRef(KindsStr.data(), KindsStr.size() - 2) + ").",
        inconvertibleErrorCode());

  return *Matches[0];
}

Expected<Session::MemoryRegionInfo &>
Session::findGOTEntryInfo(StringRef FileName, StringRef TargetName) {
  auto FI = findFileInfo(FileName);
  if (!FI)
    return FI.takeError();
  auto GOTInfoItr = FI->GOTEntryInfos.find(TargetName);
  if (GOTInfoItr == FI->GOTEntryInfos.end())
    return make_error<StringError>("no GOT entry for \"" + TargetName +
                                       "\" registered for file \"" + FileName +
                                       "\"",
                                   inconvertibleErrorCode());
  return GOTInfoItr->second;
}

bool Session::isSymbolRegistered(StringRef SymbolName) {
  return SymbolInfos.count(SymbolName);
}

Expected<Session::MemoryRegionInfo &>
Session::findSymbolInfo(StringRef SymbolName, Twine ErrorMsgStem) {
  auto SymInfoItr = SymbolInfos.find(SymbolName);
  if (SymInfoItr == SymbolInfos.end())
    return make_error<StringError>(ErrorMsgStem + ": symbol " + SymbolName +
                                       " not found",
                                   inconvertibleErrorCode());
  return SymInfoItr->second;
}

} // end namespace llvm

static std::pair<Triple, SubtargetFeatures> getFirstFileTripleAndFeatures() {
  static std::pair<Triple, SubtargetFeatures> FirstTTAndFeatures = []() {
    assert(!InputFiles.empty() && "InputFiles can not be empty");
    for (auto InputFile : InputFiles) {
      auto ObjBuffer = ExitOnErr(getFile(InputFile));
      file_magic Magic = identify_magic(ObjBuffer->getBuffer());
      switch (Magic) {
      case file_magic::coff_object:
      case file_magic::elf_relocatable:
      case file_magic::macho_object: {
        auto Obj = ExitOnErr(
            object::ObjectFile::createObjectFile(ObjBuffer->getMemBufferRef()));
        Triple TT = Obj->makeTriple();
        if (Magic == file_magic::coff_object) {
          // TODO: Move this to makeTriple() if possible.
          TT.setObjectFormat(Triple::COFF);
          TT.setOS(Triple::OSType::Win32);
        }
        SubtargetFeatures Features;
        if (auto ObjFeatures = Obj->getFeatures())
          Features = std::move(*ObjFeatures);
        return std::make_pair(TT, Features);
      }
      default:
        break;
      }
    }
    return std::make_pair(Triple(), SubtargetFeatures());
  }();

  return FirstTTAndFeatures;
}

static Error sanitizeArguments(const Triple &TT, const char *ArgV0) {

  // -noexec and --args should not be used together.
  if (NoExec && !InputArgv.empty())
    errs() << "Warning: --args passed to -noexec run will be ignored.\n";

  // Set the entry point name if not specified.
  if (EntryPointName.empty())
    EntryPointName = TT.getObjectFormat() == Triple::MachO ? "_main" : "main";

  // Disable debugger support by default in noexec tests.
  if (DebuggerSupport.getNumOccurrences() == 0 && NoExec)
    DebuggerSupport = false;

  if (!OrcRuntime.empty() && NoProcessSymbols)
    return make_error<StringError>("-orc-runtime requires process symbols",
                                   inconvertibleErrorCode());

  // If -slab-allocate is passed, check that we're not trying to use it in
  // -oop-executor or -oop-executor-connect mode.
  //
  // FIXME: Remove once we enable remote slab allocation.
  if (SlabAllocateSizeString != "") {
    if (OutOfProcessExecutor.getNumOccurrences() ||
        OutOfProcessExecutorConnect.getNumOccurrences())
      return make_error<StringError>(
          "-slab-allocate cannot be used with -oop-executor or "
          "-oop-executor-connect",
          inconvertibleErrorCode());
  }

  // If -slab-address is passed, require -slab-allocate and -noexec
  if (SlabAddress != ~0ULL) {
    if (SlabAllocateSizeString == "" || !NoExec)
      return make_error<StringError>(
          "-slab-address requires -slab-allocate and -noexec",
          inconvertibleErrorCode());

    if (SlabPageSize == 0)
      errs() << "Warning: -slab-address used without -slab-page-size.\n";
  }

  if (SlabPageSize != 0) {
    // -slab-page-size requires slab alloc.
    if (SlabAllocateSizeString == "")
      return make_error<StringError>("-slab-page-size requires -slab-allocate",
                                     inconvertibleErrorCode());

    // Check -slab-page-size / -noexec interactions.
    if (!NoExec) {
      if (auto RealPageSize = sys::Process::getPageSize()) {
        if (SlabPageSize % *RealPageSize)
          return make_error<StringError>(
              "-slab-page-size must be a multiple of real page size for exec "
              "tests (did you mean to use -noexec ?)\n",
              inconvertibleErrorCode());
      } else {
        errs() << "Could not retrieve process page size:\n";
        logAllUnhandledErrors(RealPageSize.takeError(), errs(), "");
        errs() << "Executing with slab page size = "
               << formatv("{0:x}", SlabPageSize) << ".\n"
               << "Tool may crash if " << formatv("{0:x}", SlabPageSize)
               << " is not a multiple of the real process page size.\n"
               << "(did you mean to use -noexec ?)";
      }
    }
  }

  // Only one of -oop-executor and -oop-executor-connect can be used.
  if (!!OutOfProcessExecutor.getNumOccurrences() &&
      !!OutOfProcessExecutorConnect.getNumOccurrences())
    return make_error<StringError>(
        "Only one of -" + OutOfProcessExecutor.ArgStr + " and -" +
            OutOfProcessExecutorConnect.ArgStr + " can be specified",
        inconvertibleErrorCode());

  // If -oop-executor was used but no value was specified then use a sensible
  // default.
  if (!!OutOfProcessExecutor.getNumOccurrences() &&
      OutOfProcessExecutor.empty()) {
    SmallString<256> OOPExecutorPath(sys::fs::getMainExecutable(
        ArgV0, reinterpret_cast<void *>(&sanitizeArguments)));
    sys::path::remove_filename(OOPExecutorPath);
    sys::path::append(OOPExecutorPath, "llvm-jitlink-executor");
    OutOfProcessExecutor = OOPExecutorPath.str().str();
  }

  return Error::success();
}

static void addPhonyExternalsGenerator(Session &S) {
  S.MainJD->addGenerator(std::make_unique<PhonyExternalsGenerator>());
}

static Error createJITDylibs(Session &S,
                             std::map<unsigned, JITDylib *> &IdxToJD) {
  // First, set up JITDylibs.
  LLVM_DEBUG(dbgs() << "Creating JITDylibs...\n");
  {
    // Create a "main" JITLinkDylib.
    IdxToJD[0] = S.MainJD;
    S.JDSearchOrder.push_back({S.MainJD, JITDylibLookupFlags::MatchAllSymbols});
    LLVM_DEBUG(dbgs() << "  0: " << S.MainJD->getName() << "\n");

    // Add any extra JITDylibs from the command line.
    for (auto JDItr = JITDylibs.begin(), JDEnd = JITDylibs.end();
         JDItr != JDEnd; ++JDItr) {
      auto JD = S.ES.createJITDylib(*JDItr);
      if (!JD)
        return JD.takeError();
      unsigned JDIdx = JITDylibs.getPosition(JDItr - JITDylibs.begin());
      IdxToJD[JDIdx] = &*JD;
      S.JDSearchOrder.push_back({&*JD, JITDylibLookupFlags::MatchAllSymbols});
      LLVM_DEBUG(dbgs() << "  " << JDIdx << ": " << JD->getName() << "\n");
    }
  }

  if (S.PlatformJD)
    S.JDSearchOrder.push_back(
        {S.PlatformJD, JITDylibLookupFlags::MatchExportedSymbolsOnly});
  if (S.ProcessSymsJD)
    S.JDSearchOrder.push_back(
        {S.ProcessSymsJD, JITDylibLookupFlags::MatchExportedSymbolsOnly});

  LLVM_DEBUG({
    dbgs() << "Dylib search order is [ ";
    for (auto &KV : S.JDSearchOrder)
      dbgs() << KV.first->getName() << " ";
    dbgs() << "]\n";
  });

  return Error::success();
}

static Error addAbsoluteSymbols(Session &S,
                                const std::map<unsigned, JITDylib *> &IdxToJD) {
  // Define absolute symbols.
  LLVM_DEBUG(dbgs() << "Defining absolute symbols...\n");
  for (auto AbsDefItr = AbsoluteDefs.begin(), AbsDefEnd = AbsoluteDefs.end();
       AbsDefItr != AbsDefEnd; ++AbsDefItr) {
    unsigned AbsDefArgIdx =
      AbsoluteDefs.getPosition(AbsDefItr - AbsoluteDefs.begin());
    auto &JD = *std::prev(IdxToJD.lower_bound(AbsDefArgIdx))->second;

    StringRef AbsDefStmt = *AbsDefItr;
    size_t EqIdx = AbsDefStmt.find_first_of('=');
    if (EqIdx == StringRef::npos)
      return make_error<StringError>("Invalid absolute define \"" + AbsDefStmt +
                                     "\". Syntax: <name>=<addr>",
                                     inconvertibleErrorCode());
    StringRef Name = AbsDefStmt.substr(0, EqIdx).trim();
    StringRef AddrStr = AbsDefStmt.substr(EqIdx + 1).trim();

    uint64_t Addr;
    if (AddrStr.getAsInteger(0, Addr))
      return make_error<StringError>("Invalid address expression \"" + AddrStr +
                                         "\" in absolute symbol definition \"" +
                                         AbsDefStmt + "\"",
                                     inconvertibleErrorCode());
    ExecutorSymbolDef AbsDef(ExecutorAddr(Addr), JITSymbolFlags::Exported);
    if (auto Err = JD.define(absoluteSymbols({{S.ES.intern(Name), AbsDef}})))
      return Err;

    // Register the absolute symbol with the session symbol infos.
    S.SymbolInfos[Name] = {ArrayRef<char>(), Addr,
                           AbsDef.getFlags().getTargetFlags()};
  }

  return Error::success();
}

static Error addAliases(Session &S,
                        const std::map<unsigned, JITDylib *> &IdxToJD) {
  // Define absolute symbols.
  LLVM_DEBUG(dbgs() << "Defining aliases...\n");

  DenseMap<std::pair<JITDylib *, JITDylib *>, SymbolAliasMap> Reexports;
  for (auto AliasItr = Aliases.begin(), AliasEnd = Aliases.end();
       AliasItr != AliasEnd; ++AliasItr) {

    auto BadExpr = [&]() {
      return make_error<StringError>(
          "Invalid alias definition \"" + *AliasItr +
              "\". Syntax: [<dst-jd>:]<alias>=[<src-jd>:]<aliasee>",
          inconvertibleErrorCode());
    };

    auto GetJD = [&](StringRef JDName) -> Expected<JITDylib *> {
      if (JDName.empty()) {
        unsigned AliasArgIdx = Aliases.getPosition(AliasItr - Aliases.begin());
        return std::prev(IdxToJD.lower_bound(AliasArgIdx))->second;
      }

      auto *JD = S.ES.getJITDylibByName(JDName);
      if (!JD)
        return make_error<StringError>(StringRef("In alias definition \"") +
                                           *AliasItr + "\" no dylib named " +
                                           JDName,
                                       inconvertibleErrorCode());

      return JD;
    };

    {
      // First split on '=' to get alias and aliasee.
      StringRef AliasStmt = *AliasItr;
      auto [AliasExpr, AliaseeExpr] = AliasStmt.split('=');
      if (AliaseeExpr.empty())
        return BadExpr();

      auto [AliasJDName, Alias] = AliasExpr.split(':');
      if (Alias.empty())
        std::swap(AliasJDName, Alias);

      auto AliasJD = GetJD(AliasJDName);
      if (!AliasJD)
        return AliasJD.takeError();

      auto [AliaseeJDName, Aliasee] = AliaseeExpr.split(':');
      if (Aliasee.empty())
        std::swap(AliaseeJDName, Aliasee);

      if (AliaseeJDName.empty() && !AliasJDName.empty())
        AliaseeJDName = AliasJDName;
      auto AliaseeJD = GetJD(AliaseeJDName);
      if (!AliaseeJD)
        return AliaseeJD.takeError();

      Reexports[{*AliasJD, *AliaseeJD}][S.ES.intern(Alias)] = {
          S.ES.intern(Aliasee), JITSymbolFlags::Exported};
    }
  }

  for (auto &[JDs, AliasMap] : Reexports) {
    auto [DstJD, SrcJD] = JDs;
    if (auto Err = DstJD->define(reexports(*SrcJD, std::move(AliasMap))))
      return Err;
  }

  return Error::success();
}

static Error addSectCreates(Session &S,
                            const std::map<unsigned, JITDylib *> &IdxToJD) {
  for (auto SCItr = SectCreate.begin(), SCEnd = SectCreate.end();
       SCItr != SCEnd; ++SCItr) {

    unsigned SCArgIdx = SectCreate.getPosition(SCItr - SectCreate.begin());
    auto &JD = *std::prev(IdxToJD.lower_bound(SCArgIdx))->second;

    StringRef SCArg(*SCItr);

    auto [SectAndFileName, ExtraSymbolsString] = SCArg.split('@');
    auto [SectName, FileName] = SectAndFileName.rsplit(',');
    if (SectName.empty())
      return make_error<StringError>("In -sectcreate=" + SCArg +
                                         ", filename component cannot be empty",
                                     inconvertibleErrorCode());
    if (FileName.empty())
      return make_error<StringError>("In -sectcreate=" + SCArg +
                                         ", filename component cannot be empty",
                                     inconvertibleErrorCode());

    auto Content = MemoryBuffer::getFile(FileName);
    if (!Content)
      return createFileError(FileName, errorCodeToError(Content.getError()));

    SectCreateMaterializationUnit::ExtraSymbolsMap ExtraSymbols;
    while (!ExtraSymbolsString.empty()) {
      StringRef NextSymPair;
      std::tie(NextSymPair, ExtraSymbolsString) = ExtraSymbolsString.split(',');

      auto [Sym, OffsetString] = NextSymPair.split('=');
      size_t Offset;

      if (OffsetString.getAsInteger(0, Offset))
        return make_error<StringError>("In -sectcreate=" + SCArg + ", " +
                                           OffsetString +
                                           " is not a valid integer",
                                       inconvertibleErrorCode());

      ExtraSymbols[S.ES.intern(Sym)] = {JITSymbolFlags::Exported, Offset};
    }

    if (auto Err = JD.define(std::make_unique<SectCreateMaterializationUnit>(
            S.ObjLayer, SectName.str(), MemProt::Read, 16, std::move(*Content),
            std::move(ExtraSymbols))))
      return Err;
  }

  return Error::success();
}

static Error addTestHarnesses(Session &S) {
  LLVM_DEBUG(dbgs() << "Adding test harness objects...\n");
  for (auto HarnessFile : TestHarnesses) {
    LLVM_DEBUG(dbgs() << "  " << HarnessFile << "\n");
    auto ObjBuffer = getFile(HarnessFile);
    if (!ObjBuffer)
      return ObjBuffer.takeError();
    if (auto Err = S.ObjLayer.add(*S.MainJD, std::move(*ObjBuffer)))
      return Err;
  }
  return Error::success();
}

static Error addObjects(Session &S,
                        const std::map<unsigned, JITDylib *> &IdxToJD) {

  // Load each object into the corresponding JITDylib..
  LLVM_DEBUG(dbgs() << "Adding objects...\n");
  for (auto InputFileItr = InputFiles.begin(), InputFileEnd = InputFiles.end();
       InputFileItr != InputFileEnd; ++InputFileItr) {
    unsigned InputFileArgIdx =
        InputFiles.getPosition(InputFileItr - InputFiles.begin());
    const std::string &InputFile = *InputFileItr;
    if (StringRef(InputFile).ends_with(".a") ||
        StringRef(InputFile).ends_with(".lib"))
      continue;
    auto &JD = *std::prev(IdxToJD.lower_bound(InputFileArgIdx))->second;
    LLVM_DEBUG(dbgs() << "  " << InputFileArgIdx << ": \"" << InputFile
                      << "\" to " << JD.getName() << "\n";);
    auto ObjBuffer = getFile(InputFile);
    if (!ObjBuffer)
      return ObjBuffer.takeError();

    if (S.HarnessFiles.empty()) {
      if (auto Err = S.ObjLayer.add(JD, std::move(*ObjBuffer)))
        return Err;
    } else {
      // We're in -harness mode. Use a custom interface for this
      // test object.
      auto ObjInterface =
          getTestObjectFileInterface(S, (*ObjBuffer)->getMemBufferRef());
      if (!ObjInterface)
        return ObjInterface.takeError();
      if (auto Err = S.ObjLayer.add(JD, std::move(*ObjBuffer),
                                    std::move(*ObjInterface)))
        return Err;
    }
  }

  return Error::success();
}

static Expected<MaterializationUnit::Interface>
getObjectFileInterfaceHidden(ExecutionSession &ES, MemoryBufferRef ObjBuffer) {
  auto I = getObjectFileInterface(ES, ObjBuffer);
  if (I) {
    for (auto &KV : I->SymbolFlags)
      KV.second &= ~JITSymbolFlags::Exported;
  }
  return I;
}

static SmallVector<StringRef, 5> getSearchPathsFromEnvVar(Session &S) {
  // FIXME: Handle EPC environment.
  SmallVector<StringRef, 5> PathVec;
  auto TT = S.ES.getTargetTriple();
  if (TT.isOSBinFormatCOFF())
    StringRef(getenv("PATH")).split(PathVec, ";");
  else if (TT.isOSBinFormatELF())
    StringRef(getenv("LD_LIBRARY_PATH")).split(PathVec, ":");

  return PathVec;
}

static Error addLibraries(Session &S,
                          const std::map<unsigned, JITDylib *> &IdxToJD) {

  // 1. Collect search paths for each JITDylib.
  DenseMap<const JITDylib *, SmallVector<StringRef, 2>> JDSearchPaths;

  for (auto LSPItr = LibrarySearchPaths.begin(),
            LSPEnd = LibrarySearchPaths.end();
       LSPItr != LSPEnd; ++LSPItr) {
    unsigned LibrarySearchPathIdx =
        LibrarySearchPaths.getPosition(LSPItr - LibrarySearchPaths.begin());
    auto &JD = *std::prev(IdxToJD.lower_bound(LibrarySearchPathIdx))->second;

    StringRef LibrarySearchPath = *LSPItr;
    if (sys::fs::get_file_type(LibrarySearchPath) !=
        sys::fs::file_type::directory_file)
      return make_error<StringError>("While linking " + JD.getName() + ", -L" +
                                         LibrarySearchPath +
                                         " does not point to a directory",
                                     inconvertibleErrorCode());

    JDSearchPaths[&JD].push_back(*LSPItr);
  }

  LLVM_DEBUG({
    if (!JDSearchPaths.empty())
      dbgs() << "Search paths:\n";
    for (auto &KV : JDSearchPaths) {
      dbgs() << "  " << KV.first->getName() << ": [";
      for (auto &LibSearchPath : KV.second)
        dbgs() << " \"" << LibSearchPath << "\"";
      dbgs() << " ]\n";
    }
  });

  // 2. Collect library loads
  struct LibraryLoad {
    std::string LibName;
    bool IsPath = false;
    unsigned Position;
    StringRef *CandidateExtensions;
    enum { Standard, Hidden } Modifier;
  };

  // Queue to load library as in the order as it appears in the argument list.
  std::deque<LibraryLoad> LibraryLoadQueue;
  // Add archive files from the inputs to LibraryLoads.
  for (auto InputFileItr = InputFiles.begin(), InputFileEnd = InputFiles.end();
       InputFileItr != InputFileEnd; ++InputFileItr) {
    StringRef InputFile = *InputFileItr;
    if (!InputFile.ends_with(".a") && !InputFile.ends_with(".lib"))
      continue;
    LibraryLoad LL;
    LL.LibName = InputFile.str();
    LL.IsPath = true;
    LL.Position = InputFiles.getPosition(InputFileItr - InputFiles.begin());
    LL.CandidateExtensions = nullptr;
    LL.Modifier = LibraryLoad::Standard;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  // Add -load_hidden arguments to LibraryLoads.
  for (auto LibItr = LoadHidden.begin(), LibEnd = LoadHidden.end();
       LibItr != LibEnd; ++LibItr) {
    LibraryLoad LL;
    LL.LibName = *LibItr;
    LL.IsPath = true;
    LL.Position = LoadHidden.getPosition(LibItr - LoadHidden.begin());
    LL.CandidateExtensions = nullptr;
    LL.Modifier = LibraryLoad::Hidden;
    LibraryLoadQueue.push_back(std::move(LL));
  }
  StringRef StandardExtensions[] = {".so", ".dylib", ".dll", ".a", ".lib"};
  StringRef DynLibExtensionsOnly[] = {".so", ".dylib", ".dll"};
  StringRef ArchiveExtensionsOnly[] = {".a", ".lib"};

  // Add -lx arguments to LibraryLoads.
  for (auto LibItr = Libraries.begin(), LibEnd = Libraries.end();
       LibItr != LibEnd; ++LibItr) {
    LibraryLoad LL;
    LL.LibName = *LibItr;
    LL.Position = Libraries.getPosition(LibItr - Libraries.begin());
    LL.CandidateExtensions = StandardExtensions;
    LL.Modifier = LibraryLoad::Standard;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  // Add -hidden-lx arguments to LibraryLoads.
  for (auto LibHiddenItr = LibrariesHidden.begin(),
            LibHiddenEnd = LibrariesHidden.end();
       LibHiddenItr != LibHiddenEnd; ++LibHiddenItr) {
    LibraryLoad LL;
    LL.LibName = *LibHiddenItr;
    LL.Position =
        LibrariesHidden.getPosition(LibHiddenItr - LibrariesHidden.begin());
    LL.CandidateExtensions = ArchiveExtensionsOnly;
    LL.Modifier = LibraryLoad::Hidden;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  // If there are any load-<modified> options then turn on flag overrides
  // to avoid flag mismatch errors.
  if (!LibrariesHidden.empty() || !LoadHidden.empty())
    S.ObjLayer.setOverrideObjectFlagsWithResponsibilityFlags(true);

  // Sort library loads by position in the argument list.
  llvm::sort(LibraryLoadQueue,
             [](const LibraryLoad &LHS, const LibraryLoad &RHS) {
               return LHS.Position < RHS.Position;
             });

  // 3. Process library loads.
  auto AddArchive = [&](const char *Path, const LibraryLoad &LL)
      -> Expected<std::unique_ptr<StaticLibraryDefinitionGenerator>> {
    unique_function<Expected<MaterializationUnit::Interface>(
        ExecutionSession & ES, MemoryBufferRef ObjBuffer)>
        GetObjFileInterface;
    switch (LL.Modifier) {
    case LibraryLoad::Standard:
      GetObjFileInterface = getObjectFileInterface;
      break;
    case LibraryLoad::Hidden:
      GetObjFileInterface = getObjectFileInterfaceHidden;
      break;
    }
    auto G = StaticLibraryDefinitionGenerator::Load(
        S.ObjLayer, Path, std::move(GetObjFileInterface));
    if (!G)
      return G.takeError();

    // Push additional dynamic libraries to search.
    // Note that this mechanism only happens in COFF.
    for (auto FileName : (*G)->getImportedDynamicLibraries()) {
      LibraryLoad NewLL;
      auto FileNameRef = StringRef(FileName);
      if (!FileNameRef.ends_with_insensitive(".dll"))
        return make_error<StringError>(
            "COFF Imported library not ending with dll extension?",
            inconvertibleErrorCode());
      NewLL.LibName = FileNameRef.drop_back(strlen(".dll")).str();
      NewLL.Position = LL.Position;
      NewLL.CandidateExtensions = DynLibExtensionsOnly;
      NewLL.Modifier = LibraryLoad::Standard;
      LibraryLoadQueue.push_front(std::move(NewLL));
    }
    return G;
  };

  SmallVector<StringRef, 5> SystemSearchPaths;
  if (SearchSystemLibrary.getValue())
    SystemSearchPaths = getSearchPathsFromEnvVar(S);
  while (!LibraryLoadQueue.empty()) {
    bool LibFound = false;
    auto LL = LibraryLoadQueue.front();
    LibraryLoadQueue.pop_front();
    auto &JD = *std::prev(IdxToJD.lower_bound(LL.Position))->second;

    // If this is the name of a JITDylib then link against that.
    if (auto *LJD = S.ES.getJITDylibByName(LL.LibName)) {
      JD.addToLinkOrder(*LJD);
      continue;
    }

    if (LL.IsPath) {
      auto G = AddArchive(LL.LibName.c_str(), LL);
      if (!G)
        return createFileError(LL.LibName, G.takeError());
      JD.addGenerator(std::move(*G));
      LLVM_DEBUG({
        dbgs() << "Adding generator for static library " << LL.LibName << " to "
               << JD.getName() << "\n";
      });
      continue;
    }

    // Otherwise look through the search paths.
    auto CurJDSearchPaths = JDSearchPaths[&JD];
    for (StringRef SearchPath :
         concat<StringRef>(CurJDSearchPaths, SystemSearchPaths)) {
      for (const char *LibExt : {".dylib", ".so", ".dll", ".a", ".lib"}) {
        SmallVector<char, 256> LibPath;
        LibPath.reserve(SearchPath.size() + strlen("lib") + LL.LibName.size() +
                        strlen(LibExt) + 2); // +2 for pathsep, null term.
        llvm::copy(SearchPath, std::back_inserter(LibPath));
        if (StringRef(LibExt) != ".lib" && StringRef(LibExt) != ".dll")
          sys::path::append(LibPath, "lib" + LL.LibName + LibExt);
        else
          sys::path::append(LibPath, LL.LibName + LibExt);
        LibPath.push_back('\0');

        // Skip missing or non-regular paths.
        if (sys::fs::get_file_type(LibPath.data()) !=
            sys::fs::file_type::regular_file) {
          continue;
        }

        file_magic Magic;
        if (auto EC = identify_magic(LibPath, Magic)) {
          // If there was an error loading the file then skip it.
          LLVM_DEBUG({
            dbgs() << "Library search found \"" << LibPath
                   << "\", but could not identify file type (" << EC.message()
                   << "). Skipping.\n";
          });
          continue;
        }

        // We identified the magic. Assume that we can load it -- we'll reset
        // in the default case.
        LibFound = true;
        switch (Magic) {
        case file_magic::pecoff_executable:
        case file_magic::elf_shared_object:
        case file_magic::macho_dynamically_linked_shared_lib: {
          if (auto Err = S.loadAndLinkDynamicLibrary(JD, LibPath.data()))
            return Err;
          break;
        }
        case file_magic::archive:
        case file_magic::macho_universal_binary: {
          auto G = AddArchive(LibPath.data(), LL);
          if (!G)
            return G.takeError();
          JD.addGenerator(std::move(*G));
          LLVM_DEBUG({
            dbgs() << "Adding generator for static library " << LibPath.data()
                   << " to " << JD.getName() << "\n";
          });
          break;
        }
        default:
          // This file isn't a recognized library kind.
          LLVM_DEBUG({
            dbgs() << "Library search found \"" << LibPath
                   << "\", but file type is not supported. Skipping.\n";
          });
          LibFound = false;
          break;
        }
        if (LibFound)
          break;
      }
      if (LibFound)
        break;
    }

    if (!LibFound)
      return make_error<StringError>("While linking " + JD.getName() +
                                         ", could not find library for -l" +
                                         LL.LibName,
                                     inconvertibleErrorCode());
  }

  // Add platform and process symbols if available.
  for (auto &[Idx, JD] : IdxToJD) {
    if (S.PlatformJD)
      JD->addToLinkOrder(*S.PlatformJD);
    if (S.ProcessSymsJD)
      JD->addToLinkOrder(*S.ProcessSymsJD);
  }

  return Error::success();
}

static Error addSessionInputs(Session &S) {
  std::map<unsigned, JITDylib *> IdxToJD;

  if (auto Err = createJITDylibs(S, IdxToJD))
    return Err;

  if (auto Err = addAbsoluteSymbols(S, IdxToJD))
    return Err;

  if (auto Err = addAliases(S, IdxToJD))
    return Err;

  if (auto Err = addSectCreates(S, IdxToJD))
    return Err;

  if (!TestHarnesses.empty())
    if (auto Err = addTestHarnesses(S))
      return Err;

  if (auto Err = addObjects(S, IdxToJD))
    return Err;

  if (auto Err = addLibraries(S, IdxToJD))
    return Err;

  return Error::success();
}

namespace {
struct TargetInfo {
  const Target *TheTarget;
  std::unique_ptr<MCSubtargetInfo> STI;
  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCContext> Ctx;
  std::unique_ptr<MCDisassembler> Disassembler;
  std::unique_ptr<MCInstrInfo> MII;
  std::unique_ptr<MCInstrAnalysis> MIA;
  std::unique_ptr<MCInstPrinter> InstPrinter;
};
} // anonymous namespace

static TargetInfo
getTargetInfo(const Triple &TT,
              const SubtargetFeatures &TF = SubtargetFeatures()) {
  auto TripleName = TT.str();
  std::string ErrorStr;
  const Target *TheTarget = TargetRegistry::lookupTarget(TripleName, ErrorStr);
  if (!TheTarget)
    ExitOnErr(make_error<StringError>("Error accessing target '" + TripleName +
                                          "': " + ErrorStr,
                                      inconvertibleErrorCode()));

  std::unique_ptr<MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, "", TF.getString()));
  if (!STI)
    ExitOnErr(
        make_error<StringError>("Unable to create subtarget for " + TripleName,
                                inconvertibleErrorCode()));

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  if (!MRI)
    ExitOnErr(make_error<StringError>("Unable to create target register info "
                                      "for " +
                                          TripleName,
                                      inconvertibleErrorCode()));

  MCTargetOptions MCOptions;
  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  if (!MAI)
    ExitOnErr(make_error<StringError>("Unable to create target asm info " +
                                          TripleName,
                                      inconvertibleErrorCode()));

  auto Ctx = std::make_unique<MCContext>(Triple(TripleName), MAI.get(),
                                         MRI.get(), STI.get());

  std::unique_ptr<MCDisassembler> Disassembler(
      TheTarget->createMCDisassembler(*STI, *Ctx));
  if (!Disassembler)
    ExitOnErr(make_error<StringError>("Unable to create disassembler for " +
                                          TripleName,
                                      inconvertibleErrorCode()));

  std::unique_ptr<MCInstrInfo> MII(TheTarget->createMCInstrInfo());
  if (!MII)
    ExitOnErr(make_error<StringError>("Unable to create instruction info for" +
                                          TripleName,
                                      inconvertibleErrorCode()));

  std::unique_ptr<MCInstrAnalysis> MIA(
      TheTarget->createMCInstrAnalysis(MII.get()));
  if (!MIA)
    ExitOnErr(make_error<StringError>(
        "Unable to create instruction analysis for" + TripleName,
        inconvertibleErrorCode()));

  std::unique_ptr<MCInstPrinter> InstPrinter(
      TheTarget->createMCInstPrinter(Triple(TripleName), 0, *MAI, *MII, *MRI));
  if (!InstPrinter)
    ExitOnErr(make_error<StringError>(
        "Unable to create instruction printer for" + TripleName,
        inconvertibleErrorCode()));
  return {TheTarget,      std::move(STI), std::move(MRI),
          std::move(MAI), std::move(Ctx), std::move(Disassembler),
          std::move(MII), std::move(MIA), std::move(InstPrinter)};
}
static Error runChecks(Session &S, Triple TT, SubtargetFeatures Features) {
  if (CheckFiles.empty())
    return Error::success();

  LLVM_DEBUG(dbgs() << "Running checks...\n");

  auto IsSymbolValid = [&S](StringRef Symbol) {
    return S.isSymbolRegistered(Symbol);
  };

  auto GetSymbolInfo = [&S](StringRef Symbol) {
    return S.findSymbolInfo(Symbol, "Can not get symbol info");
  };

  auto GetSectionInfo = [&S](StringRef FileName, StringRef SectionName) {
    return S.findSectionInfo(FileName, SectionName);
  };

  auto GetStubInfo = [&S](StringRef FileName, StringRef SectionName,
                          StringRef KindNameFilter) {
    return S.findStubInfo(FileName, SectionName, KindNameFilter);
  };

  auto GetGOTInfo = [&S](StringRef FileName, StringRef SectionName) {
    return S.findGOTEntryInfo(FileName, SectionName);
  };

  RuntimeDyldChecker Checker(
      IsSymbolValid, GetSymbolInfo, GetSectionInfo, GetStubInfo, GetGOTInfo,
      S.ES.getTargetTriple().isLittleEndian() ? llvm::endianness::little
                                              : llvm::endianness::big,
      TT, StringRef(), Features, dbgs());

  std::string CheckLineStart = "# " + CheckName + ":";
  for (auto &CheckFile : CheckFiles) {
    auto CheckerFileBuf = ExitOnErr(getFile(CheckFile));
    if (!Checker.checkAllRulesInBuffer(CheckLineStart, &*CheckerFileBuf))
      ExitOnErr(make_error<StringError>(
          "Some checks in " + CheckFile + " failed", inconvertibleErrorCode()));
  }

  return Error::success();
}

static Error addSelfRelocations(LinkGraph &G) {
  auto TI = getTargetInfo(G.getTargetTriple());
  for (auto *Sym : G.defined_symbols())
    if (Sym->isCallable())
      if (auto Err = addFunctionPointerRelocationsToCurrentSymbol(
              *Sym, G, *TI.Disassembler, *TI.MIA))
        return Err;
  return Error::success();
}

static Expected<ExecutorSymbolDef> getMainEntryPoint(Session &S) {
  return S.ES.lookup(S.JDSearchOrder, S.ES.intern(EntryPointName));
}

static Expected<ExecutorSymbolDef> getOrcRuntimeEntryPoint(Session &S) {
  std::string RuntimeEntryPoint = "__orc_rt_run_program_wrapper";
  if (S.ES.getTargetTriple().getObjectFormat() == Triple::MachO)
    RuntimeEntryPoint = '_' + RuntimeEntryPoint;
  return S.ES.lookup(S.JDSearchOrder, S.ES.intern(RuntimeEntryPoint));
}

static Expected<ExecutorSymbolDef> getEntryPoint(Session &S) {
  ExecutorSymbolDef EntryPoint;

  // Find the entry-point function unconditionally, since we want to force
  // it to be materialized to collect stats.
  if (auto EP = getMainEntryPoint(S))
    EntryPoint = *EP;
  else
    return EP.takeError();
  LLVM_DEBUG({
    dbgs() << "Using entry point \"" << EntryPointName
           << "\": " << formatv("{0:x16}", EntryPoint.getAddress()) << "\n";
  });

  // If we're running with the ORC runtime then replace the entry-point
  // with the __orc_rt_run_program symbol.
  if (!OrcRuntime.empty()) {
    if (auto EP = getOrcRuntimeEntryPoint(S))
      EntryPoint = *EP;
    else
      return EP.takeError();
    LLVM_DEBUG({
      dbgs() << "(called via __orc_rt_run_program_wrapper at "
             << formatv("{0:x16}", EntryPoint.getAddress()) << ")\n";
    });
  }

  return EntryPoint;
}

static Expected<int> runWithRuntime(Session &S, ExecutorAddr EntryPointAddr) {
  StringRef DemangledEntryPoint = EntryPointName;
  if (S.ES.getTargetTriple().getObjectFormat() == Triple::MachO &&
      DemangledEntryPoint.front() == '_')
    DemangledEntryPoint = DemangledEntryPoint.drop_front();
  using llvm::orc::shared::SPSString;
  using SPSRunProgramSig =
      int64_t(SPSString, SPSString, shared::SPSSequence<SPSString>);
  int64_t Result;
  if (auto Err = S.ES.callSPSWrapper<SPSRunProgramSig>(
          EntryPointAddr, Result, S.MainJD->getName(), DemangledEntryPoint,
          static_cast<std::vector<std::string> &>(InputArgv)))
    return std::move(Err);
  return Result;
}

static Expected<int> runWithoutRuntime(Session &S,
                                       ExecutorAddr EntryPointAddr) {
  return S.ES.getExecutorProcessControl().runAsMain(EntryPointAddr, InputArgv);
}

namespace {
struct JITLinkTimers {
  TimerGroup JITLinkTG{"llvm-jitlink timers", "timers for llvm-jitlink phases"};
  Timer LoadObjectsTimer{"load", "time to load/add object files", JITLinkTG};
  Timer LinkTimer{"link", "time to link object files", JITLinkTG};
  Timer RunTimer{"run", "time to execute jitlink'd code", JITLinkTG};
};
} // namespace

int main(int argc, char *argv[]) {
  InitLLVM X(argc, argv);

  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllDisassemblers();

  cl::HideUnrelatedOptions({&JITLinkCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv, "llvm jitlink tool");
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  /// If timers are enabled, create a JITLinkTimers instance.
  std::unique_ptr<JITLinkTimers> Timers =
      ShowTimes ? std::make_unique<JITLinkTimers>() : nullptr;

  auto [TT, Features] = getFirstFileTripleAndFeatures();
  ExitOnErr(sanitizeArguments(TT, argv[0]));

  auto S = ExitOnErr(Session::Create(TT, Features));

  enableStatistics(*S, !OrcRuntime.empty());

  {
    TimeRegion TR(Timers ? &Timers->LoadObjectsTimer : nullptr);
    ExitOnErr(addSessionInputs(*S));
  }

  if (PhonyExternals)
    addPhonyExternalsGenerator(*S);

  if (ShowInitialExecutionSessionState)
    S->ES.dump(outs());

  Expected<ExecutorSymbolDef> EntryPoint((ExecutorSymbolDef()));
  {
    ExpectedAsOutParameter<ExecutorSymbolDef> _(&EntryPoint);
    TimeRegion TR(Timers ? &Timers->LinkTimer : nullptr);
    EntryPoint = getEntryPoint(*S);
  }

  // Print any reports regardless of whether we succeeded or failed.
  if (ShowEntryExecutionSessionState)
    S->ES.dump(outs());

  if (ShowAddrs)
    S->dumpSessionInfo(outs());

  if (!EntryPoint) {
    if (Timers)
      Timers->JITLinkTG.printAll(errs());
    reportLLVMJITLinkError(EntryPoint.takeError());
    exit(1);
  }

  ExitOnErr(runChecks(*S, std::move(TT), std::move(Features)));

  int Result = 0;
  if (!NoExec) {
    LLVM_DEBUG(dbgs() << "Running \"" << EntryPointName << "\"...\n");
    TimeRegion TR(Timers ? &Timers->RunTimer : nullptr);
    if (!OrcRuntime.empty())
      Result =
          ExitOnErr(runWithRuntime(*S, ExecutorAddr(EntryPoint->getAddress())));
    else
      Result = ExitOnErr(
          runWithoutRuntime(*S, ExecutorAddr(EntryPoint->getAddress())));
  }

  // Destroy the session.
  ExitOnErr(S->ES.endSession());
  S.reset();

  if (Timers)
    Timers->JITLinkTG.printAll(errs());

  // If the executing code set a test result override then use that.
  if (UseTestResultOverride)
    Result = TestResultOverride;

  return Result;
}
