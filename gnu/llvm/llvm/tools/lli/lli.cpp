//===- lli.cpp - LLVM Interpreter / Dynamic compiler ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility provides a simple wrapper around the LLVM Execution Engines,
// which allow the direct execution of LLVM programs through a Just-In-Time
// compiler, or through an interpreter if no JIT is available for this platform.
//
//===----------------------------------------------------------------------===//

#include "ForwardingMemoryManager.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/ObjectCache.h"
#include "llvm/ExecutionEngine/Orc/DebugUtils.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h"
#include "llvm/ExecutionEngine/Orc/EPCDynamicLibrarySearchGenerator.h"
#include "llvm/ExecutionEngine/Orc/EPCEHFrameRegistrar.h"
#include "llvm/ExecutionEngine/Orc/EPCGenericRTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/SimpleRemoteEPC.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderGDB.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/TargetExecutionUtils.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Instrumentation.h"
#include <cerrno>
#include <optional>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

#ifdef __CYGWIN__
#include <cygwin/version.h>
#if defined(CYGWIN_VERSION_DLL_MAJOR) && CYGWIN_VERSION_DLL_MAJOR<1007
#define DO_NOTHING_ATEXIT 1
#endif
#endif

using namespace llvm;

static codegen::RegisterCodeGenFlags CGF;

#define DEBUG_TYPE "lli"

namespace {

  enum class JITKind { MCJIT, Orc, OrcLazy };
  enum class JITLinkerKind { Default, RuntimeDyld, JITLink };

  cl::opt<std::string>
  InputFile(cl::desc("<input bitcode>"), cl::Positional, cl::init("-"));

  cl::list<std::string>
  InputArgv(cl::ConsumeAfter, cl::desc("<program arguments>..."));

  cl::opt<bool> ForceInterpreter("force-interpreter",
                                 cl::desc("Force interpretation: disable JIT"),
                                 cl::init(false));

  cl::opt<JITKind> UseJITKind(
      "jit-kind", cl::desc("Choose underlying JIT kind."),
      cl::init(JITKind::Orc),
      cl::values(clEnumValN(JITKind::MCJIT, "mcjit", "MCJIT"),
                 clEnumValN(JITKind::Orc, "orc", "Orc JIT"),
                 clEnumValN(JITKind::OrcLazy, "orc-lazy",
                            "Orc-based lazy JIT.")));

  cl::opt<JITLinkerKind>
      JITLinker("jit-linker", cl::desc("Choose the dynamic linker/loader."),
                cl::init(JITLinkerKind::Default),
                cl::values(clEnumValN(JITLinkerKind::Default, "default",
                                      "Default for platform and JIT-kind"),
                           clEnumValN(JITLinkerKind::RuntimeDyld, "rtdyld",
                                      "RuntimeDyld"),
                           clEnumValN(JITLinkerKind::JITLink, "jitlink",
                                      "Orc-specific linker")));
  cl::opt<std::string> OrcRuntime("orc-runtime",
                                  cl::desc("Use ORC runtime from given path"),
                                  cl::init(""));

  cl::opt<unsigned>
  LazyJITCompileThreads("compile-threads",
                        cl::desc("Choose the number of compile threads "
                                 "(jit-kind=orc-lazy only)"),
                        cl::init(0));

  cl::list<std::string>
  ThreadEntryPoints("thread-entry",
                    cl::desc("calls the given entry-point on a new thread "
                             "(jit-kind=orc-lazy only)"));

  cl::opt<bool> PerModuleLazy(
      "per-module-lazy",
      cl::desc("Performs lazy compilation on whole module boundaries "
               "rather than individual functions"),
      cl::init(false));

  cl::list<std::string>
      JITDylibs("jd",
                cl::desc("Specifies the JITDylib to be used for any subsequent "
                         "-extra-module arguments."));

  cl::list<std::string>
      Dylibs("dlopen", cl::desc("Dynamic libraries to load before linking"));

  // The MCJIT supports building for a target address space separate from
  // the JIT compilation process. Use a forked process and a copying
  // memory manager with IPC to execute using this functionality.
  cl::opt<bool> RemoteMCJIT("remote-mcjit",
    cl::desc("Execute MCJIT'ed code in a separate process."),
    cl::init(false));

  // Manually specify the child process for remote execution. This overrides
  // the simulated remote execution that allocates address space for child
  // execution. The child process will be executed and will communicate with
  // lli via stdin/stdout pipes.
  cl::opt<std::string>
  ChildExecPath("mcjit-remote-process",
                cl::desc("Specify the filename of the process to launch "
                         "for remote MCJIT execution.  If none is specified,"
                         "\n\tremote execution will be simulated in-process."),
                cl::value_desc("filename"), cl::init(""));

  // Determine optimization level.
  cl::opt<char> OptLevel("O",
                         cl::desc("Optimization level. [-O0, -O1, -O2, or -O3] "
                                  "(default = '-O2')"),
                         cl::Prefix, cl::init('2'));

  cl::opt<std::string>
  TargetTriple("mtriple", cl::desc("Override target triple for module"));

  cl::opt<std::string>
  EntryFunc("entry-function",
            cl::desc("Specify the entry function (default = 'main') "
                     "of the executable"),
            cl::value_desc("function"),
            cl::init("main"));

  cl::list<std::string>
  ExtraModules("extra-module",
         cl::desc("Extra modules to be loaded"),
         cl::value_desc("input bitcode"));

  cl::list<std::string>
  ExtraObjects("extra-object",
         cl::desc("Extra object files to be loaded"),
         cl::value_desc("input object"));

  cl::list<std::string>
  ExtraArchives("extra-archive",
         cl::desc("Extra archive files to be loaded"),
         cl::value_desc("input archive"));

  cl::opt<bool>
  EnableCacheManager("enable-cache-manager",
        cl::desc("Use cache manager to save/load modules"),
        cl::init(false));

  cl::opt<std::string>
  ObjectCacheDir("object-cache-dir",
                  cl::desc("Directory to store cached object files "
                           "(must be user writable)"),
                  cl::init(""));

  cl::opt<std::string>
  FakeArgv0("fake-argv0",
            cl::desc("Override the 'argv[0]' value passed into the executing"
                     " program"), cl::value_desc("executable"));

