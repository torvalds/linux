//===- llvm-link.cpp - Low-level LLVM linker ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-link a.bc b.bc c.bc -o x.bc
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"

#include <memory>
#include <utility>
using namespace llvm;

static cl::OptionCategory LinkCategory("Link Options");

static cl::list<std::string> InputFilenames(cl::Positional, cl::OneOrMore,
                                            cl::desc("<input bitcode files>"),
                                            cl::cat(LinkCategory));

static cl::list<std::string> OverridingInputs(
    "override", cl::value_desc("filename"),
    cl::desc(
        "input bitcode file which can override previously defined symbol(s)"),
    cl::cat(LinkCategory));

// Option to simulate function importing for testing. This enables using
// llvm-link to simulate ThinLTO backend processes.
static cl::list<std::string> Imports(
    "import", cl::value_desc("function:filename"),
    cl::desc("Pair of function name and filename, where function should be "
             "imported from bitcode in filename"),
    cl::cat(LinkCategory));

// Option to support testing of function importing. The module summary
// must be specified in the case were we request imports via the -import
// option, as well as when compiling any module with functions that may be
// exported (imported by a different llvm-link -import invocation), to ensure
// consistent promotion and renaming of locals.
static cl::opt<std::string>
    SummaryIndex("summary-index", cl::desc("Module summary index filename"),
                 cl::init(""), cl::value_desc("filename"),
                 cl::cat(LinkCategory));

static cl::opt<std::string>
    OutputFilename("o", cl::desc("Override output filename"), cl::init("-"),
                   cl::value_desc("filename"), cl::cat(LinkCategory));

static cl::opt<bool> Internalize("internalize",
                                 cl::desc("Internalize linked symbols"),
                                 cl::cat(LinkCategory));

static cl::opt<bool>
    DisableDITypeMap("disable-debug-info-type-map",
                     cl::desc("Don't use a uniquing type map for debug info"),
                     cl::cat(LinkCategory));

static cl::opt<bool> OnlyNeeded("only-needed",
                                cl::desc("Link only needed symbols"),
                                cl::cat(LinkCategory));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"),
                           cl::cat(LinkCategory));

static cl::opt<bool> DisableLazyLoad("disable-lazy-loading",
                                     cl::desc("Disable lazy module loading"),
                                     cl::cat(LinkCategory));

static cl::opt<bool> OutputAssembly("S",
                                    cl::desc("Write output as LLVM assembly"),
                                    cl::Hidden, cl::cat(LinkCategory));

static cl::opt<bool> Verbose("v",
                             cl::desc("Print information about actions taken"),
                             cl::cat(LinkCategory));

static cl::opt<bool> DumpAsm("d", cl::desc("Print assembly as linked"),
                             cl::Hidden, cl::cat(LinkCategory));

static cl::opt<bool> SuppressWarnings("suppress-warnings",
                                      cl::desc("Suppress all linking warnings"),
                                      cl::init(false), cl::cat(LinkCategory));

static cl::opt<bool> PreserveBitcodeUseListOrder(
    "preserve-bc-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM bitcode."),
    cl::init(true), cl::Hidden, cl::cat(LinkCategory));

static cl::opt<bool> PreserveAssemblyUseListOrder(
    "preserve-ll-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM assembly."),
    cl::init(false), cl::Hidden, cl::cat(LinkCategory));

static cl::opt<bool> NoVerify("disable-verify",
                              cl::desc("Do not run the verifier"), cl::Hidden,
                              cl::cat(LinkCategory));

static cl::opt<bool> IgnoreNonBitcode(
    "ignore-non-bitcode",
    cl::desc("Do not report an error for non-bitcode files in archives"),
    cl::Hidden);

static cl::opt<bool> TryUseNewDbgInfoFormat(
    "try-experimental-debuginfo-iterators",
    cl::desc("Enable debuginfo iterator positions, if they're built in"),
    cl::init(false));

