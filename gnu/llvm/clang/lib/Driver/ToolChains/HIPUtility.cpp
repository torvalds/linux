//===--- HIPUtility.cpp - Common HIP Tool Chain Utilities -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HIPUtility.h"
#include "Clang.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <deque>
#include <set>

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::tools;
using namespace llvm::opt;
using llvm::dyn_cast;

#if defined(_WIN32) || defined(_WIN64)
#define NULL_FILE "nul"
#else
#define NULL_FILE "/dev/null"
#endif

namespace {
const unsigned HIPCodeObjectAlign = 4096;
} // namespace

// Constructs a triple string for clang offload bundler.
static std::string normalizeForBundler(const llvm::Triple &T,
                                       bool HasTargetID) {
  return HasTargetID ? (T.getArchName() + "-" + T.getVendorName() + "-" +
                        T.getOSName() + "-" + T.getEnvironmentName())
                           .str()
                     : T.normalize();
}

// Collect undefined __hip_fatbin* and __hip_gpubin_handle* symbols from all
// input object or archive files.
class HIPUndefinedFatBinSymbols {
public:
  HIPUndefinedFatBinSymbols(const Compilation &C)
      : C(C), DiagID(C.getDriver().getDiags().getCustomDiagID(
                  DiagnosticsEngine::Error,
                  "Error collecting HIP undefined fatbin symbols: %0")),
        Quiet(C.getArgs().hasArg(options::OPT__HASH_HASH_HASH)),
        Verbose(C.getArgs().hasArg(options::OPT_v)) {
    populateSymbols();
    if (Verbose) {
      for (const auto &Name : FatBinSymbols)
        llvm::errs() << "Found undefined HIP fatbin symbol: " << Name << "\n";
      for (const auto &Name : GPUBinHandleSymbols)
        llvm::errs() << "Found undefined HIP gpubin handle symbol: " << Name
                     << "\n";
    }
  }

  const std::set<std::string> &getFatBinSymbols() const {
    return FatBinSymbols;
  }

  const std::set<std::string> &getGPUBinHandleSymbols() const {
    return GPUBinHandleSymbols;
  }

private:
  const Compilation &C;
  unsigned DiagID;
  bool Quiet;
  bool Verbose;
  std::set<std::string> FatBinSymbols;
  std::set<std::string> GPUBinHandleSymbols;
  std::set<std::string> DefinedFatBinSymbols;
  std::set<std::string> DefinedGPUBinHandleSymbols;
  const std::string FatBinPrefix = "__hip_fatbin";
  const std::string GPUBinHandlePrefix = "__hip_gpubin_handle";

  void populateSymbols() {
    std::deque<const Action *> WorkList;
    std::set<const Action *> Visited;

    for (const auto &Action : C.getActions())
      WorkList.push_back(Action);

    while (!WorkList.empty()) {
      const Action *CurrentAction = WorkList.front();
      WorkList.pop_front();

      if (!CurrentAction || !Visited.insert(CurrentAction).second)
        continue;

      if (const auto *IA = dyn_cast<InputAction>(CurrentAction)) {
        std::string ID = IA->getId().str();
        if (!ID.empty()) {
          ID = llvm::utohexstr(llvm::MD5Hash(ID), /*LowerCase=*/true);
          FatBinSymbols.insert((FatBinPrefix + Twine('_') + ID).str());
          GPUBinHandleSymbols.insert(
              (GPUBinHandlePrefix + Twine('_') + ID).str());
          continue;
        }
        if (IA->getInputArg().getNumValues() == 0)
          continue;
        const char *Filename = IA->getInputArg().getValue();
        if (!Filename)
          continue;
        auto BufferOrErr = llvm::MemoryBuffer::getFile(Filename);
        // Input action could be options to linker, therefore, ignore it
        // if cannot read it. If it turns out to be a file that cannot be read,
        // the error will be caught by the linker.
        if (!BufferOrErr)
          continue;

        processInput(BufferOrErr.get()->getMemBufferRef());
      } else
        WorkList.insert(WorkList.end(), CurrentAction->getInputs().begin(),
                        CurrentAction->getInputs().end());
    }
  }