  cl::opt<bool>
  DisableCoreFiles("disable-core-files", cl::Hidden,
                   cl::desc("Disable emission of core files if possible"));

  cl::opt<bool>
  NoLazyCompilation("disable-lazy-compilation",
                  cl::desc("Disable JIT lazy compilation"),
                  cl::init(false));

  cl::opt<bool>
  GenerateSoftFloatCalls("soft-float",
    cl::desc("Generate software floating point library calls"),
    cl::init(false));

  cl::opt<bool> NoProcessSymbols(
      "no-process-syms",
      cl::desc("Do not resolve lli process symbols in JIT'd code"),
      cl::init(false));

  enum class LLJITPlatform { Inactive, Auto, ExecutorNative, GenericIR };

  cl::opt<LLJITPlatform> Platform(
      "lljit-platform", cl::desc("Platform to use with LLJIT"),
      cl::init(LLJITPlatform::Auto),
      cl::values(clEnumValN(LLJITPlatform::Auto, "Auto",
                            "Like 'ExecutorNative' if ORC runtime "
                            "provided, otherwise like 'GenericIR'"),
                 clEnumValN(LLJITPlatform::ExecutorNative, "ExecutorNative",
                            "Use the native platform for the executor."
                            "Requires -orc-runtime"),
                 clEnumValN(LLJITPlatform::GenericIR, "GenericIR",
                            "Use LLJITGenericIRPlatform"),
                 clEnumValN(LLJITPlatform::Inactive, "Inactive",
                            "Disable platform support explicitly")),
      cl::Hidden);

  enum class DumpKind {
    NoDump,
    DumpFuncsToStdOut,
    DumpModsToStdOut,
    DumpModsToDisk,
    DumpDebugDescriptor,
    DumpDebugObjects,
  };

  cl::opt<DumpKind> OrcDumpKind(
      "orc-lazy-debug", cl::desc("Debug dumping for the orc-lazy JIT."),
      cl::init(DumpKind::NoDump),
      cl::values(
          clEnumValN(DumpKind::NoDump, "no-dump", "Don't dump anything."),
          clEnumValN(DumpKind::DumpFuncsToStdOut, "funcs-to-stdout",
                     "Dump function names to stdout."),
          clEnumValN(DumpKind::DumpModsToStdOut, "mods-to-stdout",
                     "Dump modules to stdout."),
          clEnumValN(DumpKind::DumpModsToDisk, "mods-to-disk",
                     "Dump modules to the current "
                     "working directory. (WARNING: "
                     "will overwrite existing files)."),
          clEnumValN(DumpKind::DumpDebugDescriptor, "jit-debug-descriptor",
                     "Dump __jit_debug_descriptor contents to stdout"),
          clEnumValN(DumpKind::DumpDebugObjects, "jit-debug-objects",
                     "Dump __jit_debug_descriptor in-memory debug "
                     "objects as tool output")),
      cl::Hidden);

  ExitOnError ExitOnErr;
}

LLVM_ATTRIBUTE_USED void linkComponents() {
  errs() << (void *)&llvm_orc_registerEHFrameSectionWrapper
         << (void *)&llvm_orc_deregisterEHFrameSectionWrapper
         << (void *)&llvm_orc_registerJITLoaderGDBWrapper
         << (void *)&llvm_orc_registerJITLoaderGDBAllocAction;
}

//===----------------------------------------------------------------------===//
// Object cache
//
// This object cache implementation writes cached objects to disk to the
// directory specified by CacheDir, using a filename provided in the module
// descriptor. The cache tries to load a saved object using that path if the
// file exists. CacheDir defaults to "", in which case objects are cached
// alongside their originating bitcodes.
//
class LLIObjectCache : public ObjectCache {
public:
  LLIObjectCache(const std::string& CacheDir) : CacheDir(CacheDir) {
    // Add trailing '/' to cache dir if necessary.
    if (!this->CacheDir.empty() &&
        this->CacheDir[this->CacheDir.size() - 1] != '/')
      this->CacheDir += '/';
  }
  ~LLIObjectCache() override {}

  void notifyObjectCompiled(const Module *M, MemoryBufferRef Obj) override {
    const std::string &ModuleID = M->getModuleIdentifier();
    std::string CacheName;
    if (!getCacheFilename(ModuleID, CacheName))
      return;
    if (!CacheDir.empty()) { // Create user-defined cache dir.
      SmallString<128> dir(sys::path::parent_path(CacheName));
      sys::fs::create_directories(Twine(dir));
    }

    std::error_code EC;
    raw_fd_ostream outfile(CacheName, EC, sys::fs::OF_None);
    outfile.write(Obj.getBufferStart(), Obj.getBufferSize());
    outfile.close();
  }

  std::unique_ptr<MemoryBuffer> getObject(const Module* M) override {
    const std::string &ModuleID = M->getModuleIdentifier();
    std::string CacheName;
    if (!getCacheFilename(ModuleID, CacheName))
      return nullptr;
    // Load the object from the cache filename
    ErrorOr<std::unique_ptr<MemoryBuffer>> IRObjectBuffer =
        MemoryBuffer::getFile(CacheName, /*IsText=*/false,
                              /*RequiresNullTerminator=*/false);
    // If the file isn't there, that's OK.
    if (!IRObjectBuffer)
      return nullptr;
    // MCJIT will want to write into this buffer, and we don't want that
    // because the file has probably just been mmapped.  Instead we make
    // a copy.  The filed-based buffer will be released when it goes
    // out of scope.
    return MemoryBuffer::getMemBufferCopy(IRObjectBuffer.get()->getBuffer());
  }

private:
  std::string CacheDir;

  bool getCacheFilename(StringRef ModID, std::string &CacheName) {
    if (!ModID.consume_front("file:"))
      return false;

    std::string CacheSubdir = std::string(ModID);
    // Transform "X:\foo" => "/X\foo" for convenience on Windows.
    if (is_style_windows(llvm::sys::path::Style::native) &&
        isalpha(CacheSubdir[0]) && CacheSubdir[1] == ':') {
      CacheSubdir[1] = CacheSubdir[0];
      CacheSubdir[0] = '/';
    }

    CacheName = CacheDir + CacheSubdir;
    size_t pos = CacheName.rfind('.');
    CacheName.replace(pos, CacheName.length() - pos, ".o");
    return true;
  }
};