extern cl::opt<bool> UseNewDbgInfoFormat;
extern cl::opt<cl::boolOrDefault> PreserveInputDbgFormat;
extern cl::opt<bool> WriteNewDbgInfoFormat;
extern bool WriteNewDbgInfoFormatToBitcode;

extern cl::opt<cl::boolOrDefault> LoadBitcodeIntoNewDbgInfoFormat;

static ExitOnError ExitOnErr;

// Read the specified bitcode file in and return it. This routine searches the
// link path for the specified file to try to find it...
//
static std::unique_ptr<Module> loadFile(const char *argv0,
                                        std::unique_ptr<MemoryBuffer> Buffer,
                                        LLVMContext &Context,
                                        bool MaterializeMetadata = true) {
  SMDiagnostic Err;
  if (Verbose)
    errs() << "Loading '" << Buffer->getBufferIdentifier() << "'\n";
  std::unique_ptr<Module> Result;
  if (DisableLazyLoad)
    Result = parseIR(*Buffer, Err, Context);
  else
    Result =
        getLazyIRModule(std::move(Buffer), Err, Context, !MaterializeMetadata);

  if (!Result) {
    Err.print(argv0, errs());
    return nullptr;
  }

  if (MaterializeMetadata) {
    ExitOnErr(Result->materializeMetadata());
    UpgradeDebugInfo(*Result);
  }

  return Result;
}

static std::unique_ptr<Module> loadArFile(const char *Argv0,
                                          std::unique_ptr<MemoryBuffer> Buffer,
                                          LLVMContext &Context) {
  std::unique_ptr<Module> Result(new Module("ArchiveModule", Context));
  StringRef ArchiveName = Buffer->getBufferIdentifier();
  if (Verbose)
    errs() << "Reading library archive file '" << ArchiveName
           << "' to memory\n";
  Expected<std::unique_ptr<object::Archive>> ArchiveOrError =
      object::Archive::create(Buffer->getMemBufferRef());
  if (!ArchiveOrError)
    ExitOnErr(ArchiveOrError.takeError());

  std::unique_ptr<object::Archive> Archive = std::move(ArchiveOrError.get());

  Linker L(*Result);
  Error Err = Error::success();
  for (const object::Archive::Child &C : Archive->children(Err)) {
    Expected<StringRef> Ename = C.getName();
    if (Error E = Ename.takeError()) {
      errs() << Argv0 << ": ";
      WithColor::error() << " failed to read name of archive member"
                         << ArchiveName << "'\n";
      return nullptr;
    }
    std::string ChildName = Ename.get().str();
    if (Verbose)
      errs() << "Parsing member '" << ChildName
             << "' of archive library to module.\n";
    SMDiagnostic ParseErr;
    Expected<MemoryBufferRef> MemBuf = C.getMemoryBufferRef();
    if (Error E = MemBuf.takeError()) {
      errs() << Argv0 << ": ";
      WithColor::error() << " loading memory for member '" << ChildName
                         << "' of archive library failed'" << ArchiveName
                         << "'\n";
      return nullptr;
    };

    if (!isBitcode(reinterpret_cast<const unsigned char *>(
                       MemBuf.get().getBufferStart()),
                   reinterpret_cast<const unsigned char *>(
                       MemBuf.get().getBufferEnd()))) {
      if (IgnoreNonBitcode)
        continue;
      errs() << Argv0 << ": ";
      WithColor::error() << "  member of archive is not a bitcode file: '"
                         << ChildName << "'\n";
      return nullptr;
    }

    std::unique_ptr<Module> M;
    if (DisableLazyLoad)
      M = parseIR(MemBuf.get(), ParseErr, Context);
    else
      M = getLazyIRModule(MemoryBuffer::getMemBuffer(MemBuf.get(), false),
                          ParseErr, Context);

    if (!M) {
      errs() << Argv0 << ": ";
      WithColor::error() << " parsing member '" << ChildName
                         << "' of archive library failed'" << ArchiveName
                         << "'\n";
      return nullptr;
    }
    if (Verbose)
      errs() << "Linking member '" << ChildName << "' of archive library.\n";
    if (L.linkInModule(std::move(M)))
      return nullptr;
  } // end for each child
  ExitOnErr(std::move(Err));
  return Result;
}