  void processInput(const llvm::MemoryBufferRef &Buffer) {
    // Try processing as object file first.
    auto ObjFileOrErr = llvm::object::ObjectFile::createObjectFile(Buffer);
    if (ObjFileOrErr) {
      processSymbols(**ObjFileOrErr);
      return;
    }

    // Then try processing as archive files.
    llvm::consumeError(ObjFileOrErr.takeError());
    auto ArchiveOrErr = llvm::object::Archive::create(Buffer);
    if (ArchiveOrErr) {
      llvm::Error Err = llvm::Error::success();
      llvm::object::Archive &Archive = *ArchiveOrErr.get();
      for (auto &Child : Archive.children(Err)) {
        auto ChildBufOrErr = Child.getMemoryBufferRef();
        if (ChildBufOrErr)
          processInput(*ChildBufOrErr);
        else
          errorHandler(ChildBufOrErr.takeError());
      }

      if (Err)
        errorHandler(std::move(Err));
      return;
    }

    // Ignore other files.
    llvm::consumeError(ArchiveOrErr.takeError());
  }

  void processSymbols(const llvm::object::ObjectFile &Obj) {
    for (const auto &Symbol : Obj.symbols()) {
      auto FlagOrErr = Symbol.getFlags();
      if (!FlagOrErr) {
        errorHandler(FlagOrErr.takeError());
        continue;
      }

      auto NameOrErr = Symbol.getName();
      if (!NameOrErr) {
        errorHandler(NameOrErr.takeError());
        continue;
      }
      llvm::StringRef Name = *NameOrErr;

      bool isUndefined =
          FlagOrErr.get() & llvm::object::SymbolRef::SF_Undefined;
      bool isFatBinSymbol = Name.starts_with(FatBinPrefix);
      bool isGPUBinHandleSymbol = Name.starts_with(GPUBinHandlePrefix);

      // Handling for defined symbols
      if (!isUndefined) {
        if (isFatBinSymbol) {
          DefinedFatBinSymbols.insert(Name.str());
          FatBinSymbols.erase(Name.str());
        } else if (isGPUBinHandleSymbol) {
          DefinedGPUBinHandleSymbols.insert(Name.str());
          GPUBinHandleSymbols.erase(Name.str());
        }
        continue;
      }

      // Add undefined symbols if they are not in the defined sets
      if (isFatBinSymbol &&
          DefinedFatBinSymbols.find(Name.str()) == DefinedFatBinSymbols.end())
        FatBinSymbols.insert(Name.str());
      else if (isGPUBinHandleSymbol &&
               DefinedGPUBinHandleSymbols.find(Name.str()) ==
                   DefinedGPUBinHandleSymbols.end())
        GPUBinHandleSymbols.insert(Name.str());
    }
  }

  void errorHandler(llvm::Error Err) {
    if (Quiet)
      return;
    C.getDriver().Diag(DiagID) << llvm::toString(std::move(Err));
  }
};