// On Mingw and Cygwin, an external symbol named '__main' is called from the
// generated 'main' function to allow static initialization.  To avoid linking
// problems with remote targets (because lli's remote target support does not
// currently handle external linking) we add a secondary module which defines
// an empty '__main' function.
static void addCygMingExtraModule(ExecutionEngine &EE, LLVMContext &Context,
                                  StringRef TargetTripleStr) {
  IRBuilder<> Builder(Context);
  Triple TargetTriple(TargetTripleStr);

  // Create a new module.
  std::unique_ptr<Module> M = std::make_unique<Module>("CygMingHelper", Context);
  M->setTargetTriple(TargetTripleStr);

  // Create an empty function named "__main".
  Type *ReturnTy;
  if (TargetTriple.isArch64Bit())
    ReturnTy = Type::getInt64Ty(Context);
  else
    ReturnTy = Type::getInt32Ty(Context);
  Function *Result =
      Function::Create(FunctionType::get(ReturnTy, {}, false),
                       GlobalValue::ExternalLinkage, "__main", M.get());

  BasicBlock *BB = BasicBlock::Create(Context, "__main", Result);
  Builder.SetInsertPoint(BB);
  Value *ReturnVal = ConstantInt::get(ReturnTy, 0);
  Builder.CreateRet(ReturnVal);

  // Add this new module to the ExecutionEngine.
  EE.addModule(std::move(M));
}

CodeGenOptLevel getOptLevel() {
  if (auto Level = CodeGenOpt::parseLevel(OptLevel))
    return *Level;
  WithColor::error(errs(), "lli") << "invalid optimization level.\n";
  exit(1);
}

[[noreturn]] static void reportError(SMDiagnostic Err, const char *ProgName) {
  Err.print(ProgName, errs());
  exit(1);
}

Error loadDylibs();
int runOrcJIT(const char *ProgName);
void disallowOrcOptions();
Expected<std::unique_ptr<orc::ExecutorProcessControl>> launchRemote();