namespace {

/// Helper to load on demand a Module from file and cache it for subsequent
/// queries during function importing.
class ModuleLazyLoaderCache {
  /// Cache of lazily loaded module for import.
  StringMap<std::unique_ptr<Module>> ModuleMap;

  /// Retrieve a Module from the cache or lazily load it on demand.
  std::function<std::unique_ptr<Module>(const char *argv0,
                                        const std::string &FileName)>
      createLazyModule;

public:
  /// Create the loader, Module will be initialized in \p Context.
  ModuleLazyLoaderCache(std::function<std::unique_ptr<Module>(
                            const char *argv0, const std::string &FileName)>
                            createLazyModule)
      : createLazyModule(std::move(createLazyModule)) {}

  /// Retrieve a Module from the cache or lazily load it on demand.
  Module &operator()(const char *argv0, const std::string &FileName);

  std::unique_ptr<Module> takeModule(const std::string &FileName) {
    auto I = ModuleMap.find(FileName);
    assert(I != ModuleMap.end());
    std::unique_ptr<Module> Ret = std::move(I->second);
    ModuleMap.erase(I);
    return Ret;
  }
};

// Get a Module for \p FileName from the cache, or load it lazily.
Module &ModuleLazyLoaderCache::operator()(const char *argv0,
                                          const std::string &Identifier) {
  auto &Module = ModuleMap[Identifier];
  if (!Module) {
    Module = createLazyModule(argv0, Identifier);
    assert(Module && "Failed to create lazy module!");
  }
  return *Module;
}
} // anonymous namespace

namespace {
struct LLVMLinkDiagnosticHandler : public DiagnosticHandler {
  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    unsigned Severity = DI.getSeverity();
    switch (Severity) {
    case DS_Error:
      WithColor::error();
      break;
    case DS_Warning:
      if (SuppressWarnings)
        return true;
      WithColor::warning();
      break;
    case DS_Remark:
    case DS_Note:
      llvm_unreachable("Only expecting warnings and errors");
    }

    DiagnosticPrinterRawOStream DP(errs());
    DI.print(DP);
    errs() << '\n';
    return true;
  }
};
} // namespace