// Construct a clang-offload-bundler command to bundle code objects for
// different devices into a HIP fat binary.
void HIP::constructHIPFatbinCommand(Compilation &C, const JobAction &JA,
                                    llvm::StringRef OutputFileName,
                                    const InputInfoList &Inputs,
                                    const llvm::opt::ArgList &Args,
                                    const Tool &T) {
  // Construct clang-offload-bundler command to bundle object files for
  // for different GPU archs.
  ArgStringList BundlerArgs;
  BundlerArgs.push_back(Args.MakeArgString("-type=o"));
  BundlerArgs.push_back(
      Args.MakeArgString("-bundle-align=" + Twine(HIPCodeObjectAlign)));

  // ToDo: Remove the dummy host binary entry which is required by
  // clang-offload-bundler.
  std::string BundlerTargetArg = "-targets=host-x86_64-unknown-linux";
  // AMDGCN:
  // For code object version 2 and 3, the offload kind in bundle ID is 'hip'
  // for backward compatibility. For code object version 4 and greater, the
  // offload kind in bundle ID is 'hipv4'.
  std::string OffloadKind = "hip";
  auto &TT = T.getToolChain().getTriple();
  if (TT.isAMDGCN() && getAMDGPUCodeObjectVersion(C.getDriver(), Args) >= 4)
    OffloadKind = OffloadKind + "v4";
  for (const auto &II : Inputs) {
    const auto *A = II.getAction();
    auto ArchStr = llvm::StringRef(A->getOffloadingArch());
    BundlerTargetArg +=
        "," + OffloadKind + "-" + normalizeForBundler(TT, !ArchStr.empty());
    if (!ArchStr.empty())
      BundlerTargetArg += "-" + ArchStr.str();
  }
  BundlerArgs.push_back(Args.MakeArgString(BundlerTargetArg));

  // Use a NULL file as input for the dummy host binary entry
  std::string BundlerInputArg = "-input=" NULL_FILE;
  BundlerArgs.push_back(Args.MakeArgString(BundlerInputArg));
  for (const auto &II : Inputs) {
    BundlerInputArg = std::string("-input=") + II.getFilename();
    BundlerArgs.push_back(Args.MakeArgString(BundlerInputArg));
  }

  std::string Output = std::string(OutputFileName);
  auto *BundlerOutputArg =
      Args.MakeArgString(std::string("-output=").append(Output));
  BundlerArgs.push_back(BundlerOutputArg);

  addOffloadCompressArgs(Args, BundlerArgs);

  const char *Bundler = Args.MakeArgString(
      T.getToolChain().GetProgramPath("clang-offload-bundler"));
  C.addCommand(std::make_unique<Command>(
      JA, T, ResponseFileSupport::None(), Bundler, BundlerArgs, Inputs,
      InputInfo(&JA, Args.MakeArgString(Output))));
}