//===----------------------------------------------------------------------===//
// main Driver function
//
int main(int argc, char **argv, char * const *envp) {
  InitLLVM X(argc, argv);

  if (argc > 1)
    ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  // If we have a native target, initialize it to ensure it is linked in and
  // usable by the JIT.
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  cl::ParseCommandLineOptions(argc, argv,
                              "llvm interpreter & dynamic compiler\n");

  // If the user doesn't want core files, disable them.
  if (DisableCoreFiles)
    sys::Process::PreventCoreFiles();

  ExitOnErr(loadDylibs());

  if (EntryFunc.empty()) {
    WithColor::error(errs(), argv[0])
        << "--entry-function name cannot be empty\n";
    exit(1);
  }

  if (UseJITKind == JITKind::MCJIT || ForceInterpreter)
    disallowOrcOptions();
  else
    return runOrcJIT(argv[0]);

  // Old lli implementation based on ExecutionEngine and MCJIT.
  LLVMContext Context;

  // Load the bitcode...
  SMDiagnostic Err;
  std::unique_ptr<Module> Owner = parseIRFile(InputFile, Err, Context);
  Module *Mod = Owner.get();
  if (!Mod)
    reportError(Err, argv[0]);

  if (EnableCacheManager) {
    std::string CacheName("file:");
    CacheName.append(InputFile);
    Mod->setModuleIdentifier(CacheName);
  }

  // If not jitting lazily, load the whole bitcode file eagerly too.
  if (NoLazyCompilation) {
    // Use *argv instead of argv[0] to work around a wrong GCC warning.
    ExitOnError ExitOnErr(std::string(*argv) +
                          ": bitcode didn't read correctly: ");
    ExitOnErr(Mod->materializeAll());
  }

  std::string ErrorMsg;
  EngineBuilder builder(std::move(Owner));
  builder.setMArch(codegen::getMArch());
  builder.setMCPU(codegen::getCPUStr());
  builder.setMAttrs(codegen::getFeatureList());
  if (auto RM = codegen::getExplicitRelocModel())
    builder.setRelocationModel(*RM);
  if (auto CM = codegen::getExplicitCodeModel())
    builder.setCodeModel(*CM);
  builder.setErrorStr(&ErrorMsg);
  builder.setEngineKind(ForceInterpreter
                        ? EngineKind::Interpreter
                        : EngineKind::JIT);

  // If we are supposed to override the target triple, do so now.
  if (!TargetTriple.empty())
    Mod->setTargetTriple(Triple::normalize(TargetTriple));

  // Enable MCJIT if desired.
  RTDyldMemoryManager *RTDyldMM = nullptr;
  if (!ForceInterpreter) {
    if (RemoteMCJIT)
      RTDyldMM = new ForwardingMemoryManager();
    else
      RTDyldMM = new SectionMemoryManager();

    // Deliberately construct a temp std::unique_ptr to pass in. Do not null out
    // RTDyldMM: We still use it below, even though we don't own it.
    builder.setMCJITMemoryManager(
      std::unique_ptr<RTDyldMemoryManager>(RTDyldMM));
  } else if (RemoteMCJIT) {
    WithColor::error(errs(), argv[0])
        << "remote process execution does not work with the interpreter.\n";
    exit(1);
  }

  builder.setOptLevel(getOptLevel());

  TargetOptions Options =
      codegen::InitTargetOptionsFromCodeGenFlags(Triple(TargetTriple));
  if (codegen::getFloatABIForCalls() != FloatABI::Default)
    Options.FloatABIType = codegen::getFloatABIForCalls();

  builder.setTargetOptions(Options);

  std::unique_ptr<ExecutionEngine> EE(builder.create());
  if (!EE) {
    if (!ErrorMsg.empty())
      WithColor::error(errs(), argv[0])
          << "error creating EE: " << ErrorMsg << "\n";
    else
      WithColor::error(errs(), argv[0]) << "unknown error creating EE!\n";
    exit(1);
  }

  std::unique_ptr<LLIObjectCache> CacheManager;
  if (EnableCacheManager) {
    CacheManager.reset(new LLIObjectCache(ObjectCacheDir));
    EE->setObjectCache(CacheManager.get());
  }

  // Load any additional modules specified on the command line.
  for (unsigned i = 0, e = ExtraModules.size(); i != e; ++i) {
    std::unique_ptr<Module> XMod = parseIRFile(ExtraModules[i], Err, Context);
    if (!XMod)
      reportError(Err, argv[0]);
    if (EnableCacheManager) {
      std::string CacheName("file:");
      CacheName.append(ExtraModules[i]);
      XMod->setModuleIdentifier(CacheName);
    }
    EE->addModule(std::move(XMod));
  }

  for (unsigned i = 0, e = ExtraObjects.size(); i != e; ++i) {
    Expected<object::OwningBinary<object::ObjectFile>> Obj =
        object::ObjectFile::createObjectFile(ExtraObjects[i]);
    if (!Obj) {
      // TODO: Actually report errors helpfully.
      consumeError(Obj.takeError());
      reportError(Err, argv[0]);
    }
    object::OwningBinary<object::ObjectFile> &O = Obj.get();
    EE->addObjectFile(std::move(O));
  }

  for (unsigned i = 0, e = ExtraArchives.size(); i != e; ++i) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> ArBufOrErr =
        MemoryBuffer::getFileOrSTDIN(ExtraArchives[i]);
    if (!ArBufOrErr)
      reportError(Err, argv[0]);
    std::unique_ptr<MemoryBuffer> &ArBuf = ArBufOrErr.get();

    Expected<std::unique_ptr<object::Archive>> ArOrErr =
        object::Archive::create(ArBuf->getMemBufferRef());
    if (!ArOrErr) {
      std::string Buf;
      raw_string_ostream OS(Buf);
      logAllUnhandledErrors(ArOrErr.takeError(), OS);
      OS.flush();
      errs() << Buf;
      exit(1);
    }
    std::unique_ptr<object::Archive> &Ar = ArOrErr.get();

    object::OwningBinary<object::Archive> OB(std::move(Ar), std::move(ArBuf));

    EE->addArchive(std::move(OB));
  }

  // If the target is Cygwin/MingW and we are generating remote code, we
  // need an extra module to help out with linking.
  if (RemoteMCJIT && Triple(Mod->getTargetTriple()).isOSCygMing()) {
    addCygMingExtraModule(*EE, Context, Mod->getTargetTriple());
  }

  // The following functions have no effect if their respective profiling
  // support wasn't enabled in the build configuration.
  EE->RegisterJITEventListener(
                JITEventListener::createOProfileJITEventListener());
  EE->RegisterJITEventListener(
                JITEventListener::createIntelJITEventListener());
  if (!RemoteMCJIT)
    EE->RegisterJITEventListener(
                JITEventListener::createPerfJITEventListener());

  if (!NoLazyCompilation && RemoteMCJIT) {
    WithColor::warning(errs(), argv[0])
        << "remote mcjit does not support lazy compilation\n";
    NoLazyCompilation = true;
  }
  EE->DisableLazyCompilation(NoLazyCompilation);

  // If the user specifically requested an argv[0] to pass into the program,
  // do it now.
  if (!FakeArgv0.empty()) {
    InputFile = static_cast<std::string>(FakeArgv0);
  } else {
    // Otherwise, if there is a .bc suffix on the executable strip it off, it
    // might confuse the program.
    if (StringRef(InputFile).ends_with(".bc"))
      InputFile.erase(InputFile.length() - 3);
  }

  // Add the module's name to the start of the vector of arguments to main().
  InputArgv.insert(InputArgv.begin(), InputFile);

  // Call the main function from M as if its signature were:
  //   int main (int argc, char **argv, const char **envp)
  // using the contents of Args to determine argc & argv, and the contents of
  // EnvVars to determine envp.
  //
  Function *EntryFn = Mod->getFunction(EntryFunc);
  if (!EntryFn) {
    WithColor::error(errs(), argv[0])
        << '\'' << EntryFunc << "\' function not found in module.\n";
    return -1;
  }

  // Reset errno to zero on entry to main.
  errno = 0;

  int Result = -1;

  // Sanity check use of remote-jit: LLI currently only supports use of the
  // remote JIT on Unix platforms.
  if (RemoteMCJIT) {
#ifndef LLVM_ON_UNIX
    WithColor::warning(errs(), argv[0])
        << "host does not support external remote targets.\n";
    WithColor::note() << "defaulting to local execution\n";
    return -1;
#else
    if (ChildExecPath.empty()) {
      WithColor::error(errs(), argv[0])
          << "-remote-mcjit requires -mcjit-remote-process.\n";
      exit(1);
    } else if (!sys::fs::can_execute(ChildExecPath)) {
      WithColor::error(errs(), argv[0])
          << "unable to find usable child executable: '" << ChildExecPath
          << "'\n";
      return -1;
    }
#endif
  }

  if (!RemoteMCJIT) {
    // If the program doesn't explicitly call exit, we will need the Exit
    // function later on to make an explicit call, so get the function now.
    FunctionCallee Exit = Mod->getOrInsertFunction(
        "exit", Type::getVoidTy(Context), Type::getInt32Ty(Context));

    // Run static constructors.
    if (!ForceInterpreter) {
      // Give MCJIT a chance to apply relocations and set page permissions.
      EE->finalizeObject();
    }
    EE->runStaticConstructorsDestructors(false);

    // Trigger compilation separately so code regions that need to be
    // invalidated will be known.
    (void)EE->getPointerToFunction(EntryFn);
    // Clear instruction cache before code will be executed.
    if (RTDyldMM)
      static_cast<SectionMemoryManager*>(RTDyldMM)->invalidateInstructionCache();

    // Run main.
    Result = EE->runFunctionAsMain(EntryFn, InputArgv, envp);

    // Run static destructors.
    EE->runStaticConstructorsDestructors(true);

    // If the program didn't call exit explicitly, we should call it now.
    // This ensures that any atexit handlers get called correctly.
    if (Function *ExitF =
            dyn_cast<Function>(Exit.getCallee()->stripPointerCasts())) {
      if (ExitF->getFunctionType() == Exit.getFunctionType()) {
        std::vector<GenericValue> Args;
        GenericValue ResultGV;
        ResultGV.IntVal = APInt(32, Result);
        Args.push_back(ResultGV);
        EE->runFunction(ExitF, Args);
        WithColor::error(errs(), argv[0])
            << "exit(" << Result << ") returned!\n";
        abort();
      }
    }
    WithColor::error(errs(), argv[0]) << "exit defined with wrong prototype!\n";
    abort();
  } else {
    // else == "if (RemoteMCJIT)"
    std::unique_ptr<orc::ExecutorProcessControl> EPC = ExitOnErr(launchRemote());

    // Remote target MCJIT doesn't (yet) support static constructors. No reason
    // it couldn't. This is a limitation of the LLI implementation, not the
    // MCJIT itself. FIXME.

    // Create a remote memory manager.
    auto RemoteMM = ExitOnErr(
        orc::EPCGenericRTDyldMemoryManager::CreateWithDefaultBootstrapSymbols(
            *EPC));

    // Forward MCJIT's memory manager calls to the remote memory manager.
    static_cast<ForwardingMemoryManager*>(RTDyldMM)->setMemMgr(
      std::move(RemoteMM));

    // Forward MCJIT's symbol resolution calls to the remote.
    static_cast<ForwardingMemoryManager *>(RTDyldMM)->setResolver(
        ExitOnErr(RemoteResolver::Create(*EPC)));
    // Grab the target address of the JIT'd main function on the remote and call
    // it.
    // FIXME: argv and envp handling.
    auto Entry =
        orc::ExecutorAddr(EE->getFunctionAddress(EntryFn->getName().str()));
    EE->finalizeObject();
    LLVM_DEBUG(dbgs() << "Executing '" << EntryFn->getName() << "' at 0x"
                      << format("%llx", Entry.getValue()) << "\n");
    Result = ExitOnErr(EPC->runAsMain(Entry, {}));

    // Like static constructors, the remote target MCJIT support doesn't handle
    // this yet. It could. FIXME.

    // Delete the EE - we need to tear it down *before* we terminate the session
    // with the remote, otherwise it'll crash when it tries to release resources
    // on a remote that has already been disconnected.
    EE.reset();

    // Signal the remote target that we're done JITing.
    ExitOnErr(EPC->disconnect());
  }

  return Result;
}