/// Import any functions requested via the -import option.
static bool importFunctions(const char *argv0, Module &DestModule) {
  if (SummaryIndex.empty())
    return true;
  std::unique_ptr<ModuleSummaryIndex> Index =
      ExitOnErr(llvm::getModuleSummaryIndexForFile(SummaryIndex));

  // Map of Module -> List of globals to import from the Module
  FunctionImporter::ImportMapTy ImportList;

  auto ModuleLoader = [&DestModule](const char *argv0,
                                    const std::string &Identifier) {
    std::unique_ptr<MemoryBuffer> Buffer =
        ExitOnErr(errorOrToExpected(MemoryBuffer::getFileOrSTDIN(Identifier)));
    return loadFile(argv0, std::move(Buffer), DestModule.getContext(), false);
  };

  ModuleLazyLoaderCache ModuleLoaderCache(ModuleLoader);
  // Owns the filename strings used to key into the ImportList. Normally this is
  // constructed from the index and the strings are owned by the index, however,
  // since we are synthesizing this data structure from options we need a cache
  // to own those strings.
  StringSet<> FileNameStringCache;
  for (const auto &Import : Imports) {
    // Identify the requested function and its bitcode source file.
    size_t Idx = Import.find(':');
    if (Idx == std::string::npos) {
      errs() << "Import parameter bad format: " << Import << "\n";
      return false;
    }
    std::string FunctionName = Import.substr(0, Idx);
    std::string FileName = Import.substr(Idx + 1, std::string::npos);

    // Load the specified source module.
    auto &SrcModule = ModuleLoaderCache(argv0, FileName);

    if (!NoVerify && verifyModule(SrcModule, &errs())) {
      errs() << argv0 << ": " << FileName;
      WithColor::error() << "input module is broken!\n";
      return false;
    }

    Function *F = SrcModule.getFunction(FunctionName);
    if (!F) {
      errs() << "Ignoring import request for non-existent function "
             << FunctionName << " from " << FileName << "\n";
      continue;
    }
    // We cannot import weak_any functions without possibly affecting the
    // order they are seen and selected by the linker, changing program
    // semantics.
    if (F->hasWeakAnyLinkage()) {
      errs() << "Ignoring import request for weak-any function " << FunctionName
             << " from " << FileName << "\n";
      continue;
    }

    if (Verbose)
      errs() << "Importing " << FunctionName << " from " << FileName << "\n";

    // `-import` specifies the `<filename,function-name>` pairs to import as
    // definition, so make the import type definition directly.
    // FIXME: A follow-up patch should add test coverage for import declaration
    // in `llvm-link` CLI (e.g., by introducing a new command line option).
    auto &Entry =
        ImportList[FileNameStringCache.insert(FileName).first->getKey()];
    Entry[F->getGUID()] = GlobalValueSummary::Definition;
  }
  auto CachedModuleLoader = [&](StringRef Identifier) {
    return ModuleLoaderCache.takeModule(std::string(Identifier));
  };
  FunctionImporter Importer(*Index, CachedModuleLoader,
                            /*ClearDSOLocalOnDeclarations=*/false);
  ExitOnErr(Importer.importFunctions(DestModule, ImportList));

  return true;
}