/// Add Generated HIP Object File which has device images embedded into the
/// host to the argument list for linking. Using MC directives, embed the
/// device code and also define symbols required by the code generation so that
/// the image can be retrieved at runtime.
void HIP::constructGenerateObjFileFromHIPFatBinary(
    Compilation &C, const InputInfo &Output, const InputInfoList &Inputs,
    const ArgList &Args, const JobAction &JA, const Tool &T) {
  const ToolChain &TC = T.getToolChain();
  std::string Name = std::string(llvm::sys::path::stem(Output.getFilename()));

  // Create Temp Object File Generator,
  // Offload Bundled file and Bundled Object file.
  // Keep them if save-temps is enabled.
  const char *McinFile;
  const char *BundleFile;
  if (C.getDriver().isSaveTempsEnabled()) {
    McinFile = C.getArgs().MakeArgString(Name + ".mcin");
    BundleFile = C.getArgs().MakeArgString(Name + ".hipfb");
  } else {
    auto TmpNameMcin = C.getDriver().GetTemporaryPath(Name, "mcin");
    McinFile = C.addTempFile(C.getArgs().MakeArgString(TmpNameMcin));
    auto TmpNameFb = C.getDriver().GetTemporaryPath(Name, "hipfb");
    BundleFile = C.addTempFile(C.getArgs().MakeArgString(TmpNameFb));
  }
  HIP::constructHIPFatbinCommand(C, JA, BundleFile, Inputs, Args, T);

  // Create a buffer to write the contents of the temp obj generator.
  std::string ObjBuffer;
  llvm::raw_string_ostream ObjStream(ObjBuffer);

  auto HostTriple =
      C.getSingleOffloadToolChain<Action::OFK_Host>()->getTriple();

  HIPUndefinedFatBinSymbols Symbols(C);

  std::string PrimaryHipFatbinSymbol;
  std::string PrimaryGpuBinHandleSymbol;
  bool FoundPrimaryHipFatbinSymbol = false;
  bool FoundPrimaryGpuBinHandleSymbol = false;

  std::vector<std::string> AliasHipFatbinSymbols;
  std::vector<std::string> AliasGpuBinHandleSymbols;

  // Iterate through symbols to find the primary ones and collect others for
  // aliasing
  for (const auto &Symbol : Symbols.getFatBinSymbols()) {
    if (!FoundPrimaryHipFatbinSymbol) {
      PrimaryHipFatbinSymbol = Symbol;
      FoundPrimaryHipFatbinSymbol = true;
    } else
      AliasHipFatbinSymbols.push_back(Symbol);
  }

  for (const auto &Symbol : Symbols.getGPUBinHandleSymbols()) {
    if (!FoundPrimaryGpuBinHandleSymbol) {
      PrimaryGpuBinHandleSymbol = Symbol;
      FoundPrimaryGpuBinHandleSymbol = true;
    } else
      AliasGpuBinHandleSymbols.push_back(Symbol);
  }

  // Add MC directives to embed target binaries. We ensure that each
  // section and image is 16-byte aligned. This is not mandatory, but
  // increases the likelihood of data to be aligned with a cache block
  // in several main host machines.
  ObjStream << "#       HIP Object Generator\n";
  ObjStream << "# *** Automatically generated by Clang ***\n";
  if (FoundPrimaryGpuBinHandleSymbol) {
    // Define the first gpubin handle symbol
    if (HostTriple.isWindowsMSVCEnvironment())
      ObjStream << "  .section .hip_gpubin_handle,\"dw\"\n";
    else {
      ObjStream << "  .protected " << PrimaryGpuBinHandleSymbol << "\n";
      ObjStream << "  .type " << PrimaryGpuBinHandleSymbol << ",@object\n";
      ObjStream << "  .section .hip_gpubin_handle,\"aw\"\n";
    }
    ObjStream << "  .globl " << PrimaryGpuBinHandleSymbol << "\n";
    ObjStream << "  .p2align 3\n"; // Align 8
    ObjStream << PrimaryGpuBinHandleSymbol << ":\n";
    ObjStream << "  .zero 8\n"; // Size 8

    // Generate alias directives for other gpubin handle symbols
    for (const auto &AliasSymbol : AliasGpuBinHandleSymbols) {
      ObjStream << "  .globl " << AliasSymbol << "\n";
      ObjStream << "  .set " << AliasSymbol << "," << PrimaryGpuBinHandleSymbol
                << "\n";
    }
  }
  if (FoundPrimaryHipFatbinSymbol) {
    // Define the first fatbin symbol
    if (HostTriple.isWindowsMSVCEnvironment())
      ObjStream << "  .section .hip_fatbin,\"dw\"\n";
    else {
      ObjStream << "  .protected " << PrimaryHipFatbinSymbol << "\n";
      ObjStream << "  .type " << PrimaryHipFatbinSymbol << ",@object\n";
      ObjStream << "  .section .hip_fatbin,\"a\",@progbits\n";
    }
    ObjStream << "  .globl " << PrimaryHipFatbinSymbol << "\n";
    ObjStream << "  .p2align " << llvm::Log2(llvm::Align(HIPCodeObjectAlign))
              << "\n";
    // Generate alias directives for other fatbin symbols
    for (const auto &AliasSymbol : AliasHipFatbinSymbols) {
      ObjStream << "  .globl " << AliasSymbol << "\n";
      ObjStream << "  .set " << AliasSymbol << "," << PrimaryHipFatbinSymbol
                << "\n";
    }
    ObjStream << PrimaryHipFatbinSymbol << ":\n";
    ObjStream << "  .incbin ";
    llvm::sys::printArg(ObjStream, BundleFile, /*Quote=*/true);
    ObjStream << "\n";
  }
  if (HostTriple.isOSLinux() && HostTriple.isOSBinFormatELF())
    ObjStream << "  .section .note.GNU-stack, \"\", @progbits\n";
  ObjStream.flush();

  // Dump the contents of the temp object file gen if the user requested that.
  // We support this option to enable testing of behavior with -###.
  if (C.getArgs().hasArg(options::OPT_fhip_dump_offload_linker_script))
    llvm::errs() << ObjBuffer;

  // Open script file and write the contents.
  std::error_code EC;
  llvm::raw_fd_ostream Objf(McinFile, EC, llvm::sys::fs::OF_None);

  if (EC) {
    C.getDriver().Diag(clang::diag::err_unable_to_make_temp) << EC.message();
    return;
  }

  Objf << ObjBuffer;

  ArgStringList McArgs{"-triple", Args.MakeArgString(HostTriple.normalize()),
                       "-o",      Output.getFilename(),
                       McinFile,  "--filetype=obj"};
  const char *Mc = Args.MakeArgString(TC.GetProgramPath("llvm-mc"));
  C.addCommand(std::make_unique<Command>(JA, T, ResponseFileSupport::None(), Mc,
                                         McArgs, Inputs, Output));
}