// JITLink debug support plugins put information about JITed code in this GDB
// JIT Interface global from OrcTargetProcess.
extern "C" struct jit_descriptor __jit_debug_descriptor;

static struct jit_code_entry *
findNextDebugDescriptorEntry(struct jit_code_entry *Latest) {
  if (Latest == nullptr)
    return __jit_debug_descriptor.first_entry;
  if (Latest->next_entry)
    return Latest->next_entry;
  return nullptr;
}

static ToolOutputFile &claimToolOutput() {
  static std::unique_ptr<ToolOutputFile> ToolOutput = nullptr;
  if (ToolOutput) {
    WithColor::error(errs(), "lli")
        << "Can not claim stdout for tool output twice\n";
    exit(1);
  }
  std::error_code EC;
  ToolOutput = std::make_unique<ToolOutputFile>("-", EC, sys::fs::OF_None);
  if (EC) {
    WithColor::error(errs(), "lli")
        << "Failed to create tool output file: " << EC.message() << "\n";
    exit(1);
  }
  return *ToolOutput;
}

static std::function<void(Module &)> createIRDebugDumper() {
  switch (OrcDumpKind) {
  case DumpKind::NoDump:
  case DumpKind::DumpDebugDescriptor:
  case DumpKind::DumpDebugObjects:
    return [](Module &M) {};

  case DumpKind::DumpFuncsToStdOut:
    return [](Module &M) {
      printf("[ ");

      for (const auto &F : M) {
        if (F.isDeclaration())
          continue;

        if (F.hasName()) {
          std::string Name(std::string(F.getName()));
          printf("%s ", Name.c_str());
        } else
          printf("<anon> ");
      }

      printf("]\n");
    };

  case DumpKind::DumpModsToStdOut:
    return [](Module &M) {
      outs() << "----- Module Start -----\n" << M << "----- Module End -----\n";
    };

  case DumpKind::DumpModsToDisk:
    return [](Module &M) {
      std::error_code EC;
      raw_fd_ostream Out(M.getModuleIdentifier() + ".ll", EC,
                         sys::fs::OF_TextWithCRLF);
      if (EC) {
        errs() << "Couldn't open " << M.getModuleIdentifier()
               << " for dumping.\nError:" << EC.message() << "\n";
        exit(1);
      }
      Out << M;
    };
  }
  llvm_unreachable("Unknown DumpKind");
}

static std::function<void(MemoryBuffer &)> createObjDebugDumper() {
  switch (OrcDumpKind) {
  case DumpKind::NoDump:
  case DumpKind::DumpFuncsToStdOut:
  case DumpKind::DumpModsToStdOut:
  case DumpKind::DumpModsToDisk:
    return [](MemoryBuffer &) {};

  case DumpKind::DumpDebugDescriptor: {
    // Dump the empty descriptor at startup once
    fprintf(stderr, "jit_debug_descriptor 0x%016" PRIx64 "\n",
            pointerToJITTargetAddress(__jit_debug_descriptor.first_entry));
    return [](MemoryBuffer &) {
      // Dump new entries as they appear
      static struct jit_code_entry *Latest = nullptr;
      while (auto *NewEntry = findNextDebugDescriptorEntry(Latest)) {
        fprintf(stderr, "jit_debug_descriptor 0x%016" PRIx64 "\n",
                pointerToJITTargetAddress(NewEntry));
        Latest = NewEntry;
      }
    };
  }

  case DumpKind::DumpDebugObjects: {
    return [](MemoryBuffer &Obj) {
      static struct jit_code_entry *Latest = nullptr;
      static ToolOutputFile &ToolOutput = claimToolOutput();
      while (auto *NewEntry = findNextDebugDescriptorEntry(Latest)) {
        ToolOutput.os().write(NewEntry->symfile_addr, NewEntry->symfile_size);
        Latest = NewEntry;
      }
    };
  }
  }
  llvm_unreachable("Unknown DumpKind");
}

Error loadDylibs() {
  for (const auto &Dylib : Dylibs) {
    std::string ErrMsg;
    if (sys::DynamicLibrary::LoadLibraryPermanently(Dylib.c_str(), &ErrMsg))
      return make_error<StringError>(ErrMsg, inconvertibleErrorCode());
  }

  return Error::success();
}