static bool linkFiles(const char *argv0, LLVMContext &Context, Linker &L,
                      const cl::list<std::string> &Files, unsigned Flags) {
  // Filter out flags that don't apply to the first file we load.
  unsigned ApplicableFlags = Flags & Linker::Flags::OverrideFromSrc;
  // Similar to some flags, internalization doesn't apply to the first file.
  bool InternalizeLinkedSymbols = false;
  for (const auto &File : Files) {
    auto BufferOrErr = MemoryBuffer::getFileOrSTDIN(File);

    // When we encounter a missing file, make sure we expose its name.
    if (auto EC = BufferOrErr.getError())
      if (EC == std::errc::no_such_file_or_directory)
        ExitOnErr(createStringError(EC, "No such file or directory: '%s'",
                                    File.c_str()));

    std::unique_ptr<MemoryBuffer> Buffer =
        ExitOnErr(errorOrToExpected(std::move(BufferOrErr)));

    std::unique_ptr<Module> M =
        identify_magic(Buffer->getBuffer()) == file_magic::archive
            ? loadArFile(argv0, std::move(Buffer), Context)
            : loadFile(argv0, std::move(Buffer), Context);
    if (!M) {
      errs() << argv0 << ": ";
      WithColor::error() << " loading file '" << File << "'\n";
      return false;
    }

    // Note that when ODR merging types cannot verify input files in here When
    // doing that debug metadata in the src module might already be pointing to
    // the destination.
    if (DisableDITypeMap && !NoVerify && verifyModule(*M, &errs())) {
      errs() << argv0 << ": " << File << ": ";
      WithColor::error() << "input module is broken!\n";
      return false;
    }

    // If a module summary index is supplied, load it so linkInModule can treat
    // local functions/variables as exported and promote if necessary.
    if (!SummaryIndex.empty()) {
      std::unique_ptr<ModuleSummaryIndex> Index =
          ExitOnErr(llvm::getModuleSummaryIndexForFile(SummaryIndex));

      // Conservatively mark all internal values as promoted, since this tool
      // does not do the ThinLink that would normally determine what values to
      // promote.
      for (auto &I : *Index) {
        for (auto &S : I.second.SummaryList) {
          if (GlobalValue::isLocalLinkage(S->linkage()))
            S->setLinkage(GlobalValue::ExternalLinkage);
        }
      }

      // Promotion
      if (renameModuleForThinLTO(*M, *Index,
                                 /*ClearDSOLocalOnDeclarations=*/false))
        return true;
    }

    if (Verbose)
      errs() << "Linking in '" << File << "'\n";

    bool Err = false;
    if (InternalizeLinkedSymbols) {
      Err = L.linkInModule(
          std::move(M), ApplicableFlags, [](Module &M, const StringSet<> &GVS) {
            internalizeModule(M, [&GVS](const GlobalValue &GV) {
              return !GV.hasName() || (GVS.count(GV.getName()) == 0);
            });
          });
    } else {
      Err = L.linkInModule(std::move(M), ApplicableFlags);
    }

    if (Err)
      return false;

    // Internalization applies to linking of subsequent files.
    InternalizeLinkedSymbols = Internalize;

    // All linker flags apply to linking of subsequent files.
    ApplicableFlags = Flags;
  }

  return true;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  cl::HideUnrelatedOptions({&LinkCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv, "llvm linker\n");

  // Load bitcode into the new debug info format by default.
  if (LoadBitcodeIntoNewDbgInfoFormat == cl::boolOrDefault::BOU_UNSET)
    LoadBitcodeIntoNewDbgInfoFormat = cl::boolOrDefault::BOU_TRUE;

  // Since llvm-link collects multiple IR modules together, for simplicity's
  // sake we disable the "PreserveInputDbgFormat" flag to enforce a single
  // debug info format.
  PreserveInputDbgFormat = cl::boolOrDefault::BOU_FALSE;

  LLVMContext Context;
  Context.setDiagnosticHandler(std::make_unique<LLVMLinkDiagnosticHandler>(),
                               true);

  if (!DisableDITypeMap)
    Context.enableDebugTypeODRUniquing();

  auto Composite = std::make_unique<Module>("llvm-link", Context);
  Linker L(*Composite);

  unsigned Flags = Linker::Flags::None;
  if (OnlyNeeded)
    Flags |= Linker::Flags::LinkOnlyNeeded;

  // First add all the regular input files
  if (!linkFiles(argv[0], Context, L, InputFilenames, Flags))
    return 1;

  // Next the -override ones.
  if (!linkFiles(argv[0], Context, L, OverridingInputs,
                 Flags | Linker::Flags::OverrideFromSrc))
    return 1;

  // Import any functions requested via -import
  if (!importFunctions(argv[0], *Composite))
    return 1;

  if (DumpAsm)
    errs() << "Here's the assembly:\n" << *Composite;

  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC,
                     OutputAssembly ? sys::fs::OF_TextWithCRLF
                                    : sys::fs::OF_None);
  if (EC) {
    WithColor::error() << EC.message() << '\n';
    return 1;
  }

  if (!NoVerify && verifyModule(*Composite, &errs())) {
    errs() << argv[0] << ": ";
    WithColor::error() << "linked module is broken!\n";
    return 1;
  }

  if (Verbose)
    errs() << "Writing bitcode...\n";
  auto SetFormat = [&](bool NewFormat) {
    Composite->setIsNewDbgInfoFormat(NewFormat);
    if (NewFormat)
      Composite->removeDebugIntrinsicDeclarations();
  };
  if (OutputAssembly) {
    SetFormat(WriteNewDbgInfoFormat);
    Composite->print(Out.os(), nullptr, PreserveAssemblyUseListOrder);
  } else if (Force || !CheckBitcodeOutputToConsole(Out.os())) {
    SetFormat(UseNewDbgInfoFormat && WriteNewDbgInfoFormatToBitcode);
    WriteBitcodeToFile(*Composite, Out.os(), PreserveBitcodeUseListOrder);
  }

  // Declare success.
  Out.keep();

  return 0;
}