static void exitOnLazyCallThroughFailure() { exit(1); }

Expected<orc::ThreadSafeModule>
loadModule(StringRef Path, orc::ThreadSafeContext TSCtx) {
  SMDiagnostic Err;
  auto M = parseIRFile(Path, Err, *TSCtx.getContext());
  if (!M) {
    std::string ErrMsg;
    {
      raw_string_ostream ErrMsgStream(ErrMsg);
      Err.print("lli", ErrMsgStream);
    }
    return make_error<StringError>(std::move(ErrMsg), inconvertibleErrorCode());
  }

  if (EnableCacheManager)
    M->setModuleIdentifier("file:" + M->getModuleIdentifier());

  return orc::ThreadSafeModule(std::move(M), std::move(TSCtx));
}

int mingw_noop_main(void) {
  // Cygwin and MinGW insert calls from the main function to the runtime
  // function __main. The __main function is responsible for setting up main's
  // environment (e.g. running static constructors), however this is not needed
  // when running under lli: the executor process will have run non-JIT ctors,
  // and ORC will take care of running JIT'd ctors. To avoid a missing symbol
  // error we just implement __main as a no-op.
  //
  // FIXME: Move this to ORC-RT (and the ORC-RT substitution library once it
  //        exists). That will allow it to work out-of-process, and for all
  //        ORC tools (the problem isn't lli specific).
  return 0;
}

// Try to enable debugger support for the given instance.
// This alway returns success, but prints a warning if it's not able to enable
// debugger support.
Error tryEnableDebugSupport(orc::LLJIT &J) {
  if (auto Err = enableDebuggerSupport(J)) {
    [[maybe_unused]] std::string ErrMsg = toString(std::move(Err));
    LLVM_DEBUG(dbgs() << "lli: " << ErrMsg << "\n");
  }
  return Error::success();
}

int runOrcJIT(const char *ProgName) {
  // Start setting up the JIT environment.

  // Parse the main module.
  orc::ThreadSafeContext TSCtx(std::make_unique<LLVMContext>());
  auto MainModule = ExitOnErr(loadModule(InputFile, TSCtx));

  // Get TargetTriple and DataLayout from the main module if they're explicitly
  // set.
  std::optional<Triple> TT;
  std::optional<DataLayout> DL;
  MainModule.withModuleDo([&](Module &M) {
      if (!M.getTargetTriple().empty())
        TT = Triple(M.getTargetTriple());
      if (!M.getDataLayout().isDefault())
        DL = M.getDataLayout();
    });

  orc::LLLazyJITBuilder Builder;

  Builder.setJITTargetMachineBuilder(
      TT ? orc::JITTargetMachineBuilder(*TT)
         : ExitOnErr(orc::JITTargetMachineBuilder::detectHost()));

  TT = Builder.getJITTargetMachineBuilder()->getTargetTriple();
  if (DL)
    Builder.setDataLayout(DL);

  if (!codegen::getMArch().empty())
    Builder.getJITTargetMachineBuilder()->getTargetTriple().setArchName(
        codegen::getMArch());

  Builder.getJITTargetMachineBuilder()
      ->setCPU(codegen::getCPUStr())
      .addFeatures(codegen::getFeatureList())
      .setRelocationModel(codegen::getExplicitRelocModel())
      .setCodeModel(codegen::getExplicitCodeModel());

  // Link process symbols unless NoProcessSymbols is set.
  Builder.setLinkProcessSymbolsByDefault(!NoProcessSymbols);

  // FIXME: Setting a dummy call-through manager in non-lazy mode prevents the
  // JIT builder to instantiate a default (which would fail with an error for
  // unsupported architectures).
  if (UseJITKind != JITKind::OrcLazy) {
    auto ES = std::make_unique<orc::ExecutionSession>(
        ExitOnErr(orc::SelfExecutorProcessControl::Create()));
    Builder.setLazyCallthroughManager(
        std::make_unique<orc::LazyCallThroughManager>(*ES, orc::ExecutorAddr(),
                                                      nullptr));
    Builder.setExecutionSession(std::move(ES));
  }

  Builder.setLazyCompileFailureAddr(
      orc::ExecutorAddr::fromPtr(exitOnLazyCallThroughFailure));
  Builder.setNumCompileThreads(LazyJITCompileThreads);

  // If the object cache is enabled then set a custom compile function
  // creator to use the cache.
  std::unique_ptr<LLIObjectCache> CacheManager;
  if (EnableCacheManager) {

    CacheManager = std::make_unique<LLIObjectCache>(ObjectCacheDir);

    Builder.setCompileFunctionCreator(
      [&](orc::JITTargetMachineBuilder JTMB)
            -> Expected<std::unique_ptr<orc::IRCompileLayer::IRCompiler>> {
        if (LazyJITCompileThreads > 0)
          return std::make_unique<orc::ConcurrentIRCompiler>(std::move(JTMB),
                                                        CacheManager.get());

        auto TM = JTMB.createTargetMachine();
        if (!TM)
          return TM.takeError();

        return std::make_unique<orc::TMOwningSimpleCompiler>(std::move(*TM),
                                                        CacheManager.get());
      });
  }

  // Enable debugging of JIT'd code (only works on JITLink for ELF and MachO).
  Builder.setPrePlatformSetup(tryEnableDebugSupport);

  // Set up LLJIT platform.
  LLJITPlatform P = Platform;
  if (P == LLJITPlatform::Auto)
    P = OrcRuntime.empty() ? LLJITPlatform::GenericIR
                           : LLJITPlatform::ExecutorNative;

  switch (P) {
  case LLJITPlatform::ExecutorNative: {
    Builder.setPlatformSetUp(orc::ExecutorNativePlatform(OrcRuntime));
    break;
  }
  case LLJITPlatform::GenericIR:
    // Nothing to do: LLJITBuilder will use this by default.
    break;
  case LLJITPlatform::Inactive:
    Builder.setPlatformSetUp(orc::setUpInactivePlatform);
    break;
  default:
    llvm_unreachable("Unrecognized platform value");
  }

  std::unique_ptr<orc::ExecutorProcessControl> EPC = nullptr;
  if (JITLinker == JITLinkerKind::JITLink) {
    EPC = ExitOnErr(orc::SelfExecutorProcessControl::Create(
        std::make_shared<orc::SymbolStringPool>()));

    Builder.getJITTargetMachineBuilder()
        ->setRelocationModel(Reloc::PIC_)
        .setCodeModel(CodeModel::Small);
    Builder.setObjectLinkingLayerCreator([&P](orc::ExecutionSession &ES,
                                              const Triple &TT) {
      auto L = std::make_unique<orc::ObjectLinkingLayer>(ES);
      if (P != LLJITPlatform::ExecutorNative)
        L->addPlugin(std::make_unique<orc::EHFrameRegistrationPlugin>(
            ES, ExitOnErr(orc::EPCEHFrameRegistrar::Create(ES))));
      return L;
    });
  }

  auto J = ExitOnErr(Builder.create());

  auto *ObjLayer = &J->getObjLinkingLayer();
  if (auto *RTDyldObjLayer = dyn_cast<orc::RTDyldObjectLinkingLayer>(ObjLayer)) {
    RTDyldObjLayer->registerJITEventListener(
        *JITEventListener::createGDBRegistrationListener());
#if LLVM_USE_OPROFILE
    RTDyldObjLayer->registerJITEventListener(
        *JITEventListener::createOProfileJITEventListener());
#endif
#if LLVM_USE_INTEL_JITEVENTS
    RTDyldObjLayer->registerJITEventListener(
        *JITEventListener::createIntelJITEventListener());
#endif
#if LLVM_USE_PERF
    RTDyldObjLayer->registerJITEventListener(
        *JITEventListener::createPerfJITEventListener());
#endif
  }

  if (PerModuleLazy)
    J->setPartitionFunction(orc::CompileOnDemandLayer::compileWholeModule);

  auto IRDump = createIRDebugDumper();
  J->getIRTransformLayer().setTransform(
      [&](orc::ThreadSafeModule TSM,
          const orc::MaterializationResponsibility &R) {
        TSM.withModuleDo([&](Module &M) {
          if (verifyModule(M, &dbgs())) {
            dbgs() << "Bad module: " << &M << "\n";
            exit(1);
          }
          IRDump(M);
        });
        return TSM;
      });

  auto ObjDump = createObjDebugDumper();
  J->getObjTransformLayer().setTransform(
      [&](std::unique_ptr<MemoryBuffer> Obj)
          -> Expected<std::unique_ptr<MemoryBuffer>> {
        ObjDump(*Obj);
        return std::move(Obj);
      });

  // If this is a Mingw or Cygwin executor then we need to alias __main to
  // orc_rt_int_void_return_0.
  if (J->getTargetTriple().isOSCygMing())
    ExitOnErr(J->getProcessSymbolsJITDylib()->define(
        orc::absoluteSymbols({{J->mangleAndIntern("__main"),
                               {orc::ExecutorAddr::fromPtr(mingw_noop_main),
                                JITSymbolFlags::Exported}}})));

  // Regular modules are greedy: They materialize as a whole and trigger
  // materialization for all required symbols recursively. Lazy modules go
  // through partitioning and they replace outgoing calls with reexport stubs
  // that resolve on call-through.
  auto AddModule = [&](orc::JITDylib &JD, orc::ThreadSafeModule M) {
    return UseJITKind == JITKind::OrcLazy ? J->addLazyIRModule(JD, std::move(M))
                                          : J->addIRModule(JD, std::move(M));
  };

  // Add the main module.
  ExitOnErr(AddModule(J->getMainJITDylib(), std::move(MainModule)));

  // Create JITDylibs and add any extra modules.
  {
    // Create JITDylibs, keep a map from argument index to dylib. We will use
    // -extra-module argument indexes to determine what dylib to use for each
    // -extra-module.
    std::map<unsigned, orc::JITDylib *> IdxToDylib;
    IdxToDylib[0] = &J->getMainJITDylib();
    for (auto JDItr = JITDylibs.begin(), JDEnd = JITDylibs.end();
         JDItr != JDEnd; ++JDItr) {
      orc::JITDylib *JD = J->getJITDylibByName(*JDItr);
      if (!JD) {
        JD = &ExitOnErr(J->createJITDylib(*JDItr));
        J->getMainJITDylib().addToLinkOrder(*JD);
        JD->addToLinkOrder(J->getMainJITDylib());
      }
      IdxToDylib[JITDylibs.getPosition(JDItr - JITDylibs.begin())] = JD;
    }

    for (auto EMItr = ExtraModules.begin(), EMEnd = ExtraModules.end();
         EMItr != EMEnd; ++EMItr) {
      auto M = ExitOnErr(loadModule(*EMItr, TSCtx));

      auto EMIdx = ExtraModules.getPosition(EMItr - ExtraModules.begin());
      assert(EMIdx != 0 && "ExtraModule should have index > 0");
      auto JDItr = std::prev(IdxToDylib.lower_bound(EMIdx));
      auto &JD = *JDItr->second;
      ExitOnErr(AddModule(JD, std::move(M)));
    }

    for (auto EAItr = ExtraArchives.begin(), EAEnd = ExtraArchives.end();
         EAItr != EAEnd; ++EAItr) {
      auto EAIdx = ExtraArchives.getPosition(EAItr - ExtraArchives.begin());
      assert(EAIdx != 0 && "ExtraArchive should have index > 0");
      auto JDItr = std::prev(IdxToDylib.lower_bound(EAIdx));
      auto &JD = *JDItr->second;
      ExitOnErr(J->linkStaticLibraryInto(JD, EAItr->c_str()));
    }
  }

  // Add the objects.
  for (auto &ObjPath : ExtraObjects) {
    auto Obj = ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(ObjPath)));
    ExitOnErr(J->addObjectFile(std::move(Obj)));
  }

  // Run any static constructors.
  ExitOnErr(J->initialize(J->getMainJITDylib()));

  // Run any -thread-entry points.
  std::vector<std::thread> AltEntryThreads;
  for (auto &ThreadEntryPoint : ThreadEntryPoints) {
    auto EntryPointSym = ExitOnErr(J->lookup(ThreadEntryPoint));
    typedef void (*EntryPointPtr)();
    auto EntryPoint = EntryPointSym.toPtr<EntryPointPtr>();
    AltEntryThreads.push_back(std::thread([EntryPoint]() { EntryPoint(); }));
  }

  // Resolve and run the main function.
  auto MainAddr = ExitOnErr(J->lookup(EntryFunc));
  int Result;

  if (EPC) {
    // ExecutorProcessControl-based execution with JITLink.
    Result = ExitOnErr(EPC->runAsMain(MainAddr, InputArgv));
  } else {
    // Manual in-process execution with RuntimeDyld.
    using MainFnTy = int(int, char *[]);
    auto MainFn = MainAddr.toPtr<MainFnTy *>();
    Result = orc::runAsMain(MainFn, InputArgv, StringRef(InputFile));
  }

  // Wait for -entry-point threads.
  for (auto &AltEntryThread : AltEntryThreads)
    AltEntryThread.join();

  // Run destructors.
  ExitOnErr(J->deinitialize(J->getMainJITDylib()));

  return Result;
}

void disallowOrcOptions() {
  // Make sure nobody used an orc-lazy specific option accidentally.

  if (LazyJITCompileThreads != 0) {
    errs() << "-compile-threads requires -jit-kind=orc-lazy\n";
    exit(1);
  }

  if (!ThreadEntryPoints.empty()) {
    errs() << "-thread-entry requires -jit-kind=orc-lazy\n";
    exit(1);
  }

  if (PerModuleLazy) {
    errs() << "-per-module-lazy requires -jit-kind=orc-lazy\n";
    exit(1);
  }
}

Expected<std::unique_ptr<orc::ExecutorProcessControl>> launchRemote() {
#ifndef LLVM_ON_UNIX
  llvm_unreachable("launchRemote not supported on non-Unix platforms");
#else
  int PipeFD[2][2];
  pid_t ChildPID;

  // Create two pipes.
  if (pipe(PipeFD[0]) != 0 || pipe(PipeFD[1]) != 0)
    perror("Error creating pipe: ");

  ChildPID = fork();

  if (ChildPID == 0) {
    // In the child...

    // Close the parent ends of the pipes
    close(PipeFD[0][1]);
    close(PipeFD[1][0]);


    // Execute the child process.
    std::unique_ptr<char[]> ChildPath, ChildIn, ChildOut;
    {
      ChildPath.reset(new char[ChildExecPath.size() + 1]);
      std::copy(ChildExecPath.begin(), ChildExecPath.end(), &ChildPath[0]);
      ChildPath[ChildExecPath.size()] = '\0';
      std::string ChildInStr = utostr(PipeFD[0][0]);
      ChildIn.reset(new char[ChildInStr.size() + 1]);
      std::copy(ChildInStr.begin(), ChildInStr.end(), &ChildIn[0]);
      ChildIn[ChildInStr.size()] = '\0';
      std::string ChildOutStr = utostr(PipeFD[1][1]);
      ChildOut.reset(new char[ChildOutStr.size() + 1]);
      std::copy(ChildOutStr.begin(), ChildOutStr.end(), &ChildOut[0]);
      ChildOut[ChildOutStr.size()] = '\0';
    }

    char * const args[] = { &ChildPath[0], &ChildIn[0], &ChildOut[0], nullptr };
    int rc = execv(ChildExecPath.c_str(), args);
    if (rc != 0)
      perror("Error executing child process: ");
    llvm_unreachable("Error executing child process");
  }
  // else we're the parent...

  // Close the child ends of the pipes
  close(PipeFD[0][0]);
  close(PipeFD[1][1]);

  // Return a SimpleRemoteEPC instance connected to our end of the pipes.
  return orc::SimpleRemoteEPC::Create<orc::FDSimpleRemoteEPCTransport>(
      std::make_unique<llvm::orc::InPlaceTaskDispatcher>(),
      llvm::orc::SimpleRemoteEPC::Setup(), PipeFD[1][0], PipeFD[0][1]);
#endif
}

// For MinGW environments, manually export the __chkstk function from the lli
// executable.
//
// Normally, this function is provided by compiler-rt builtins or libgcc.
// It is named "_alloca" on i386, "___chkstk_ms" on x86_64, and "__chkstk" on
// arm/aarch64. In MSVC configurations, it's named "__chkstk" in all
// configurations.
//
// When Orc tries to resolve symbols at runtime, this succeeds in MSVC
// configurations, somewhat by accident/luck; kernelbase.dll does export a
// symbol named "__chkstk" which gets found by Orc, even if regular applications
// never link against that function from that DLL (it's linked in statically
// from a compiler support library).
//
// The MinGW specific symbol names aren't available in that DLL though.
// Therefore, manually export the relevant symbol from lli, to let it be
// found at runtime during tests.
//
// For real JIT uses, the real compiler support libraries should be linked
// in, somehow; this is a workaround to let tests pass.
//
// We need to make sure that this symbol actually is linked in when we
// try to export it; if no functions allocate a large enough stack area,
// nothing would reference it. Therefore, manually declare it and add a
// reference to it. (Note, the declarations of _alloca/___chkstk_ms/__chkstk
// are somewhat bogus, these functions use a different custom calling
// convention.)
//
// TODO: Move this into libORC at some point, see
// https://github.com/llvm/llvm-project/issues/56603.
#ifdef __MINGW32__
// This is a MinGW version of #pragma comment(linker, "...") that doesn't
// require compiling with -fms-extensions.
#if defined(__i386__)
#undef _alloca
extern "C" void _alloca(void);
static __attribute__((used)) void (*const ref_func)(void) = _alloca;
static __attribute__((section(".drectve"), used)) const char export_chkstk[] =
    "-export:_alloca";
#elif defined(__x86_64__)
extern "C" void ___chkstk_ms(void);
static __attribute__((used)) void (*const ref_func)(void) = ___chkstk_ms;
static __attribute__((section(".drectve"), used)) const char export_chkstk[] =
    "-export:___chkstk_ms";
#else
extern "C" void __chkstk(void);
static __attribute__((used)) void (*const ref_func)(void) = __chkstk;
static __attribute__((section(".drectve"), used)) const char export_chkstk[] =
    "-export:__chkstk";
#endif
#endif
