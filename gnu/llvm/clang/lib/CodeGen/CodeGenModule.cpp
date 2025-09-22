//===--- CodeGenModule.cpp - Emit LLVM Code from ASTs for a Module --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This coordinates the per-module state used while generating code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenModule.h"
#include "ABIInfo.h"
#include "CGBlocks.h"
#include "CGCUDARuntime.h"
#include "CGCXXABI.h"
#include "CGCall.h"
#include "CGDebugInfo.h"
#include "CGHLSLRuntime.h"
#include "CGObjCRuntime.h"
#include "CGOpenCLRuntime.h"
#include "CGOpenMPRuntime.h"
#include "CGOpenMPRuntimeGPU.h"
#include "CodeGenFunction.h"
#include "CodeGenPGO.h"
#include "ConstantEmitter.h"
#include "CoverageMappingGen.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Version.h"
#include "clang/CodeGen/BackendUtil.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/xxhash.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include <optional>

using namespace clang;
using namespace CodeGen;

static llvm::cl::opt<bool> LimitedCoverage(
    "limited-coverage-experimental", llvm::cl::Hidden,
    llvm::cl::desc("Emit limited coverage mapping information (experimental)"));

static const char AnnotationSection[] = "llvm.metadata";

static CGCXXABI *createCXXABI(CodeGenModule &CGM) {
  switch (CGM.getContext().getCXXABIKind()) {
  case TargetCXXABI::AppleARM64:
  case TargetCXXABI::Fuchsia:
  case TargetCXXABI::GenericAArch64:
  case TargetCXXABI::GenericARM:
  case TargetCXXABI::iOS:
  case TargetCXXABI::WatchOS:
  case TargetCXXABI::GenericMIPS:
  case TargetCXXABI::GenericItanium:
  case TargetCXXABI::WebAssembly:
  case TargetCXXABI::XL:
    return CreateItaniumCXXABI(CGM);
  case TargetCXXABI::Microsoft:
    return CreateMicrosoftCXXABI(CGM);
  }

  llvm_unreachable("invalid C++ ABI kind");
}

static std::unique_ptr<TargetCodeGenInfo>
createTargetCodeGenInfo(CodeGenModule &CGM) {
  const TargetInfo &Target = CGM.getTarget();
  const llvm::Triple &Triple = Target.getTriple();
  const CodeGenOptions &CodeGenOpts = CGM.getCodeGenOpts();

  switch (Triple.getArch()) {
  default:
    return createDefaultTargetCodeGenInfo(CGM);

  case llvm::Triple::le32:
    return createPNaClTargetCodeGenInfo(CGM);
  case llvm::Triple::m68k:
    return createM68kTargetCodeGenInfo(CGM);
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
    if (Triple.getOS() == llvm::Triple::NaCl)
      return createPNaClTargetCodeGenInfo(CGM);
    return createMIPSTargetCodeGenInfo(CGM, /*IsOS32=*/true);

  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    return createMIPSTargetCodeGenInfo(CGM, /*IsOS32=*/false);

  case llvm::Triple::avr: {
    // For passing parameters, R8~R25 are used on avr, and R18~R25 are used
    // on avrtiny. For passing return value, R18~R25 are used on avr, and
    // R22~R25 are used on avrtiny.
    unsigned NPR = Target.getABI() == "avrtiny" ? 6 : 18;
    unsigned NRR = Target.getABI() == "avrtiny" ? 4 : 8;
    return createAVRTargetCodeGenInfo(CGM, NPR, NRR);
  }

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be: {
    AArch64ABIKind Kind = AArch64ABIKind::AAPCS;
    if (Target.getABI() == "darwinpcs")
      Kind = AArch64ABIKind::DarwinPCS;
    else if (Triple.isOSWindows())
      return createWindowsAArch64TargetCodeGenInfo(CGM, AArch64ABIKind::Win64);
    else if (Target.getABI() == "aapcs-soft")
      Kind = AArch64ABIKind::AAPCSSoft;
    else if (Target.getABI() == "pauthtest")
      Kind = AArch64ABIKind::PAuthTest;

    return createAArch64TargetCodeGenInfo(CGM, Kind);
  }

  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64: {
    WebAssemblyABIKind Kind = WebAssemblyABIKind::MVP;
    if (Target.getABI() == "experimental-mv")
      Kind = WebAssemblyABIKind::ExperimentalMV;
    return createWebAssemblyTargetCodeGenInfo(CGM, Kind);
  }

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb: {
    if (Triple.getOS() == llvm::Triple::Win32)
      return createWindowsARMTargetCodeGenInfo(CGM, ARMABIKind::AAPCS_VFP);

    ARMABIKind Kind = ARMABIKind::AAPCS;
    StringRef ABIStr = Target.getABI();
    if (ABIStr == "apcs-gnu")
      Kind = ARMABIKind::APCS;
    else if (ABIStr == "aapcs16")
      Kind = ARMABIKind::AAPCS16_VFP;
    else if (CodeGenOpts.FloatABI == "hard" ||
             (CodeGenOpts.FloatABI != "soft" && Triple.isHardFloatABI()))
      Kind = ARMABIKind::AAPCS_VFP;

    return createARMTargetCodeGenInfo(CGM, Kind);
  }

  case llvm::Triple::ppc: {
    if (Triple.isOSAIX())
      return createAIXTargetCodeGenInfo(CGM, /*Is64Bit=*/false);

    bool IsSoftFloat =
        CodeGenOpts.FloatABI == "soft" || Target.hasFeature("spe");
    return createPPC32TargetCodeGenInfo(CGM, IsSoftFloat);
  }
  case llvm::Triple::ppcle: {
    bool IsSoftFloat = CodeGenOpts.FloatABI == "soft";
    return createPPC32TargetCodeGenInfo(CGM, IsSoftFloat);
  }
  case llvm::Triple::ppc64:
    if (Triple.isOSAIX())
      return createAIXTargetCodeGenInfo(CGM, /*Is64Bit=*/true);

    if (Triple.isOSBinFormatELF()) {
      PPC64_SVR4_ABIKind Kind = PPC64_SVR4_ABIKind::ELFv1;
      if (Target.getABI() == "elfv2")
        Kind = PPC64_SVR4_ABIKind::ELFv2;
      bool IsSoftFloat = CodeGenOpts.FloatABI == "soft";

      return createPPC64_SVR4_TargetCodeGenInfo(CGM, Kind, IsSoftFloat);
    }
    return createPPC64TargetCodeGenInfo(CGM);
  case llvm::Triple::ppc64le: {
    assert(Triple.isOSBinFormatELF() && "PPC64 LE non-ELF not supported!");
    PPC64_SVR4_ABIKind Kind = PPC64_SVR4_ABIKind::ELFv2;
    if (Target.getABI() == "elfv1")
      Kind = PPC64_SVR4_ABIKind::ELFv1;
    bool IsSoftFloat = CodeGenOpts.FloatABI == "soft";

    return createPPC64_SVR4_TargetCodeGenInfo(CGM, Kind, IsSoftFloat);
  }

  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    return createNVPTXTargetCodeGenInfo(CGM);

  case llvm::Triple::msp430:
    return createMSP430TargetCodeGenInfo(CGM);

  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64: {
    StringRef ABIStr = Target.getABI();
    unsigned XLen = Target.getPointerWidth(LangAS::Default);
    unsigned ABIFLen = 0;
    if (ABIStr.ends_with("f"))
      ABIFLen = 32;
    else if (ABIStr.ends_with("d"))
      ABIFLen = 64;
    bool EABI = ABIStr.ends_with("e");
    return createRISCVTargetCodeGenInfo(CGM, XLen, ABIFLen, EABI);
  }

  case llvm::Triple::systemz: {
    bool SoftFloat = CodeGenOpts.FloatABI == "soft";
    bool HasVector = !SoftFloat && Target.getABI() == "vector";
    return createSystemZTargetCodeGenInfo(CGM, HasVector, SoftFloat);
  }

  case llvm::Triple::tce:
  case llvm::Triple::tcele:
    return createTCETargetCodeGenInfo(CGM);

  case llvm::Triple::x86: {
    bool IsDarwinVectorABI = Triple.isOSDarwin();
    bool IsWin32FloatStructABI = Triple.isOSWindows() && !Triple.isOSCygMing();

    if (Triple.getOS() == llvm::Triple::Win32) {
      return createWinX86_32TargetCodeGenInfo(
          CGM, IsDarwinVectorABI, IsWin32FloatStructABI,
          CodeGenOpts.NumRegisterParameters);
    }
    return createX86_32TargetCodeGenInfo(
        CGM, IsDarwinVectorABI, IsWin32FloatStructABI,
        CodeGenOpts.NumRegisterParameters, CodeGenOpts.FloatABI == "soft");
  }

  case llvm::Triple::x86_64: {
    StringRef ABI = Target.getABI();
    X86AVXABILevel AVXLevel = (ABI == "avx512" ? X86AVXABILevel::AVX512
                               : ABI == "avx"  ? X86AVXABILevel::AVX
                                               : X86AVXABILevel::None);

    switch (Triple.getOS()) {
    case llvm::Triple::Win32:
      return createWinX86_64TargetCodeGenInfo(CGM, AVXLevel);
    default:
      return createX86_64TargetCodeGenInfo(CGM, AVXLevel);
    }
  }
  case llvm::Triple::hexagon:
    return createHexagonTargetCodeGenInfo(CGM);
  case llvm::Triple::lanai:
    return createLanaiTargetCodeGenInfo(CGM);
  case llvm::Triple::r600:
    return createAMDGPUTargetCodeGenInfo(CGM);
  case llvm::Triple::amdgcn:
    return createAMDGPUTargetCodeGenInfo(CGM);
  case llvm::Triple::sparc:
    return createSparcV8TargetCodeGenInfo(CGM);
  case llvm::Triple::sparcv9:
    return createSparcV9TargetCodeGenInfo(CGM);
  case llvm::Triple::xcore:
    return createXCoreTargetCodeGenInfo(CGM);
  case llvm::Triple::arc:
    return createARCTargetCodeGenInfo(CGM);
  case llvm::Triple::spir:
  case llvm::Triple::spir64:
    return createCommonSPIRTargetCodeGenInfo(CGM);
  case llvm::Triple::spirv32:
  case llvm::Triple::spirv64:
    return createSPIRVTargetCodeGenInfo(CGM);
  case llvm::Triple::ve:
    return createVETargetCodeGenInfo(CGM);
  case llvm::Triple::csky: {
    bool IsSoftFloat = !Target.hasFeature("hard-float-abi");
    bool hasFP64 =
        Target.hasFeature("fpuv2_df") || Target.hasFeature("fpuv3_df");
    return createCSKYTargetCodeGenInfo(CGM, IsSoftFloat ? 0
                                            : hasFP64   ? 64
                                                        : 32);
  }
  case llvm::Triple::bpfeb:
  case llvm::Triple::bpfel:
    return createBPFTargetCodeGenInfo(CGM);
  case llvm::Triple::loongarch32:
  case llvm::Triple::loongarch64: {
    StringRef ABIStr = Target.getABI();
    unsigned ABIFRLen = 0;
    if (ABIStr.ends_with("f"))
      ABIFRLen = 32;
    else if (ABIStr.ends_with("d"))
      ABIFRLen = 64;
    return createLoongArchTargetCodeGenInfo(
        CGM, Target.getPointerWidth(LangAS::Default), ABIFRLen);
  }
  }
}

const TargetCodeGenInfo &CodeGenModule::getTargetCodeGenInfo() {
  if (!TheTargetCodeGenInfo)
    TheTargetCodeGenInfo = createTargetCodeGenInfo(*this);
  return *TheTargetCodeGenInfo;
}

CodeGenModule::CodeGenModule(ASTContext &C,
                             IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                             const HeaderSearchOptions &HSO,
                             const PreprocessorOptions &PPO,
                             const CodeGenOptions &CGO, llvm::Module &M,
                             DiagnosticsEngine &diags,
                             CoverageSourceInfo *CoverageInfo)
    : Context(C), LangOpts(C.getLangOpts()), FS(FS), HeaderSearchOpts(HSO),
      PreprocessorOpts(PPO), CodeGenOpts(CGO), TheModule(M), Diags(diags),
      Target(C.getTargetInfo()), ABI(createCXXABI(*this)),
      VMContext(M.getContext()), VTables(*this),
      SanitizerMD(new SanitizerMetadata(*this)) {

  // Initialize the type cache.
  Types.reset(new CodeGenTypes(*this));
  llvm::LLVMContext &LLVMContext = M.getContext();
  VoidTy = llvm::Type::getVoidTy(LLVMContext);
  Int8Ty = llvm::Type::getInt8Ty(LLVMContext);
  Int16Ty = llvm::Type::getInt16Ty(LLVMContext);
  Int32Ty = llvm::Type::getInt32Ty(LLVMContext);
  Int64Ty = llvm::Type::getInt64Ty(LLVMContext);
  HalfTy = llvm::Type::getHalfTy(LLVMContext);
  BFloatTy = llvm::Type::getBFloatTy(LLVMContext);
  FloatTy = llvm::Type::getFloatTy(LLVMContext);
  DoubleTy = llvm::Type::getDoubleTy(LLVMContext);
  PointerWidthInBits = C.getTargetInfo().getPointerWidth(LangAS::Default);
  PointerAlignInBytes =
      C.toCharUnitsFromBits(C.getTargetInfo().getPointerAlign(LangAS::Default))
          .getQuantity();
  SizeSizeInBytes =
    C.toCharUnitsFromBits(C.getTargetInfo().getMaxPointerWidth()).getQuantity();
  IntAlignInBytes =
    C.toCharUnitsFromBits(C.getTargetInfo().getIntAlign()).getQuantity();
  CharTy =
    llvm::IntegerType::get(LLVMContext, C.getTargetInfo().getCharWidth());
  IntTy = llvm::IntegerType::get(LLVMContext, C.getTargetInfo().getIntWidth());
  IntPtrTy = llvm::IntegerType::get(LLVMContext,
    C.getTargetInfo().getMaxPointerWidth());
  Int8PtrTy = llvm::PointerType::get(LLVMContext,
                                     C.getTargetAddressSpace(LangAS::Default));
  const llvm::DataLayout &DL = M.getDataLayout();
  AllocaInt8PtrTy =
      llvm::PointerType::get(LLVMContext, DL.getAllocaAddrSpace());
  GlobalsInt8PtrTy =
      llvm::PointerType::get(LLVMContext, DL.getDefaultGlobalsAddressSpace());
  ConstGlobalsPtrTy = llvm::PointerType::get(
      LLVMContext, C.getTargetAddressSpace(GetGlobalConstantAddressSpace()));
  ASTAllocaAddressSpace = getTargetCodeGenInfo().getASTAllocaAddressSpace();

  // Build C++20 Module initializers.
  // TODO: Add Microsoft here once we know the mangling required for the
  // initializers.
  CXX20ModuleInits =
      LangOpts.CPlusPlusModules && getCXXABI().getMangleContext().getKind() ==
                                       ItaniumMangleContext::MK_Itanium;

  RuntimeCC = getTargetCodeGenInfo().getABIInfo().getRuntimeCC();

  if (LangOpts.ObjC)
    createObjCRuntime();
  if (LangOpts.OpenCL)
    createOpenCLRuntime();
  if (LangOpts.OpenMP)
    createOpenMPRuntime();
  if (LangOpts.CUDA)
    createCUDARuntime();
  if (LangOpts.HLSL)
    createHLSLRuntime();

  // Enable TBAA unless it's suppressed. ThreadSanitizer needs TBAA even at O0.
  if (LangOpts.Sanitize.has(SanitizerKind::Thread) ||
      (!CodeGenOpts.RelaxedAliasing && CodeGenOpts.OptimizationLevel > 0))
    TBAA.reset(new CodeGenTBAA(Context, getTypes(), TheModule, CodeGenOpts,
                               getLangOpts()));

  // If debug info or coverage generation is enabled, create the CGDebugInfo
  // object.
  if (CodeGenOpts.getDebugInfo() != llvm::codegenoptions::NoDebugInfo ||
      CodeGenOpts.CoverageNotesFile.size() ||
      CodeGenOpts.CoverageDataFile.size())
    DebugInfo.reset(new CGDebugInfo(*this));

  Block.GlobalUniqueCount = 0;

  if (C.getLangOpts().ObjC)
    ObjCData.reset(new ObjCEntrypoints());

  if (CodeGenOpts.hasProfileClangUse()) {
    auto ReaderOrErr = llvm::IndexedInstrProfReader::create(
        CodeGenOpts.ProfileInstrumentUsePath, *FS,
        CodeGenOpts.ProfileRemappingFile);
    // We're checking for profile read errors in CompilerInvocation, so if
    // there was an error it should've already been caught. If it hasn't been
    // somehow, trip an assertion.
    assert(ReaderOrErr);
    PGOReader = std::move(ReaderOrErr.get());
  }

  // If coverage mapping generation is enabled, create the
  // CoverageMappingModuleGen object.
  if (CodeGenOpts.CoverageMapping)
    CoverageMapping.reset(new CoverageMappingModuleGen(*this, *CoverageInfo));

  // Generate the module name hash here if needed.
  if (CodeGenOpts.UniqueInternalLinkageNames &&
      !getModule().getSourceFileName().empty()) {
    std::string Path = getModule().getSourceFileName();
    // Check if a path substitution is needed from the MacroPrefixMap.
    for (const auto &Entry : LangOpts.MacroPrefixMap)
      if (Path.rfind(Entry.first, 0) != std::string::npos) {
        Path = Entry.second + Path.substr(Entry.first.size());
        break;
      }
    ModuleNameHash = llvm::getUniqueInternalLinkagePostfix(Path);
  }

  // Record mregparm value now so it is visible through all of codegen.
  if (Context.getTargetInfo().getTriple().getArch() == llvm::Triple::x86)
    getModule().addModuleFlag(llvm::Module::Error, "NumRegisterParameters",
                              CodeGenOpts.NumRegisterParameters);
}

CodeGenModule::~CodeGenModule() {}

void CodeGenModule::createObjCRuntime() {
  // This is just isGNUFamily(), but we want to force implementors of
  // new ABIs to decide how best to do this.
  switch (LangOpts.ObjCRuntime.getKind()) {
  case ObjCRuntime::GNUstep:
  case ObjCRuntime::GCC:
  case ObjCRuntime::ObjFW:
    ObjCRuntime.reset(CreateGNUObjCRuntime(*this));
    return;

  case ObjCRuntime::FragileMacOSX:
  case ObjCRuntime::MacOSX:
  case ObjCRuntime::iOS:
  case ObjCRuntime::WatchOS:
    ObjCRuntime.reset(CreateMacObjCRuntime(*this));
    return;
  }
  llvm_unreachable("bad runtime kind");
}

void CodeGenModule::createOpenCLRuntime() {
  OpenCLRuntime.reset(new CGOpenCLRuntime(*this));
}

void CodeGenModule::createOpenMPRuntime() {
  // Select a specialized code generation class based on the target, if any.
  // If it does not exist use the default implementation.
  switch (getTriple().getArch()) {
  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
  case llvm::Triple::amdgcn:
    assert(getLangOpts().OpenMPIsTargetDevice &&
           "OpenMP AMDGPU/NVPTX is only prepared to deal with device code.");
    OpenMPRuntime.reset(new CGOpenMPRuntimeGPU(*this));
    break;
  default:
    if (LangOpts.OpenMPSimd)
      OpenMPRuntime.reset(new CGOpenMPSIMDRuntime(*this));
    else
      OpenMPRuntime.reset(new CGOpenMPRuntime(*this));
    break;
  }
}

void CodeGenModule::createCUDARuntime() {
  CUDARuntime.reset(CreateNVCUDARuntime(*this));
}

void CodeGenModule::createHLSLRuntime() {
  HLSLRuntime.reset(new CGHLSLRuntime(*this));
}

void CodeGenModule::addReplacement(StringRef Name, llvm::Constant *C) {
  Replacements[Name] = C;
}

void CodeGenModule::applyReplacements() {
  for (auto &I : Replacements) {
    StringRef MangledName = I.first;
    llvm::Constant *Replacement = I.second;
    llvm::GlobalValue *Entry = GetGlobalValue(MangledName);
    if (!Entry)
      continue;
    auto *OldF = cast<llvm::Function>(Entry);
    auto *NewF = dyn_cast<llvm::Function>(Replacement);
    if (!NewF) {
      if (auto *Alias = dyn_cast<llvm::GlobalAlias>(Replacement)) {
        NewF = dyn_cast<llvm::Function>(Alias->getAliasee());
      } else {
        auto *CE = cast<llvm::ConstantExpr>(Replacement);
        assert(CE->getOpcode() == llvm::Instruction::BitCast ||
               CE->getOpcode() == llvm::Instruction::GetElementPtr);
        NewF = dyn_cast<llvm::Function>(CE->getOperand(0));
      }
    }

    // Replace old with new, but keep the old order.
    OldF->replaceAllUsesWith(Replacement);
    if (NewF) {
      NewF->removeFromParent();
      OldF->getParent()->getFunctionList().insertAfter(OldF->getIterator(),
                                                       NewF);
    }
    OldF->eraseFromParent();
  }
}

void CodeGenModule::addGlobalValReplacement(llvm::GlobalValue *GV, llvm::Constant *C) {
  GlobalValReplacements.push_back(std::make_pair(GV, C));
}

void CodeGenModule::applyGlobalValReplacements() {
  for (auto &I : GlobalValReplacements) {
    llvm::GlobalValue *GV = I.first;
    llvm::Constant *C = I.second;

    GV->replaceAllUsesWith(C);
    GV->eraseFromParent();
  }
}

// This is only used in aliases that we created and we know they have a
// linear structure.
static const llvm::GlobalValue *getAliasedGlobal(const llvm::GlobalValue *GV) {
  const llvm::Constant *C;
  if (auto *GA = dyn_cast<llvm::GlobalAlias>(GV))
    C = GA->getAliasee();
  else if (auto *GI = dyn_cast<llvm::GlobalIFunc>(GV))
    C = GI->getResolver();
  else
    return GV;

  const auto *AliaseeGV = dyn_cast<llvm::GlobalValue>(C->stripPointerCasts());
  if (!AliaseeGV)
    return nullptr;

  const llvm::GlobalValue *FinalGV = AliaseeGV->getAliaseeObject();
  if (FinalGV == GV)
    return nullptr;

  return FinalGV;
}

static bool checkAliasedGlobal(
    const ASTContext &Context, DiagnosticsEngine &Diags, SourceLocation Location,
    bool IsIFunc, const llvm::GlobalValue *Alias, const llvm::GlobalValue *&GV,
    const llvm::MapVector<GlobalDecl, StringRef> &MangledDeclNames,
    SourceRange AliasRange) {
  GV = getAliasedGlobal(Alias);
  if (!GV) {
    Diags.Report(Location, diag::err_cyclic_alias) << IsIFunc;
    return false;
  }

  if (GV->hasCommonLinkage()) {
    const llvm::Triple &Triple = Context.getTargetInfo().getTriple();
    if (Triple.getObjectFormat() == llvm::Triple::XCOFF) {
      Diags.Report(Location, diag::err_alias_to_common);
      return false;
    }
  }

  if (GV->isDeclaration()) {
    Diags.Report(Location, diag::err_alias_to_undefined) << IsIFunc << IsIFunc;
    Diags.Report(Location, diag::note_alias_requires_mangled_name)
        << IsIFunc << IsIFunc;
    // Provide a note if the given function is not found and exists as a
    // mangled name.
    for (const auto &[Decl, Name] : MangledDeclNames) {
      if (const auto *ND = dyn_cast<NamedDecl>(Decl.getDecl())) {
        if (ND->getName() == GV->getName()) {
          Diags.Report(Location, diag::note_alias_mangled_name_alternative)
              << Name
              << FixItHint::CreateReplacement(
                     AliasRange,
                     (Twine(IsIFunc ? "ifunc" : "alias") + "(\"" + Name + "\")")
                         .str());
        }
      }
    }
    return false;
  }

  if (IsIFunc) {
    // Check resolver function type.
    const auto *F = dyn_cast<llvm::Function>(GV);
    if (!F) {
      Diags.Report(Location, diag::err_alias_to_undefined)
          << IsIFunc << IsIFunc;
      return false;
    }

    llvm::FunctionType *FTy = F->getFunctionType();
    if (!FTy->getReturnType()->isPointerTy()) {
      Diags.Report(Location, diag::err_ifunc_resolver_return);
      return false;
    }
  }

  return true;
}

// Emit a warning if toc-data attribute is requested for global variables that
// have aliases and remove the toc-data attribute.
static void checkAliasForTocData(llvm::GlobalVariable *GVar,
                                 const CodeGenOptions &CodeGenOpts,
                                 DiagnosticsEngine &Diags,
                                 SourceLocation Location) {
  if (GVar->hasAttribute("toc-data")) {
    auto GVId = GVar->getName();
    // Is this a global variable specified by the user as local?
    if ((llvm::binary_search(CodeGenOpts.TocDataVarsUserSpecified, GVId))) {
      Diags.Report(Location, diag::warn_toc_unsupported_type)
          << GVId << "the variable has an alias";
    }
    llvm::AttributeSet CurrAttributes = GVar->getAttributes();
    llvm::AttributeSet NewAttributes =
        CurrAttributes.removeAttribute(GVar->getContext(), "toc-data");
    GVar->setAttributes(NewAttributes);
  }
}

void CodeGenModule::checkAliases() {
  // Check if the constructed aliases are well formed. It is really unfortunate
  // that we have to do this in CodeGen, but we only construct mangled names
  // and aliases during codegen.
  bool Error = false;
  DiagnosticsEngine &Diags = getDiags();
  for (const GlobalDecl &GD : Aliases) {
    const auto *D = cast<ValueDecl>(GD.getDecl());
    SourceLocation Location;
    SourceRange Range;
    bool IsIFunc = D->hasAttr<IFuncAttr>();
    if (const Attr *A = D->getDefiningAttr()) {
      Location = A->getLocation();
      Range = A->getRange();
    } else
      llvm_unreachable("Not an alias or ifunc?");

    StringRef MangledName = getMangledName(GD);
    llvm::GlobalValue *Alias = GetGlobalValue(MangledName);
    const llvm::GlobalValue *GV = nullptr;
    if (!checkAliasedGlobal(getContext(), Diags, Location, IsIFunc, Alias, GV,
                            MangledDeclNames, Range)) {
      Error = true;
      continue;
    }

    if (getContext().getTargetInfo().getTriple().isOSAIX())
      if (const llvm::GlobalVariable *GVar =
              dyn_cast<const llvm::GlobalVariable>(GV))
        checkAliasForTocData(const_cast<llvm::GlobalVariable *>(GVar),
                             getCodeGenOpts(), Diags, Location);

    llvm::Constant *Aliasee =
        IsIFunc ? cast<llvm::GlobalIFunc>(Alias)->getResolver()
                : cast<llvm::GlobalAlias>(Alias)->getAliasee();

    llvm::GlobalValue *AliaseeGV;
    if (auto CE = dyn_cast<llvm::ConstantExpr>(Aliasee))
      AliaseeGV = cast<llvm::GlobalValue>(CE->getOperand(0));
    else
      AliaseeGV = cast<llvm::GlobalValue>(Aliasee);

    if (const SectionAttr *SA = D->getAttr<SectionAttr>()) {
      StringRef AliasSection = SA->getName();
      if (AliasSection != AliaseeGV->getSection())
        Diags.Report(SA->getLocation(), diag::warn_alias_with_section)
            << AliasSection << IsIFunc << IsIFunc;
    }

    // We have to handle alias to weak aliases in here. LLVM itself disallows
    // this since the object semantics would not match the IL one. For
    // compatibility with gcc we implement it by just pointing the alias
    // to its aliasee's aliasee. We also warn, since the user is probably
    // expecting the link to be weak.
    if (auto *GA = dyn_cast<llvm::GlobalAlias>(AliaseeGV)) {
      if (GA->isInterposable()) {
        Diags.Report(Location, diag::warn_alias_to_weak_alias)
            << GV->getName() << GA->getName() << IsIFunc;
        Aliasee = llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(
            GA->getAliasee(), Alias->getType());

        if (IsIFunc)
          cast<llvm::GlobalIFunc>(Alias)->setResolver(Aliasee);
        else
          cast<llvm::GlobalAlias>(Alias)->setAliasee(Aliasee);
      }
    }
    // ifunc resolvers are usually implemented to run before sanitizer
    // initialization. Disable instrumentation to prevent the ordering issue.
    if (IsIFunc)
      cast<llvm::Function>(Aliasee)->addFnAttr(
          llvm::Attribute::DisableSanitizerInstrumentation);
  }
  if (!Error)
    return;

  for (const GlobalDecl &GD : Aliases) {
    StringRef MangledName = getMangledName(GD);
    llvm::GlobalValue *Alias = GetGlobalValue(MangledName);
    Alias->replaceAllUsesWith(llvm::UndefValue::get(Alias->getType()));
    Alias->eraseFromParent();
  }
}

void CodeGenModule::clear() {
  DeferredDeclsToEmit.clear();
  EmittedDeferredDecls.clear();
  DeferredAnnotations.clear();
  if (OpenMPRuntime)
    OpenMPRuntime->clear();
}

void InstrProfStats::reportDiagnostics(DiagnosticsEngine &Diags,
                                       StringRef MainFile) {
  if (!hasDiagnostics())
    return;
  if (VisitedInMainFile > 0 && VisitedInMainFile == MissingInMainFile) {
    if (MainFile.empty())
      MainFile = "<stdin>";
    Diags.Report(diag::warn_profile_data_unprofiled) << MainFile;
  } else {
    if (Mismatched > 0)
      Diags.Report(diag::warn_profile_data_out_of_date) << Visited << Mismatched;

    if (Missing > 0)
      Diags.Report(diag::warn_profile_data_missing) << Visited << Missing;
  }
}

static std::optional<llvm::GlobalValue::VisibilityTypes>
getLLVMVisibility(clang::LangOptions::VisibilityFromDLLStorageClassKinds K) {
  // Map to LLVM visibility.
  switch (K) {
  case clang::LangOptions::VisibilityFromDLLStorageClassKinds::Keep:
    return std::nullopt;
  case clang::LangOptions::VisibilityFromDLLStorageClassKinds::Default:
    return llvm::GlobalValue::DefaultVisibility;
  case clang::LangOptions::VisibilityFromDLLStorageClassKinds::Hidden:
    return llvm::GlobalValue::HiddenVisibility;
  case clang::LangOptions::VisibilityFromDLLStorageClassKinds::Protected:
    return llvm::GlobalValue::ProtectedVisibility;
  }
  llvm_unreachable("unknown option value!");
}

void setLLVMVisibility(llvm::GlobalValue &GV,
                       std::optional<llvm::GlobalValue::VisibilityTypes> V) {
  if (!V)
    return;

  // Reset DSO locality before setting the visibility. This removes
  // any effects that visibility options and annotations may have
  // had on the DSO locality. Setting the visibility will implicitly set
  // appropriate globals to DSO Local; however, this will be pessimistic
  // w.r.t. to the normal compiler IRGen.
  GV.setDSOLocal(false);
  GV.setVisibility(*V);
}

static void setVisibilityFromDLLStorageClass(const clang::LangOptions &LO,
                                             llvm::Module &M) {
  if (!LO.VisibilityFromDLLStorageClass)
    return;

  std::optional<llvm::GlobalValue::VisibilityTypes> DLLExportVisibility =
      getLLVMVisibility(LO.getDLLExportVisibility());

  std::optional<llvm::GlobalValue::VisibilityTypes>
      NoDLLStorageClassVisibility =
          getLLVMVisibility(LO.getNoDLLStorageClassVisibility());

  std::optional<llvm::GlobalValue::VisibilityTypes>
      ExternDeclDLLImportVisibility =
          getLLVMVisibility(LO.getExternDeclDLLImportVisibility());

  std::optional<llvm::GlobalValue::VisibilityTypes>
      ExternDeclNoDLLStorageClassVisibility =
          getLLVMVisibility(LO.getExternDeclNoDLLStorageClassVisibility());

  for (llvm::GlobalValue &GV : M.global_values()) {
    if (GV.hasAppendingLinkage() || GV.hasLocalLinkage())
      continue;

    if (GV.isDeclarationForLinker())
      setLLVMVisibility(GV, GV.getDLLStorageClass() ==
                                    llvm::GlobalValue::DLLImportStorageClass
                                ? ExternDeclDLLImportVisibility
                                : ExternDeclNoDLLStorageClassVisibility);
    else
      setLLVMVisibility(GV, GV.getDLLStorageClass() ==
                                    llvm::GlobalValue::DLLExportStorageClass
                                ? DLLExportVisibility
                                : NoDLLStorageClassVisibility);

    GV.setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
  }
}

static bool isStackProtectorOn(const LangOptions &LangOpts,
                               const llvm::Triple &Triple,
                               clang::LangOptions::StackProtectorMode Mode) {
  if (Triple.isAMDGPU() || Triple.isNVPTX())
    return false;
  return LangOpts.getStackProtector() == Mode;
}

void CodeGenModule::Release() {
  Module *Primary = getContext().getCurrentNamedModule();
  if (CXX20ModuleInits && Primary && !Primary->isHeaderLikeModule())
    EmitModuleInitializers(Primary);
  EmitDeferred();
  DeferredDecls.insert(EmittedDeferredDecls.begin(),
                       EmittedDeferredDecls.end());
  EmittedDeferredDecls.clear();
  EmitVTablesOpportunistically();
  applyGlobalValReplacements();
  applyReplacements();
  emitMultiVersionFunctions();

  if (Context.getLangOpts().IncrementalExtensions &&
      GlobalTopLevelStmtBlockInFlight.first) {
    const TopLevelStmtDecl *TLSD = GlobalTopLevelStmtBlockInFlight.second;
    GlobalTopLevelStmtBlockInFlight.first->FinishFunction(TLSD->getEndLoc());
    GlobalTopLevelStmtBlockInFlight = {nullptr, nullptr};
  }

  // Module implementations are initialized the same way as a regular TU that
  // imports one or more modules.
  if (CXX20ModuleInits && Primary && Primary->isInterfaceOrPartition())
    EmitCXXModuleInitFunc(Primary);
  else
    EmitCXXGlobalInitFunc();
  EmitCXXGlobalCleanUpFunc();
  registerGlobalDtorsWithAtExit();
  EmitCXXThreadLocalInitFunc();
  if (ObjCRuntime)
    if (llvm::Function *ObjCInitFunction = ObjCRuntime->ModuleInitFunction())
      AddGlobalCtor(ObjCInitFunction);
  if (Context.getLangOpts().CUDA && CUDARuntime) {
    if (llvm::Function *CudaCtorFunction = CUDARuntime->finalizeModule())
      AddGlobalCtor(CudaCtorFunction);
  }
  if (OpenMPRuntime) {
    OpenMPRuntime->createOffloadEntriesAndInfoMetadata();
    OpenMPRuntime->clear();
  }
  if (PGOReader) {
    getModule().setProfileSummary(
        PGOReader->getSummary(/* UseCS */ false).getMD(VMContext),
        llvm::ProfileSummary::PSK_Instr);
    if (PGOStats.hasDiagnostics())
      PGOStats.reportDiagnostics(getDiags(), getCodeGenOpts().MainFileName);
  }
  llvm::stable_sort(GlobalCtors, [](const Structor &L, const Structor &R) {
    return L.LexOrder < R.LexOrder;
  });
  EmitCtorList(GlobalCtors, "llvm.global_ctors");
  EmitCtorList(GlobalDtors, "llvm.global_dtors");
  EmitGlobalAnnotations();
  EmitStaticExternCAliases();
  checkAliases();
  EmitDeferredUnusedCoverageMappings();
  CodeGenPGO(*this).setValueProfilingFlag(getModule());
  CodeGenPGO(*this).setProfileVersion(getModule());
  if (CoverageMapping)
    CoverageMapping->emit();
  if (CodeGenOpts.SanitizeCfiCrossDso) {
    CodeGenFunction(*this).EmitCfiCheckFail();
    CodeGenFunction(*this).EmitCfiCheckStub();
  }
  if (LangOpts.Sanitize.has(SanitizerKind::KCFI))
    finalizeKCFITypes();
  emitAtAvailableLinkGuard();
  if (Context.getTargetInfo().getTriple().isWasm())
    EmitMainVoidAlias();

  if (getTriple().isAMDGPU() ||
      (getTriple().isSPIRV() && getTriple().getVendor() == llvm::Triple::AMD)) {
    // Emit amdhsa_code_object_version module flag, which is code object version
    // times 100.
    if (getTarget().getTargetOpts().CodeObjectVersion !=
        llvm::CodeObjectVersionKind::COV_None) {
      getModule().addModuleFlag(llvm::Module::Error,
                                "amdhsa_code_object_version",
                                getTarget().getTargetOpts().CodeObjectVersion);
    }

    // Currently, "-mprintf-kind" option is only supported for HIP
    if (LangOpts.HIP) {
      auto *MDStr = llvm::MDString::get(
          getLLVMContext(), (getTarget().getTargetOpts().AMDGPUPrintfKindVal ==
                             TargetOptions::AMDGPUPrintfKind::Hostcall)
                                ? "hostcall"
                                : "buffered");
      getModule().addModuleFlag(llvm::Module::Error, "amdgpu_printf_kind",
                                MDStr);
    }
  }

  // Emit a global array containing all external kernels or device variables
  // used by host functions and mark it as used for CUDA/HIP. This is necessary
  // to get kernels or device variables in archives linked in even if these
  // kernels or device variables are only used in host functions.
  if (!Context.CUDAExternalDeviceDeclODRUsedByHost.empty()) {
    SmallVector<llvm::Constant *, 8> UsedArray;
    for (auto D : Context.CUDAExternalDeviceDeclODRUsedByHost) {
      GlobalDecl GD;
      if (auto *FD = dyn_cast<FunctionDecl>(D))
        GD = GlobalDecl(FD, KernelReferenceKind::Kernel);
      else
        GD = GlobalDecl(D);
      UsedArray.push_back(llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(
          GetAddrOfGlobal(GD), Int8PtrTy));
    }

    llvm::ArrayType *ATy = llvm::ArrayType::get(Int8PtrTy, UsedArray.size());

    auto *GV = new llvm::GlobalVariable(
        getModule(), ATy, false, llvm::GlobalValue::InternalLinkage,
        llvm::ConstantArray::get(ATy, UsedArray), "__clang_gpu_used_external");
    addCompilerUsedGlobal(GV);
  }
  if (LangOpts.HIP && !getLangOpts().OffloadingNewDriver) {
    // Emit a unique ID so that host and device binaries from the same
    // compilation unit can be associated.
    auto *GV = new llvm::GlobalVariable(
        getModule(), Int8Ty, false, llvm::GlobalValue::ExternalLinkage,
        llvm::Constant::getNullValue(Int8Ty),
        "__hip_cuid_" + getContext().getCUIDHash());
    addCompilerUsedGlobal(GV);
  }
  emitLLVMUsed();
  if (SanStats)
    SanStats->finish();

  if (CodeGenOpts.Autolink &&
      (Context.getLangOpts().Modules || !LinkerOptionsMetadata.empty())) {
    EmitModuleLinkOptions();
  }

  // On ELF we pass the dependent library specifiers directly to the linker
  // without manipulating them. This is in contrast to other platforms where
  // they are mapped to a specific linker option by the compiler. This
  // difference is a result of the greater variety of ELF linkers and the fact
  // that ELF linkers tend to handle libraries in a more complicated fashion
  // than on other platforms. This forces us to defer handling the dependent
  // libs to the linker.
  //
  // CUDA/HIP device and host libraries are different. Currently there is no
  // way to differentiate dependent libraries for host or device. Existing
  // usage of #pragma comment(lib, *) is intended for host libraries on
  // Windows. Therefore emit llvm.dependent-libraries only for host.
  if (!ELFDependentLibraries.empty() && !Context.getLangOpts().CUDAIsDevice) {
    auto *NMD = getModule().getOrInsertNamedMetadata("llvm.dependent-libraries");
    for (auto *MD : ELFDependentLibraries)
      NMD->addOperand(MD);
  }

  if (CodeGenOpts.DwarfVersion) {
    getModule().addModuleFlag(llvm::Module::Max, "Dwarf Version",
                              CodeGenOpts.DwarfVersion);
  }

  if (CodeGenOpts.Dwarf64)
    getModule().addModuleFlag(llvm::Module::Max, "DWARF64", 1);

  if (Context.getLangOpts().SemanticInterposition)
    // Require various optimization to respect semantic interposition.
    getModule().setSemanticInterposition(true);

  if (CodeGenOpts.EmitCodeView) {
    // Indicate that we want CodeView in the metadata.
    getModule().addModuleFlag(llvm::Module::Warning, "CodeView", 1);
  }
  if (CodeGenOpts.CodeViewGHash) {
    getModule().addModuleFlag(llvm::Module::Warning, "CodeViewGHash", 1);
  }
  if (CodeGenOpts.ControlFlowGuard) {
    // Function ID tables and checks for Control Flow Guard (cfguard=2).
    getModule().addModuleFlag(llvm::Module::Warning, "cfguard", 2);
  } else if (CodeGenOpts.ControlFlowGuardNoChecks) {
    // Function ID tables for Control Flow Guard (cfguard=1).
    getModule().addModuleFlag(llvm::Module::Warning, "cfguard", 1);
  }
  if (CodeGenOpts.EHContGuard) {
    // Function ID tables for EH Continuation Guard.
    getModule().addModuleFlag(llvm::Module::Warning, "ehcontguard", 1);
  }
  if (Context.getLangOpts().Kernel) {
    // Note if we are compiling with /kernel.
    getModule().addModuleFlag(llvm::Module::Warning, "ms-kernel", 1);
  }
  if (CodeGenOpts.OptimizationLevel > 0 && CodeGenOpts.StrictVTablePointers) {
    // We don't support LTO with 2 with different StrictVTablePointers
    // FIXME: we could support it by stripping all the information introduced
    // by StrictVTablePointers.

    getModule().addModuleFlag(llvm::Module::Error, "StrictVTablePointers",1);

    llvm::Metadata *Ops[2] = {
              llvm::MDString::get(VMContext, "StrictVTablePointers"),
              llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                  llvm::Type::getInt32Ty(VMContext), 1))};

    getModule().addModuleFlag(llvm::Module::Require,
                              "StrictVTablePointersRequirement",
                              llvm::MDNode::get(VMContext, Ops));
  }
  if (getModuleDebugInfo())
    // We support a single version in the linked module. The LLVM
    // parser will drop debug info with a different version number
    // (and warn about it, too).
    getModule().addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                              llvm::DEBUG_METADATA_VERSION);

  // We need to record the widths of enums and wchar_t, so that we can generate
  // the correct build attributes in the ARM backend. wchar_size is also used by
  // TargetLibraryInfo.
  uint64_t WCharWidth =
      Context.getTypeSizeInChars(Context.getWideCharType()).getQuantity();
  getModule().addModuleFlag(llvm::Module::Error, "wchar_size", WCharWidth);

  if (getTriple().isOSzOS()) {
    getModule().addModuleFlag(llvm::Module::Warning,
                              "zos_product_major_version",
                              uint32_t(CLANG_VERSION_MAJOR));
    getModule().addModuleFlag(llvm::Module::Warning,
                              "zos_product_minor_version",
                              uint32_t(CLANG_VERSION_MINOR));
    getModule().addModuleFlag(llvm::Module::Warning, "zos_product_patchlevel",
                              uint32_t(CLANG_VERSION_PATCHLEVEL));
    std::string ProductId = getClangVendor() + "clang";
    getModule().addModuleFlag(llvm::Module::Error, "zos_product_id",
                              llvm::MDString::get(VMContext, ProductId));

    // Record the language because we need it for the PPA2.
    StringRef lang_str = languageToString(
        LangStandard::getLangStandardForKind(LangOpts.LangStd).Language);
    getModule().addModuleFlag(llvm::Module::Error, "zos_cu_language",
                              llvm::MDString::get(VMContext, lang_str));

    time_t TT = PreprocessorOpts.SourceDateEpoch
                    ? *PreprocessorOpts.SourceDateEpoch
                    : std::time(nullptr);
    getModule().addModuleFlag(llvm::Module::Max, "zos_translation_time",
                              static_cast<uint64_t>(TT));

    // Multiple modes will be supported here.
    getModule().addModuleFlag(llvm::Module::Error, "zos_le_char_mode",
                              llvm::MDString::get(VMContext, "ascii"));
  }

  llvm::Triple T = Context.getTargetInfo().getTriple();
  if (T.isARM() || T.isThumb()) {
    // The minimum width of an enum in bytes
    uint64_t EnumWidth = Context.getLangOpts().ShortEnums ? 1 : 4;
    getModule().addModuleFlag(llvm::Module::Error, "min_enum_size", EnumWidth);
  }

  if (T.isRISCV()) {
    StringRef ABIStr = Target.getABI();
    llvm::LLVMContext &Ctx = TheModule.getContext();
    getModule().addModuleFlag(llvm::Module::Error, "target-abi",
                              llvm::MDString::get(Ctx, ABIStr));

    // Add the canonical ISA string as metadata so the backend can set the ELF
    // attributes correctly. We use AppendUnique so LTO will keep all of the
    // unique ISA strings that were linked together.
    const std::vector<std::string> &Features =
        getTarget().getTargetOpts().Features;
    auto ParseResult =
        llvm::RISCVISAInfo::parseFeatures(T.isRISCV64() ? 64 : 32, Features);
    if (!errorToBool(ParseResult.takeError()))
      getModule().addModuleFlag(
          llvm::Module::AppendUnique, "riscv-isa",
          llvm::MDNode::get(
              Ctx, llvm::MDString::get(Ctx, (*ParseResult)->toString())));
  }

  if (CodeGenOpts.SanitizeCfiCrossDso) {
    // Indicate that we want cross-DSO control flow integrity checks.
    getModule().addModuleFlag(llvm::Module::Override, "Cross-DSO CFI", 1);
  }

  if (CodeGenOpts.WholeProgramVTables) {
    // Indicate whether VFE was enabled for this module, so that the
    // vcall_visibility metadata added under whole program vtables is handled
    // appropriately in the optimizer.
    getModule().addModuleFlag(llvm::Module::Error, "Virtual Function Elim",
                              CodeGenOpts.VirtualFunctionElimination);
  }

  if (LangOpts.Sanitize.has(SanitizerKind::CFIICall)) {
    getModule().addModuleFlag(llvm::Module::Override,
                              "CFI Canonical Jump Tables",
                              CodeGenOpts.SanitizeCfiCanonicalJumpTables);
  }

  if (CodeGenOpts.SanitizeCfiICallNormalizeIntegers) {
    getModule().addModuleFlag(llvm::Module::Override, "cfi-normalize-integers",
                              1);
  }

  if (LangOpts.Sanitize.has(SanitizerKind::KCFI)) {
    getModule().addModuleFlag(llvm::Module::Override, "kcfi", 1);
    // KCFI assumes patchable-function-prefix is the same for all indirectly
    // called functions. Store the expected offset for code generation.
    if (CodeGenOpts.PatchableFunctionEntryOffset)
      getModule().addModuleFlag(llvm::Module::Override, "kcfi-offset",
                                CodeGenOpts.PatchableFunctionEntryOffset);
  }

  if (CodeGenOpts.CFProtectionReturn &&
      Target.checkCFProtectionReturnSupported(getDiags())) {
    // Indicate that we want to instrument return control flow protection.
    getModule().addModuleFlag(llvm::Module::Min, "cf-protection-return",
                              1);
  }

  if (CodeGenOpts.CFProtectionBranch &&
      Target.checkCFProtectionBranchSupported(getDiags())) {
    // Indicate that we want to instrument branch control flow protection.
    getModule().addModuleFlag(llvm::Module::Min, "cf-protection-branch",
                              1);
  }

  if (CodeGenOpts.FunctionReturnThunks)
    getModule().addModuleFlag(llvm::Module::Override, "function_return_thunk_extern", 1);

  if (CodeGenOpts.IndirectBranchCSPrefix)
    getModule().addModuleFlag(llvm::Module::Override, "indirect_branch_cs_prefix", 1);

  // Add module metadata for return address signing (ignoring
  // non-leaf/all) and stack tagging. These are actually turned on by function
  // attributes, but we use module metadata to emit build attributes. This is
  // needed for LTO, where the function attributes are inside bitcode
  // serialised into a global variable by the time build attributes are
  // emitted, so we can't access them. LTO objects could be compiled with
  // different flags therefore module flags are set to "Min" behavior to achieve
  // the same end result of the normal build where e.g BTI is off if any object
  // doesn't support it.
  if (Context.getTargetInfo().hasFeature("ptrauth") &&
      LangOpts.getSignReturnAddressScope() !=
          LangOptions::SignReturnAddressScopeKind::None)
    getModule().addModuleFlag(llvm::Module::Override,
                              "sign-return-address-buildattr", 1);
  if (LangOpts.Sanitize.has(SanitizerKind::MemtagStack))
    getModule().addModuleFlag(llvm::Module::Override,
                              "tag-stack-memory-buildattr", 1);

  if (T.isARM() || T.isThumb() || T.isAArch64()) {
    if (LangOpts.BranchTargetEnforcement)
      getModule().addModuleFlag(llvm::Module::Min, "branch-target-enforcement",
                                1);
    if (LangOpts.BranchProtectionPAuthLR)
      getModule().addModuleFlag(llvm::Module::Min, "branch-protection-pauth-lr",
                                1);
    if (LangOpts.GuardedControlStack)
      getModule().addModuleFlag(llvm::Module::Min, "guarded-control-stack", 1);
    if (LangOpts.hasSignReturnAddress())
      getModule().addModuleFlag(llvm::Module::Min, "sign-return-address", 1);
    if (LangOpts.isSignReturnAddressScopeAll())
      getModule().addModuleFlag(llvm::Module::Min, "sign-return-address-all",
                                1);
    if (!LangOpts.isSignReturnAddressWithAKey())
      getModule().addModuleFlag(llvm::Module::Min,
                                "sign-return-address-with-bkey", 1);

    if (getTriple().isOSLinux()) {
      assert(getTriple().isOSBinFormatELF());
      using namespace llvm::ELF;
      uint64_t PAuthABIVersion =
          (LangOpts.PointerAuthIntrinsics
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INTRINSICS) |
          (LangOpts.PointerAuthCalls
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_CALLS) |
          (LangOpts.PointerAuthReturns
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_RETURNS) |
          (LangOpts.PointerAuthAuthTraps
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_AUTHTRAPS) |
          (LangOpts.PointerAuthVTPtrAddressDiscrimination
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_VPTRADDRDISCR) |
          (LangOpts.PointerAuthVTPtrTypeDiscrimination
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_VPTRTYPEDISCR) |
          (LangOpts.PointerAuthInitFini
           << AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INITFINI);
      static_assert(AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INITFINI ==
                        AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_LAST,
                    "Update when new enum items are defined");
      if (PAuthABIVersion != 0) {
        getModule().addModuleFlag(llvm::Module::Error,
                                  "aarch64-elf-pauthabi-platform",
                                  AARCH64_PAUTH_PLATFORM_LLVM_LINUX);
        getModule().addModuleFlag(llvm::Module::Error,
                                  "aarch64-elf-pauthabi-version",
                                  PAuthABIVersion);
      }
    }
  }

  if (CodeGenOpts.StackClashProtector)
    getModule().addModuleFlag(
        llvm::Module::Override, "probe-stack",
        llvm::MDString::get(TheModule.getContext(), "inline-asm"));

  if (CodeGenOpts.StackProbeSize && CodeGenOpts.StackProbeSize != 4096)
    getModule().addModuleFlag(llvm::Module::Min, "stack-probe-size",
                              CodeGenOpts.StackProbeSize);

  if (!CodeGenOpts.MemoryProfileOutput.empty()) {
    llvm::LLVMContext &Ctx = TheModule.getContext();
    getModule().addModuleFlag(
        llvm::Module::Error, "MemProfProfileFilename",
        llvm::MDString::get(Ctx, CodeGenOpts.MemoryProfileOutput));
  }

  if (LangOpts.CUDAIsDevice && getTriple().isNVPTX()) {
    // Indicate whether __nvvm_reflect should be configured to flush denormal
    // floating point values to 0.  (This corresponds to its "__CUDA_FTZ"
    // property.)
    getModule().addModuleFlag(llvm::Module::Override, "nvvm-reflect-ftz",
                              CodeGenOpts.FP32DenormalMode.Output !=
                                  llvm::DenormalMode::IEEE);
  }

  if (LangOpts.EHAsynch)
    getModule().addModuleFlag(llvm::Module::Warning, "eh-asynch", 1);

  // Indicate whether this Module was compiled with -fopenmp
  if (getLangOpts().OpenMP && !getLangOpts().OpenMPSimd)
    getModule().addModuleFlag(llvm::Module::Max, "openmp", LangOpts.OpenMP);
  if (getLangOpts().OpenMPIsTargetDevice)
    getModule().addModuleFlag(llvm::Module::Max, "openmp-device",
                              LangOpts.OpenMP);

  // Emit OpenCL specific module metadata: OpenCL/SPIR version.
  if (LangOpts.OpenCL || (LangOpts.CUDAIsDevice && getTriple().isSPIRV())) {
    EmitOpenCLMetadata();
    // Emit SPIR version.
    if (getTriple().isSPIR()) {
      // SPIR v2.0 s2.12 - The SPIR version used by the module is stored in the
      // opencl.spir.version named metadata.
      // C++ for OpenCL has a distinct mapping for version compatibility with
      // OpenCL.
      auto Version = LangOpts.getOpenCLCompatibleVersion();
      llvm::Metadata *SPIRVerElts[] = {
          llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
              Int32Ty, Version / 100)),
          llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
              Int32Ty, (Version / 100 > 1) ? 0 : 2))};
      llvm::NamedMDNode *SPIRVerMD =
          TheModule.getOrInsertNamedMetadata("opencl.spir.version");
      llvm::LLVMContext &Ctx = TheModule.getContext();
      SPIRVerMD->addOperand(llvm::MDNode::get(Ctx, SPIRVerElts));
    }
  }

  // HLSL related end of code gen work items.
  if (LangOpts.HLSL)
    getHLSLRuntime().finishCodeGen();

  if (uint32_t PLevel = Context.getLangOpts().PICLevel) {
    assert(PLevel < 3 && "Invalid PIC Level");
    getModule().setPICLevel(static_cast<llvm::PICLevel::Level>(PLevel));
    if (Context.getLangOpts().PIE)
      getModule().setPIELevel(static_cast<llvm::PIELevel::Level>(PLevel));
  }

  if (getCodeGenOpts().CodeModel.size() > 0) {
    unsigned CM = llvm::StringSwitch<unsigned>(getCodeGenOpts().CodeModel)
                  .Case("tiny", llvm::CodeModel::Tiny)
                  .Case("small", llvm::CodeModel::Small)
                  .Case("kernel", llvm::CodeModel::Kernel)
                  .Case("medium", llvm::CodeModel::Medium)
                  .Case("large", llvm::CodeModel::Large)
                  .Default(~0u);
    if (CM != ~0u) {
      llvm::CodeModel::Model codeModel = static_cast<llvm::CodeModel::Model>(CM);
      getModule().setCodeModel(codeModel);

      if ((CM == llvm::CodeModel::Medium || CM == llvm::CodeModel::Large) &&
          Context.getTargetInfo().getTriple().getArch() ==
              llvm::Triple::x86_64) {
        getModule().setLargeDataThreshold(getCodeGenOpts().LargeDataThreshold);
      }
    }
  }

  if (CodeGenOpts.NoPLT)
    getModule().setRtLibUseGOT();
  if (getTriple().isOSBinFormatELF() &&
      CodeGenOpts.DirectAccessExternalData !=
          getModule().getDirectAccessExternalData()) {
    getModule().setDirectAccessExternalData(
        CodeGenOpts.DirectAccessExternalData);
  }
  if (CodeGenOpts.UnwindTables)
    getModule().setUwtable(llvm::UWTableKind(CodeGenOpts.UnwindTables));

  switch (CodeGenOpts.getFramePointer()) {
  case CodeGenOptions::FramePointerKind::None:
    // 0 ("none") is the default.
    break;
  case CodeGenOptions::FramePointerKind::Reserved:
    getModule().setFramePointer(llvm::FramePointerKind::Reserved);
    break;
  case CodeGenOptions::FramePointerKind::NonLeaf:
    getModule().setFramePointer(llvm::FramePointerKind::NonLeaf);
    break;
  case CodeGenOptions::FramePointerKind::All:
    getModule().setFramePointer(llvm::FramePointerKind::All);
    break;
  }

  SimplifyPersonality();

  if (getCodeGenOpts().EmitDeclMetadata)
    EmitDeclMetadata();

  if (getCodeGenOpts().CoverageNotesFile.size() ||
      getCodeGenOpts().CoverageDataFile.size())
    EmitCoverageFile();

  if (CGDebugInfo *DI = getModuleDebugInfo())
    DI->finalize();

  if (getCodeGenOpts().EmitVersionIdentMetadata)
    EmitVersionIdentMetadata();

  if (!getCodeGenOpts().RecordCommandLine.empty())
    EmitCommandLineMetadata();

  if (!getCodeGenOpts().StackProtectorGuard.empty())
    getModule().setStackProtectorGuard(getCodeGenOpts().StackProtectorGuard);
  if (!getCodeGenOpts().StackProtectorGuardReg.empty())
    getModule().setStackProtectorGuardReg(
        getCodeGenOpts().StackProtectorGuardReg);
  if (!getCodeGenOpts().StackProtectorGuardSymbol.empty())
    getModule().setStackProtectorGuardSymbol(
        getCodeGenOpts().StackProtectorGuardSymbol);
  if (getCodeGenOpts().StackProtectorGuardOffset != INT_MAX)
    getModule().setStackProtectorGuardOffset(
        getCodeGenOpts().StackProtectorGuardOffset);
  if (getCodeGenOpts().StackAlignment)
    getModule().setOverrideStackAlignment(getCodeGenOpts().StackAlignment);
  if (getCodeGenOpts().SkipRaxSetup)
    getModule().addModuleFlag(llvm::Module::Override, "SkipRaxSetup", 1);
  if (getLangOpts().RegCall4)
    getModule().addModuleFlag(llvm::Module::Override, "RegCallv4", 1);

  if (getContext().getTargetInfo().getMaxTLSAlign())
    getModule().addModuleFlag(llvm::Module::Error, "MaxTLSAlign",
                              getContext().getTargetInfo().getMaxTLSAlign());

  getTargetCodeGenInfo().emitTargetGlobals(*this);

  getTargetCodeGenInfo().emitTargetMetadata(*this, MangledDeclNames);

  EmitBackendOptionsMetadata(getCodeGenOpts());

  // If there is device offloading code embed it in the host now.
  EmbedObject(&getModule(), CodeGenOpts, getDiags());

  // Set visibility from DLL storage class
  // We do this at the end of LLVM IR generation; after any operation
  // that might affect the DLL storage class or the visibility, and
  // before anything that might act on these.
  setVisibilityFromDLLStorageClass(LangOpts, getModule());

  // Check the tail call symbols are truly undefined.
  if (getTriple().isPPC() && !MustTailCallUndefinedGlobals.empty()) {
    for (auto &I : MustTailCallUndefinedGlobals) {
      if (!I.first->isDefined())
        getDiags().Report(I.second, diag::err_ppc_impossible_musttail) << 2;
      else {
        StringRef MangledName = getMangledName(GlobalDecl(I.first));
        llvm::GlobalValue *Entry = GetGlobalValue(MangledName);
        if (!Entry || Entry->isWeakForLinker() ||
            Entry->isDeclarationForLinker())
          getDiags().Report(I.second, diag::err_ppc_impossible_musttail) << 2;
      }
    }
  }
}

void CodeGenModule::EmitOpenCLMetadata() {
  // SPIR v2.0 s2.13 - The OpenCL version used by the module is stored in the
  // opencl.ocl.version named metadata node.
  // C++ for OpenCL has a distinct mapping for versions compatible with OpenCL.
  auto CLVersion = LangOpts.getOpenCLCompatibleVersion();

  auto EmitVersion = [this](StringRef MDName, int Version) {
    llvm::Metadata *OCLVerElts[] = {
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(Int32Ty, Version / 100)),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(Int32Ty, (Version % 100) / 10))};
    llvm::NamedMDNode *OCLVerMD = TheModule.getOrInsertNamedMetadata(MDName);
    llvm::LLVMContext &Ctx = TheModule.getContext();
    OCLVerMD->addOperand(llvm::MDNode::get(Ctx, OCLVerElts));
  };

  EmitVersion("opencl.ocl.version", CLVersion);
  if (LangOpts.OpenCLCPlusPlus) {
    // In addition to the OpenCL compatible version, emit the C++ version.
    EmitVersion("opencl.cxx.version", LangOpts.OpenCLCPlusPlusVersion);
  }
}

void CodeGenModule::EmitBackendOptionsMetadata(
    const CodeGenOptions &CodeGenOpts) {
  if (getTriple().isRISCV()) {
    getModule().addModuleFlag(llvm::Module::Min, "SmallDataLimit",
                              CodeGenOpts.SmallDataLimit);
  }
}

void CodeGenModule::UpdateCompletedType(const TagDecl *TD) {
  // Make sure that this type is translated.
  getTypes().UpdateCompletedType(TD);
}

void CodeGenModule::RefreshTypeCacheForClass(const CXXRecordDecl *RD) {
  // Make sure that this type is translated.
  getTypes().RefreshTypeCacheForClass(RD);
}

llvm::MDNode *CodeGenModule::getTBAATypeInfo(QualType QTy) {
  if (!TBAA)
    return nullptr;
  return TBAA->getTypeInfo(QTy);
}

TBAAAccessInfo CodeGenModule::getTBAAAccessInfo(QualType AccessType) {
  if (!TBAA)
    return TBAAAccessInfo();
  if (getLangOpts().CUDAIsDevice) {
    // As CUDA builtin surface/texture types are replaced, skip generating TBAA
    // access info.
    if (AccessType->isCUDADeviceBuiltinSurfaceType()) {
      if (getTargetCodeGenInfo().getCUDADeviceBuiltinSurfaceDeviceType() !=
          nullptr)
        return TBAAAccessInfo();
    } else if (AccessType->isCUDADeviceBuiltinTextureType()) {
      if (getTargetCodeGenInfo().getCUDADeviceBuiltinTextureDeviceType() !=
          nullptr)
        return TBAAAccessInfo();
    }
  }
  return TBAA->getAccessInfo(AccessType);
}

TBAAAccessInfo
CodeGenModule::getTBAAVTablePtrAccessInfo(llvm::Type *VTablePtrType) {
  if (!TBAA)
    return TBAAAccessInfo();
  return TBAA->getVTablePtrAccessInfo(VTablePtrType);
}

llvm::MDNode *CodeGenModule::getTBAAStructInfo(QualType QTy) {
  if (!TBAA)
    return nullptr;
  return TBAA->getTBAAStructInfo(QTy);
}

llvm::MDNode *CodeGenModule::getTBAABaseTypeInfo(QualType QTy) {
  if (!TBAA)
    return nullptr;
  return TBAA->getBaseTypeInfo(QTy);
}

llvm::MDNode *CodeGenModule::getTBAAAccessTagInfo(TBAAAccessInfo Info) {
  if (!TBAA)
    return nullptr;
  return TBAA->getAccessTagInfo(Info);
}

TBAAAccessInfo CodeGenModule::mergeTBAAInfoForCast(TBAAAccessInfo SourceInfo,
                                                   TBAAAccessInfo TargetInfo) {
  if (!TBAA)
    return TBAAAccessInfo();
  return TBAA->mergeTBAAInfoForCast(SourceInfo, TargetInfo);
}

TBAAAccessInfo
CodeGenModule::mergeTBAAInfoForConditionalOperator(TBAAAccessInfo InfoA,
                                                   TBAAAccessInfo InfoB) {
  if (!TBAA)
    return TBAAAccessInfo();
  return TBAA->mergeTBAAInfoForConditionalOperator(InfoA, InfoB);
}

TBAAAccessInfo
CodeGenModule::mergeTBAAInfoForMemoryTransfer(TBAAAccessInfo DestInfo,
                                              TBAAAccessInfo SrcInfo) {
  if (!TBAA)
    return TBAAAccessInfo();
  return TBAA->mergeTBAAInfoForConditionalOperator(DestInfo, SrcInfo);
}

void CodeGenModule::DecorateInstructionWithTBAA(llvm::Instruction *Inst,
                                                TBAAAccessInfo TBAAInfo) {
  if (llvm::MDNode *Tag = getTBAAAccessTagInfo(TBAAInfo))
    Inst->setMetadata(llvm::LLVMContext::MD_tbaa, Tag);
}

void CodeGenModule::DecorateInstructionWithInvariantGroup(
    llvm::Instruction *I, const CXXRecordDecl *RD) {
  I->setMetadata(llvm::LLVMContext::MD_invariant_group,
                 llvm::MDNode::get(getLLVMContext(), {}));
}

void CodeGenModule::Error(SourceLocation loc, StringRef message) {
  unsigned diagID = getDiags().getCustomDiagID(DiagnosticsEngine::Error, "%0");
  getDiags().Report(Context.getFullLoc(loc), diagID) << message;
}

/// ErrorUnsupported - Print out an error that codegen doesn't support the
/// specified stmt yet.
void CodeGenModule::ErrorUnsupported(const Stmt *S, const char *Type) {
  unsigned DiagID = getDiags().getCustomDiagID(DiagnosticsEngine::Error,
                                               "cannot compile this %0 yet");
  std::string Msg = Type;
  getDiags().Report(Context.getFullLoc(S->getBeginLoc()), DiagID)
      << Msg << S->getSourceRange();
}

/// ErrorUnsupported - Print out an error that codegen doesn't support the
/// specified decl yet.
void CodeGenModule::ErrorUnsupported(const Decl *D, const char *Type) {
  unsigned DiagID = getDiags().getCustomDiagID(DiagnosticsEngine::Error,
                                               "cannot compile this %0 yet");
  std::string Msg = Type;
  getDiags().Report(Context.getFullLoc(D->getLocation()), DiagID) << Msg;
}

llvm::ConstantInt *CodeGenModule::getSize(CharUnits size) {
  return llvm::ConstantInt::get(SizeTy, size.getQuantity());
}

void CodeGenModule::setGlobalVisibility(llvm::GlobalValue *GV,
                                        const NamedDecl *D) const {
  // Internal definitions always have default visibility.
  if (GV->hasLocalLinkage()) {
    GV->setVisibility(llvm::GlobalValue::DefaultVisibility);
    return;
  }
  if (!D)
    return;

  // Set visibility for definitions, and for declarations if requested globally
  // or set explicitly.
  LinkageInfo LV = D->getLinkageAndVisibility();

  // OpenMP declare target variables must be visible to the host so they can
  // be registered. We require protected visibility unless the variable has
  // the DT_nohost modifier and does not need to be registered.
  if (Context.getLangOpts().OpenMP &&
      Context.getLangOpts().OpenMPIsTargetDevice && isa<VarDecl>(D) &&
      D->hasAttr<OMPDeclareTargetDeclAttr>() &&
      D->getAttr<OMPDeclareTargetDeclAttr>()->getDevType() !=
          OMPDeclareTargetDeclAttr::DT_NoHost &&
      LV.getVisibility() == HiddenVisibility) {
    GV->setVisibility(llvm::GlobalValue::ProtectedVisibility);
    return;
  }

  if (GV->hasDLLExportStorageClass() || GV->hasDLLImportStorageClass()) {
    // Reject incompatible dlllstorage and visibility annotations.
    if (!LV.isVisibilityExplicit())
      return;
    if (GV->hasDLLExportStorageClass()) {
      if (LV.getVisibility() == HiddenVisibility)
        getDiags().Report(D->getLocation(),
                          diag::err_hidden_visibility_dllexport);
    } else if (LV.getVisibility() != DefaultVisibility) {
      getDiags().Report(D->getLocation(),
                        diag::err_non_default_visibility_dllimport);
    }
    return;
  }

  if (LV.isVisibilityExplicit() || getLangOpts().SetVisibilityForExternDecls ||
      !GV->isDeclarationForLinker())
    GV->setVisibility(GetLLVMVisibility(LV.getVisibility()));
}

static bool shouldAssumeDSOLocal(const CodeGenModule &CGM,
                                 llvm::GlobalValue *GV) {
  if (GV->hasLocalLinkage())
    return true;

  if (!GV->hasDefaultVisibility() && !GV->hasExternalWeakLinkage())
    return true;

  // DLLImport explicitly marks the GV as external.
  if (GV->hasDLLImportStorageClass())
    return false;

  const llvm::Triple &TT = CGM.getTriple();
  const auto &CGOpts = CGM.getCodeGenOpts();
  if (TT.isWindowsGNUEnvironment()) {
    // In MinGW, variables without DLLImport can still be automatically
    // imported from a DLL by the linker; don't mark variables that
    // potentially could come from another DLL as DSO local.

    // With EmulatedTLS, TLS variables can be autoimported from other DLLs
    // (and this actually happens in the public interface of libstdc++), so
    // such variables can't be marked as DSO local. (Native TLS variables
    // can't be dllimported at all, though.)
    if (GV->isDeclarationForLinker() && isa<llvm::GlobalVariable>(GV) &&
        (!GV->isThreadLocal() || CGM.getCodeGenOpts().EmulatedTLS) &&
        CGOpts.AutoImport)
      return false;
  }

  // On COFF, don't mark 'extern_weak' symbols as DSO local. If these symbols
  // remain unresolved in the link, they can be resolved to zero, which is
  // outside the current DSO.
  if (TT.isOSBinFormatCOFF() && GV->hasExternalWeakLinkage())
    return false;

  // Every other GV is local on COFF.
  // Make an exception for windows OS in the triple: Some firmware builds use
  // *-win32-macho triples. This (accidentally?) produced windows relocations
  // without GOT tables in older clang versions; Keep this behaviour.
  // FIXME: even thread local variables?
  if (TT.isOSBinFormatCOFF() || (TT.isOSWindows() && TT.isOSBinFormatMachO()))
    return true;

  // Only handle COFF and ELF for now.
  if (!TT.isOSBinFormatELF())
    return false;

  // If this is not an executable, don't assume anything is local.
  llvm::Reloc::Model RM = CGOpts.RelocationModel;
  const auto &LOpts = CGM.getLangOpts();
  if (RM != llvm::Reloc::Static && !LOpts.PIE) {
    // On ELF, if -fno-semantic-interposition is specified and the target
    // supports local aliases, there will be neither CC1
    // -fsemantic-interposition nor -fhalf-no-semantic-interposition. Set
    // dso_local on the function if using a local alias is preferable (can avoid
    // PLT indirection).
    if (!(isa<llvm::Function>(GV) && GV->canBenefitFromLocalAlias()))
      return false;
    return !(CGM.getLangOpts().SemanticInterposition ||
             CGM.getLangOpts().HalfNoSemanticInterposition);
  }

  // A definition cannot be preempted from an executable.
  if (!GV->isDeclarationForLinker())
    return true;

  // Most PIC code sequences that assume that a symbol is local cannot produce a
  // 0 if it turns out the symbol is undefined. While this is ABI and relocation
  // depended, it seems worth it to handle it here.
  if (RM == llvm::Reloc::PIC_ && GV->hasExternalWeakLinkage())
    return false;

  // PowerPC64 prefers TOC indirection to avoid copy relocations.
  if (TT.isPPC64())
    return false;

  if (CGOpts.DirectAccessExternalData) {
    // If -fdirect-access-external-data (default for -fno-pic), set dso_local
    // for non-thread-local variables. If the symbol is not defined in the
    // executable, a copy relocation will be needed at link time. dso_local is
    // excluded for thread-local variables because they generally don't support
    // copy relocations.
    if (auto *Var = dyn_cast<llvm::GlobalVariable>(GV))
      if (!Var->isThreadLocal())
        return true;

    // -fno-pic sets dso_local on a function declaration to allow direct
    // accesses when taking its address (similar to a data symbol). If the
    // function is not defined in the executable, a canonical PLT entry will be
    // needed at link time. -fno-direct-access-external-data can avoid the
    // canonical PLT entry. We don't generalize this condition to -fpie/-fpic as
    // it could just cause trouble without providing perceptible benefits.
    if (isa<llvm::Function>(GV) && !CGOpts.NoPLT && RM == llvm::Reloc::Static)
      return true;
  }

  // If we can use copy relocations we can assume it is local.

  // Otherwise don't assume it is local.
  return false;
}

void CodeGenModule::setDSOLocal(llvm::GlobalValue *GV) const {
  GV->setDSOLocal(shouldAssumeDSOLocal(*this, GV));
}

void CodeGenModule::setDLLImportDLLExport(llvm::GlobalValue *GV,
                                          GlobalDecl GD) const {
  const auto *D = dyn_cast<NamedDecl>(GD.getDecl());
  // C++ destructors have a few C++ ABI specific special cases.
  if (const auto *Dtor = dyn_cast_or_null<CXXDestructorDecl>(D)) {
    getCXXABI().setCXXDestructorDLLStorage(GV, Dtor, GD.getDtorType());
    return;
  }
  setDLLImportDLLExport(GV, D);
}

void CodeGenModule::setDLLImportDLLExport(llvm::GlobalValue *GV,
                                          const NamedDecl *D) const {
  if (D && D->isExternallyVisible()) {
    if (D->hasAttr<DLLImportAttr>())
      GV->setDLLStorageClass(llvm::GlobalVariable::DLLImportStorageClass);
    else if ((D->hasAttr<DLLExportAttr>() ||
              shouldMapVisibilityToDLLExport(D)) &&
             !GV->isDeclarationForLinker())
      GV->setDLLStorageClass(llvm::GlobalVariable::DLLExportStorageClass);
  }
}

void CodeGenModule::setGVProperties(llvm::GlobalValue *GV,
                                    GlobalDecl GD) const {
  setDLLImportDLLExport(GV, GD);
  setGVPropertiesAux(GV, dyn_cast<NamedDecl>(GD.getDecl()));
}

void CodeGenModule::setGVProperties(llvm::GlobalValue *GV,
                                    const NamedDecl *D) const {
  setDLLImportDLLExport(GV, D);
  setGVPropertiesAux(GV, D);
}

void CodeGenModule::setGVPropertiesAux(llvm::GlobalValue *GV,
                                       const NamedDecl *D) const {
  setGlobalVisibility(GV, D);
  setDSOLocal(GV);
  GV->setPartition(CodeGenOpts.SymbolPartition);
}

static llvm::GlobalVariable::ThreadLocalMode GetLLVMTLSModel(StringRef S) {
  return llvm::StringSwitch<llvm::GlobalVariable::ThreadLocalMode>(S)
      .Case("global-dynamic", llvm::GlobalVariable::GeneralDynamicTLSModel)
      .Case("local-dynamic", llvm::GlobalVariable::LocalDynamicTLSModel)
      .Case("initial-exec", llvm::GlobalVariable::InitialExecTLSModel)
      .Case("local-exec", llvm::GlobalVariable::LocalExecTLSModel);
}

llvm::GlobalVariable::ThreadLocalMode
CodeGenModule::GetDefaultLLVMTLSModel() const {
  switch (CodeGenOpts.getDefaultTLSModel()) {
  case CodeGenOptions::GeneralDynamicTLSModel:
    return llvm::GlobalVariable::GeneralDynamicTLSModel;
  case CodeGenOptions::LocalDynamicTLSModel:
    return llvm::GlobalVariable::LocalDynamicTLSModel;
  case CodeGenOptions::InitialExecTLSModel:
    return llvm::GlobalVariable::InitialExecTLSModel;
  case CodeGenOptions::LocalExecTLSModel:
    return llvm::GlobalVariable::LocalExecTLSModel;
  }
  llvm_unreachable("Invalid TLS model!");
}

void CodeGenModule::setTLSMode(llvm::GlobalValue *GV, const VarDecl &D) const {
  assert(D.getTLSKind() && "setting TLS mode on non-TLS var!");

  llvm::GlobalValue::ThreadLocalMode TLM;
  TLM = GetDefaultLLVMTLSModel();

  // Override the TLS model if it is explicitly specified.
  if (const TLSModelAttr *Attr = D.getAttr<TLSModelAttr>()) {
    TLM = GetLLVMTLSModel(Attr->getModel());
  }

  GV->setThreadLocalMode(TLM);
}

static std::string getCPUSpecificMangling(const CodeGenModule &CGM,
                                          StringRef Name) {
  const TargetInfo &Target = CGM.getTarget();
  return (Twine('.') + Twine(Target.CPUSpecificManglingCharacter(Name))).str();
}

static void AppendCPUSpecificCPUDispatchMangling(const CodeGenModule &CGM,
                                                 const CPUSpecificAttr *Attr,
                                                 unsigned CPUIndex,
                                                 raw_ostream &Out) {
  // cpu_specific gets the current name, dispatch gets the resolver if IFunc is
  // supported.
  if (Attr)
    Out << getCPUSpecificMangling(CGM, Attr->getCPUName(CPUIndex)->getName());
  else if (CGM.getTarget().supportsIFunc())
    Out << ".resolver";
}

// Returns true if GD is a function decl with internal linkage and
// needs a unique suffix after the mangled name.
static bool isUniqueInternalLinkageDecl(GlobalDecl GD,
                                        CodeGenModule &CGM) {
  const Decl *D = GD.getDecl();
  return !CGM.getModuleNameHash().empty() && isa<FunctionDecl>(D) &&
         (CGM.getFunctionLinkage(GD) == llvm::GlobalValue::InternalLinkage);
}

static std::string getMangledNameImpl(CodeGenModule &CGM, GlobalDecl GD,
                                      const NamedDecl *ND,
                                      bool OmitMultiVersionMangling = false) {
  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  MangleContext &MC = CGM.getCXXABI().getMangleContext();
  if (!CGM.getModuleNameHash().empty())
    MC.needsUniqueInternalLinkageNames();
  bool ShouldMangle = MC.shouldMangleDeclName(ND);
  if (ShouldMangle)
    MC.mangleName(GD.getWithDecl(ND), Out);
  else {
    IdentifierInfo *II = ND->getIdentifier();
    assert(II && "Attempt to mangle unnamed decl.");
    const auto *FD = dyn_cast<FunctionDecl>(ND);

    if (FD &&
        FD->getType()->castAs<FunctionType>()->getCallConv() == CC_X86RegCall) {
      if (CGM.getLangOpts().RegCall4)
        Out << "__regcall4__" << II->getName();
      else
        Out << "__regcall3__" << II->getName();
    } else if (FD && FD->hasAttr<CUDAGlobalAttr>() &&
               GD.getKernelReferenceKind() == KernelReferenceKind::Stub) {
      Out << "__device_stub__" << II->getName();
    } else {
      Out << II->getName();
    }
  }

  // Check if the module name hash should be appended for internal linkage
  // symbols.   This should come before multi-version target suffixes are
  // appended. This is to keep the name and module hash suffix of the
  // internal linkage function together.  The unique suffix should only be
  // added when name mangling is done to make sure that the final name can
  // be properly demangled.  For example, for C functions without prototypes,
  // name mangling is not done and the unique suffix should not be appeneded
  // then.
  if (ShouldMangle && isUniqueInternalLinkageDecl(GD, CGM)) {
    assert(CGM.getCodeGenOpts().UniqueInternalLinkageNames &&
           "Hash computed when not explicitly requested");
    Out << CGM.getModuleNameHash();
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(ND))
    if (FD->isMultiVersion() && !OmitMultiVersionMangling) {
      switch (FD->getMultiVersionKind()) {
      case MultiVersionKind::CPUDispatch:
      case MultiVersionKind::CPUSpecific:
        AppendCPUSpecificCPUDispatchMangling(CGM,
                                             FD->getAttr<CPUSpecificAttr>(),
                                             GD.getMultiVersionIndex(), Out);
        break;
      case MultiVersionKind::Target: {
        auto *Attr = FD->getAttr<TargetAttr>();
        assert(Attr && "Expected TargetAttr to be present "
                       "for attribute mangling");
        const ABIInfo &Info = CGM.getTargetCodeGenInfo().getABIInfo();
        Info.appendAttributeMangling(Attr, Out);
        break;
      }
      case MultiVersionKind::TargetVersion: {
        auto *Attr = FD->getAttr<TargetVersionAttr>();
        assert(Attr && "Expected TargetVersionAttr to be present "
                       "for attribute mangling");
        const ABIInfo &Info = CGM.getTargetCodeGenInfo().getABIInfo();
        Info.appendAttributeMangling(Attr, Out);
        break;
      }
      case MultiVersionKind::TargetClones: {
        auto *Attr = FD->getAttr<TargetClonesAttr>();
        assert(Attr && "Expected TargetClonesAttr to be present "
                       "for attribute mangling");
        unsigned Index = GD.getMultiVersionIndex();
        const ABIInfo &Info = CGM.getTargetCodeGenInfo().getABIInfo();
        Info.appendAttributeMangling(Attr, Index, Out);
        break;
      }
      case MultiVersionKind::None:
        llvm_unreachable("None multiversion type isn't valid here");
      }
    }

  // Make unique name for device side static file-scope variable for HIP.
  if (CGM.getContext().shouldExternalize(ND) &&
      CGM.getLangOpts().GPURelocatableDeviceCode &&
      CGM.getLangOpts().CUDAIsDevice)
    CGM.printPostfixForExternalizedDecl(Out, ND);

  return std::string(Out.str());
}

void CodeGenModule::UpdateMultiVersionNames(GlobalDecl GD,
                                            const FunctionDecl *FD,
                                            StringRef &CurName) {
  if (!FD->isMultiVersion())
    return;

  // Get the name of what this would be without the 'target' attribute.  This
  // allows us to lookup the version that was emitted when this wasn't a
  // multiversion function.
  std::string NonTargetName =
      getMangledNameImpl(*this, GD, FD, /*OmitMultiVersionMangling=*/true);
  GlobalDecl OtherGD;
  if (lookupRepresentativeDecl(NonTargetName, OtherGD)) {
    assert(OtherGD.getCanonicalDecl()
               .getDecl()
               ->getAsFunction()
               ->isMultiVersion() &&
           "Other GD should now be a multiversioned function");
    // OtherFD is the version of this function that was mangled BEFORE
    // becoming a MultiVersion function.  It potentially needs to be updated.
    const FunctionDecl *OtherFD = OtherGD.getCanonicalDecl()
                                      .getDecl()
                                      ->getAsFunction()
                                      ->getMostRecentDecl();
    std::string OtherName = getMangledNameImpl(*this, OtherGD, OtherFD);
    // This is so that if the initial version was already the 'default'
    // version, we don't try to update it.
    if (OtherName != NonTargetName) {
      // Remove instead of erase, since others may have stored the StringRef
      // to this.
      const auto ExistingRecord = Manglings.find(NonTargetName);
      if (ExistingRecord != std::end(Manglings))
        Manglings.remove(&(*ExistingRecord));
      auto Result = Manglings.insert(std::make_pair(OtherName, OtherGD));
      StringRef OtherNameRef = MangledDeclNames[OtherGD.getCanonicalDecl()] =
          Result.first->first();
      // If this is the current decl is being created, make sure we update the name.
      if (GD.getCanonicalDecl() == OtherGD.getCanonicalDecl())
        CurName = OtherNameRef;
      if (llvm::GlobalValue *Entry = GetGlobalValue(NonTargetName))
        Entry->setName(OtherName);
    }
  }
}

StringRef CodeGenModule::getMangledName(GlobalDecl GD) {
  GlobalDecl CanonicalGD = GD.getCanonicalDecl();

  // Some ABIs don't have constructor variants.  Make sure that base and
  // complete constructors get mangled the same.
  if (const auto *CD = dyn_cast<CXXConstructorDecl>(CanonicalGD.getDecl())) {
    if (!getTarget().getCXXABI().hasConstructorVariants()) {
      CXXCtorType OrigCtorType = GD.getCtorType();
      assert(OrigCtorType == Ctor_Base || OrigCtorType == Ctor_Complete);
      if (OrigCtorType == Ctor_Base)
        CanonicalGD = GlobalDecl(CD, Ctor_Complete);
    }
  }

  // In CUDA/HIP device compilation with -fgpu-rdc, the mangled name of a
  // static device variable depends on whether the variable is referenced by
  // a host or device host function. Therefore the mangled name cannot be
  // cached.
  if (!LangOpts.CUDAIsDevice || !getContext().mayExternalize(GD.getDecl())) {
    auto FoundName = MangledDeclNames.find(CanonicalGD);
    if (FoundName != MangledDeclNames.end())
      return FoundName->second;
  }

  // Keep the first result in the case of a mangling collision.
  const auto *ND = cast<NamedDecl>(GD.getDecl());
  std::string MangledName = getMangledNameImpl(*this, GD, ND);

  // Ensure either we have different ABIs between host and device compilations,
  // says host compilation following MSVC ABI but device compilation follows
  // Itanium C++ ABI or, if they follow the same ABI, kernel names after
  // mangling should be the same after name stubbing. The later checking is
  // very important as the device kernel name being mangled in host-compilation
  // is used to resolve the device binaries to be executed. Inconsistent naming
  // result in undefined behavior. Even though we cannot check that naming
  // directly between host- and device-compilations, the host- and
  // device-mangling in host compilation could help catching certain ones.
  assert(!isa<FunctionDecl>(ND) || !ND->hasAttr<CUDAGlobalAttr>() ||
         getContext().shouldExternalize(ND) || getLangOpts().CUDAIsDevice ||
         (getContext().getAuxTargetInfo() &&
          (getContext().getAuxTargetInfo()->getCXXABI() !=
           getContext().getTargetInfo().getCXXABI())) ||
         getCUDARuntime().getDeviceSideName(ND) ==
             getMangledNameImpl(
                 *this,
                 GD.getWithKernelReferenceKind(KernelReferenceKind::Kernel),
                 ND));

  auto Result = Manglings.insert(std::make_pair(MangledName, GD));
  return MangledDeclNames[CanonicalGD] = Result.first->first();
}

StringRef CodeGenModule::getBlockMangledName(GlobalDecl GD,
                                             const BlockDecl *BD) {
  MangleContext &MangleCtx = getCXXABI().getMangleContext();
  const Decl *D = GD.getDecl();

  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  if (!D)
    MangleCtx.mangleGlobalBlock(BD,
      dyn_cast_or_null<VarDecl>(initializedGlobalDecl.getDecl()), Out);
  else if (const auto *CD = dyn_cast<CXXConstructorDecl>(D))
    MangleCtx.mangleCtorBlock(CD, GD.getCtorType(), BD, Out);
  else if (const auto *DD = dyn_cast<CXXDestructorDecl>(D))
    MangleCtx.mangleDtorBlock(DD, GD.getDtorType(), BD, Out);
  else
    MangleCtx.mangleBlock(cast<DeclContext>(D), BD, Out);

  auto Result = Manglings.insert(std::make_pair(Out.str(), BD));
  return Result.first->first();
}

const GlobalDecl CodeGenModule::getMangledNameDecl(StringRef Name) {
  auto it = MangledDeclNames.begin();
  while (it != MangledDeclNames.end()) {
    if (it->second == Name)
      return it->first;
    it++;
  }
  return GlobalDecl();
}

llvm::GlobalValue *CodeGenModule::GetGlobalValue(StringRef Name) {
  return getModule().getNamedValue(Name);
}

/// AddGlobalCtor - Add a function to the list that will be called before
/// main() runs.
void CodeGenModule::AddGlobalCtor(llvm::Function *Ctor, int Priority,
                                  unsigned LexOrder,
                                  llvm::Constant *AssociatedData) {
  // FIXME: Type coercion of void()* types.
  GlobalCtors.push_back(Structor(Priority, LexOrder, Ctor, AssociatedData));
}

/// AddGlobalDtor - Add a function to the list that will be called
/// when the module is unloaded.
void CodeGenModule::AddGlobalDtor(llvm::Function *Dtor, int Priority,
                                  bool IsDtorAttrFunc) {
  if (CodeGenOpts.RegisterGlobalDtorsWithAtExit &&
      (!getContext().getTargetInfo().getTriple().isOSAIX() || IsDtorAttrFunc)) {
    DtorsUsingAtExit[Priority].push_back(Dtor);
    return;
  }

  // FIXME: Type coercion of void()* types.
  GlobalDtors.push_back(Structor(Priority, ~0U, Dtor, nullptr));
}

void CodeGenModule::EmitCtorList(CtorList &Fns, const char *GlobalName) {
  if (Fns.empty()) return;

  // Ctor function type is void()*.
  llvm::FunctionType* CtorFTy = llvm::FunctionType::get(VoidTy, false);
  llvm::Type *CtorPFTy = llvm::PointerType::get(CtorFTy,
      TheModule.getDataLayout().getProgramAddressSpace());

  // Get the type of a ctor entry, { i32, void ()*, i8* }.
  llvm::StructType *CtorStructTy = llvm::StructType::get(
      Int32Ty, CtorPFTy, VoidPtrTy);

  // Construct the constructor and destructor arrays.
  ConstantInitBuilder builder(*this);
  auto ctors = builder.beginArray(CtorStructTy);
  for (const auto &I : Fns) {
    auto ctor = ctors.beginStruct(CtorStructTy);
    ctor.addInt(Int32Ty, I.Priority);
    ctor.add(I.Initializer);
    if (I.AssociatedData)
      ctor.add(I.AssociatedData);
    else
      ctor.addNullPointer(VoidPtrTy);
    ctor.finishAndAddTo(ctors);
  }

  auto list =
    ctors.finishAndCreateGlobal(GlobalName, getPointerAlign(),
                                /*constant*/ false,
                                llvm::GlobalValue::AppendingLinkage);

  // The LTO linker doesn't seem to like it when we set an alignment
  // on appending variables.  Take it off as a workaround.
  list->setAlignment(std::nullopt);

  Fns.clear();
}

llvm::GlobalValue::LinkageTypes
CodeGenModule::getFunctionLinkage(GlobalDecl GD) {
  const auto *D = cast<FunctionDecl>(GD.getDecl());

  GVALinkage Linkage = getContext().GetGVALinkageForFunction(D);

  if (const auto *Dtor = dyn_cast<CXXDestructorDecl>(D))
    return getCXXABI().getCXXDestructorLinkage(Linkage, Dtor, GD.getDtorType());

  return getLLVMLinkageForDeclarator(D, Linkage);
}

llvm::ConstantInt *CodeGenModule::CreateCrossDsoCfiTypeId(llvm::Metadata *MD) {
  llvm::MDString *MDS = dyn_cast<llvm::MDString>(MD);
  if (!MDS) return nullptr;

  return llvm::ConstantInt::get(Int64Ty, llvm::MD5Hash(MDS->getString()));
}

llvm::ConstantInt *CodeGenModule::CreateKCFITypeId(QualType T) {
  if (auto *FnType = T->getAs<FunctionProtoType>())
    T = getContext().getFunctionType(
        FnType->getReturnType(), FnType->getParamTypes(),
        FnType->getExtProtoInfo().withExceptionSpec(EST_None));

  std::string OutName;
  llvm::raw_string_ostream Out(OutName);
  getCXXABI().getMangleContext().mangleCanonicalTypeName(
      T, Out, getCodeGenOpts().SanitizeCfiICallNormalizeIntegers);

  if (getCodeGenOpts().SanitizeCfiICallNormalizeIntegers)
    Out << ".normalized";

  return llvm::ConstantInt::get(Int32Ty,
                                static_cast<uint32_t>(llvm::xxHash64(OutName)));
}

void CodeGenModule::SetLLVMFunctionAttributes(GlobalDecl GD,
                                              const CGFunctionInfo &Info,
                                              llvm::Function *F, bool IsThunk) {
  unsigned CallingConv;
  llvm::AttributeList PAL;
  ConstructAttributeList(F->getName(), Info, GD, PAL, CallingConv,
                         /*AttrOnCallSite=*/false, IsThunk);
  if (CallingConv == llvm::CallingConv::X86_VectorCall &&
      getTarget().getTriple().isWindowsArm64EC()) {
    SourceLocation Loc;
    if (const Decl *D = GD.getDecl())
      Loc = D->getLocation();

    Error(Loc, "__vectorcall calling convention is not currently supported");
  }
  F->setAttributes(PAL);
  F->setCallingConv(static_cast<llvm::CallingConv::ID>(CallingConv));
}

static void removeImageAccessQualifier(std::string& TyName) {
  std::string ReadOnlyQual("__read_only");
  std::string::size_type ReadOnlyPos = TyName.find(ReadOnlyQual);
  if (ReadOnlyPos != std::string::npos)
    // "+ 1" for the space after access qualifier.
    TyName.erase(ReadOnlyPos, ReadOnlyQual.size() + 1);
  else {
    std::string WriteOnlyQual("__write_only");
    std::string::size_type WriteOnlyPos = TyName.find(WriteOnlyQual);
    if (WriteOnlyPos != std::string::npos)
      TyName.erase(WriteOnlyPos, WriteOnlyQual.size() + 1);
    else {
      std::string ReadWriteQual("__read_write");
      std::string::size_type ReadWritePos = TyName.find(ReadWriteQual);
      if (ReadWritePos != std::string::npos)
        TyName.erase(ReadWritePos, ReadWriteQual.size() + 1);
    }
  }
}

// Returns the address space id that should be produced to the
// kernel_arg_addr_space metadata. This is always fixed to the ids
// as specified in the SPIR 2.0 specification in order to differentiate
// for example in clGetKernelArgInfo() implementation between the address
// spaces with targets without unique mapping to the OpenCL address spaces
// (basically all single AS CPUs).
static unsigned ArgInfoAddressSpace(LangAS AS) {
  switch (AS) {
  case LangAS::opencl_global:
    return 1;
  case LangAS::opencl_constant:
    return 2;
  case LangAS::opencl_local:
    return 3;
  case LangAS::opencl_generic:
    return 4; // Not in SPIR 2.0 specs.
  case LangAS::opencl_global_device:
    return 5;
  case LangAS::opencl_global_host:
    return 6;
  default:
    return 0; // Assume private.
  }
}

void CodeGenModule::GenKernelArgMetadata(llvm::Function *Fn,
                                         const FunctionDecl *FD,
                                         CodeGenFunction *CGF) {
  assert(((FD && CGF) || (!FD && !CGF)) &&
         "Incorrect use - FD and CGF should either be both null or not!");
  // Create MDNodes that represent the kernel arg metadata.
  // Each MDNode is a list in the form of "key", N number of values which is
  // the same number of values as their are kernel arguments.

  const PrintingPolicy &Policy = Context.getPrintingPolicy();

  // MDNode for the kernel argument address space qualifiers.
  SmallVector<llvm::Metadata *, 8> addressQuals;

  // MDNode for the kernel argument access qualifiers (images only).
  SmallVector<llvm::Metadata *, 8> accessQuals;

  // MDNode for the kernel argument type names.
  SmallVector<llvm::Metadata *, 8> argTypeNames;

  // MDNode for the kernel argument base type names.
  SmallVector<llvm::Metadata *, 8> argBaseTypeNames;

  // MDNode for the kernel argument type qualifiers.
  SmallVector<llvm::Metadata *, 8> argTypeQuals;

  // MDNode for the kernel argument names.
  SmallVector<llvm::Metadata *, 8> argNames;

  if (FD && CGF)
    for (unsigned i = 0, e = FD->getNumParams(); i != e; ++i) {
      const ParmVarDecl *parm = FD->getParamDecl(i);
      // Get argument name.
      argNames.push_back(llvm::MDString::get(VMContext, parm->getName()));

      if (!getLangOpts().OpenCL)
        continue;
      QualType ty = parm->getType();
      std::string typeQuals;

      // Get image and pipe access qualifier:
      if (ty->isImageType() || ty->isPipeType()) {
        const Decl *PDecl = parm;
        if (const auto *TD = ty->getAs<TypedefType>())
          PDecl = TD->getDecl();
        const OpenCLAccessAttr *A = PDecl->getAttr<OpenCLAccessAttr>();
        if (A && A->isWriteOnly())
          accessQuals.push_back(llvm::MDString::get(VMContext, "write_only"));
        else if (A && A->isReadWrite())
          accessQuals.push_back(llvm::MDString::get(VMContext, "read_write"));
        else
          accessQuals.push_back(llvm::MDString::get(VMContext, "read_only"));
      } else
        accessQuals.push_back(llvm::MDString::get(VMContext, "none"));

      auto getTypeSpelling = [&](QualType Ty) {
        auto typeName = Ty.getUnqualifiedType().getAsString(Policy);

        if (Ty.isCanonical()) {
          StringRef typeNameRef = typeName;
          // Turn "unsigned type" to "utype"
          if (typeNameRef.consume_front("unsigned "))
            return std::string("u") + typeNameRef.str();
          if (typeNameRef.consume_front("signed "))
            return typeNameRef.str();
        }

        return typeName;
      };

      if (ty->isPointerType()) {
        QualType pointeeTy = ty->getPointeeType();

        // Get address qualifier.
        addressQuals.push_back(
            llvm::ConstantAsMetadata::get(CGF->Builder.getInt32(
                ArgInfoAddressSpace(pointeeTy.getAddressSpace()))));

        // Get argument type name.
        std::string typeName = getTypeSpelling(pointeeTy) + "*";
        std::string baseTypeName =
            getTypeSpelling(pointeeTy.getCanonicalType()) + "*";
        argTypeNames.push_back(llvm::MDString::get(VMContext, typeName));
        argBaseTypeNames.push_back(
            llvm::MDString::get(VMContext, baseTypeName));

        // Get argument type qualifiers:
        if (ty.isRestrictQualified())
          typeQuals = "restrict";
        if (pointeeTy.isConstQualified() ||
            (pointeeTy.getAddressSpace() == LangAS::opencl_constant))
          typeQuals += typeQuals.empty() ? "const" : " const";
        if (pointeeTy.isVolatileQualified())
          typeQuals += typeQuals.empty() ? "volatile" : " volatile";
      } else {
        uint32_t AddrSpc = 0;
        bool isPipe = ty->isPipeType();
        if (ty->isImageType() || isPipe)
          AddrSpc = ArgInfoAddressSpace(LangAS::opencl_global);

        addressQuals.push_back(
            llvm::ConstantAsMetadata::get(CGF->Builder.getInt32(AddrSpc)));

        // Get argument type name.
        ty = isPipe ? ty->castAs<PipeType>()->getElementType() : ty;
        std::string typeName = getTypeSpelling(ty);
        std::string baseTypeName = getTypeSpelling(ty.getCanonicalType());

        // Remove access qualifiers on images
        // (as they are inseparable from type in clang implementation,
        // but OpenCL spec provides a special query to get access qualifier
        // via clGetKernelArgInfo with CL_KERNEL_ARG_ACCESS_QUALIFIER):
        if (ty->isImageType()) {
          removeImageAccessQualifier(typeName);
          removeImageAccessQualifier(baseTypeName);
        }

        argTypeNames.push_back(llvm::MDString::get(VMContext, typeName));
        argBaseTypeNames.push_back(
            llvm::MDString::get(VMContext, baseTypeName));

        if (isPipe)
          typeQuals = "pipe";
      }
      argTypeQuals.push_back(llvm::MDString::get(VMContext, typeQuals));
    }

  if (getLangOpts().OpenCL) {
    Fn->setMetadata("kernel_arg_addr_space",
                    llvm::MDNode::get(VMContext, addressQuals));
    Fn->setMetadata("kernel_arg_access_qual",
                    llvm::MDNode::get(VMContext, accessQuals));
    Fn->setMetadata("kernel_arg_type",
                    llvm::MDNode::get(VMContext, argTypeNames));
    Fn->setMetadata("kernel_arg_base_type",
                    llvm::MDNode::get(VMContext, argBaseTypeNames));
    Fn->setMetadata("kernel_arg_type_qual",
                    llvm::MDNode::get(VMContext, argTypeQuals));
  }
  if (getCodeGenOpts().EmitOpenCLArgMetadata ||
      getCodeGenOpts().HIPSaveKernelArgName)
    Fn->setMetadata("kernel_arg_name",
                    llvm::MDNode::get(VMContext, argNames));
}

/// Determines whether the language options require us to model
/// unwind exceptions.  We treat -fexceptions as mandating this
/// except under the fragile ObjC ABI with only ObjC exceptions
/// enabled.  This means, for example, that C with -fexceptions
/// enables this.
static bool hasUnwindExceptions(const LangOptions &LangOpts) {
  // If exceptions are completely disabled, obviously this is false.
  if (!LangOpts.Exceptions) return false;

  // If C++ exceptions are enabled, this is true.
  if (LangOpts.CXXExceptions) return true;

  // If ObjC exceptions are enabled, this depends on the ABI.
  if (LangOpts.ObjCExceptions) {
    return LangOpts.ObjCRuntime.hasUnwindExceptions();
  }

  return true;
}

static bool requiresMemberFunctionPointerTypeMetadata(CodeGenModule &CGM,
                                                      const CXXMethodDecl *MD) {
  // Check that the type metadata can ever actually be used by a call.
  if (!CGM.getCodeGenOpts().LTOUnit ||
      !CGM.HasHiddenLTOVisibility(MD->getParent()))
    return false;

  // Only functions whose address can be taken with a member function pointer
  // need this sort of type metadata.
  return MD->isImplicitObjectMemberFunction() && !MD->isVirtual() &&
         !isa<CXXConstructorDecl, CXXDestructorDecl>(MD);
}

SmallVector<const CXXRecordDecl *, 0>
CodeGenModule::getMostBaseClasses(const CXXRecordDecl *RD) {
  llvm::SetVector<const CXXRecordDecl *> MostBases;

  std::function<void (const CXXRecordDecl *)> CollectMostBases;
  CollectMostBases = [&](const CXXRecordDecl *RD) {
    if (RD->getNumBases() == 0)
      MostBases.insert(RD);
    for (const CXXBaseSpecifier &B : RD->bases())
      CollectMostBases(B.getType()->getAsCXXRecordDecl());
  };
  CollectMostBases(RD);
  return MostBases.takeVector();
}

void CodeGenModule::SetLLVMFunctionAttributesForDefinition(const Decl *D,
                                                           llvm::Function *F) {
  llvm::AttrBuilder B(F->getContext());

  if ((!D || !D->hasAttr<NoUwtableAttr>()) && CodeGenOpts.UnwindTables)
    B.addUWTableAttr(llvm::UWTableKind(CodeGenOpts.UnwindTables));

  if (CodeGenOpts.StackClashProtector)
    B.addAttribute("probe-stack", "inline-asm");

  if (CodeGenOpts.StackProbeSize && CodeGenOpts.StackProbeSize != 4096)
    B.addAttribute("stack-probe-size",
                   std::to_string(CodeGenOpts.StackProbeSize));

  if (!hasUnwindExceptions(LangOpts))
    B.addAttribute(llvm::Attribute::NoUnwind);

  if (D && D->hasAttr<NoStackProtectorAttr>())
    ; // Do nothing.
  else if (D && D->hasAttr<StrictGuardStackCheckAttr>() &&
           isStackProtectorOn(LangOpts, getTriple(), LangOptions::SSPOn))
    B.addAttribute(llvm::Attribute::StackProtectStrong);
  else if (isStackProtectorOn(LangOpts, getTriple(), LangOptions::SSPOn))
    B.addAttribute(llvm::Attribute::StackProtect);
  else if (isStackProtectorOn(LangOpts, getTriple(), LangOptions::SSPStrong))
    B.addAttribute(llvm::Attribute::StackProtectStrong);
  else if (isStackProtectorOn(LangOpts, getTriple(), LangOptions::SSPReq))
    B.addAttribute(llvm::Attribute::StackProtectReq);

  if (!D) {
    // If we don't have a declaration to control inlining, the function isn't
    // explicitly marked as alwaysinline for semantic reasons, and inlining is
    // disabled, mark the function as noinline.
    if (!F->hasFnAttribute(llvm::Attribute::AlwaysInline) &&
        CodeGenOpts.getInlining() == CodeGenOptions::OnlyAlwaysInlining)
      B.addAttribute(llvm::Attribute::NoInline);

    F->addFnAttrs(B);
    return;
  }

  // Handle SME attributes that apply to function definitions,
  // rather than to function prototypes.
  if (D->hasAttr<ArmLocallyStreamingAttr>())
    B.addAttribute("aarch64_pstate_sm_body");

  if (auto *Attr = D->getAttr<ArmNewAttr>()) {
    if (Attr->isNewZA())
      B.addAttribute("aarch64_new_za");
    if (Attr->isNewZT0())
      B.addAttribute("aarch64_new_zt0");
  }

  // Track whether we need to add the optnone LLVM attribute,
  // starting with the default for this optimization level.
  bool ShouldAddOptNone =
      !CodeGenOpts.DisableO0ImplyOptNone && CodeGenOpts.OptimizationLevel == 0;
  // We can't add optnone in the following cases, it won't pass the verifier.
  ShouldAddOptNone &= !D->hasAttr<MinSizeAttr>();
  ShouldAddOptNone &= !D->hasAttr<AlwaysInlineAttr>();

  // Add optnone, but do so only if the function isn't always_inline.
  if ((ShouldAddOptNone || D->hasAttr<OptimizeNoneAttr>()) &&
      !F->hasFnAttribute(llvm::Attribute::AlwaysInline)) {
    B.addAttribute(llvm::Attribute::OptimizeNone);

    // OptimizeNone implies noinline; we should not be inlining such functions.
    B.addAttribute(llvm::Attribute::NoInline);

    // We still need to handle naked functions even though optnone subsumes
    // much of their semantics.
    if (D->hasAttr<NakedAttr>())
      B.addAttribute(llvm::Attribute::Naked);

    // OptimizeNone wins over OptimizeForSize and MinSize.
    F->removeFnAttr(llvm::Attribute::OptimizeForSize);
    F->removeFnAttr(llvm::Attribute::MinSize);
  } else if (D->hasAttr<NakedAttr>()) {
    // Naked implies noinline: we should not be inlining such functions.
    B.addAttribute(llvm::Attribute::Naked);
    B.addAttribute(llvm::Attribute::NoInline);
  } else if (D->hasAttr<NoDuplicateAttr>()) {
    B.addAttribute(llvm::Attribute::NoDuplicate);
  } else if (D->hasAttr<NoInlineAttr>() && !F->hasFnAttribute(llvm::Attribute::AlwaysInline)) {
    // Add noinline if the function isn't always_inline.
    B.addAttribute(llvm::Attribute::NoInline);
  } else if (D->hasAttr<AlwaysInlineAttr>() &&
             !F->hasFnAttribute(llvm::Attribute::NoInline)) {
    // (noinline wins over always_inline, and we can't specify both in IR)
    B.addAttribute(llvm::Attribute::AlwaysInline);
  } else if (CodeGenOpts.getInlining() == CodeGenOptions::OnlyAlwaysInlining) {
    // If we're not inlining, then force everything that isn't always_inline to
    // carry an explicit noinline attribute.
    if (!F->hasFnAttribute(llvm::Attribute::AlwaysInline))
      B.addAttribute(llvm::Attribute::NoInline);
  } else {
    // Otherwise, propagate the inline hint attribute and potentially use its
    // absence to mark things as noinline.
    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      // Search function and template pattern redeclarations for inline.
      auto CheckForInline = [](const FunctionDecl *FD) {
        auto CheckRedeclForInline = [](const FunctionDecl *Redecl) {
          return Redecl->isInlineSpecified();
        };
        if (any_of(FD->redecls(), CheckRedeclForInline))
          return true;
        const FunctionDecl *Pattern = FD->getTemplateInstantiationPattern();
        if (!Pattern)
          return false;
        return any_of(Pattern->redecls(), CheckRedeclForInline);
      };
      if (CheckForInline(FD)) {
        B.addAttribute(llvm::Attribute::InlineHint);
      } else if (CodeGenOpts.getInlining() ==
                     CodeGenOptions::OnlyHintInlining &&
                 !FD->isInlined() &&
                 !F->hasFnAttribute(llvm::Attribute::AlwaysInline)) {
        B.addAttribute(llvm::Attribute::NoInline);
      }
    }
  }

  // Add other optimization related attributes if we are optimizing this
  // function.
  if (!D->hasAttr<OptimizeNoneAttr>()) {
    if (D->hasAttr<ColdAttr>()) {
      if (!ShouldAddOptNone)
        B.addAttribute(llvm::Attribute::OptimizeForSize);
      B.addAttribute(llvm::Attribute::Cold);
    }
    if (D->hasAttr<HotAttr>())
      B.addAttribute(llvm::Attribute::Hot);
    if (D->hasAttr<MinSizeAttr>())
      B.addAttribute(llvm::Attribute::MinSize);
  }

  F->addFnAttrs(B);

  unsigned alignment = D->getMaxAlignment() / Context.getCharWidth();
  if (alignment)
    F->setAlignment(llvm::Align(alignment));

  if (!D->hasAttr<AlignedAttr>())
    if (LangOpts.FunctionAlignment)
      F->setAlignment(llvm::Align(1ull << LangOpts.FunctionAlignment));

  // Some C++ ABIs require 2-byte alignment for member functions, in order to
  // reserve a bit for differentiating between virtual and non-virtual member
  // functions. If the current target's C++ ABI requires this and this is a
  // member function, set its alignment accordingly.
  if (getTarget().getCXXABI().areMemberFunctionsAligned()) {
    if (isa<CXXMethodDecl>(D) && F->getPointerAlignment(getDataLayout()) < 2)
      F->setAlignment(std::max(llvm::Align(2), F->getAlign().valueOrOne()));
  }

  // In the cross-dso CFI mode with canonical jump tables, we want !type
  // attributes on definitions only.
  if (CodeGenOpts.SanitizeCfiCrossDso &&
      CodeGenOpts.SanitizeCfiCanonicalJumpTables) {
    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      // Skip available_externally functions. They won't be codegen'ed in the
      // current module anyway.
      if (getContext().GetGVALinkageForFunction(FD) != GVA_AvailableExternally)
        CreateFunctionTypeMetadataForIcall(FD, F);
    }
  }

  // Emit type metadata on member functions for member function pointer checks.
  // These are only ever necessary on definitions; we're guaranteed that the
  // definition will be present in the LTO unit as a result of LTO visibility.
  auto *MD = dyn_cast<CXXMethodDecl>(D);
  if (MD && requiresMemberFunctionPointerTypeMetadata(*this, MD)) {
    for (const CXXRecordDecl *Base : getMostBaseClasses(MD->getParent())) {
      llvm::Metadata *Id =
          CreateMetadataIdentifierForType(Context.getMemberPointerType(
              MD->getType(), Context.getRecordType(Base).getTypePtr()));
      F->addTypeMetadata(0, Id);
    }
  }
}

void CodeGenModule::SetCommonAttributes(GlobalDecl GD, llvm::GlobalValue *GV) {
  const Decl *D = GD.getDecl();
  if (isa_and_nonnull<NamedDecl>(D))
    setGVProperties(GV, GD);
  else
    GV->setVisibility(llvm::GlobalValue::DefaultVisibility);

  if (D && D->hasAttr<UsedAttr>())
    addUsedOrCompilerUsedGlobal(GV);

  if (const auto *VD = dyn_cast_if_present<VarDecl>(D);
      VD &&
      ((CodeGenOpts.KeepPersistentStorageVariables &&
        (VD->getStorageDuration() == SD_Static ||
         VD->getStorageDuration() == SD_Thread)) ||
       (CodeGenOpts.KeepStaticConsts && VD->getStorageDuration() == SD_Static &&
        VD->getType().isConstQualified())))
    addUsedOrCompilerUsedGlobal(GV);
}

bool CodeGenModule::GetCPUAndFeaturesAttributes(GlobalDecl GD,
                                                llvm::AttrBuilder &Attrs,
                                                bool SetTargetFeatures) {
  // Add target-cpu and target-features attributes to functions. If
  // we have a decl for the function and it has a target attribute then
  // parse that and add it to the feature set.
  StringRef TargetCPU = getTarget().getTargetOpts().CPU;
  StringRef TuneCPU = getTarget().getTargetOpts().TuneCPU;
  std::vector<std::string> Features;
  const auto *FD = dyn_cast_or_null<FunctionDecl>(GD.getDecl());
  FD = FD ? FD->getMostRecentDecl() : FD;
  const auto *TD = FD ? FD->getAttr<TargetAttr>() : nullptr;
  const auto *TV = FD ? FD->getAttr<TargetVersionAttr>() : nullptr;
  assert((!TD || !TV) && "both target_version and target specified");
  const auto *SD = FD ? FD->getAttr<CPUSpecificAttr>() : nullptr;
  const auto *TC = FD ? FD->getAttr<TargetClonesAttr>() : nullptr;
  bool AddedAttr = false;
  if (TD || TV || SD || TC) {
    llvm::StringMap<bool> FeatureMap;
    getContext().getFunctionFeatureMap(FeatureMap, GD);

    // Produce the canonical string for this set of features.
    for (const llvm::StringMap<bool>::value_type &Entry : FeatureMap)
      Features.push_back((Entry.getValue() ? "+" : "-") + Entry.getKey().str());

    // Now add the target-cpu and target-features to the function.
    // While we populated the feature map above, we still need to
    // get and parse the target attribute so we can get the cpu for
    // the function.
    if (TD) {
      ParsedTargetAttr ParsedAttr =
          Target.parseTargetAttr(TD->getFeaturesStr());
      if (!ParsedAttr.CPU.empty() &&
          getTarget().isValidCPUName(ParsedAttr.CPU)) {
        TargetCPU = ParsedAttr.CPU;
        TuneCPU = ""; // Clear the tune CPU.
      }
      if (!ParsedAttr.Tune.empty() &&
          getTarget().isValidCPUName(ParsedAttr.Tune))
        TuneCPU = ParsedAttr.Tune;
    }

    if (SD) {
      // Apply the given CPU name as the 'tune-cpu' so that the optimizer can
      // favor this processor.
      TuneCPU = SD->getCPUName(GD.getMultiVersionIndex())->getName();
    }
  } else {
    // Otherwise just add the existing target cpu and target features to the
    // function.
    Features = getTarget().getTargetOpts().Features;
  }

  if (!TargetCPU.empty()) {
    Attrs.addAttribute("target-cpu", TargetCPU);
    AddedAttr = true;
  }
  if (!TuneCPU.empty()) {
    Attrs.addAttribute("tune-cpu", TuneCPU);
    AddedAttr = true;
  }
  if (!Features.empty() && SetTargetFeatures) {
    llvm::erase_if(Features, [&](const std::string& F) {
       return getTarget().isReadOnlyFeature(F.substr(1));
    });
    llvm::sort(Features);
    Attrs.addAttribute("target-features", llvm::join(Features, ","));
    AddedAttr = true;
  }

  return AddedAttr;
}

void CodeGenModule::setNonAliasAttributes(GlobalDecl GD,
                                          llvm::GlobalObject *GO) {
  const Decl *D = GD.getDecl();
  SetCommonAttributes(GD, GO);

  if (D) {
    if (auto *GV = dyn_cast<llvm::GlobalVariable>(GO)) {
      if (D->hasAttr<RetainAttr>())
        addUsedGlobal(GV);
      if (auto *SA = D->getAttr<PragmaClangBSSSectionAttr>())
        GV->addAttribute("bss-section", SA->getName());
      if (auto *SA = D->getAttr<PragmaClangDataSectionAttr>())
        GV->addAttribute("data-section", SA->getName());
      if (auto *SA = D->getAttr<PragmaClangRodataSectionAttr>())
        GV->addAttribute("rodata-section", SA->getName());
      if (auto *SA = D->getAttr<PragmaClangRelroSectionAttr>())
        GV->addAttribute("relro-section", SA->getName());
    }

    if (auto *F = dyn_cast<llvm::Function>(GO)) {
      if (D->hasAttr<RetainAttr>())
        addUsedGlobal(F);
      if (auto *SA = D->getAttr<PragmaClangTextSectionAttr>())
        if (!D->getAttr<SectionAttr>())
          F->setSection(SA->getName());

      llvm::AttrBuilder Attrs(F->getContext());
      if (GetCPUAndFeaturesAttributes(GD, Attrs)) {
        // We know that GetCPUAndFeaturesAttributes will always have the
        // newest set, since it has the newest possible FunctionDecl, so the
        // new ones should replace the old.
        llvm::AttributeMask RemoveAttrs;
        RemoveAttrs.addAttribute("target-cpu");
        RemoveAttrs.addAttribute("target-features");
        RemoveAttrs.addAttribute("tune-cpu");
        F->removeFnAttrs(RemoveAttrs);
        F->addFnAttrs(Attrs);
      }
    }

    if (const auto *CSA = D->getAttr<CodeSegAttr>())
      GO->setSection(CSA->getName());
    else if (const auto *SA = D->getAttr<SectionAttr>())
      GO->setSection(SA->getName());
  }

  getTargetCodeGenInfo().setTargetAttributes(D, GO, *this);
}

void CodeGenModule::SetInternalFunctionAttributes(GlobalDecl GD,
                                                  llvm::Function *F,
                                                  const CGFunctionInfo &FI) {
  const Decl *D = GD.getDecl();
  SetLLVMFunctionAttributes(GD, FI, F, /*IsThunk=*/false);
  SetLLVMFunctionAttributesForDefinition(D, F);

  F->setLinkage(llvm::Function::InternalLinkage);

  setNonAliasAttributes(GD, F);
}

static void setLinkageForGV(llvm::GlobalValue *GV, const NamedDecl *ND) {
  // Set linkage and visibility in case we never see a definition.
  LinkageInfo LV = ND->getLinkageAndVisibility();
  // Don't set internal linkage on declarations.
  // "extern_weak" is overloaded in LLVM; we probably should have
  // separate linkage types for this.
  if (isExternallyVisible(LV.getLinkage()) &&
      (ND->hasAttr<WeakAttr>() || ND->isWeakImported()))
    GV->setLinkage(llvm::GlobalValue::ExternalWeakLinkage);
}

void CodeGenModule::CreateFunctionTypeMetadataForIcall(const FunctionDecl *FD,
                                                       llvm::Function *F) {
  // Only if we are checking indirect calls.
  if (!LangOpts.Sanitize.has(SanitizerKind::CFIICall))
    return;

  // Non-static class methods are handled via vtable or member function pointer
  // checks elsewhere.
  if (isa<CXXMethodDecl>(FD) && !cast<CXXMethodDecl>(FD)->isStatic())
    return;

  llvm::Metadata *MD = CreateMetadataIdentifierForType(FD->getType());
  F->addTypeMetadata(0, MD);
  F->addTypeMetadata(0, CreateMetadataIdentifierGeneralized(FD->getType()));

  // Emit a hash-based bit set entry for cross-DSO calls.
  if (CodeGenOpts.SanitizeCfiCrossDso)
    if (auto CrossDsoTypeId = CreateCrossDsoCfiTypeId(MD))
      F->addTypeMetadata(0, llvm::ConstantAsMetadata::get(CrossDsoTypeId));
}

void CodeGenModule::setKCFIType(const FunctionDecl *FD, llvm::Function *F) {
  llvm::LLVMContext &Ctx = F->getContext();
  llvm::MDBuilder MDB(Ctx);
  F->setMetadata(llvm::LLVMContext::MD_kcfi_type,
                 llvm::MDNode::get(
                     Ctx, MDB.createConstant(CreateKCFITypeId(FD->getType()))));
}

static bool allowKCFIIdentifier(StringRef Name) {
  // KCFI type identifier constants are only necessary for external assembly
  // functions, which means it's safe to skip unusual names. Subset of
  // MCAsmInfo::isAcceptableChar() and MCAsmInfoXCOFF::isAcceptableChar().
  return llvm::all_of(Name, [](const char &C) {
    return llvm::isAlnum(C) || C == '_' || C == '.';
  });
}

void CodeGenModule::finalizeKCFITypes() {
  llvm::Module &M = getModule();
  for (auto &F : M.functions()) {
    // Remove KCFI type metadata from non-address-taken local functions.
    bool AddressTaken = F.hasAddressTaken();
    if (!AddressTaken && F.hasLocalLinkage())
      F.eraseMetadata(llvm::LLVMContext::MD_kcfi_type);

    // Generate a constant with the expected KCFI type identifier for all
    // address-taken function declarations to support annotating indirectly
    // called assembly functions.
    if (!AddressTaken || !F.isDeclaration())
      continue;

    const llvm::ConstantInt *Type;
    if (const llvm::MDNode *MD = F.getMetadata(llvm::LLVMContext::MD_kcfi_type))
      Type = llvm::mdconst::extract<llvm::ConstantInt>(MD->getOperand(0));
    else
      continue;

    StringRef Name = F.getName();
    if (!allowKCFIIdentifier(Name))
      continue;

    std::string Asm = (".weak __kcfi_typeid_" + Name + "\n.set __kcfi_typeid_" +
                       Name + ", " + Twine(Type->getZExtValue()) + "\n")
                          .str();
    M.appendModuleInlineAsm(Asm);
  }
}

void CodeGenModule::SetFunctionAttributes(GlobalDecl GD, llvm::Function *F,
                                          bool IsIncompleteFunction,
                                          bool IsThunk) {

  if (llvm::Intrinsic::ID IID = F->getIntrinsicID()) {
    // If this is an intrinsic function, set the function's attributes
    // to the intrinsic's attributes.
    F->setAttributes(llvm::Intrinsic::getAttributes(getLLVMContext(), IID));
    return;
  }

  const auto *FD = cast<FunctionDecl>(GD.getDecl());

  if (!IsIncompleteFunction)
    SetLLVMFunctionAttributes(GD, getTypes().arrangeGlobalDeclaration(GD), F,
                              IsThunk);

  // Add the Returned attribute for "this", except for iOS 5 and earlier
  // where substantial code, including the libstdc++ dylib, was compiled with
  // GCC and does not actually return "this".
  if (!IsThunk && getCXXABI().HasThisReturn(GD) &&
      !(getTriple().isiOS() && getTriple().isOSVersionLT(6))) {
    assert(!F->arg_empty() &&
           F->arg_begin()->getType()
             ->canLosslesslyBitCastTo(F->getReturnType()) &&
           "unexpected this return");
    F->addParamAttr(0, llvm::Attribute::Returned);
  }

  // Only a few attributes are set on declarations; these may later be
  // overridden by a definition.

  setLinkageForGV(F, FD);
  setGVProperties(F, FD);

  // Setup target-specific attributes.
  if (!IsIncompleteFunction && F->isDeclaration())
    getTargetCodeGenInfo().setTargetAttributes(FD, F, *this);

  if (const auto *CSA = FD->getAttr<CodeSegAttr>())
    F->setSection(CSA->getName());
  else if (const auto *SA = FD->getAttr<SectionAttr>())
     F->setSection(SA->getName());

  if (const auto *EA = FD->getAttr<ErrorAttr>()) {
    if (EA->isError())
      F->addFnAttr("dontcall-error", EA->getUserDiagnostic());
    else if (EA->isWarning())
      F->addFnAttr("dontcall-warn", EA->getUserDiagnostic());
  }

  // If we plan on emitting this inline builtin, we can't treat it as a builtin.
  if (FD->isInlineBuiltinDeclaration()) {
    const FunctionDecl *FDBody;
    bool HasBody = FD->hasBody(FDBody);
    (void)HasBody;
    assert(HasBody && "Inline builtin declarations should always have an "
                      "available body!");
    if (shouldEmitFunction(FDBody))
      F->addFnAttr(llvm::Attribute::NoBuiltin);
  }

  if (FD->isReplaceableGlobalAllocationFunction()) {
    // A replaceable global allocation function does not act like a builtin by
    // default, only if it is invoked by a new-expression or delete-expression.
    F->addFnAttr(llvm::Attribute::NoBuiltin);
  }

  if (isa<CXXConstructorDecl>(FD) || isa<CXXDestructorDecl>(FD))
    F->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  else if (const auto *MD = dyn_cast<CXXMethodDecl>(FD))
    if (MD->isVirtual())
      F->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // Don't emit entries for function declarations in the cross-DSO mode. This
  // is handled with better precision by the receiving DSO. But if jump tables
  // are non-canonical then we need type metadata in order to produce the local
  // jump table.
  if (!CodeGenOpts.SanitizeCfiCrossDso ||
      !CodeGenOpts.SanitizeCfiCanonicalJumpTables)
    CreateFunctionTypeMetadataForIcall(FD, F);

  if (LangOpts.Sanitize.has(SanitizerKind::KCFI))
    setKCFIType(FD, F);

  if (getLangOpts().OpenMP && FD->hasAttr<OMPDeclareSimdDeclAttr>())
    getOpenMPRuntime().emitDeclareSimdFunction(FD, F);

  if (CodeGenOpts.InlineMaxStackSize != UINT_MAX)
    F->addFnAttr("inline-max-stacksize", llvm::utostr(CodeGenOpts.InlineMaxStackSize));

  if (const auto *CB = FD->getAttr<CallbackAttr>()) {
    // Annotate the callback behavior as metadata:
    //  - The callback callee (as argument number).
    //  - The callback payloads (as argument numbers).
    llvm::LLVMContext &Ctx = F->getContext();
    llvm::MDBuilder MDB(Ctx);

    // The payload indices are all but the first one in the encoding. The first
    // identifies the callback callee.
    int CalleeIdx = *CB->encoding_begin();
    ArrayRef<int> PayloadIndices(CB->encoding_begin() + 1, CB->encoding_end());
    F->addMetadata(llvm::LLVMContext::MD_callback,
                   *llvm::MDNode::get(Ctx, {MDB.createCallbackEncoding(
                                               CalleeIdx, PayloadIndices,
                                               /* VarArgsArePassed */ false)}));
  }
}

void CodeGenModule::addUsedGlobal(llvm::GlobalValue *GV) {
  assert((isa<llvm::Function>(GV) || !GV->isDeclaration()) &&
         "Only globals with definition can force usage.");
  LLVMUsed.emplace_back(GV);
}

void CodeGenModule::addCompilerUsedGlobal(llvm::GlobalValue *GV) {
  assert(!GV->isDeclaration() &&
         "Only globals with definition can force usage.");
  LLVMCompilerUsed.emplace_back(GV);
}

void CodeGenModule::addUsedOrCompilerUsedGlobal(llvm::GlobalValue *GV) {
  assert((isa<llvm::Function>(GV) || !GV->isDeclaration()) &&
         "Only globals with definition can force usage.");
  if (getTriple().isOSBinFormatELF())
    LLVMCompilerUsed.emplace_back(GV);
  else
    LLVMUsed.emplace_back(GV);
}

static void emitUsed(CodeGenModule &CGM, StringRef Name,
                     std::vector<llvm::WeakTrackingVH> &List) {
  // Don't create llvm.used if there is no need.
  if (List.empty())
    return;

  // Convert List to what ConstantArray needs.
  SmallVector<llvm::Constant*, 8> UsedArray;
  UsedArray.resize(List.size());
  for (unsigned i = 0, e = List.size(); i != e; ++i) {
    UsedArray[i] =
        llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(
            cast<llvm::Constant>(&*List[i]), CGM.Int8PtrTy);
  }

  if (UsedArray.empty())
    return;
  llvm::ArrayType *ATy = llvm::ArrayType::get(CGM.Int8PtrTy, UsedArray.size());

  auto *GV = new llvm::GlobalVariable(
      CGM.getModule(), ATy, false, llvm::GlobalValue::AppendingLinkage,
      llvm::ConstantArray::get(ATy, UsedArray), Name);

  GV->setSection("llvm.metadata");
}

void CodeGenModule::emitLLVMUsed() {
  emitUsed(*this, "llvm.used", LLVMUsed);
  emitUsed(*this, "llvm.compiler.used", LLVMCompilerUsed);
}

void CodeGenModule::AppendLinkerOptions(StringRef Opts) {
  auto *MDOpts = llvm::MDString::get(getLLVMContext(), Opts);
  LinkerOptionsMetadata.push_back(llvm::MDNode::get(getLLVMContext(), MDOpts));
}

void CodeGenModule::AddDetectMismatch(StringRef Name, StringRef Value) {
  llvm::SmallString<32> Opt;
  getTargetCodeGenInfo().getDetectMismatchOption(Name, Value, Opt);
  if (Opt.empty())
    return;
  auto *MDOpts = llvm::MDString::get(getLLVMContext(), Opt);
  LinkerOptionsMetadata.push_back(llvm::MDNode::get(getLLVMContext(), MDOpts));
}

void CodeGenModule::AddDependentLib(StringRef Lib) {
  auto &C = getLLVMContext();
  if (getTarget().getTriple().isOSBinFormatELF()) {
      ELFDependentLibraries.push_back(
        llvm::MDNode::get(C, llvm::MDString::get(C, Lib)));
    return;
  }

  llvm::SmallString<24> Opt;
  getTargetCodeGenInfo().getDependentLibraryOption(Lib, Opt);
  auto *MDOpts = llvm::MDString::get(getLLVMContext(), Opt);
  LinkerOptionsMetadata.push_back(llvm::MDNode::get(C, MDOpts));
}

/// Add link options implied by the given module, including modules
/// it depends on, using a postorder walk.
static void addLinkOptionsPostorder(CodeGenModule &CGM, Module *Mod,
                                    SmallVectorImpl<llvm::MDNode *> &Metadata,
                                    llvm::SmallPtrSet<Module *, 16> &Visited) {
  // Import this module's parent.
  if (Mod->Parent && Visited.insert(Mod->Parent).second) {
    addLinkOptionsPostorder(CGM, Mod->Parent, Metadata, Visited);
  }

  // Import this module's dependencies.
  for (Module *Import : llvm::reverse(Mod->Imports)) {
    if (Visited.insert(Import).second)
      addLinkOptionsPostorder(CGM, Import, Metadata, Visited);
  }

  // Add linker options to link against the libraries/frameworks
  // described by this module.
  llvm::LLVMContext &Context = CGM.getLLVMContext();
  bool IsELF = CGM.getTarget().getTriple().isOSBinFormatELF();

  // For modules that use export_as for linking, use that module
  // name instead.
  if (Mod->UseExportAsModuleLinkName)
    return;

  for (const Module::LinkLibrary &LL : llvm::reverse(Mod->LinkLibraries)) {
    // Link against a framework.  Frameworks are currently Darwin only, so we
    // don't to ask TargetCodeGenInfo for the spelling of the linker option.
    if (LL.IsFramework) {
      llvm::Metadata *Args[2] = {llvm::MDString::get(Context, "-framework"),
                                 llvm::MDString::get(Context, LL.Library)};

      Metadata.push_back(llvm::MDNode::get(Context, Args));
      continue;
    }

    // Link against a library.
    if (IsELF) {
      llvm::Metadata *Args[2] = {
          llvm::MDString::get(Context, "lib"),
          llvm::MDString::get(Context, LL.Library),
      };
      Metadata.push_back(llvm::MDNode::get(Context, Args));
    } else {
      llvm::SmallString<24> Opt;
      CGM.getTargetCodeGenInfo().getDependentLibraryOption(LL.Library, Opt);
      auto *OptString = llvm::MDString::get(Context, Opt);
      Metadata.push_back(llvm::MDNode::get(Context, OptString));
    }
  }
}

void CodeGenModule::EmitModuleInitializers(clang::Module *Primary) {
  assert(Primary->isNamedModuleUnit() &&
         "We should only emit module initializers for named modules.");

  // Emit the initializers in the order that sub-modules appear in the
  // source, first Global Module Fragments, if present.
  if (auto GMF = Primary->getGlobalModuleFragment()) {
    for (Decl *D : getContext().getModuleInitializers(GMF)) {
      if (isa<ImportDecl>(D))
        continue;
      assert(isa<VarDecl>(D) && "GMF initializer decl is not a var?");
      EmitTopLevelDecl(D);
    }
  }
  // Second any associated with the module, itself.
  for (Decl *D : getContext().getModuleInitializers(Primary)) {
    // Skip import decls, the inits for those are called explicitly.
    if (isa<ImportDecl>(D))
      continue;
    EmitTopLevelDecl(D);
  }
  // Third any associated with the Privat eMOdule Fragment, if present.
  if (auto PMF = Primary->getPrivateModuleFragment()) {
    for (Decl *D : getContext().getModuleInitializers(PMF)) {
      // Skip import decls, the inits for those are called explicitly.
      if (isa<ImportDecl>(D))
        continue;
      assert(isa<VarDecl>(D) && "PMF initializer decl is not a var?");
      EmitTopLevelDecl(D);
    }
  }
}

void CodeGenModule::EmitModuleLinkOptions() {
  // Collect the set of all of the modules we want to visit to emit link
  // options, which is essentially the imported modules and all of their
  // non-explicit child modules.
  llvm::SetVector<clang::Module *> LinkModules;
  llvm::SmallPtrSet<clang::Module *, 16> Visited;
  SmallVector<clang::Module *, 16> Stack;

  // Seed the stack with imported modules.
  for (Module *M : ImportedModules) {
    // Do not add any link flags when an implementation TU of a module imports
    // a header of that same module.
    if (M->getTopLevelModuleName() == getLangOpts().CurrentModule &&
        !getLangOpts().isCompilingModule())
      continue;
    if (Visited.insert(M).second)
      Stack.push_back(M);
  }

  // Find all of the modules to import, making a little effort to prune
  // non-leaf modules.
  while (!Stack.empty()) {
    clang::Module *Mod = Stack.pop_back_val();

    bool AnyChildren = false;

    // Visit the submodules of this module.
    for (const auto &SM : Mod->submodules()) {
      // Skip explicit children; they need to be explicitly imported to be
      // linked against.
      if (SM->IsExplicit)
        continue;

      if (Visited.insert(SM).second) {
        Stack.push_back(SM);
        AnyChildren = true;
      }
    }

    // We didn't find any children, so add this module to the list of
    // modules to link against.
    if (!AnyChildren) {
      LinkModules.insert(Mod);
    }
  }

  // Add link options for all of the imported modules in reverse topological
  // order.  We don't do anything to try to order import link flags with respect
  // to linker options inserted by things like #pragma comment().
  SmallVector<llvm::MDNode *, 16> MetadataArgs;
  Visited.clear();
  for (Module *M : LinkModules)
    if (Visited.insert(M).second)
      addLinkOptionsPostorder(*this, M, MetadataArgs, Visited);
  std::reverse(MetadataArgs.begin(), MetadataArgs.end());
  LinkerOptionsMetadata.append(MetadataArgs.begin(), MetadataArgs.end());

  // Add the linker options metadata flag.
  auto *NMD = getModule().getOrInsertNamedMetadata("llvm.linker.options");
  for (auto *MD : LinkerOptionsMetadata)
    NMD->addOperand(MD);
}

void CodeGenModule::EmitDeferred() {
  // Emit deferred declare target declarations.
  if (getLangOpts().OpenMP && !getLangOpts().OpenMPSimd)
    getOpenMPRuntime().emitDeferredTargetDecls();

  // Emit code for any potentially referenced deferred decls.  Since a
  // previously unused static decl may become used during the generation of code
  // for a static function, iterate until no changes are made.

  if (!DeferredVTables.empty()) {
    EmitDeferredVTables();

    // Emitting a vtable doesn't directly cause more vtables to
    // become deferred, although it can cause functions to be
    // emitted that then need those vtables.
    assert(DeferredVTables.empty());
  }

  // Emit CUDA/HIP static device variables referenced by host code only.
  // Note we should not clear CUDADeviceVarODRUsedByHost since it is still
  // needed for further handling.
  if (getLangOpts().CUDA && getLangOpts().CUDAIsDevice)
    llvm::append_range(DeferredDeclsToEmit,
                       getContext().CUDADeviceVarODRUsedByHost);

  // Stop if we're out of both deferred vtables and deferred declarations.
  if (DeferredDeclsToEmit.empty())
    return;

  // Grab the list of decls to emit. If EmitGlobalDefinition schedules more
  // work, it will not interfere with this.
  std::vector<GlobalDecl> CurDeclsToEmit;
  CurDeclsToEmit.swap(DeferredDeclsToEmit);

  for (GlobalDecl &D : CurDeclsToEmit) {
    // We should call GetAddrOfGlobal with IsForDefinition set to true in order
    // to get GlobalValue with exactly the type we need, not something that
    // might had been created for another decl with the same mangled name but
    // different type.
    llvm::GlobalValue *GV = dyn_cast<llvm::GlobalValue>(
        GetAddrOfGlobal(D, ForDefinition));

    // In case of different address spaces, we may still get a cast, even with
    // IsForDefinition equal to true. Query mangled names table to get
    // GlobalValue.
    if (!GV)
      GV = GetGlobalValue(getMangledName(D));

    // Make sure GetGlobalValue returned non-null.
    assert(GV);

    // Check to see if we've already emitted this.  This is necessary
    // for a couple of reasons: first, decls can end up in the
    // deferred-decls queue multiple times, and second, decls can end
    // up with definitions in unusual ways (e.g. by an extern inline
    // function acquiring a strong function redefinition).  Just
    // ignore these cases.
    if (!GV->isDeclaration())
      continue;

    // If this is OpenMP, check if it is legal to emit this global normally.
    if (LangOpts.OpenMP && OpenMPRuntime && OpenMPRuntime->emitTargetGlobal(D))
      continue;

    // Otherwise, emit the definition and move on to the next one.
    EmitGlobalDefinition(D, GV);

    // If we found out that we need to emit more decls, do that recursively.
    // This has the advantage that the decls are emitted in a DFS and related
    // ones are close together, which is convenient for testing.
    if (!DeferredVTables.empty() || !DeferredDeclsToEmit.empty()) {
      EmitDeferred();
      assert(DeferredVTables.empty() && DeferredDeclsToEmit.empty());
    }
  }
}

void CodeGenModule::EmitVTablesOpportunistically() {
  // Try to emit external vtables as available_externally if they have emitted
  // all inlined virtual functions.  It runs after EmitDeferred() and therefore
  // is not allowed to create new references to things that need to be emitted
  // lazily. Note that it also uses fact that we eagerly emitting RTTI.

  assert((OpportunisticVTables.empty() || shouldOpportunisticallyEmitVTables())
         && "Only emit opportunistic vtables with optimizations");

  for (const CXXRecordDecl *RD : OpportunisticVTables) {
    assert(getVTables().isVTableExternal(RD) &&
           "This queue should only contain external vtables");
    if (getCXXABI().canSpeculativelyEmitVTable(RD))
      VTables.GenerateClassData(RD);
  }
  OpportunisticVTables.clear();
}

void CodeGenModule::EmitGlobalAnnotations() {
  for (const auto& [MangledName, VD] : DeferredAnnotations) {
    llvm::GlobalValue *GV = GetGlobalValue(MangledName);
    if (GV)
      AddGlobalAnnotations(VD, GV);
  }
  DeferredAnnotations.clear();

  if (Annotations.empty())
    return;

  // Create a new global variable for the ConstantStruct in the Module.
  llvm::Constant *Array = llvm::ConstantArray::get(llvm::ArrayType::get(
    Annotations[0]->getType(), Annotations.size()), Annotations);
  auto *gv = new llvm::GlobalVariable(getModule(), Array->getType(), false,
                                      llvm::GlobalValue::AppendingLinkage,
                                      Array, "llvm.global.annotations");
  gv->setSection(AnnotationSection);
}

llvm::Constant *CodeGenModule::EmitAnnotationString(StringRef Str) {
  llvm::Constant *&AStr = AnnotationStrings[Str];
  if (AStr)
    return AStr;

  // Not found yet, create a new global.
  llvm::Constant *s = llvm::ConstantDataArray::getString(getLLVMContext(), Str);
  auto *gv = new llvm::GlobalVariable(
      getModule(), s->getType(), true, llvm::GlobalValue::PrivateLinkage, s,
      ".str", nullptr, llvm::GlobalValue::NotThreadLocal,
      ConstGlobalsPtrTy->getAddressSpace());
  gv->setSection(AnnotationSection);
  gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  AStr = gv;
  return gv;
}

llvm::Constant *CodeGenModule::EmitAnnotationUnit(SourceLocation Loc) {
  SourceManager &SM = getContext().getSourceManager();
  PresumedLoc PLoc = SM.getPresumedLoc(Loc);
  if (PLoc.isValid())
    return EmitAnnotationString(PLoc.getFilename());
  return EmitAnnotationString(SM.getBufferName(Loc));
}

llvm::Constant *CodeGenModule::EmitAnnotationLineNo(SourceLocation L) {
  SourceManager &SM = getContext().getSourceManager();
  PresumedLoc PLoc = SM.getPresumedLoc(L);
  unsigned LineNo = PLoc.isValid() ? PLoc.getLine() :
    SM.getExpansionLineNumber(L);
  return llvm::ConstantInt::get(Int32Ty, LineNo);
}

llvm::Constant *CodeGenModule::EmitAnnotationArgs(const AnnotateAttr *Attr) {
  ArrayRef<Expr *> Exprs = {Attr->args_begin(), Attr->args_size()};
  if (Exprs.empty())
    return llvm::ConstantPointerNull::get(ConstGlobalsPtrTy);

  llvm::FoldingSetNodeID ID;
  for (Expr *E : Exprs) {
    ID.Add(cast<clang::ConstantExpr>(E)->getAPValueResult());
  }
  llvm::Constant *&Lookup = AnnotationArgs[ID.ComputeHash()];
  if (Lookup)
    return Lookup;

  llvm::SmallVector<llvm::Constant *, 4> LLVMArgs;
  LLVMArgs.reserve(Exprs.size());
  ConstantEmitter ConstEmiter(*this);
  llvm::transform(Exprs, std::back_inserter(LLVMArgs), [&](const Expr *E) {
    const auto *CE = cast<clang::ConstantExpr>(E);
    return ConstEmiter.emitAbstract(CE->getBeginLoc(), CE->getAPValueResult(),
                                    CE->getType());
  });
  auto *Struct = llvm::ConstantStruct::getAnon(LLVMArgs);
  auto *GV = new llvm::GlobalVariable(getModule(), Struct->getType(), true,
                                      llvm::GlobalValue::PrivateLinkage, Struct,
                                      ".args");
  GV->setSection(AnnotationSection);
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  Lookup = GV;
  return GV;
}

llvm::Constant *CodeGenModule::EmitAnnotateAttr(llvm::GlobalValue *GV,
                                                const AnnotateAttr *AA,
                                                SourceLocation L) {
  // Get the globals for file name, annotation, and the line number.
  llvm::Constant *AnnoGV = EmitAnnotationString(AA->getAnnotation()),
                 *UnitGV = EmitAnnotationUnit(L),
                 *LineNoCst = EmitAnnotationLineNo(L),
                 *Args = EmitAnnotationArgs(AA);

  llvm::Constant *GVInGlobalsAS = GV;
  if (GV->getAddressSpace() !=
      getDataLayout().getDefaultGlobalsAddressSpace()) {
    GVInGlobalsAS = llvm::ConstantExpr::getAddrSpaceCast(
        GV,
        llvm::PointerType::get(
            GV->getContext(), getDataLayout().getDefaultGlobalsAddressSpace()));
  }

  // Create the ConstantStruct for the global annotation.
  llvm::Constant *Fields[] = {
      GVInGlobalsAS, AnnoGV, UnitGV, LineNoCst, Args,
  };
  return llvm::ConstantStruct::getAnon(Fields);
}

void CodeGenModule::AddGlobalAnnotations(const ValueDecl *D,
                                         llvm::GlobalValue *GV) {
  assert(D->hasAttr<AnnotateAttr>() && "no annotate attribute");
  // Get the struct elements for these annotations.
  for (const auto *I : D->specific_attrs<AnnotateAttr>())
    Annotations.push_back(EmitAnnotateAttr(GV, I, D->getLocation()));
}

bool CodeGenModule::isInNoSanitizeList(SanitizerMask Kind, llvm::Function *Fn,
                                       SourceLocation Loc) const {
  const auto &NoSanitizeL = getContext().getNoSanitizeList();
  // NoSanitize by function name.
  if (NoSanitizeL.containsFunction(Kind, Fn->getName()))
    return true;
  // NoSanitize by location. Check "mainfile" prefix.
  auto &SM = Context.getSourceManager();
  FileEntryRef MainFile = *SM.getFileEntryRefForID(SM.getMainFileID());
  if (NoSanitizeL.containsMainFile(Kind, MainFile.getName()))
    return true;

  // Check "src" prefix.
  if (Loc.isValid())
    return NoSanitizeL.containsLocation(Kind, Loc);
  // If location is unknown, this may be a compiler-generated function. Assume
  // it's located in the main file.
  return NoSanitizeL.containsFile(Kind, MainFile.getName());
}

bool CodeGenModule::isInNoSanitizeList(SanitizerMask Kind,
                                       llvm::GlobalVariable *GV,
                                       SourceLocation Loc, QualType Ty,
                                       StringRef Category) const {
  const auto &NoSanitizeL = getContext().getNoSanitizeList();
  if (NoSanitizeL.containsGlobal(Kind, GV->getName(), Category))
    return true;
  auto &SM = Context.getSourceManager();
  if (NoSanitizeL.containsMainFile(
          Kind, SM.getFileEntryRefForID(SM.getMainFileID())->getName(),
          Category))
    return true;
  if (NoSanitizeL.containsLocation(Kind, Loc, Category))
    return true;

  // Check global type.
  if (!Ty.isNull()) {
    // Drill down the array types: if global variable of a fixed type is
    // not sanitized, we also don't instrument arrays of them.
    while (auto AT = dyn_cast<ArrayType>(Ty.getTypePtr()))
      Ty = AT->getElementType();
    Ty = Ty.getCanonicalType().getUnqualifiedType();
    // Only record types (classes, structs etc.) are ignored.
    if (Ty->isRecordType()) {
      std::string TypeStr = Ty.getAsString(getContext().getPrintingPolicy());
      if (NoSanitizeL.containsType(Kind, TypeStr, Category))
        return true;
    }
  }
  return false;
}

bool CodeGenModule::imbueXRayAttrs(llvm::Function *Fn, SourceLocation Loc,
                                   StringRef Category) const {
  const auto &XRayFilter = getContext().getXRayFilter();
  using ImbueAttr = XRayFunctionFilter::ImbueAttribute;
  auto Attr = ImbueAttr::NONE;
  if (Loc.isValid())
    Attr = XRayFilter.shouldImbueLocation(Loc, Category);
  if (Attr == ImbueAttr::NONE)
    Attr = XRayFilter.shouldImbueFunction(Fn->getName());
  switch (Attr) {
  case ImbueAttr::NONE:
    return false;
  case ImbueAttr::ALWAYS:
    Fn->addFnAttr("function-instrument", "xray-always");
    break;
  case ImbueAttr::ALWAYS_ARG1:
    Fn->addFnAttr("function-instrument", "xray-always");
    Fn->addFnAttr("xray-log-args", "1");
    break;
  case ImbueAttr::NEVER:
    Fn->addFnAttr("function-instrument", "xray-never");
    break;
  }
  return true;
}

ProfileList::ExclusionType
CodeGenModule::isFunctionBlockedByProfileList(llvm::Function *Fn,
                                              SourceLocation Loc) const {
  const auto &ProfileList = getContext().getProfileList();
  // If the profile list is empty, then instrument everything.
  if (ProfileList.isEmpty())
    return ProfileList::Allow;
  CodeGenOptions::ProfileInstrKind Kind = getCodeGenOpts().getProfileInstr();
  // First, check the function name.
  if (auto V = ProfileList.isFunctionExcluded(Fn->getName(), Kind))
    return *V;
  // Next, check the source location.
  if (Loc.isValid())
    if (auto V = ProfileList.isLocationExcluded(Loc, Kind))
      return *V;
  // If location is unknown, this may be a compiler-generated function. Assume
  // it's located in the main file.
  auto &SM = Context.getSourceManager();
  if (auto MainFile = SM.getFileEntryRefForID(SM.getMainFileID()))
    if (auto V = ProfileList.isFileExcluded(MainFile->getName(), Kind))
      return *V;
  return ProfileList.getDefault(Kind);
}

ProfileList::ExclusionType
CodeGenModule::isFunctionBlockedFromProfileInstr(llvm::Function *Fn,
                                                 SourceLocation Loc) const {
  auto V = isFunctionBlockedByProfileList(Fn, Loc);
  if (V != ProfileList::Allow)
    return V;

  auto NumGroups = getCodeGenOpts().ProfileTotalFunctionGroups;
  if (NumGroups > 1) {
    auto Group = llvm::crc32(arrayRefFromStringRef(Fn->getName())) % NumGroups;
    if (Group != getCodeGenOpts().ProfileSelectedFunctionGroup)
      return ProfileList::Skip;
  }
  return ProfileList::Allow;
}

bool CodeGenModule::MustBeEmitted(const ValueDecl *Global) {
  // Never defer when EmitAllDecls is specified.
  if (LangOpts.EmitAllDecls)
    return true;

  const auto *VD = dyn_cast<VarDecl>(Global);
  if (VD &&
      ((CodeGenOpts.KeepPersistentStorageVariables &&
        (VD->getStorageDuration() == SD_Static ||
         VD->getStorageDuration() == SD_Thread)) ||
       (CodeGenOpts.KeepStaticConsts && VD->getStorageDuration() == SD_Static &&
        VD->getType().isConstQualified())))
    return true;

  return getContext().DeclMustBeEmitted(Global);
}

bool CodeGenModule::MayBeEmittedEagerly(const ValueDecl *Global) {
  // In OpenMP 5.0 variables and function may be marked as
  // device_type(host/nohost) and we should not emit them eagerly unless we sure
  // that they must be emitted on the host/device. To be sure we need to have
  // seen a declare target with an explicit mentioning of the function, we know
  // we have if the level of the declare target attribute is -1. Note that we
  // check somewhere else if we should emit this at all.
  if (LangOpts.OpenMP >= 50 && !LangOpts.OpenMPSimd) {
    std::optional<OMPDeclareTargetDeclAttr *> ActiveAttr =
        OMPDeclareTargetDeclAttr::getActiveAttr(Global);
    if (!ActiveAttr || (*ActiveAttr)->getLevel() != (unsigned)-1)
      return false;
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(Global)) {
    if (FD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      // Implicit template instantiations may change linkage if they are later
      // explicitly instantiated, so they should not be emitted eagerly.
      return false;
    // Defer until all versions have been semantically checked.
    if (FD->hasAttr<TargetVersionAttr>() && !FD->isMultiVersion())
      return false;
  }
  if (const auto *VD = dyn_cast<VarDecl>(Global)) {
    if (Context.getInlineVariableDefinitionKind(VD) ==
        ASTContext::InlineVariableDefinitionKind::WeakUnknown)
      // A definition of an inline constexpr static data member may change
      // linkage later if it's redeclared outside the class.
      return false;
    if (CXX20ModuleInits && VD->getOwningModule() &&
        !VD->getOwningModule()->isModuleMapModule()) {
      // For CXX20, module-owned initializers need to be deferred, since it is
      // not known at this point if they will be run for the current module or
      // as part of the initializer for an imported one.
      return false;
    }
  }
  // If OpenMP is enabled and threadprivates must be generated like TLS, delay
  // codegen for global variables, because they may be marked as threadprivate.
  if (LangOpts.OpenMP && LangOpts.OpenMPUseTLS &&
      getContext().getTargetInfo().isTLSSupported() && isa<VarDecl>(Global) &&
      !Global->getType().isConstantStorage(getContext(), false, false) &&
      !OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(Global))
    return false;

  return true;
}

ConstantAddress CodeGenModule::GetAddrOfMSGuidDecl(const MSGuidDecl *GD) {
  StringRef Name = getMangledName(GD);

  // The UUID descriptor should be pointer aligned.
  CharUnits Alignment = CharUnits::fromQuantity(PointerAlignInBytes);

  // Look for an existing global.
  if (llvm::GlobalVariable *GV = getModule().getNamedGlobal(Name))
    return ConstantAddress(GV, GV->getValueType(), Alignment);

  ConstantEmitter Emitter(*this);
  llvm::Constant *Init;

  APValue &V = GD->getAsAPValue();
  if (!V.isAbsent()) {
    // If possible, emit the APValue version of the initializer. In particular,
    // this gets the type of the constant right.
    Init = Emitter.emitForInitializer(
        GD->getAsAPValue(), GD->getType().getAddressSpace(), GD->getType());
  } else {
    // As a fallback, directly construct the constant.
    // FIXME: This may get padding wrong under esoteric struct layout rules.
    // MSVC appears to create a complete type 'struct __s_GUID' that it
    // presumably uses to represent these constants.
    MSGuidDecl::Parts Parts = GD->getParts();
    llvm::Constant *Fields[4] = {
        llvm::ConstantInt::get(Int32Ty, Parts.Part1),
        llvm::ConstantInt::get(Int16Ty, Parts.Part2),
        llvm::ConstantInt::get(Int16Ty, Parts.Part3),
        llvm::ConstantDataArray::getRaw(
            StringRef(reinterpret_cast<char *>(Parts.Part4And5), 8), 8,
            Int8Ty)};
    Init = llvm::ConstantStruct::getAnon(Fields);
  }

  auto *GV = new llvm::GlobalVariable(
      getModule(), Init->getType(),
      /*isConstant=*/true, llvm::GlobalValue::LinkOnceODRLinkage, Init, Name);
  if (supportsCOMDAT())
    GV->setComdat(TheModule.getOrInsertComdat(GV->getName()));
  setDSOLocal(GV);

  if (!V.isAbsent()) {
    Emitter.finalize(GV);
    return ConstantAddress(GV, GV->getValueType(), Alignment);
  }

  llvm::Type *Ty = getTypes().ConvertTypeForMem(GD->getType());
  return ConstantAddress(GV, Ty, Alignment);
}

ConstantAddress CodeGenModule::GetAddrOfUnnamedGlobalConstantDecl(
    const UnnamedGlobalConstantDecl *GCD) {
  CharUnits Alignment = getContext().getTypeAlignInChars(GCD->getType());

  llvm::GlobalVariable **Entry = nullptr;
  Entry = &UnnamedGlobalConstantDeclMap[GCD];
  if (*Entry)
    return ConstantAddress(*Entry, (*Entry)->getValueType(), Alignment);

  ConstantEmitter Emitter(*this);
  llvm::Constant *Init;

  const APValue &V = GCD->getValue();

  assert(!V.isAbsent());
  Init = Emitter.emitForInitializer(V, GCD->getType().getAddressSpace(),
                                    GCD->getType());

  auto *GV = new llvm::GlobalVariable(getModule(), Init->getType(),
                                      /*isConstant=*/true,
                                      llvm::GlobalValue::PrivateLinkage, Init,
                                      ".constant");
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(Alignment.getAsAlign());

  Emitter.finalize(GV);

  *Entry = GV;
  return ConstantAddress(GV, GV->getValueType(), Alignment);
}

ConstantAddress CodeGenModule::GetAddrOfTemplateParamObject(
    const TemplateParamObjectDecl *TPO) {
  StringRef Name = getMangledName(TPO);
  CharUnits Alignment = getNaturalTypeAlignment(TPO->getType());

  if (llvm::GlobalVariable *GV = getModule().getNamedGlobal(Name))
    return ConstantAddress(GV, GV->getValueType(), Alignment);

  ConstantEmitter Emitter(*this);
  llvm::Constant *Init = Emitter.emitForInitializer(
        TPO->getValue(), TPO->getType().getAddressSpace(), TPO->getType());

  if (!Init) {
    ErrorUnsupported(TPO, "template parameter object");
    return ConstantAddress::invalid();
  }

  llvm::GlobalValue::LinkageTypes Linkage =
      isExternallyVisible(TPO->getLinkageAndVisibility().getLinkage())
          ? llvm::GlobalValue::LinkOnceODRLinkage
          : llvm::GlobalValue::InternalLinkage;
  auto *GV = new llvm::GlobalVariable(getModule(), Init->getType(),
                                      /*isConstant=*/true, Linkage, Init, Name);
  setGVProperties(GV, TPO);
  if (supportsCOMDAT())
    GV->setComdat(TheModule.getOrInsertComdat(GV->getName()));
  Emitter.finalize(GV);

    return ConstantAddress(GV, GV->getValueType(), Alignment);
}

ConstantAddress CodeGenModule::GetWeakRefReference(const ValueDecl *VD) {
  const AliasAttr *AA = VD->getAttr<AliasAttr>();
  assert(AA && "No alias?");

  CharUnits Alignment = getContext().getDeclAlign(VD);
  llvm::Type *DeclTy = getTypes().ConvertTypeForMem(VD->getType());

  // See if there is already something with the target's name in the module.
  llvm::GlobalValue *Entry = GetGlobalValue(AA->getAliasee());
  if (Entry)
    return ConstantAddress(Entry, DeclTy, Alignment);

  llvm::Constant *Aliasee;
  if (isa<llvm::FunctionType>(DeclTy))
    Aliasee = GetOrCreateLLVMFunction(AA->getAliasee(), DeclTy,
                                      GlobalDecl(cast<FunctionDecl>(VD)),
                                      /*ForVTable=*/false);
  else
    Aliasee = GetOrCreateLLVMGlobal(AA->getAliasee(), DeclTy, LangAS::Default,
                                    nullptr);

  auto *F = cast<llvm::GlobalValue>(Aliasee);
  F->setLinkage(llvm::Function::ExternalWeakLinkage);
  WeakRefReferences.insert(F);

  return ConstantAddress(Aliasee, DeclTy, Alignment);
}

template <typename AttrT> static bool hasImplicitAttr(const ValueDecl *D) {
  if (!D)
    return false;
  if (auto *A = D->getAttr<AttrT>())
    return A->isImplicit();
  return D->isImplicit();
}

bool CodeGenModule::shouldEmitCUDAGlobalVar(const VarDecl *Global) const {
  assert(LangOpts.CUDA && "Should not be called by non-CUDA languages");
  // We need to emit host-side 'shadows' for all global
  // device-side variables because the CUDA runtime needs their
  // size and host-side address in order to provide access to
  // their device-side incarnations.
  return !LangOpts.CUDAIsDevice || Global->hasAttr<CUDADeviceAttr>() ||
         Global->hasAttr<CUDAConstantAttr>() ||
         Global->hasAttr<CUDASharedAttr>() ||
         Global->getType()->isCUDADeviceBuiltinSurfaceType() ||
         Global->getType()->isCUDADeviceBuiltinTextureType();
}

void CodeGenModule::EmitGlobal(GlobalDecl GD) {
  const auto *Global = cast<ValueDecl>(GD.getDecl());

  // Weak references don't produce any output by themselves.
  if (Global->hasAttr<WeakRefAttr>())
    return;

  // If this is an alias definition (which otherwise looks like a declaration)
  // emit it now.
  if (Global->hasAttr<AliasAttr>())
    return EmitAliasDefinition(GD);

  // IFunc like an alias whose value is resolved at runtime by calling resolver.
  if (Global->hasAttr<IFuncAttr>())
    return emitIFuncDefinition(GD);

  // If this is a cpu_dispatch multiversion function, emit the resolver.
  if (Global->hasAttr<CPUDispatchAttr>())
    return emitCPUDispatchDefinition(GD);

  // If this is CUDA, be selective about which declarations we emit.
  // Non-constexpr non-lambda implicit host device functions are not emitted
  // unless they are used on device side.
  if (LangOpts.CUDA) {
    assert((isa<FunctionDecl>(Global) || isa<VarDecl>(Global)) &&
           "Expected Variable or Function");
    if (const auto *VD = dyn_cast<VarDecl>(Global)) {
      if (!shouldEmitCUDAGlobalVar(VD))
        return;
    } else if (LangOpts.CUDAIsDevice) {
      const auto *FD = dyn_cast<FunctionDecl>(Global);
      if ((!Global->hasAttr<CUDADeviceAttr>() ||
           (LangOpts.OffloadImplicitHostDeviceTemplates &&
            hasImplicitAttr<CUDAHostAttr>(FD) &&
            hasImplicitAttr<CUDADeviceAttr>(FD) && !FD->isConstexpr() &&
            !isLambdaCallOperator(FD) &&
            !getContext().CUDAImplicitHostDeviceFunUsedByDevice.count(FD))) &&
          !Global->hasAttr<CUDAGlobalAttr>() &&
          !(LangOpts.HIPStdPar && isa<FunctionDecl>(Global) &&
            !Global->hasAttr<CUDAHostAttr>()))
        return;
      // Device-only functions are the only things we skip.
    } else if (!Global->hasAttr<CUDAHostAttr>() &&
               Global->hasAttr<CUDADeviceAttr>())
      return;
  }

  if (LangOpts.OpenMP) {
    // If this is OpenMP, check if it is legal to emit this global normally.
    if (OpenMPRuntime && OpenMPRuntime->emitTargetGlobal(GD))
      return;
    if (auto *DRD = dyn_cast<OMPDeclareReductionDecl>(Global)) {
      if (MustBeEmitted(Global))
        EmitOMPDeclareReduction(DRD);
      return;
    }
    if (auto *DMD = dyn_cast<OMPDeclareMapperDecl>(Global)) {
      if (MustBeEmitted(Global))
        EmitOMPDeclareMapper(DMD);
      return;
    }
  }

  // Ignore declarations, they will be emitted on their first use.
  if (const auto *FD = dyn_cast<FunctionDecl>(Global)) {
    // Update deferred annotations with the latest declaration if the function
    // function was already used or defined.
    if (FD->hasAttr<AnnotateAttr>()) {
      StringRef MangledName = getMangledName(GD);
      if (GetGlobalValue(MangledName))
        DeferredAnnotations[MangledName] = FD;
    }

    // Forward declarations are emitted lazily on first use.
    if (!FD->doesThisDeclarationHaveABody()) {
      if (!FD->doesDeclarationForceExternallyVisibleDefinition() &&
          (!FD->isMultiVersion() || !getTarget().getTriple().isAArch64()))
        return;

      StringRef MangledName = getMangledName(GD);

      // Compute the function info and LLVM type.
      const CGFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
      llvm::Type *Ty = getTypes().GetFunctionType(FI);

      GetOrCreateLLVMFunction(MangledName, Ty, GD, /*ForVTable=*/false,
                              /*DontDefer=*/false);
      return;
    }
  } else {
    const auto *VD = cast<VarDecl>(Global);
    assert(VD->isFileVarDecl() && "Cannot emit local var decl as global.");
    if (VD->isThisDeclarationADefinition() != VarDecl::Definition &&
        !Context.isMSStaticDataMemberInlineDefinition(VD)) {
      if (LangOpts.OpenMP) {
        // Emit declaration of the must-be-emitted declare target variable.
        if (std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
                OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD)) {

          // If this variable has external storage and doesn't require special
          // link handling we defer to its canonical definition.
          if (VD->hasExternalStorage() &&
              Res != OMPDeclareTargetDeclAttr::MT_Link)
            return;

          bool UnifiedMemoryEnabled =
              getOpenMPRuntime().hasRequiresUnifiedSharedMemory();
          if ((*Res == OMPDeclareTargetDeclAttr::MT_To ||
               *Res == OMPDeclareTargetDeclAttr::MT_Enter) &&
              !UnifiedMemoryEnabled) {
            (void)GetAddrOfGlobalVar(VD);
          } else {
            assert(((*Res == OMPDeclareTargetDeclAttr::MT_Link) ||
                    ((*Res == OMPDeclareTargetDeclAttr::MT_To ||
                      *Res == OMPDeclareTargetDeclAttr::MT_Enter) &&
                     UnifiedMemoryEnabled)) &&
                   "Link clause or to clause with unified memory expected.");
            (void)getOpenMPRuntime().getAddrOfDeclareTargetVar(VD);
          }

          return;
        }
      }
      // If this declaration may have caused an inline variable definition to
      // change linkage, make sure that it's emitted.
      if (Context.getInlineVariableDefinitionKind(VD) ==
          ASTContext::InlineVariableDefinitionKind::Strong)
        GetAddrOfGlobalVar(VD);
      return;
    }
  }

  // Defer code generation to first use when possible, e.g. if this is an inline
  // function. If the global must always be emitted, do it eagerly if possible
  // to benefit from cache locality.
  if (MustBeEmitted(Global) && MayBeEmittedEagerly(Global)) {
    // Emit the definition if it can't be deferred.
    EmitGlobalDefinition(GD);
    addEmittedDeferredDecl(GD);
    return;
  }

  // If we're deferring emission of a C++ variable with an
  // initializer, remember the order in which it appeared in the file.
  if (getLangOpts().CPlusPlus && isa<VarDecl>(Global) &&
      cast<VarDecl>(Global)->hasInit()) {
    DelayedCXXInitPosition[Global] = CXXGlobalInits.size();
    CXXGlobalInits.push_back(nullptr);
  }

  StringRef MangledName = getMangledName(GD);
  if (GetGlobalValue(MangledName) != nullptr) {
    // The value has already been used and should therefore be emitted.
    addDeferredDeclToEmit(GD);
  } else if (MustBeEmitted(Global)) {
    // The value must be emitted, but cannot be emitted eagerly.
    assert(!MayBeEmittedEagerly(Global));
    addDeferredDeclToEmit(GD);
  } else {
    // Otherwise, remember that we saw a deferred decl with this name.  The
    // first use of the mangled name will cause it to move into
    // DeferredDeclsToEmit.
    DeferredDecls[MangledName] = GD;
  }
}

// Check if T is a class type with a destructor that's not dllimport.
static bool HasNonDllImportDtor(QualType T) {
  if (const auto *RT = T->getBaseElementTypeUnsafe()->getAs<RecordType>())
    if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(RT->getDecl()))
      if (RD->getDestructor() && !RD->getDestructor()->hasAttr<DLLImportAttr>())
        return true;

  return false;
}

namespace {
  struct FunctionIsDirectlyRecursive
      : public ConstStmtVisitor<FunctionIsDirectlyRecursive, bool> {
    const StringRef Name;
    const Builtin::Context &BI;
    FunctionIsDirectlyRecursive(StringRef N, const Builtin::Context &C)
        : Name(N), BI(C) {}

    bool VisitCallExpr(const CallExpr *E) {
      const FunctionDecl *FD = E->getDirectCallee();
      if (!FD)
        return false;
      AsmLabelAttr *Attr = FD->getAttr<AsmLabelAttr>();
      if (Attr && Name == Attr->getLabel())
        return true;
      unsigned BuiltinID = FD->getBuiltinID();
      if (!BuiltinID || !BI.isLibFunction(BuiltinID))
        return false;
      StringRef BuiltinName = BI.getName(BuiltinID);
      if (BuiltinName.starts_with("__builtin_") &&
          Name == BuiltinName.slice(strlen("__builtin_"), StringRef::npos)) {
        return true;
      }
      return false;
    }

    bool VisitStmt(const Stmt *S) {
      for (const Stmt *Child : S->children())
        if (Child && this->Visit(Child))
          return true;
      return false;
    }
  };

  // Make sure we're not referencing non-imported vars or functions.
  struct DLLImportFunctionVisitor
      : public RecursiveASTVisitor<DLLImportFunctionVisitor> {
    bool SafeToInline = true;

    bool shouldVisitImplicitCode() const { return true; }

    bool VisitVarDecl(VarDecl *VD) {
      if (VD->getTLSKind()) {
        // A thread-local variable cannot be imported.
        SafeToInline = false;
        return SafeToInline;
      }

      // A variable definition might imply a destructor call.
      if (VD->isThisDeclarationADefinition())
        SafeToInline = !HasNonDllImportDtor(VD->getType());

      return SafeToInline;
    }

    bool VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E) {
      if (const auto *D = E->getTemporary()->getDestructor())
        SafeToInline = D->hasAttr<DLLImportAttr>();
      return SafeToInline;
    }

    bool VisitDeclRefExpr(DeclRefExpr *E) {
      ValueDecl *VD = E->getDecl();
      if (isa<FunctionDecl>(VD))
        SafeToInline = VD->hasAttr<DLLImportAttr>();
      else if (VarDecl *V = dyn_cast<VarDecl>(VD))
        SafeToInline = !V->hasGlobalStorage() || V->hasAttr<DLLImportAttr>();
      return SafeToInline;
    }

    bool VisitCXXConstructExpr(CXXConstructExpr *E) {
      SafeToInline = E->getConstructor()->hasAttr<DLLImportAttr>();
      return SafeToInline;
    }

    bool VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
      CXXMethodDecl *M = E->getMethodDecl();
      if (!M) {
        // Call through a pointer to member function. This is safe to inline.
        SafeToInline = true;
      } else {
        SafeToInline = M->hasAttr<DLLImportAttr>();
      }
      return SafeToInline;
    }

    bool VisitCXXDeleteExpr(CXXDeleteExpr *E) {
      SafeToInline = E->getOperatorDelete()->hasAttr<DLLImportAttr>();
      return SafeToInline;
    }

    bool VisitCXXNewExpr(CXXNewExpr *E) {
      SafeToInline = E->getOperatorNew()->hasAttr<DLLImportAttr>();
      return SafeToInline;
    }
  };
}

// isTriviallyRecursive - Check if this function calls another
// decl that, because of the asm attribute or the other decl being a builtin,
// ends up pointing to itself.
bool
CodeGenModule::isTriviallyRecursive(const FunctionDecl *FD) {
  StringRef Name;
  if (getCXXABI().getMangleContext().shouldMangleDeclName(FD)) {
    // asm labels are a special kind of mangling we have to support.
    AsmLabelAttr *Attr = FD->getAttr<AsmLabelAttr>();
    if (!Attr)
      return false;
    Name = Attr->getLabel();
  } else {
    Name = FD->getName();
  }

  FunctionIsDirectlyRecursive Walker(Name, Context.BuiltinInfo);
  const Stmt *Body = FD->getBody();
  return Body ? Walker.Visit(Body) : false;
}

bool CodeGenModule::shouldEmitFunction(GlobalDecl GD) {
  if (getFunctionLinkage(GD) != llvm::Function::AvailableExternallyLinkage)
    return true;

  const auto *F = cast<FunctionDecl>(GD.getDecl());
  if (CodeGenOpts.OptimizationLevel == 0 && !F->hasAttr<AlwaysInlineAttr>())
    return false;

  // We don't import function bodies from other named module units since that
  // behavior may break ABI compatibility of the current unit.
  if (const Module *M = F->getOwningModule();
      M && M->getTopLevelModule()->isNamedModule() &&
      getContext().getCurrentNamedModule() != M->getTopLevelModule()) {
    // There are practices to mark template member function as always-inline
    // and mark the template as extern explicit instantiation but not give
    // the definition for member function. So we have to emit the function
    // from explicitly instantiation with always-inline.
    //
    // See https://github.com/llvm/llvm-project/issues/86893 for details.
    //
    // TODO: Maybe it is better to give it a warning if we call a non-inline
    // function from other module units which is marked as always-inline.
    if (!F->isTemplateInstantiation() || !F->hasAttr<AlwaysInlineAttr>()) {
      return false;
    }
  }

  if (F->hasAttr<NoInlineAttr>())
    return false;

  if (F->hasAttr<DLLImportAttr>() && !F->hasAttr<AlwaysInlineAttr>()) {
    // Check whether it would be safe to inline this dllimport function.
    DLLImportFunctionVisitor Visitor;
    Visitor.TraverseFunctionDecl(const_cast<FunctionDecl*>(F));
    if (!Visitor.SafeToInline)
      return false;

    if (const CXXDestructorDecl *Dtor = dyn_cast<CXXDestructorDecl>(F)) {
      // Implicit destructor invocations aren't captured in the AST, so the
      // check above can't see them. Check for them manually here.
      for (const Decl *Member : Dtor->getParent()->decls())
        if (isa<FieldDecl>(Member))
          if (HasNonDllImportDtor(cast<FieldDecl>(Member)->getType()))
            return false;
      for (const CXXBaseSpecifier &B : Dtor->getParent()->bases())
        if (HasNonDllImportDtor(B.getType()))
          return false;
    }
  }

  // Inline builtins declaration must be emitted. They often are fortified
  // functions.
  if (F->isInlineBuiltinDeclaration())
    return true;

  // PR9614. Avoid cases where the source code is lying to us. An available
  // externally function should have an equivalent function somewhere else,
  // but a function that calls itself through asm label/`__builtin_` trickery is
  // clearly not equivalent to the real implementation.
  // This happens in glibc's btowc and in some configure checks.
  return !isTriviallyRecursive(F);
}

bool CodeGenModule::shouldOpportunisticallyEmitVTables() {
  return CodeGenOpts.OptimizationLevel > 0;
}

void CodeGenModule::EmitMultiVersionFunctionDefinition(GlobalDecl GD,
                                                       llvm::GlobalValue *GV) {
  const auto *FD = cast<FunctionDecl>(GD.getDecl());

  if (FD->isCPUSpecificMultiVersion()) {
    auto *Spec = FD->getAttr<CPUSpecificAttr>();
    for (unsigned I = 0; I < Spec->cpus_size(); ++I)
      EmitGlobalFunctionDefinition(GD.getWithMultiVersionIndex(I), nullptr);
  } else if (auto *TC = FD->getAttr<TargetClonesAttr>()) {
    for (unsigned I = 0; I < TC->featuresStrs_size(); ++I)
      // AArch64 favors the default target version over the clone if any.
      if ((!TC->isDefaultVersion(I) || !getTarget().getTriple().isAArch64()) &&
          TC->isFirstOfVersion(I))
        EmitGlobalFunctionDefinition(GD.getWithMultiVersionIndex(I), nullptr);
    // Ensure that the resolver function is also emitted.
    GetOrCreateMultiVersionResolver(GD);
  } else
    EmitGlobalFunctionDefinition(GD, GV);

  // Defer the resolver emission until we can reason whether the TU
  // contains a default target version implementation.
  if (FD->isTargetVersionMultiVersion())
    AddDeferredMultiVersionResolverToEmit(GD);
}

void CodeGenModule::EmitGlobalDefinition(GlobalDecl GD, llvm::GlobalValue *GV) {
  const auto *D = cast<ValueDecl>(GD.getDecl());

  PrettyStackTraceDecl CrashInfo(const_cast<ValueDecl *>(D), D->getLocation(),
                                 Context.getSourceManager(),
                                 "Generating code for declaration");

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    // At -O0, don't generate IR for functions with available_externally
    // linkage.
    if (!shouldEmitFunction(GD))
      return;

    llvm::TimeTraceScope TimeScope("CodeGen Function", [&]() {
      std::string Name;
      llvm::raw_string_ostream OS(Name);
      FD->getNameForDiagnostic(OS, getContext().getPrintingPolicy(),
                               /*Qualified=*/true);
      return Name;
    });

    if (const auto *Method = dyn_cast<CXXMethodDecl>(D)) {
      // Make sure to emit the definition(s) before we emit the thunks.
      // This is necessary for the generation of certain thunks.
      if (isa<CXXConstructorDecl>(Method) || isa<CXXDestructorDecl>(Method))
        ABI->emitCXXStructor(GD);
      else if (FD->isMultiVersion())
        EmitMultiVersionFunctionDefinition(GD, GV);
      else
        EmitGlobalFunctionDefinition(GD, GV);

      if (Method->isVirtual())
        getVTables().EmitThunks(GD);

      return;
    }

    if (FD->isMultiVersion())
      return EmitMultiVersionFunctionDefinition(GD, GV);
    return EmitGlobalFunctionDefinition(GD, GV);
  }

  if (const auto *VD = dyn_cast<VarDecl>(D))
    return EmitGlobalVarDefinition(VD, !VD->hasDefinition());

  llvm_unreachable("Invalid argument to EmitGlobalDefinition()");
}

static void ReplaceUsesOfNonProtoTypeWithRealFunction(llvm::GlobalValue *Old,
                                                      llvm::Function *NewFn);

static unsigned
TargetMVPriority(const TargetInfo &TI,
                 const CodeGenFunction::MultiVersionResolverOption &RO) {
  unsigned Priority = 0;
  unsigned NumFeatures = 0;
  for (StringRef Feat : RO.Conditions.Features) {
    Priority = std::max(Priority, TI.multiVersionSortPriority(Feat));
    NumFeatures++;
  }

  if (!RO.Conditions.Architecture.empty())
    Priority = std::max(
        Priority, TI.multiVersionSortPriority(RO.Conditions.Architecture));

  Priority += TI.multiVersionFeatureCost() * NumFeatures;

  return Priority;
}

// Multiversion functions should be at most 'WeakODRLinkage' so that a different
// TU can forward declare the function without causing problems.  Particularly
// in the cases of CPUDispatch, this causes issues. This also makes sure we
// work with internal linkage functions, so that the same function name can be
// used with internal linkage in multiple TUs.
llvm::GlobalValue::LinkageTypes getMultiversionLinkage(CodeGenModule &CGM,
                                                       GlobalDecl GD) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  if (FD->getFormalLinkage() == Linkage::Internal)
    return llvm::GlobalValue::InternalLinkage;
  return llvm::GlobalValue::WeakODRLinkage;
}

void CodeGenModule::emitMultiVersionFunctions() {
  std::vector<GlobalDecl> MVFuncsToEmit;
  MultiVersionFuncs.swap(MVFuncsToEmit);
  for (GlobalDecl GD : MVFuncsToEmit) {
    const auto *FD = cast<FunctionDecl>(GD.getDecl());
    assert(FD && "Expected a FunctionDecl");

    auto createFunction = [&](const FunctionDecl *Decl, unsigned MVIdx = 0) {
      GlobalDecl CurGD{Decl->isDefined() ? Decl->getDefinition() : Decl, MVIdx};
      StringRef MangledName = getMangledName(CurGD);
      llvm::Constant *Func = GetGlobalValue(MangledName);
      if (!Func) {
        if (Decl->isDefined()) {
          EmitGlobalFunctionDefinition(CurGD, nullptr);
          Func = GetGlobalValue(MangledName);
        } else {
          const CGFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(CurGD);
          llvm::FunctionType *Ty = getTypes().GetFunctionType(FI);
          Func = GetAddrOfFunction(CurGD, Ty, /*ForVTable=*/false,
                                   /*DontDefer=*/false, ForDefinition);
        }
        assert(Func && "This should have just been created");
      }
      return cast<llvm::Function>(Func);
    };

    // For AArch64, a resolver is only emitted if a function marked with
    // target_version("default")) or target_clones() is present and defined
    // in this TU. For other architectures it is always emitted.
    bool ShouldEmitResolver = !getTarget().getTriple().isAArch64();
    SmallVector<CodeGenFunction::MultiVersionResolverOption, 10> Options;

    getContext().forEachMultiversionedFunctionVersion(
        FD, [&](const FunctionDecl *CurFD) {
          llvm::SmallVector<StringRef, 8> Feats;
          bool IsDefined = CurFD->doesThisDeclarationHaveABody();

          if (const auto *TA = CurFD->getAttr<TargetAttr>()) {
            TA->getAddedFeatures(Feats);
            llvm::Function *Func = createFunction(CurFD);
            Options.emplace_back(Func, TA->getArchitecture(), Feats);
          } else if (const auto *TVA = CurFD->getAttr<TargetVersionAttr>()) {
            if (TVA->isDefaultVersion() && IsDefined)
              ShouldEmitResolver = true;
            TVA->getFeatures(Feats);
            llvm::Function *Func = createFunction(CurFD);
            Options.emplace_back(Func, /*Architecture*/ "", Feats);
          } else if (const auto *TC = CurFD->getAttr<TargetClonesAttr>()) {
            if (IsDefined)
              ShouldEmitResolver = true;
            for (unsigned I = 0; I < TC->featuresStrs_size(); ++I) {
              if (!TC->isFirstOfVersion(I))
                continue;

              llvm::Function *Func = createFunction(CurFD, I);
              StringRef Architecture;
              Feats.clear();
              if (getTarget().getTriple().isAArch64())
                TC->getFeatures(Feats, I);
              else {
                StringRef Version = TC->getFeatureStr(I);
                if (Version.starts_with("arch="))
                  Architecture = Version.drop_front(sizeof("arch=") - 1);
                else if (Version != "default")
                  Feats.push_back(Version);
              }
              Options.emplace_back(Func, Architecture, Feats);
            }
          } else
            llvm_unreachable("unexpected MultiVersionKind");
        });

    if (!ShouldEmitResolver)
      continue;

    llvm::Constant *ResolverConstant = GetOrCreateMultiVersionResolver(GD);
    if (auto *IFunc = dyn_cast<llvm::GlobalIFunc>(ResolverConstant)) {
      ResolverConstant = IFunc->getResolver();
      if (FD->isTargetClonesMultiVersion() &&
          !getTarget().getTriple().isAArch64()) {
        std::string MangledName = getMangledNameImpl(
            *this, GD, FD, /*OmitMultiVersionMangling=*/true);
        if (!GetGlobalValue(MangledName + ".ifunc")) {
          const CGFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
          llvm::FunctionType *DeclTy = getTypes().GetFunctionType(FI);
          // In prior versions of Clang, the mangling for ifuncs incorrectly
          // included an .ifunc suffix. This alias is generated for backward
          // compatibility. It is deprecated, and may be removed in the future.
          auto *Alias = llvm::GlobalAlias::create(
              DeclTy, 0, getMultiversionLinkage(*this, GD),
              MangledName + ".ifunc", IFunc, &getModule());
          SetCommonAttributes(FD, Alias);
        }
      }
    }
    llvm::Function *ResolverFunc = cast<llvm::Function>(ResolverConstant);

    ResolverFunc->setLinkage(getMultiversionLinkage(*this, GD));

    if (!ResolverFunc->hasLocalLinkage() && supportsCOMDAT())
      ResolverFunc->setComdat(
          getModule().getOrInsertComdat(ResolverFunc->getName()));

    const TargetInfo &TI = getTarget();
    llvm::stable_sort(
        Options, [&TI](const CodeGenFunction::MultiVersionResolverOption &LHS,
                       const CodeGenFunction::MultiVersionResolverOption &RHS) {
          return TargetMVPriority(TI, LHS) > TargetMVPriority(TI, RHS);
        });
    CodeGenFunction CGF(*this);
    CGF.EmitMultiVersionResolver(ResolverFunc, Options);
  }

  // Ensure that any additions to the deferred decls list caused by emitting a
  // variant are emitted.  This can happen when the variant itself is inline and
  // calls a function without linkage.
  if (!MVFuncsToEmit.empty())
    EmitDeferred();

  // Ensure that any additions to the multiversion funcs list from either the
  // deferred decls or the multiversion functions themselves are emitted.
  if (!MultiVersionFuncs.empty())
    emitMultiVersionFunctions();
}

static void replaceDeclarationWith(llvm::GlobalValue *Old,
                                   llvm::Constant *New) {
  assert(cast<llvm::Function>(Old)->isDeclaration() && "Not a declaration");
  New->takeName(Old);
  Old->replaceAllUsesWith(New);
  Old->eraseFromParent();
}

void CodeGenModule::emitCPUDispatchDefinition(GlobalDecl GD) {
  const auto *FD = cast<FunctionDecl>(GD.getDecl());
  assert(FD && "Not a FunctionDecl?");
  assert(FD->isCPUDispatchMultiVersion() && "Not a multiversion function?");
  const auto *DD = FD->getAttr<CPUDispatchAttr>();
  assert(DD && "Not a cpu_dispatch Function?");

  const CGFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
  llvm::FunctionType *DeclTy = getTypes().GetFunctionType(FI);

  StringRef ResolverName = getMangledName(GD);
  UpdateMultiVersionNames(GD, FD, ResolverName);

  llvm::Type *ResolverType;
  GlobalDecl ResolverGD;
  if (getTarget().supportsIFunc()) {
    ResolverType = llvm::FunctionType::get(
        llvm::PointerType::get(DeclTy,
                               getTypes().getTargetAddressSpace(FD->getType())),
        false);
  }
  else {
    ResolverType = DeclTy;
    ResolverGD = GD;
  }

  auto *ResolverFunc = cast<llvm::Function>(GetOrCreateLLVMFunction(
      ResolverName, ResolverType, ResolverGD, /*ForVTable=*/false));
  ResolverFunc->setLinkage(getMultiversionLinkage(*this, GD));
  if (supportsCOMDAT())
    ResolverFunc->setComdat(
        getModule().getOrInsertComdat(ResolverFunc->getName()));

  SmallVector<CodeGenFunction::MultiVersionResolverOption, 10> Options;
  const TargetInfo &Target = getTarget();
  unsigned Index = 0;
  for (const IdentifierInfo *II : DD->cpus()) {
    // Get the name of the target function so we can look it up/create it.
    std::string MangledName = getMangledNameImpl(*this, GD, FD, true) +
                              getCPUSpecificMangling(*this, II->getName());

    llvm::Constant *Func = GetGlobalValue(MangledName);

    if (!Func) {
      GlobalDecl ExistingDecl = Manglings.lookup(MangledName);
      if (ExistingDecl.getDecl() &&
          ExistingDecl.getDecl()->getAsFunction()->isDefined()) {
        EmitGlobalFunctionDefinition(ExistingDecl, nullptr);
        Func = GetGlobalValue(MangledName);
      } else {
        if (!ExistingDecl.getDecl())
          ExistingDecl = GD.getWithMultiVersionIndex(Index);

      Func = GetOrCreateLLVMFunction(
          MangledName, DeclTy, ExistingDecl,
          /*ForVTable=*/false, /*DontDefer=*/true,
          /*IsThunk=*/false, llvm::AttributeList(), ForDefinition);
      }
    }

    llvm::SmallVector<StringRef, 32> Features;
    Target.getCPUSpecificCPUDispatchFeatures(II->getName(), Features);
    llvm::transform(Features, Features.begin(),
                    [](StringRef Str) { return Str.substr(1); });
    llvm::erase_if(Features, [&Target](StringRef Feat) {
      return !Target.validateCpuSupports(Feat);
    });
    Options.emplace_back(cast<llvm::Function>(Func), StringRef{}, Features);
    ++Index;
  }

  llvm::stable_sort(
      Options, [](const CodeGenFunction::MultiVersionResolverOption &LHS,
                  const CodeGenFunction::MultiVersionResolverOption &RHS) {
        return llvm::X86::getCpuSupportsMask(LHS.Conditions.Features) >
               llvm::X86::getCpuSupportsMask(RHS.Conditions.Features);
      });

  // If the list contains multiple 'default' versions, such as when it contains
  // 'pentium' and 'generic', don't emit the call to the generic one (since we
  // always run on at least a 'pentium'). We do this by deleting the 'least
  // advanced' (read, lowest mangling letter).
  while (Options.size() > 1 &&
         llvm::all_of(llvm::X86::getCpuSupportsMask(
                          (Options.end() - 2)->Conditions.Features),
                      [](auto X) { return X == 0; })) {
    StringRef LHSName = (Options.end() - 2)->Function->getName();
    StringRef RHSName = (Options.end() - 1)->Function->getName();
    if (LHSName.compare(RHSName) < 0)
      Options.erase(Options.end() - 2);
    else
      Options.erase(Options.end() - 1);
  }

  CodeGenFunction CGF(*this);
  CGF.EmitMultiVersionResolver(ResolverFunc, Options);

  if (getTarget().supportsIFunc()) {
    llvm::GlobalValue::LinkageTypes Linkage = getMultiversionLinkage(*this, GD);
    auto *IFunc = cast<llvm::GlobalValue>(GetOrCreateMultiVersionResolver(GD));

    // Fix up function declarations that were created for cpu_specific before
    // cpu_dispatch was known
    if (!isa<llvm::GlobalIFunc>(IFunc)) {
      auto *GI = llvm::GlobalIFunc::create(DeclTy, 0, Linkage, "", ResolverFunc,
                                           &getModule());
      replaceDeclarationWith(IFunc, GI);
      IFunc = GI;
    }

    std::string AliasName = getMangledNameImpl(
        *this, GD, FD, /*OmitMultiVersionMangling=*/true);
    llvm::Constant *AliasFunc = GetGlobalValue(AliasName);
    if (!AliasFunc) {
      auto *GA = llvm::GlobalAlias::create(DeclTy, 0, Linkage, AliasName, IFunc,
                                           &getModule());
      SetCommonAttributes(GD, GA);
    }
  }
}

/// Adds a declaration to the list of multi version functions if not present.
void CodeGenModule::AddDeferredMultiVersionResolverToEmit(GlobalDecl GD) {
  const auto *FD = cast<FunctionDecl>(GD.getDecl());
  assert(FD && "Not a FunctionDecl?");

  if (FD->isTargetVersionMultiVersion() || FD->isTargetClonesMultiVersion()) {
    std::string MangledName =
        getMangledNameImpl(*this, GD, FD, /*OmitMultiVersionMangling=*/true);
    if (!DeferredResolversToEmit.insert(MangledName).second)
      return;
  }
  MultiVersionFuncs.push_back(GD);
}

/// If a dispatcher for the specified mangled name is not in the module, create
/// and return it. The dispatcher is either an llvm Function with the specified
/// type, or a global ifunc.
llvm::Constant *CodeGenModule::GetOrCreateMultiVersionResolver(GlobalDecl GD) {
  const auto *FD = cast<FunctionDecl>(GD.getDecl());
  assert(FD && "Not a FunctionDecl?");

  std::string MangledName =
      getMangledNameImpl(*this, GD, FD, /*OmitMultiVersionMangling=*/true);

  // Holds the name of the resolver, in ifunc mode this is the ifunc (which has
  // a separate resolver).
  std::string ResolverName = MangledName;
  if (getTarget().supportsIFunc()) {
    switch (FD->getMultiVersionKind()) {
    case MultiVersionKind::None:
      llvm_unreachable("unexpected MultiVersionKind::None for resolver");
    case MultiVersionKind::Target:
    case MultiVersionKind::CPUSpecific:
    case MultiVersionKind::CPUDispatch:
      ResolverName += ".ifunc";
      break;
    case MultiVersionKind::TargetClones:
    case MultiVersionKind::TargetVersion:
      break;
    }
  } else if (FD->isTargetMultiVersion()) {
    ResolverName += ".resolver";
  }

  // If the resolver has already been created, just return it. This lookup may
  // yield a function declaration instead of a resolver on AArch64. That is
  // because we didn't know whether a resolver will be generated when we first
  // encountered a use of the symbol named after this resolver. Therefore,
  // targets which support ifuncs should not return here unless we actually
  // found an ifunc.
  llvm::GlobalValue *ResolverGV = GetGlobalValue(ResolverName);
  if (ResolverGV &&
      (isa<llvm::GlobalIFunc>(ResolverGV) || !getTarget().supportsIFunc()))
    return ResolverGV;

  const CGFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
  llvm::FunctionType *DeclTy = getTypes().GetFunctionType(FI);

  // The resolver needs to be created. For target and target_clones, defer
  // creation until the end of the TU.
  if (FD->isTargetMultiVersion() || FD->isTargetClonesMultiVersion())
    AddDeferredMultiVersionResolverToEmit(GD);

  // For cpu_specific, don't create an ifunc yet because we don't know if the
  // cpu_dispatch will be emitted in this translation unit.
  if (getTarget().supportsIFunc() && !FD->isCPUSpecificMultiVersion()) {
    llvm::Type *ResolverType = llvm::FunctionType::get(
        llvm::PointerType::get(DeclTy,
                               getTypes().getTargetAddressSpace(FD->getType())),
        false);
    llvm::Constant *Resolver = GetOrCreateLLVMFunction(
        MangledName + ".resolver", ResolverType, GlobalDecl{},
        /*ForVTable=*/false);
    llvm::GlobalIFunc *GIF =
        llvm::GlobalIFunc::create(DeclTy, 0, getMultiversionLinkage(*this, GD),
                                  "", Resolver, &getModule());
    GIF->setName(ResolverName);
    SetCommonAttributes(FD, GIF);
    if (ResolverGV)
      replaceDeclarationWith(ResolverGV, GIF);
    return GIF;
  }

  llvm::Constant *Resolver = GetOrCreateLLVMFunction(
      ResolverName, DeclTy, GlobalDecl{}, /*ForVTable=*/false);
  assert(isa<llvm::GlobalValue>(Resolver) &&
         "Resolver should be created for the first time");
  SetCommonAttributes(FD, cast<llvm::GlobalValue>(Resolver));
  if (ResolverGV)
    replaceDeclarationWith(ResolverGV, Resolver);
  return Resolver;
}

bool CodeGenModule::shouldDropDLLAttribute(const Decl *D,
                                           const llvm::GlobalValue *GV) const {
  auto SC = GV->getDLLStorageClass();
  if (SC == llvm::GlobalValue::DefaultStorageClass)
    return false;
  const Decl *MRD = D->getMostRecentDecl();
  return (((SC == llvm::GlobalValue::DLLImportStorageClass &&
            !MRD->hasAttr<DLLImportAttr>()) ||
           (SC == llvm::GlobalValue::DLLExportStorageClass &&
            !MRD->hasAttr<DLLExportAttr>())) &&
          !shouldMapVisibilityToDLLExport(cast<NamedDecl>(MRD)));
}

/// GetOrCreateLLVMFunction - If the specified mangled name is not in the
/// module, create and return an llvm Function with the specified type. If there
/// is something in the module with the specified name, return it potentially
/// bitcasted to the right type.
///
/// If D is non-null, it specifies a decl that correspond to this.  This is used
/// to set the attributes on the function when it is first created.
llvm::Constant *CodeGenModule::GetOrCreateLLVMFunction(
    StringRef MangledName, llvm::Type *Ty, GlobalDecl GD, bool ForVTable,
    bool DontDefer, bool IsThunk, llvm::AttributeList ExtraAttrs,
    ForDefinition_t IsForDefinition) {
  const Decl *D = GD.getDecl();

  std::string NameWithoutMultiVersionMangling;
  // Any attempts to use a MultiVersion function should result in retrieving
  // the iFunc instead. Name Mangling will handle the rest of the changes.
  if (const FunctionDecl *FD = cast_or_null<FunctionDecl>(D)) {
    // For the device mark the function as one that should be emitted.
    if (getLangOpts().OpenMPIsTargetDevice && OpenMPRuntime &&
        !OpenMPRuntime->markAsGlobalTarget(GD) && FD->isDefined() &&
        !DontDefer && !IsForDefinition) {
      if (const FunctionDecl *FDDef = FD->getDefinition()) {
        GlobalDecl GDDef;
        if (const auto *CD = dyn_cast<CXXConstructorDecl>(FDDef))
          GDDef = GlobalDecl(CD, GD.getCtorType());
        else if (const auto *DD = dyn_cast<CXXDestructorDecl>(FDDef))
          GDDef = GlobalDecl(DD, GD.getDtorType());
        else
          GDDef = GlobalDecl(FDDef);
        EmitGlobal(GDDef);
      }
    }

    if (FD->isMultiVersion()) {
      UpdateMultiVersionNames(GD, FD, MangledName);
      if (!IsForDefinition) {
        // On AArch64 we do not immediatelly emit an ifunc resolver when a
        // function is used. Instead we defer the emission until we see a
        // default definition. In the meantime we just reference the symbol
        // without FMV mangling (it may or may not be replaced later).
        if (getTarget().getTriple().isAArch64()) {
          AddDeferredMultiVersionResolverToEmit(GD);
          NameWithoutMultiVersionMangling = getMangledNameImpl(
              *this, GD, FD, /*OmitMultiVersionMangling=*/true);
        } else
          return GetOrCreateMultiVersionResolver(GD);
      }
    }
  }

  if (!NameWithoutMultiVersionMangling.empty())
    MangledName = NameWithoutMultiVersionMangling;

  // Lookup the entry, lazily creating it if necessary.
  llvm::GlobalValue *Entry = GetGlobalValue(MangledName);
  if (Entry) {
    if (WeakRefReferences.erase(Entry)) {
      const FunctionDecl *FD = cast_or_null<FunctionDecl>(D);
      if (FD && !FD->hasAttr<WeakAttr>())
        Entry->setLinkage(llvm::Function::ExternalLinkage);
    }

    // Handle dropped DLL attributes.
    if (D && shouldDropDLLAttribute(D, Entry)) {
      Entry->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
      setDSOLocal(Entry);
    }

    // If there are two attempts to define the same mangled name, issue an
    // error.
    if (IsForDefinition && !Entry->isDeclaration()) {
      GlobalDecl OtherGD;
      // Check that GD is not yet in DiagnosedConflictingDefinitions is required
      // to make sure that we issue an error only once.
      if (lookupRepresentativeDecl(MangledName, OtherGD) &&
          (GD.getCanonicalDecl().getDecl() !=
           OtherGD.getCanonicalDecl().getDecl()) &&
          DiagnosedConflictingDefinitions.insert(GD).second) {
        getDiags().Report(D->getLocation(), diag::err_duplicate_mangled_name)
            << MangledName;
        getDiags().Report(OtherGD.getDecl()->getLocation(),
                          diag::note_previous_definition);
      }
    }

    if ((isa<llvm::Function>(Entry) || isa<llvm::GlobalAlias>(Entry)) &&
        (Entry->getValueType() == Ty)) {
      return Entry;
    }

    // Make sure the result is of the correct type.
    // (If function is requested for a definition, we always need to create a new
    // function, not just return a bitcast.)
    if (!IsForDefinition)
      return Entry;
  }

  // This function doesn't have a complete type (for example, the return
  // type is an incomplete struct). Use a fake type instead, and make
  // sure not to try to set attributes.
  bool IsIncompleteFunction = false;

  llvm::FunctionType *FTy;
  if (isa<llvm::FunctionType>(Ty)) {
    FTy = cast<llvm::FunctionType>(Ty);
  } else {
    FTy = llvm::FunctionType::get(VoidTy, false);
    IsIncompleteFunction = true;
  }

  llvm::Function *F =
      llvm::Function::Create(FTy, llvm::Function::ExternalLinkage,
                             Entry ? StringRef() : MangledName, &getModule());

  // Store the declaration associated with this function so it is potentially
  // updated by further declarations or definitions and emitted at the end.
  if (D && D->hasAttr<AnnotateAttr>())
    DeferredAnnotations[MangledName] = cast<ValueDecl>(D);

  // If we already created a function with the same mangled name (but different
  // type) before, take its name and add it to the list of functions to be
  // replaced with F at the end of CodeGen.
  //
  // This happens if there is a prototype for a function (e.g. "int f()") and
  // then a definition of a different type (e.g. "int f(int x)").
  if (Entry) {
    F->takeName(Entry);

    // This might be an implementation of a function without a prototype, in
    // which case, try to do special replacement of calls which match the new
    // prototype.  The really key thing here is that we also potentially drop
    // arguments from the call site so as to make a direct call, which makes the
    // inliner happier and suppresses a number of optimizer warnings (!) about
    // dropping arguments.
    if (!Entry->use_empty()) {
      ReplaceUsesOfNonProtoTypeWithRealFunction(Entry, F);
      Entry->removeDeadConstantUsers();
    }

    addGlobalValReplacement(Entry, F);
  }

  assert(F->getName() == MangledName && "name was uniqued!");
  if (D)
    SetFunctionAttributes(GD, F, IsIncompleteFunction, IsThunk);
  if (ExtraAttrs.hasFnAttrs()) {
    llvm::AttrBuilder B(F->getContext(), ExtraAttrs.getFnAttrs());
    F->addFnAttrs(B);
  }

  if (!DontDefer) {
    // All MSVC dtors other than the base dtor are linkonce_odr and delegate to
    // each other bottoming out with the base dtor.  Therefore we emit non-base
    // dtors on usage, even if there is no dtor definition in the TU.
    if (isa_and_nonnull<CXXDestructorDecl>(D) &&
        getCXXABI().useThunkForDtorVariant(cast<CXXDestructorDecl>(D),
                                           GD.getDtorType()))
      addDeferredDeclToEmit(GD);

    // This is the first use or definition of a mangled name.  If there is a
    // deferred decl with this name, remember that we need to emit it at the end
    // of the file.
    auto DDI = DeferredDecls.find(MangledName);
    if (DDI != DeferredDecls.end()) {
      // Move the potentially referenced deferred decl to the
      // DeferredDeclsToEmit list, and remove it from DeferredDecls (since we
      // don't need it anymore).
      addDeferredDeclToEmit(DDI->second);
      DeferredDecls.erase(DDI);

      // Otherwise, there are cases we have to worry about where we're
      // using a declaration for which we must emit a definition but where
      // we might not find a top-level definition:
      //   - member functions defined inline in their classes
      //   - friend functions defined inline in some class
      //   - special member functions with implicit definitions
      // If we ever change our AST traversal to walk into class methods,
      // this will be unnecessary.
      //
      // We also don't emit a definition for a function if it's going to be an
      // entry in a vtable, unless it's already marked as used.
    } else if (getLangOpts().CPlusPlus && D) {
      // Look for a declaration that's lexically in a record.
      for (const auto *FD = cast<FunctionDecl>(D)->getMostRecentDecl(); FD;
           FD = FD->getPreviousDecl()) {
        if (isa<CXXRecordDecl>(FD->getLexicalDeclContext())) {
          if (FD->doesThisDeclarationHaveABody()) {
            addDeferredDeclToEmit(GD.getWithDecl(FD));
            break;
          }
        }
      }
    }
  }

  // Make sure the result is of the requested type.
  if (!IsIncompleteFunction) {
    assert(F->getFunctionType() == Ty);
    return F;
  }

  return F;
}

/// GetAddrOfFunction - Return the address of the given function.  If Ty is
/// non-null, then this function will use the specified type if it has to
/// create it (this occurs when we see a definition of the function).
llvm::Constant *
CodeGenModule::GetAddrOfFunction(GlobalDecl GD, llvm::Type *Ty, bool ForVTable,
                                 bool DontDefer,
                                 ForDefinition_t IsForDefinition) {
  // If there was no specific requested type, just convert it now.
  if (!Ty) {
    const auto *FD = cast<FunctionDecl>(GD.getDecl());
    Ty = getTypes().ConvertType(FD->getType());
  }

  // Devirtualized destructor calls may come through here instead of via
  // getAddrOfCXXStructor. Make sure we use the MS ABI base destructor instead
  // of the complete destructor when necessary.
  if (const auto *DD = dyn_cast<CXXDestructorDecl>(GD.getDecl())) {
    if (getTarget().getCXXABI().isMicrosoft() &&
        GD.getDtorType() == Dtor_Complete &&
        DD->getParent()->getNumVBases() == 0)
      GD = GlobalDecl(DD, Dtor_Base);
  }

  StringRef MangledName = getMangledName(GD);
  auto *F = GetOrCreateLLVMFunction(MangledName, Ty, GD, ForVTable, DontDefer,
                                    /*IsThunk=*/false, llvm::AttributeList(),
                                    IsForDefinition);
  // Returns kernel handle for HIP kernel stub function.
  if (LangOpts.CUDA && !LangOpts.CUDAIsDevice &&
      cast<FunctionDecl>(GD.getDecl())->hasAttr<CUDAGlobalAttr>()) {
    auto *Handle = getCUDARuntime().getKernelHandle(
        cast<llvm::Function>(F->stripPointerCasts()), GD);
    if (IsForDefinition)
      return F;
    return Handle;
  }
  return F;
}

llvm::Constant *CodeGenModule::GetFunctionStart(const ValueDecl *Decl) {
  llvm::GlobalValue *F =
      cast<llvm::GlobalValue>(GetAddrOfFunction(Decl)->stripPointerCasts());

  return llvm::NoCFIValue::get(F);
}

static const FunctionDecl *
GetRuntimeFunctionDecl(ASTContext &C, StringRef Name) {
  TranslationUnitDecl *TUDecl = C.getTranslationUnitDecl();
  DeclContext *DC = TranslationUnitDecl::castToDeclContext(TUDecl);

  IdentifierInfo &CII = C.Idents.get(Name);
  for (const auto *Result : DC->lookup(&CII))
    if (const auto *FD = dyn_cast<FunctionDecl>(Result))
      return FD;

  if (!C.getLangOpts().CPlusPlus)
    return nullptr;

  // Demangle the premangled name from getTerminateFn()
  IdentifierInfo &CXXII =
      (Name == "_ZSt9terminatev" || Name == "?terminate@@YAXXZ")
          ? C.Idents.get("terminate")
          : C.Idents.get(Name);

  for (const auto &N : {"__cxxabiv1", "std"}) {
    IdentifierInfo &NS = C.Idents.get(N);
    for (const auto *Result : DC->lookup(&NS)) {
      const NamespaceDecl *ND = dyn_cast<NamespaceDecl>(Result);
      if (auto *LSD = dyn_cast<LinkageSpecDecl>(Result))
        for (const auto *Result : LSD->lookup(&NS))
          if ((ND = dyn_cast<NamespaceDecl>(Result)))
            break;

      if (ND)
        for (const auto *Result : ND->lookup(&CXXII))
          if (const auto *FD = dyn_cast<FunctionDecl>(Result))
            return FD;
    }
  }

  return nullptr;
}

/// CreateRuntimeFunction - Create a new runtime function with the specified
/// type and name.
llvm::FunctionCallee
CodeGenModule::CreateRuntimeFunction(llvm::FunctionType *FTy, StringRef Name,
                                     llvm::AttributeList ExtraAttrs, bool Local,
                                     bool AssumeConvergent) {
  if (AssumeConvergent) {
    ExtraAttrs =
        ExtraAttrs.addFnAttribute(VMContext, llvm::Attribute::Convergent);
  }

  llvm::Constant *C =
      GetOrCreateLLVMFunction(Name, FTy, GlobalDecl(), /*ForVTable=*/false,
                              /*DontDefer=*/false, /*IsThunk=*/false,
                              ExtraAttrs);

  if (auto *F = dyn_cast<llvm::Function>(C)) {
    if (F->empty()) {
      F->setCallingConv(getRuntimeCC());

      // In Windows Itanium environments, try to mark runtime functions
      // dllimport. For Mingw and MSVC, don't. We don't really know if the user
      // will link their standard library statically or dynamically. Marking
      // functions imported when they are not imported can cause linker errors
      // and warnings.
      if (!Local && getTriple().isWindowsItaniumEnvironment() &&
          !getCodeGenOpts().LTOVisibilityPublicStd) {
        const FunctionDecl *FD = GetRuntimeFunctionDecl(Context, Name);
        if (!FD || FD->hasAttr<DLLImportAttr>()) {
          F->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
          F->setLinkage(llvm::GlobalValue::ExternalLinkage);
        }
      }
      setDSOLocal(F);
      // FIXME: We should use CodeGenModule::SetLLVMFunctionAttributes() instead
      // of trying to approximate the attributes using the LLVM function
      // signature. This requires revising the API of CreateRuntimeFunction().
      markRegisterParameterAttributes(F);
    }
  }

  return {FTy, C};
}

/// GetOrCreateLLVMGlobal - If the specified mangled name is not in the module,
/// create and return an llvm GlobalVariable with the specified type and address
/// space. If there is something in the module with the specified name, return
/// it potentially bitcasted to the right type.
///
/// If D is non-null, it specifies a decl that correspond to this.  This is used
/// to set the attributes on the global when it is first created.
///
/// If IsForDefinition is true, it is guaranteed that an actual global with
/// type Ty will be returned, not conversion of a variable with the same
/// mangled name but some other type.
llvm::Constant *
CodeGenModule::GetOrCreateLLVMGlobal(StringRef MangledName, llvm::Type *Ty,
                                     LangAS AddrSpace, const VarDecl *D,
                                     ForDefinition_t IsForDefinition) {
  // Lookup the entry, lazily creating it if necessary.
  llvm::GlobalValue *Entry = GetGlobalValue(MangledName);
  unsigned TargetAS = getContext().getTargetAddressSpace(AddrSpace);
  if (Entry) {
    if (WeakRefReferences.erase(Entry)) {
      if (D && !D->hasAttr<WeakAttr>())
        Entry->setLinkage(llvm::Function::ExternalLinkage);
    }

    // Handle dropped DLL attributes.
    if (D && shouldDropDLLAttribute(D, Entry))
      Entry->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);

    if (LangOpts.OpenMP && !LangOpts.OpenMPSimd && D)
      getOpenMPRuntime().registerTargetGlobalVariable(D, Entry);

    if (Entry->getValueType() == Ty && Entry->getAddressSpace() == TargetAS)
      return Entry;

    // If there are two attempts to define the same mangled name, issue an
    // error.
    if (IsForDefinition && !Entry->isDeclaration()) {
      GlobalDecl OtherGD;
      const VarDecl *OtherD;

      // Check that D is not yet in DiagnosedConflictingDefinitions is required
      // to make sure that we issue an error only once.
      if (D && lookupRepresentativeDecl(MangledName, OtherGD) &&
          (D->getCanonicalDecl() != OtherGD.getCanonicalDecl().getDecl()) &&
          (OtherD = dyn_cast<VarDecl>(OtherGD.getDecl())) &&
          OtherD->hasInit() &&
          DiagnosedConflictingDefinitions.insert(D).second) {
        getDiags().Report(D->getLocation(), diag::err_duplicate_mangled_name)
            << MangledName;
        getDiags().Report(OtherGD.getDecl()->getLocation(),
                          diag::note_previous_definition);
      }
    }

    // Make sure the result is of the correct type.
    if (Entry->getType()->getAddressSpace() != TargetAS)
      return llvm::ConstantExpr::getAddrSpaceCast(
          Entry, llvm::PointerType::get(Ty->getContext(), TargetAS));

    // (If global is requested for a definition, we always need to create a new
    // global, not just return a bitcast.)
    if (!IsForDefinition)
      return Entry;
  }

  auto DAddrSpace = GetGlobalVarAddressSpace(D);

  auto *GV = new llvm::GlobalVariable(
      getModule(), Ty, false, llvm::GlobalValue::ExternalLinkage, nullptr,
      MangledName, nullptr, llvm::GlobalVariable::NotThreadLocal,
      getContext().getTargetAddressSpace(DAddrSpace));

  // If we already created a global with the same mangled name (but different
  // type) before, take its name and remove it from its parent.
  if (Entry) {
    GV->takeName(Entry);

    if (!Entry->use_empty()) {
      Entry->replaceAllUsesWith(GV);
    }

    Entry->eraseFromParent();
  }

  // This is the first use or definition of a mangled name.  If there is a
  // deferred decl with this name, remember that we need to emit it at the end
  // of the file.
  auto DDI = DeferredDecls.find(MangledName);
  if (DDI != DeferredDecls.end()) {
    // Move the potentially referenced deferred decl to the DeferredDeclsToEmit
    // list, and remove it from DeferredDecls (since we don't need it anymore).
    addDeferredDeclToEmit(DDI->second);
    DeferredDecls.erase(DDI);
  }

  // Handle things which are present even on external declarations.
  if (D) {
    if (LangOpts.OpenMP && !LangOpts.OpenMPSimd)
      getOpenMPRuntime().registerTargetGlobalVariable(D, GV);

    // FIXME: This code is overly simple and should be merged with other global
    // handling.
    GV->setConstant(D->getType().isConstantStorage(getContext(), false, false));

    GV->setAlignment(getContext().getDeclAlign(D).getAsAlign());

    setLinkageForGV(GV, D);

    if (D->getTLSKind()) {
      if (D->getTLSKind() == VarDecl::TLS_Dynamic)
        CXXThreadLocals.push_back(D);
      setTLSMode(GV, *D);
    }

    setGVProperties(GV, D);

    // If required by the ABI, treat declarations of static data members with
    // inline initializers as definitions.
    if (getContext().isMSStaticDataMemberInlineDefinition(D)) {
      EmitGlobalVarDefinition(D);
    }

    // Emit section information for extern variables.
    if (D->hasExternalStorage()) {
      if (const SectionAttr *SA = D->getAttr<SectionAttr>())
        GV->setSection(SA->getName());
    }

    // Handle XCore specific ABI requirements.
    if (getTriple().getArch() == llvm::Triple::xcore &&
        D->getLanguageLinkage() == CLanguageLinkage &&
        D->getType().isConstant(Context) &&
        isExternallyVisible(D->getLinkageAndVisibility().getLinkage()))
      GV->setSection(".cp.rodata");

    // Handle code model attribute
    if (const auto *CMA = D->getAttr<CodeModelAttr>())
      GV->setCodeModel(CMA->getModel());

    // Check if we a have a const declaration with an initializer, we may be
    // able to emit it as available_externally to expose it's value to the
    // optimizer.
    if (Context.getLangOpts().CPlusPlus && GV->hasExternalLinkage() &&
        D->getType().isConstQualified() && !GV->hasInitializer() &&
        !D->hasDefinition() && D->hasInit() && !D->hasAttr<DLLImportAttr>()) {
      const auto *Record =
          Context.getBaseElementType(D->getType())->getAsCXXRecordDecl();
      bool HasMutableFields = Record && Record->hasMutableFields();
      if (!HasMutableFields) {
        const VarDecl *InitDecl;
        const Expr *InitExpr = D->getAnyInitializer(InitDecl);
        if (InitExpr) {
          ConstantEmitter emitter(*this);
          llvm::Constant *Init = emitter.tryEmitForInitializer(*InitDecl);
          if (Init) {
            auto *InitType = Init->getType();
            if (GV->getValueType() != InitType) {
              // The type of the initializer does not match the definition.
              // This happens when an initializer has a different type from
              // the type of the global (because of padding at the end of a
              // structure for instance).
              GV->setName(StringRef());
              // Make a new global with the correct type, this is now guaranteed
              // to work.
              auto *NewGV = cast<llvm::GlobalVariable>(
                  GetAddrOfGlobalVar(D, InitType, IsForDefinition)
                      ->stripPointerCasts());

              // Erase the old global, since it is no longer used.
              GV->eraseFromParent();
              GV = NewGV;
            } else {
              GV->setInitializer(Init);
              GV->setConstant(true);
              GV->setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
            }
            emitter.finalize(GV);
          }
        }
      }
    }
  }

  if (D &&
      D->isThisDeclarationADefinition(Context) == VarDecl::DeclarationOnly) {
    getTargetCodeGenInfo().setTargetAttributes(D, GV, *this);
    // External HIP managed variables needed to be recorded for transformation
    // in both device and host compilations.
    if (getLangOpts().CUDA && D && D->hasAttr<HIPManagedAttr>() &&
        D->hasExternalStorage())
      getCUDARuntime().handleVarRegistration(D, *GV);
  }

  if (D)
    SanitizerMD->reportGlobal(GV, *D);

  LangAS ExpectedAS =
      D ? D->getType().getAddressSpace()
        : (LangOpts.OpenCL ? LangAS::opencl_global : LangAS::Default);
  assert(getContext().getTargetAddressSpace(ExpectedAS) == TargetAS);
  if (DAddrSpace != ExpectedAS) {
    return getTargetCodeGenInfo().performAddrSpaceCast(
        *this, GV, DAddrSpace, ExpectedAS,
        llvm::PointerType::get(getLLVMContext(), TargetAS));
  }

  return GV;
}

llvm::Constant *
CodeGenModule::GetAddrOfGlobal(GlobalDecl GD, ForDefinition_t IsForDefinition) {
  const Decl *D = GD.getDecl();

  if (isa<CXXConstructorDecl>(D) || isa<CXXDestructorDecl>(D))
    return getAddrOfCXXStructor(GD, /*FnInfo=*/nullptr, /*FnType=*/nullptr,
                                /*DontDefer=*/false, IsForDefinition);

  if (isa<CXXMethodDecl>(D)) {
    auto FInfo =
        &getTypes().arrangeCXXMethodDeclaration(cast<CXXMethodDecl>(D));
    auto Ty = getTypes().GetFunctionType(*FInfo);
    return GetAddrOfFunction(GD, Ty, /*ForVTable=*/false, /*DontDefer=*/false,
                             IsForDefinition);
  }

  if (isa<FunctionDecl>(D)) {
    const CGFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
    llvm::FunctionType *Ty = getTypes().GetFunctionType(FI);
    return GetAddrOfFunction(GD, Ty, /*ForVTable=*/false, /*DontDefer=*/false,
                             IsForDefinition);
  }

  return GetAddrOfGlobalVar(cast<VarDecl>(D), /*Ty=*/nullptr, IsForDefinition);
}

llvm::GlobalVariable *CodeGenModule::CreateOrReplaceCXXRuntimeVariable(
    StringRef Name, llvm::Type *Ty, llvm::GlobalValue::LinkageTypes Linkage,
    llvm::Align Alignment) {
  llvm::GlobalVariable *GV = getModule().getNamedGlobal(Name);
  llvm::GlobalVariable *OldGV = nullptr;

  if (GV) {
    // Check if the variable has the right type.
    if (GV->getValueType() == Ty)
      return GV;

    // Because C++ name mangling, the only way we can end up with an already
    // existing global with the same name is if it has been declared extern "C".
    assert(GV->isDeclaration() && "Declaration has wrong type!");
    OldGV = GV;
  }

  // Create a new variable.
  GV = new llvm::GlobalVariable(getModule(), Ty, /*isConstant=*/true,
                                Linkage, nullptr, Name);

  if (OldGV) {
    // Replace occurrences of the old variable if needed.
    GV->takeName(OldGV);

    if (!OldGV->use_empty()) {
      OldGV->replaceAllUsesWith(GV);
    }

    OldGV->eraseFromParent();
  }

  if (supportsCOMDAT() && GV->isWeakForLinker() &&
      !GV->hasAvailableExternallyLinkage())
    GV->setComdat(TheModule.getOrInsertComdat(GV->getName()));

  GV->setAlignment(Alignment);

  return GV;
}

/// GetAddrOfGlobalVar - Return the llvm::Constant for the address of the
/// given global variable.  If Ty is non-null and if the global doesn't exist,
/// then it will be created with the specified type instead of whatever the
/// normal requested type would be. If IsForDefinition is true, it is guaranteed
/// that an actual global with type Ty will be returned, not conversion of a
/// variable with the same mangled name but some other type.
llvm::Constant *CodeGenModule::GetAddrOfGlobalVar(const VarDecl *D,
                                                  llvm::Type *Ty,
                                           ForDefinition_t IsForDefinition) {
  assert(D->hasGlobalStorage() && "Not a global variable");
  QualType ASTTy = D->getType();
  if (!Ty)
    Ty = getTypes().ConvertTypeForMem(ASTTy);

  StringRef MangledName = getMangledName(D);
  return GetOrCreateLLVMGlobal(MangledName, Ty, ASTTy.getAddressSpace(), D,
                               IsForDefinition);
}

/// CreateRuntimeVariable - Create a new runtime global variable with the
/// specified type and name.
llvm::Constant *
CodeGenModule::CreateRuntimeVariable(llvm::Type *Ty,
                                     StringRef Name) {
  LangAS AddrSpace = getContext().getLangOpts().OpenCL ? LangAS::opencl_global
                                                       : LangAS::Default;
  auto *Ret = GetOrCreateLLVMGlobal(Name, Ty, AddrSpace, nullptr);
  setDSOLocal(cast<llvm::GlobalValue>(Ret->stripPointerCasts()));
  return Ret;
}

void CodeGenModule::EmitTentativeDefinition(const VarDecl *D) {
  assert(!D->getInit() && "Cannot emit definite definitions here!");

  StringRef MangledName = getMangledName(D);
  llvm::GlobalValue *GV = GetGlobalValue(MangledName);

  // We already have a definition, not declaration, with the same mangled name.
  // Emitting of declaration is not required (and actually overwrites emitted
  // definition).
  if (GV && !GV->isDeclaration())
    return;

  // If we have not seen a reference to this variable yet, place it into the
  // deferred declarations table to be emitted if needed later.
  if (!MustBeEmitted(D) && !GV) {
      DeferredDecls[MangledName] = D;
      return;
  }

  // The tentative definition is the only definition.
  EmitGlobalVarDefinition(D);
}

void CodeGenModule::EmitExternalDeclaration(const DeclaratorDecl *D) {
  if (auto const *V = dyn_cast<const VarDecl>(D))
    EmitExternalVarDeclaration(V);
  if (auto const *FD = dyn_cast<const FunctionDecl>(D))
    EmitExternalFunctionDeclaration(FD);
}

CharUnits CodeGenModule::GetTargetTypeStoreSize(llvm::Type *Ty) const {
  return Context.toCharUnitsFromBits(
      getDataLayout().getTypeStoreSizeInBits(Ty));
}

LangAS CodeGenModule::GetGlobalVarAddressSpace(const VarDecl *D) {
  if (LangOpts.OpenCL) {
    LangAS AS = D ? D->getType().getAddressSpace() : LangAS::opencl_global;
    assert(AS == LangAS::opencl_global ||
           AS == LangAS::opencl_global_device ||
           AS == LangAS::opencl_global_host ||
           AS == LangAS::opencl_constant ||
           AS == LangAS::opencl_local ||
           AS >= LangAS::FirstTargetAddressSpace);
    return AS;
  }

  if (LangOpts.SYCLIsDevice &&
      (!D || D->getType().getAddressSpace() == LangAS::Default))
    return LangAS::sycl_global;

  if (LangOpts.CUDA && LangOpts.CUDAIsDevice) {
    if (D) {
      if (D->hasAttr<CUDAConstantAttr>())
        return LangAS::cuda_constant;
      if (D->hasAttr<CUDASharedAttr>())
        return LangAS::cuda_shared;
      if (D->hasAttr<CUDADeviceAttr>())
        return LangAS::cuda_device;
      if (D->getType().isConstQualified())
        return LangAS::cuda_constant;
    }
    return LangAS::cuda_device;
  }

  if (LangOpts.OpenMP) {
    LangAS AS;
    if (OpenMPRuntime->hasAllocateAttributeForGlobalVar(D, AS))
      return AS;
  }
  return getTargetCodeGenInfo().getGlobalVarAddressSpace(*this, D);
}

LangAS CodeGenModule::GetGlobalConstantAddressSpace() const {
  // OpenCL v1.2 s6.5.3: a string literal is in the constant address space.
  if (LangOpts.OpenCL)
    return LangAS::opencl_constant;
  if (LangOpts.SYCLIsDevice)
    return LangAS::sycl_global;
  if (LangOpts.HIP && LangOpts.CUDAIsDevice && getTriple().isSPIRV())
    // For HIPSPV map literals to cuda_device (maps to CrossWorkGroup in SPIR-V)
    // instead of default AS (maps to Generic in SPIR-V). Otherwise, we end up
    // with OpVariable instructions with Generic storage class which is not
    // allowed (SPIR-V V1.6 s3.42.8). Also, mapping literals to SPIR-V
    // UniformConstant storage class is not viable as pointers to it may not be
    // casted to Generic pointers which are used to model HIP's "flat" pointers.
    return LangAS::cuda_device;
  if (auto AS = getTarget().getConstantAddressSpace())
    return *AS;
  return LangAS::Default;
}

// In address space agnostic languages, string literals are in default address
// space in AST. However, certain targets (e.g. amdgcn) request them to be
// emitted in constant address space in LLVM IR. To be consistent with other
// parts of AST, string literal global variables in constant address space
// need to be casted to default address space before being put into address
// map and referenced by other part of CodeGen.
// In OpenCL, string literals are in constant address space in AST, therefore
// they should not be casted to default address space.
static llvm::Constant *
castStringLiteralToDefaultAddressSpace(CodeGenModule &CGM,
                                       llvm::GlobalVariable *GV) {
  llvm::Constant *Cast = GV;
  if (!CGM.getLangOpts().OpenCL) {
    auto AS = CGM.GetGlobalConstantAddressSpace();
    if (AS != LangAS::Default)
      Cast = CGM.getTargetCodeGenInfo().performAddrSpaceCast(
          CGM, GV, AS, LangAS::Default,
          llvm::PointerType::get(
              CGM.getLLVMContext(),
              CGM.getContext().getTargetAddressSpace(LangAS::Default)));
  }
  return Cast;
}

template<typename SomeDecl>
void CodeGenModule::MaybeHandleStaticInExternC(const SomeDecl *D,
                                               llvm::GlobalValue *GV) {
  if (!getLangOpts().CPlusPlus)
    return;

  // Must have 'used' attribute, or else inline assembly can't rely on
  // the name existing.
  if (!D->template hasAttr<UsedAttr>())
    return;

  // Must have internal linkage and an ordinary name.
  if (!D->getIdentifier() || D->getFormalLinkage() != Linkage::Internal)
    return;

  // Must be in an extern "C" context. Entities declared directly within
  // a record are not extern "C" even if the record is in such a context.
  const SomeDecl *First = D->getFirstDecl();
  if (First->getDeclContext()->isRecord() || !First->isInExternCContext())
    return;

  // OK, this is an internal linkage entity inside an extern "C" linkage
  // specification. Make a note of that so we can give it the "expected"
  // mangled name if nothing else is using that name.
  std::pair<StaticExternCMap::iterator, bool> R =
      StaticExternCValues.insert(std::make_pair(D->getIdentifier(), GV));

  // If we have multiple internal linkage entities with the same name
  // in extern "C" regions, none of them gets that name.
  if (!R.second)
    R.first->second = nullptr;
}

static bool shouldBeInCOMDAT(CodeGenModule &CGM, const Decl &D) {
  if (!CGM.supportsCOMDAT())
    return false;

  if (D.hasAttr<SelectAnyAttr>())
    return true;

  GVALinkage Linkage;
  if (auto *VD = dyn_cast<VarDecl>(&D))
    Linkage = CGM.getContext().GetGVALinkageForVariable(VD);
  else
    Linkage = CGM.getContext().GetGVALinkageForFunction(cast<FunctionDecl>(&D));

  switch (Linkage) {
  case GVA_Internal:
  case GVA_AvailableExternally:
  case GVA_StrongExternal:
    return false;
  case GVA_DiscardableODR:
  case GVA_StrongODR:
    return true;
  }
  llvm_unreachable("No such linkage");
}

bool CodeGenModule::supportsCOMDAT() const {
  return getTriple().supportsCOMDAT();
}

void CodeGenModule::maybeSetTrivialComdat(const Decl &D,
                                          llvm::GlobalObject &GO) {
  if (!shouldBeInCOMDAT(*this, D))
    return;
  GO.setComdat(TheModule.getOrInsertComdat(GO.getName()));
}

const ABIInfo &CodeGenModule::getABIInfo() {
  return getTargetCodeGenInfo().getABIInfo();
}

/// Pass IsTentative as true if you want to create a tentative definition.
void CodeGenModule::EmitGlobalVarDefinition(const VarDecl *D,
                                            bool IsTentative) {
  // OpenCL global variables of sampler type are translated to function calls,
  // therefore no need to be translated.
  QualType ASTTy = D->getType();
  if (getLangOpts().OpenCL && ASTTy->isSamplerT())
    return;

  // If this is OpenMP device, check if it is legal to emit this global
  // normally.
  if (LangOpts.OpenMPIsTargetDevice && OpenMPRuntime &&
      OpenMPRuntime->emitTargetGlobalVariable(D))
    return;

  llvm::TrackingVH<llvm::Constant> Init;
  bool NeedsGlobalCtor = false;
  // Whether the definition of the variable is available externally.
  // If yes, we shouldn't emit the GloablCtor and GlobalDtor for the variable
  // since this is the job for its original source.
  bool IsDefinitionAvailableExternally =
      getContext().GetGVALinkageForVariable(D) == GVA_AvailableExternally;
  bool NeedsGlobalDtor =
      !IsDefinitionAvailableExternally &&
      D->needsDestruction(getContext()) == QualType::DK_cxx_destructor;

  // It is helpless to emit the definition for an available_externally variable
  // which can't be marked as const.
  // We don't need to check if it needs global ctor or dtor. See the above
  // comment for ideas.
  if (IsDefinitionAvailableExternally &&
      (!D->hasConstantInitialization() ||
       // TODO: Update this when we have interface to check constexpr
       // destructor.
       D->needsDestruction(getContext()) ||
       !D->getType().isConstantStorage(getContext(), true, true)))
    return;

  const VarDecl *InitDecl;
  const Expr *InitExpr = D->getAnyInitializer(InitDecl);

  std::optional<ConstantEmitter> emitter;

  // CUDA E.2.4.1 "__shared__ variables cannot have an initialization
  // as part of their declaration."  Sema has already checked for
  // error cases, so we just need to set Init to UndefValue.
  bool IsCUDASharedVar =
      getLangOpts().CUDAIsDevice && D->hasAttr<CUDASharedAttr>();
  // Shadows of initialized device-side global variables are also left
  // undefined.
  // Managed Variables should be initialized on both host side and device side.
  bool IsCUDAShadowVar =
      !getLangOpts().CUDAIsDevice && !D->hasAttr<HIPManagedAttr>() &&
      (D->hasAttr<CUDAConstantAttr>() || D->hasAttr<CUDADeviceAttr>() ||
       D->hasAttr<CUDASharedAttr>());
  bool IsCUDADeviceShadowVar =
      getLangOpts().CUDAIsDevice && !D->hasAttr<HIPManagedAttr>() &&
      (D->getType()->isCUDADeviceBuiltinSurfaceType() ||
       D->getType()->isCUDADeviceBuiltinTextureType());
  if (getLangOpts().CUDA &&
      (IsCUDASharedVar || IsCUDAShadowVar || IsCUDADeviceShadowVar))
    Init = llvm::UndefValue::get(getTypes().ConvertTypeForMem(ASTTy));
  else if (D->hasAttr<LoaderUninitializedAttr>())
    Init = llvm::UndefValue::get(getTypes().ConvertTypeForMem(ASTTy));
  else if (!InitExpr) {
    // This is a tentative definition; tentative definitions are
    // implicitly initialized with { 0 }.
    //
    // Note that tentative definitions are only emitted at the end of
    // a translation unit, so they should never have incomplete
    // type. In addition, EmitTentativeDefinition makes sure that we
    // never attempt to emit a tentative definition if a real one
    // exists. A use may still exists, however, so we still may need
    // to do a RAUW.
    assert(!ASTTy->isIncompleteType() && "Unexpected incomplete type");
    Init = EmitNullConstant(D->getType());
  } else {
    initializedGlobalDecl = GlobalDecl(D);
    emitter.emplace(*this);
    llvm::Constant *Initializer = emitter->tryEmitForInitializer(*InitDecl);
    if (!Initializer) {
      QualType T = InitExpr->getType();
      if (D->getType()->isReferenceType())
        T = D->getType();

      if (getLangOpts().CPlusPlus) {
        if (InitDecl->hasFlexibleArrayInit(getContext()))
          ErrorUnsupported(D, "flexible array initializer");
        Init = EmitNullConstant(T);

        if (!IsDefinitionAvailableExternally)
          NeedsGlobalCtor = true;
      } else {
        ErrorUnsupported(D, "static initializer");
        Init = llvm::UndefValue::get(getTypes().ConvertType(T));
      }
    } else {
      Init = Initializer;
      // We don't need an initializer, so remove the entry for the delayed
      // initializer position (just in case this entry was delayed) if we
      // also don't need to register a destructor.
      if (getLangOpts().CPlusPlus && !NeedsGlobalDtor)
        DelayedCXXInitPosition.erase(D);

#ifndef NDEBUG
      CharUnits VarSize = getContext().getTypeSizeInChars(ASTTy) +
                          InitDecl->getFlexibleArrayInitChars(getContext());
      CharUnits CstSize = CharUnits::fromQuantity(
          getDataLayout().getTypeAllocSize(Init->getType()));
      assert(VarSize == CstSize && "Emitted constant has unexpected size");
#endif
    }
  }

  llvm::Type* InitType = Init->getType();
  llvm::Constant *Entry =
      GetAddrOfGlobalVar(D, InitType, ForDefinition_t(!IsTentative));

  // Strip off pointer casts if we got them.
  Entry = Entry->stripPointerCasts();

  // Entry is now either a Function or GlobalVariable.
  auto *GV = dyn_cast<llvm::GlobalVariable>(Entry);

  // We have a definition after a declaration with the wrong type.
  // We must make a new GlobalVariable* and update everything that used OldGV
  // (a declaration or tentative definition) with the new GlobalVariable*
  // (which will be a definition).
  //
  // This happens if there is a prototype for a global (e.g.
  // "extern int x[];") and then a definition of a different type (e.g.
  // "int x[10];"). This also happens when an initializer has a different type
  // from the type of the global (this happens with unions).
  if (!GV || GV->getValueType() != InitType ||
      GV->getType()->getAddressSpace() !=
          getContext().getTargetAddressSpace(GetGlobalVarAddressSpace(D))) {

    // Move the old entry aside so that we'll create a new one.
    Entry->setName(StringRef());

    // Make a new global with the correct type, this is now guaranteed to work.
    GV = cast<llvm::GlobalVariable>(
        GetAddrOfGlobalVar(D, InitType, ForDefinition_t(!IsTentative))
            ->stripPointerCasts());

    // Replace all uses of the old global with the new global
    llvm::Constant *NewPtrForOldDecl =
        llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV,
                                                             Entry->getType());
    Entry->replaceAllUsesWith(NewPtrForOldDecl);

    // Erase the old global, since it is no longer used.
    cast<llvm::GlobalValue>(Entry)->eraseFromParent();
  }

  MaybeHandleStaticInExternC(D, GV);

  if (D->hasAttr<AnnotateAttr>())
    AddGlobalAnnotations(D, GV);

  // Set the llvm linkage type as appropriate.
  llvm::GlobalValue::LinkageTypes Linkage = getLLVMLinkageVarDefinition(D);

  // CUDA B.2.1 "The __device__ qualifier declares a variable that resides on
  // the device. [...]"
  // CUDA B.2.2 "The __constant__ qualifier, optionally used together with
  // __device__, declares a variable that: [...]
  // Is accessible from all the threads within the grid and from the host
  // through the runtime library (cudaGetSymbolAddress() / cudaGetSymbolSize()
  // / cudaMemcpyToSymbol() / cudaMemcpyFromSymbol())."
  if (LangOpts.CUDA) {
    if (LangOpts.CUDAIsDevice) {
      if (Linkage != llvm::GlobalValue::InternalLinkage &&
          (D->hasAttr<CUDADeviceAttr>() || D->hasAttr<CUDAConstantAttr>() ||
           D->getType()->isCUDADeviceBuiltinSurfaceType() ||
           D->getType()->isCUDADeviceBuiltinTextureType()))
        GV->setExternallyInitialized(true);
    } else {
      getCUDARuntime().internalizeDeviceSideVar(D, Linkage);
    }
    getCUDARuntime().handleVarRegistration(D, *GV);
  }

  GV->setInitializer(Init);
  if (emitter)
    emitter->finalize(GV);

  // If it is safe to mark the global 'constant', do so now.
  GV->setConstant(!NeedsGlobalCtor && !NeedsGlobalDtor &&
                  D->getType().isConstantStorage(getContext(), true, true));

  // If it is in a read-only section, mark it 'constant'.
  if (const SectionAttr *SA = D->getAttr<SectionAttr>()) {
    const ASTContext::SectionInfo &SI = Context.SectionInfos[SA->getName()];
    if ((SI.SectionFlags & ASTContext::PSF_Write) == 0)
      GV->setConstant(true);
  }

  CharUnits AlignVal = getContext().getDeclAlign(D);
  // Check for alignment specifed in an 'omp allocate' directive.
  if (std::optional<CharUnits> AlignValFromAllocate =
          getOMPAllocateAlignment(D))
    AlignVal = *AlignValFromAllocate;
  GV->setAlignment(AlignVal.getAsAlign());

  // On Darwin, unlike other Itanium C++ ABI platforms, the thread-wrapper
  // function is only defined alongside the variable, not also alongside
  // callers. Normally, all accesses to a thread_local go through the
  // thread-wrapper in order to ensure initialization has occurred, underlying
  // variable will never be used other than the thread-wrapper, so it can be
  // converted to internal linkage.
  //
  // However, if the variable has the 'constinit' attribute, it _can_ be
  // referenced directly, without calling the thread-wrapper, so the linkage
  // must not be changed.
  //
  // Additionally, if the variable isn't plain external linkage, e.g. if it's
  // weak or linkonce, the de-duplication semantics are important to preserve,
  // so we don't change the linkage.
  if (D->getTLSKind() == VarDecl::TLS_Dynamic &&
      Linkage == llvm::GlobalValue::ExternalLinkage &&
      Context.getTargetInfo().getTriple().isOSDarwin() &&
      !D->hasAttr<ConstInitAttr>())
    Linkage = llvm::GlobalValue::InternalLinkage;

  GV->setLinkage(Linkage);
  if (D->hasAttr<DLLImportAttr>())
    GV->setDLLStorageClass(llvm::GlobalVariable::DLLImportStorageClass);
  else if (D->hasAttr<DLLExportAttr>())
    GV->setDLLStorageClass(llvm::GlobalVariable::DLLExportStorageClass);
  else
    GV->setDLLStorageClass(llvm::GlobalVariable::DefaultStorageClass);

  if (Linkage == llvm::GlobalVariable::CommonLinkage) {
    // common vars aren't constant even if declared const.
    GV->setConstant(false);
    // Tentative definition of global variables may be initialized with
    // non-zero null pointers. In this case they should have weak linkage
    // since common linkage must have zero initializer and must not have
    // explicit section therefore cannot have non-zero initial value.
    if (!GV->getInitializer()->isNullValue())
      GV->setLinkage(llvm::GlobalVariable::WeakAnyLinkage);
  }

  setNonAliasAttributes(D, GV);

  if (D->getTLSKind() && !GV->isThreadLocal()) {
    if (D->getTLSKind() == VarDecl::TLS_Dynamic)
      CXXThreadLocals.push_back(D);
    setTLSMode(GV, *D);
  }

  maybeSetTrivialComdat(*D, *GV);

  // Emit the initializer function if necessary.
  if (NeedsGlobalCtor || NeedsGlobalDtor)
    EmitCXXGlobalVarDeclInitFunc(D, GV, NeedsGlobalCtor);

  SanitizerMD->reportGlobal(GV, *D, NeedsGlobalCtor);

  // Emit global variable debug information.
  if (CGDebugInfo *DI = getModuleDebugInfo())
    if (getCodeGenOpts().hasReducedDebugInfo())
      DI->EmitGlobalVariable(GV, D);
}

void CodeGenModule::EmitExternalVarDeclaration(const VarDecl *D) {
  if (CGDebugInfo *DI = getModuleDebugInfo())
    if (getCodeGenOpts().hasReducedDebugInfo()) {
      QualType ASTTy = D->getType();
      llvm::Type *Ty = getTypes().ConvertTypeForMem(D->getType());
      llvm::Constant *GV =
          GetOrCreateLLVMGlobal(D->getName(), Ty, ASTTy.getAddressSpace(), D);
      DI->EmitExternalVariable(
          cast<llvm::GlobalVariable>(GV->stripPointerCasts()), D);
    }
}

void CodeGenModule::EmitExternalFunctionDeclaration(const FunctionDecl *FD) {
  if (CGDebugInfo *DI = getModuleDebugInfo())
    if (getCodeGenOpts().hasReducedDebugInfo()) {
      auto *Ty = getTypes().ConvertType(FD->getType());
      StringRef MangledName = getMangledName(FD);
      auto *Fn = dyn_cast<llvm::Function>(
          GetOrCreateLLVMFunction(MangledName, Ty, FD, /* ForVTable */ false));
      if (!Fn->getSubprogram())
        DI->EmitFunctionDecl(FD, FD->getLocation(), FD->getType(), Fn);
    }
}

static bool isVarDeclStrongDefinition(const ASTContext &Context,
                                      CodeGenModule &CGM, const VarDecl *D,
                                      bool NoCommon) {
  // Don't give variables common linkage if -fno-common was specified unless it
  // was overridden by a NoCommon attribute.
  if ((NoCommon || D->hasAttr<NoCommonAttr>()) && !D->hasAttr<CommonAttr>())
    return true;

  // C11 6.9.2/2:
  //   A declaration of an identifier for an object that has file scope without
  //   an initializer, and without a storage-class specifier or with the
  //   storage-class specifier static, constitutes a tentative definition.
  if (D->getInit() || D->hasExternalStorage())
    return true;

  // A variable cannot be both common and exist in a section.
  if (D->hasAttr<SectionAttr>())
    return true;

  // A variable cannot be both common and exist in a section.
  // We don't try to determine which is the right section in the front-end.
  // If no specialized section name is applicable, it will resort to default.
  if (D->hasAttr<PragmaClangBSSSectionAttr>() ||
      D->hasAttr<PragmaClangDataSectionAttr>() ||
      D->hasAttr<PragmaClangRelroSectionAttr>() ||
      D->hasAttr<PragmaClangRodataSectionAttr>())
    return true;

  // Thread local vars aren't considered common linkage.
  if (D->getTLSKind())
    return true;

  // Tentative definitions marked with WeakImportAttr are true definitions.
  if (D->hasAttr<WeakImportAttr>())
    return true;

  // A variable cannot be both common and exist in a comdat.
  if (shouldBeInCOMDAT(CGM, *D))
    return true;

  // Declarations with a required alignment do not have common linkage in MSVC
  // mode.
  if (Context.getTargetInfo().getCXXABI().isMicrosoft()) {
    if (D->hasAttr<AlignedAttr>())
      return true;
    QualType VarType = D->getType();
    if (Context.isAlignmentRequired(VarType))
      return true;

    if (const auto *RT = VarType->getAs<RecordType>()) {
      const RecordDecl *RD = RT->getDecl();
      for (const FieldDecl *FD : RD->fields()) {
        if (FD->isBitField())
          continue;
        if (FD->hasAttr<AlignedAttr>())
          return true;
        if (Context.isAlignmentRequired(FD->getType()))
          return true;
      }
    }
  }

  // Microsoft's link.exe doesn't support alignments greater than 32 bytes for
  // common symbols, so symbols with greater alignment requirements cannot be
  // common.
  // Other COFF linkers (ld.bfd and LLD) support arbitrary power-of-two
  // alignments for common symbols via the aligncomm directive, so this
  // restriction only applies to MSVC environments.
  if (Context.getTargetInfo().getTriple().isKnownWindowsMSVCEnvironment() &&
      Context.getTypeAlignIfKnown(D->getType()) >
          Context.toBits(CharUnits::fromQuantity(32)))
    return true;

  return false;
}

llvm::GlobalValue::LinkageTypes
CodeGenModule::getLLVMLinkageForDeclarator(const DeclaratorDecl *D,
                                           GVALinkage Linkage) {
  if (Linkage == GVA_Internal)
    return llvm::Function::InternalLinkage;

  if (D->hasAttr<WeakAttr>())
    return llvm::GlobalVariable::WeakAnyLinkage;

  if (const auto *FD = D->getAsFunction())
    if (FD->isMultiVersion() && Linkage == GVA_AvailableExternally)
      return llvm::GlobalVariable::LinkOnceAnyLinkage;

  // We are guaranteed to have a strong definition somewhere else,
  // so we can use available_externally linkage.
  if (Linkage == GVA_AvailableExternally)
    return llvm::GlobalValue::AvailableExternallyLinkage;

  // Note that Apple's kernel linker doesn't support symbol
  // coalescing, so we need to avoid linkonce and weak linkages there.
  // Normally, this means we just map to internal, but for explicit
  // instantiations we'll map to external.

  // In C++, the compiler has to emit a definition in every translation unit
  // that references the function.  We should use linkonce_odr because
  // a) if all references in this translation unit are optimized away, we
  // don't need to codegen it.  b) if the function persists, it needs to be
  // merged with other definitions. c) C++ has the ODR, so we know the
  // definition is dependable.
  if (Linkage == GVA_DiscardableODR)
    return !Context.getLangOpts().AppleKext ? llvm::Function::LinkOnceODRLinkage
                                            : llvm::Function::InternalLinkage;

  // An explicit instantiation of a template has weak linkage, since
  // explicit instantiations can occur in multiple translation units
  // and must all be equivalent. However, we are not allowed to
  // throw away these explicit instantiations.
  //
  // CUDA/HIP: For -fno-gpu-rdc case, device code is limited to one TU,
  // so say that CUDA templates are either external (for kernels) or internal.
  // This lets llvm perform aggressive inter-procedural optimizations. For
  // -fgpu-rdc case, device function calls across multiple TU's are allowed,
  // therefore we need to follow the normal linkage paradigm.
  if (Linkage == GVA_StrongODR) {
    if (getLangOpts().AppleKext)
      return llvm::Function::ExternalLinkage;
    if (getLangOpts().CUDA && getLangOpts().CUDAIsDevice &&
        !getLangOpts().GPURelocatableDeviceCode)
      return D->hasAttr<CUDAGlobalAttr>() ? llvm::Function::ExternalLinkage
                                          : llvm::Function::InternalLinkage;
    return llvm::Function::WeakODRLinkage;
  }

  // C++ doesn't have tentative definitions and thus cannot have common
  // linkage.
  if (!getLangOpts().CPlusPlus && isa<VarDecl>(D) &&
      !isVarDeclStrongDefinition(Context, *this, cast<VarDecl>(D),
                                 CodeGenOpts.NoCommon))
    return llvm::GlobalVariable::CommonLinkage;

  // selectany symbols are externally visible, so use weak instead of
  // linkonce.  MSVC optimizes away references to const selectany globals, so
  // all definitions should be the same and ODR linkage should be used.
  // http://msdn.microsoft.com/en-us/library/5tkz6s71.aspx
  if (D->hasAttr<SelectAnyAttr>())
    return llvm::GlobalVariable::WeakODRLinkage;

  // Otherwise, we have strong external linkage.
  assert(Linkage == GVA_StrongExternal);
  return llvm::GlobalVariable::ExternalLinkage;
}

llvm::GlobalValue::LinkageTypes
CodeGenModule::getLLVMLinkageVarDefinition(const VarDecl *VD) {
  GVALinkage Linkage = getContext().GetGVALinkageForVariable(VD);
  return getLLVMLinkageForDeclarator(VD, Linkage);
}

/// Replace the uses of a function that was declared with a non-proto type.
/// We want to silently drop extra arguments from call sites
static void replaceUsesOfNonProtoConstant(llvm::Constant *old,
                                          llvm::Function *newFn) {
  // Fast path.
  if (old->use_empty())
    return;

  llvm::Type *newRetTy = newFn->getReturnType();
  SmallVector<llvm::Value *, 4> newArgs;

  SmallVector<llvm::CallBase *> callSitesToBeRemovedFromParent;

  for (llvm::Value::use_iterator ui = old->use_begin(), ue = old->use_end();
       ui != ue; ui++) {
    llvm::User *user = ui->getUser();

    // Recognize and replace uses of bitcasts.  Most calls to
    // unprototyped functions will use bitcasts.
    if (auto *bitcast = dyn_cast<llvm::ConstantExpr>(user)) {
      if (bitcast->getOpcode() == llvm::Instruction::BitCast)
        replaceUsesOfNonProtoConstant(bitcast, newFn);
      continue;
    }

    // Recognize calls to the function.
    llvm::CallBase *callSite = dyn_cast<llvm::CallBase>(user);
    if (!callSite)
      continue;
    if (!callSite->isCallee(&*ui))
      continue;

    // If the return types don't match exactly, then we can't
    // transform this call unless it's dead.
    if (callSite->getType() != newRetTy && !callSite->use_empty())
      continue;

    // Get the call site's attribute list.
    SmallVector<llvm::AttributeSet, 8> newArgAttrs;
    llvm::AttributeList oldAttrs = callSite->getAttributes();

    // If the function was passed too few arguments, don't transform.
    unsigned newNumArgs = newFn->arg_size();
    if (callSite->arg_size() < newNumArgs)
      continue;

    // If extra arguments were passed, we silently drop them.
    // If any of the types mismatch, we don't transform.
    unsigned argNo = 0;
    bool dontTransform = false;
    for (llvm::Argument &A : newFn->args()) {
      if (callSite->getArgOperand(argNo)->getType() != A.getType()) {
        dontTransform = true;
        break;
      }

      // Add any parameter attributes.
      newArgAttrs.push_back(oldAttrs.getParamAttrs(argNo));
      argNo++;
    }
    if (dontTransform)
      continue;

    // Okay, we can transform this.  Create the new call instruction and copy
    // over the required information.
    newArgs.append(callSite->arg_begin(), callSite->arg_begin() + argNo);

    // Copy over any operand bundles.
    SmallVector<llvm::OperandBundleDef, 1> newBundles;
    callSite->getOperandBundlesAsDefs(newBundles);

    llvm::CallBase *newCall;
    if (isa<llvm::CallInst>(callSite)) {
      newCall =
          llvm::CallInst::Create(newFn, newArgs, newBundles, "", callSite);
    } else {
      auto *oldInvoke = cast<llvm::InvokeInst>(callSite);
      newCall = llvm::InvokeInst::Create(newFn, oldInvoke->getNormalDest(),
                                         oldInvoke->getUnwindDest(), newArgs,
                                         newBundles, "", callSite);
    }
    newArgs.clear(); // for the next iteration

    if (!newCall->getType()->isVoidTy())
      newCall->takeName(callSite);
    newCall->setAttributes(
        llvm::AttributeList::get(newFn->getContext(), oldAttrs.getFnAttrs(),
                                 oldAttrs.getRetAttrs(), newArgAttrs));
    newCall->setCallingConv(callSite->getCallingConv());

    // Finally, remove the old call, replacing any uses with the new one.
    if (!callSite->use_empty())
      callSite->replaceAllUsesWith(newCall);

    // Copy debug location attached to CI.
    if (callSite->getDebugLoc())
      newCall->setDebugLoc(callSite->getDebugLoc());

    callSitesToBeRemovedFromParent.push_back(callSite);
  }

  for (auto *callSite : callSitesToBeRemovedFromParent) {
    callSite->eraseFromParent();
  }
}

/// ReplaceUsesOfNonProtoTypeWithRealFunction - This function is called when we
/// implement a function with no prototype, e.g. "int foo() {}".  If there are
/// existing call uses of the old function in the module, this adjusts them to
/// call the new function directly.
///
/// This is not just a cleanup: the always_inline pass requires direct calls to
/// functions to be able to inline them.  If there is a bitcast in the way, it
/// won't inline them.  Instcombine normally deletes these calls, but it isn't
/// run at -O0.
static void ReplaceUsesOfNonProtoTypeWithRealFunction(llvm::GlobalValue *Old,
                                                      llvm::Function *NewFn) {
  // If we're redefining a global as a function, don't transform it.
  if (!isa<llvm::Function>(Old)) return;

  replaceUsesOfNonProtoConstant(Old, NewFn);
}

void CodeGenModule::HandleCXXStaticMemberVarInstantiation(VarDecl *VD) {
  auto DK = VD->isThisDeclarationADefinition();
  if ((DK == VarDecl::Definition && VD->hasAttr<DLLImportAttr>()) ||
      (LangOpts.CUDA && !shouldEmitCUDAGlobalVar(VD)))
    return;

  TemplateSpecializationKind TSK = VD->getTemplateSpecializationKind();
  // If we have a definition, this might be a deferred decl. If the
  // instantiation is explicit, make sure we emit it at the end.
  if (VD->getDefinition() && TSK == TSK_ExplicitInstantiationDefinition)
    GetAddrOfGlobalVar(VD);

  EmitTopLevelDecl(VD);
}

void CodeGenModule::EmitGlobalFunctionDefinition(GlobalDecl GD,
                                                 llvm::GlobalValue *GV) {
  const auto *D = cast<FunctionDecl>(GD.getDecl());

  // Compute the function info and LLVM type.
  const CGFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
  llvm::FunctionType *Ty = getTypes().GetFunctionType(FI);

  // Get or create the prototype for the function.
  if (!GV || (GV->getValueType() != Ty))
    GV = cast<llvm::GlobalValue>(GetAddrOfFunction(GD, Ty, /*ForVTable=*/false,
                                                   /*DontDefer=*/true,
                                                   ForDefinition));

  // Already emitted.
  if (!GV->isDeclaration())
    return;

  // We need to set linkage and visibility on the function before
  // generating code for it because various parts of IR generation
  // want to propagate this information down (e.g. to local static
  // declarations).
  auto *Fn = cast<llvm::Function>(GV);
  setFunctionLinkage(GD, Fn);

  // FIXME: this is redundant with part of setFunctionDefinitionAttributes
  setGVProperties(Fn, GD);

  MaybeHandleStaticInExternC(D, Fn);

  maybeSetTrivialComdat(*D, *Fn);

  CodeGenFunction(*this).GenerateCode(GD, Fn, FI);

  setNonAliasAttributes(GD, Fn);
  SetLLVMFunctionAttributesForDefinition(D, Fn);

  if (const ConstructorAttr *CA = D->getAttr<ConstructorAttr>())
    AddGlobalCtor(Fn, CA->getPriority());
  if (const DestructorAttr *DA = D->getAttr<DestructorAttr>())
    AddGlobalDtor(Fn, DA->getPriority(), true);
  if (getLangOpts().OpenMP && D->hasAttr<OMPDeclareTargetDeclAttr>())
    getOpenMPRuntime().emitDeclareTargetFunction(D, GV);
}

void CodeGenModule::EmitAliasDefinition(GlobalDecl GD) {
  const auto *D = cast<ValueDecl>(GD.getDecl());
  const AliasAttr *AA = D->getAttr<AliasAttr>();
  assert(AA && "Not an alias?");

  StringRef MangledName = getMangledName(GD);

  if (AA->getAliasee() == MangledName) {
    Diags.Report(AA->getLocation(), diag::err_cyclic_alias) << 0;
    return;
  }

  // If there is a definition in the module, then it wins over the alias.
  // This is dubious, but allow it to be safe.  Just ignore the alias.
  llvm::GlobalValue *Entry = GetGlobalValue(MangledName);
  if (Entry && !Entry->isDeclaration())
    return;

  Aliases.push_back(GD);

  llvm::Type *DeclTy = getTypes().ConvertTypeForMem(D->getType());

  // Create a reference to the named value.  This ensures that it is emitted
  // if a deferred decl.
  llvm::Constant *Aliasee;
  llvm::GlobalValue::LinkageTypes LT;
  if (isa<llvm::FunctionType>(DeclTy)) {
    Aliasee = GetOrCreateLLVMFunction(AA->getAliasee(), DeclTy, GD,
                                      /*ForVTable=*/false);
    LT = getFunctionLinkage(GD);
  } else {
    Aliasee = GetOrCreateLLVMGlobal(AA->getAliasee(), DeclTy, LangAS::Default,
                                    /*D=*/nullptr);
    if (const auto *VD = dyn_cast<VarDecl>(GD.getDecl()))
      LT = getLLVMLinkageVarDefinition(VD);
    else
      LT = getFunctionLinkage(GD);
  }

  // Create the new alias itself, but don't set a name yet.
  unsigned AS = Aliasee->getType()->getPointerAddressSpace();
  auto *GA =
      llvm::GlobalAlias::create(DeclTy, AS, LT, "", Aliasee, &getModule());

  if (Entry) {
    if (GA->getAliasee() == Entry) {
      Diags.Report(AA->getLocation(), diag::err_cyclic_alias) << 0;
      return;
    }

    assert(Entry->isDeclaration());

    // If there is a declaration in the module, then we had an extern followed
    // by the alias, as in:
    //   extern int test6();
    //   ...
    //   int test6() __attribute__((alias("test7")));
    //
    // Remove it and replace uses of it with the alias.
    GA->takeName(Entry);

    Entry->replaceAllUsesWith(GA);
    Entry->eraseFromParent();
  } else {
    GA->setName(MangledName);
  }

  // Set attributes which are particular to an alias; this is a
  // specialization of the attributes which may be set on a global
  // variable/function.
  if (D->hasAttr<WeakAttr>() || D->hasAttr<WeakRefAttr>() ||
      D->isWeakImported()) {
    GA->setLinkage(llvm::Function::WeakAnyLinkage);
  }

  if (const auto *VD = dyn_cast<VarDecl>(D))
    if (VD->getTLSKind())
      setTLSMode(GA, *VD);

  SetCommonAttributes(GD, GA);

  // Emit global alias debug information.
  if (isa<VarDecl>(D))
    if (CGDebugInfo *DI = getModuleDebugInfo())
      DI->EmitGlobalAlias(cast<llvm::GlobalValue>(GA->getAliasee()->stripPointerCasts()), GD);
}

void CodeGenModule::emitIFuncDefinition(GlobalDecl GD) {
  const auto *D = cast<ValueDecl>(GD.getDecl());
  const IFuncAttr *IFA = D->getAttr<IFuncAttr>();
  assert(IFA && "Not an ifunc?");

  StringRef MangledName = getMangledName(GD);

  if (IFA->getResolver() == MangledName) {
    Diags.Report(IFA->getLocation(), diag::err_cyclic_alias) << 1;
    return;
  }

  // Report an error if some definition overrides ifunc.
  llvm::GlobalValue *Entry = GetGlobalValue(MangledName);
  if (Entry && !Entry->isDeclaration()) {
    GlobalDecl OtherGD;
    if (lookupRepresentativeDecl(MangledName, OtherGD) &&
        DiagnosedConflictingDefinitions.insert(GD).second) {
      Diags.Report(D->getLocation(), diag::err_duplicate_mangled_name)
          << MangledName;
      Diags.Report(OtherGD.getDecl()->getLocation(),
                   diag::note_previous_definition);
    }
    return;
  }

  Aliases.push_back(GD);

  // The resolver might not be visited yet. Specify a dummy non-function type to
  // indicate IsIncompleteFunction. Either the type is ignored (if the resolver
  // was emitted) or the whole function will be replaced (if the resolver has
  // not been emitted).
  llvm::Constant *Resolver =
      GetOrCreateLLVMFunction(IFA->getResolver(), VoidTy, {},
                              /*ForVTable=*/false);
  llvm::Type *DeclTy = getTypes().ConvertTypeForMem(D->getType());
  llvm::GlobalIFunc *GIF =
      llvm::GlobalIFunc::create(DeclTy, 0, llvm::Function::ExternalLinkage,
                                "", Resolver, &getModule());
  if (Entry) {
    if (GIF->getResolver() == Entry) {
      Diags.Report(IFA->getLocation(), diag::err_cyclic_alias) << 1;
      return;
    }
    assert(Entry->isDeclaration());

    // If there is a declaration in the module, then we had an extern followed
    // by the ifunc, as in:
    //   extern int test();
    //   ...
    //   int test() __attribute__((ifunc("resolver")));
    //
    // Remove it and replace uses of it with the ifunc.
    GIF->takeName(Entry);

    Entry->replaceAllUsesWith(GIF);
    Entry->eraseFromParent();
  } else
    GIF->setName(MangledName);
  SetCommonAttributes(GD, GIF);
}

llvm::Function *CodeGenModule::getIntrinsic(unsigned IID,
                                            ArrayRef<llvm::Type*> Tys) {
  return llvm::Intrinsic::getDeclaration(&getModule(), (llvm::Intrinsic::ID)IID,
                                         Tys);
}

static llvm::StringMapEntry<llvm::GlobalVariable *> &
GetConstantCFStringEntry(llvm::StringMap<llvm::GlobalVariable *> &Map,
                         const StringLiteral *Literal, bool TargetIsLSB,
                         bool &IsUTF16, unsigned &StringLength) {
  StringRef String = Literal->getString();
  unsigned NumBytes = String.size();

  // Check for simple case.
  if (!Literal->containsNonAsciiOrNull()) {
    StringLength = NumBytes;
    return *Map.insert(std::make_pair(String, nullptr)).first;
  }

  // Otherwise, convert the UTF8 literals into a string of shorts.
  IsUTF16 = true;

  SmallVector<llvm::UTF16, 128> ToBuf(NumBytes + 1); // +1 for ending nulls.
  const llvm::UTF8 *FromPtr = (const llvm::UTF8 *)String.data();
  llvm::UTF16 *ToPtr = &ToBuf[0];

  (void)llvm::ConvertUTF8toUTF16(&FromPtr, FromPtr + NumBytes, &ToPtr,
                                 ToPtr + NumBytes, llvm::strictConversion);

  // ConvertUTF8toUTF16 returns the length in ToPtr.
  StringLength = ToPtr - &ToBuf[0];

  // Add an explicit null.
  *ToPtr = 0;
  return *Map.insert(std::make_pair(
                         StringRef(reinterpret_cast<const char *>(ToBuf.data()),
                                   (StringLength + 1) * 2),
                         nullptr)).first;
}

ConstantAddress
CodeGenModule::GetAddrOfConstantCFString(const StringLiteral *Literal) {
  unsigned StringLength = 0;
  bool isUTF16 = false;
  llvm::StringMapEntry<llvm::GlobalVariable *> &Entry =
      GetConstantCFStringEntry(CFConstantStringMap, Literal,
                               getDataLayout().isLittleEndian(), isUTF16,
                               StringLength);

  if (auto *C = Entry.second)
    return ConstantAddress(
        C, C->getValueType(), CharUnits::fromQuantity(C->getAlignment()));

  const ASTContext &Context = getContext();
  const llvm::Triple &Triple = getTriple();

  const auto CFRuntime = getLangOpts().CFRuntime;
  const bool IsSwiftABI =
      static_cast<unsigned>(CFRuntime) >=
      static_cast<unsigned>(LangOptions::CoreFoundationABI::Swift);
  const bool IsSwift4_1 = CFRuntime == LangOptions::CoreFoundationABI::Swift4_1;

  // If we don't already have it, get __CFConstantStringClassReference.
  if (!CFConstantStringClassRef) {
    const char *CFConstantStringClassName = "__CFConstantStringClassReference";
    llvm::Type *Ty = getTypes().ConvertType(getContext().IntTy);
    Ty = llvm::ArrayType::get(Ty, 0);

    switch (CFRuntime) {
    default: break;
    case LangOptions::CoreFoundationABI::Swift: [[fallthrough]];
    case LangOptions::CoreFoundationABI::Swift5_0:
      CFConstantStringClassName =
          Triple.isOSDarwin() ? "$s15SwiftFoundation19_NSCFConstantStringCN"
                              : "$s10Foundation19_NSCFConstantStringCN";
      Ty = IntPtrTy;
      break;
    case LangOptions::CoreFoundationABI::Swift4_2:
      CFConstantStringClassName =
          Triple.isOSDarwin() ? "$S15SwiftFoundation19_NSCFConstantStringCN"
                              : "$S10Foundation19_NSCFConstantStringCN";
      Ty = IntPtrTy;
      break;
    case LangOptions::CoreFoundationABI::Swift4_1:
      CFConstantStringClassName =
          Triple.isOSDarwin() ? "__T015SwiftFoundation19_NSCFConstantStringCN"
                              : "__T010Foundation19_NSCFConstantStringCN";
      Ty = IntPtrTy;
      break;
    }

    llvm::Constant *C = CreateRuntimeVariable(Ty, CFConstantStringClassName);

    if (Triple.isOSBinFormatELF() || Triple.isOSBinFormatCOFF()) {
      llvm::GlobalValue *GV = nullptr;

      if ((GV = dyn_cast<llvm::GlobalValue>(C))) {
        IdentifierInfo &II = Context.Idents.get(GV->getName());
        TranslationUnitDecl *TUDecl = Context.getTranslationUnitDecl();
        DeclContext *DC = TranslationUnitDecl::castToDeclContext(TUDecl);

        const VarDecl *VD = nullptr;
        for (const auto *Result : DC->lookup(&II))
          if ((VD = dyn_cast<VarDecl>(Result)))
            break;

        if (Triple.isOSBinFormatELF()) {
          if (!VD)
            GV->setLinkage(llvm::GlobalValue::ExternalLinkage);
        } else {
          GV->setLinkage(llvm::GlobalValue::ExternalLinkage);
          if (!VD || !VD->hasAttr<DLLExportAttr>())
            GV->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
          else
            GV->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
        }

        setDSOLocal(GV);
      }
    }

    // Decay array -> ptr
    CFConstantStringClassRef =
        IsSwiftABI ? llvm::ConstantExpr::getPtrToInt(C, Ty) : C;
  }

  QualType CFTy = Context.getCFConstantStringType();

  auto *STy = cast<llvm::StructType>(getTypes().ConvertType(CFTy));

  ConstantInitBuilder Builder(*this);
  auto Fields = Builder.beginStruct(STy);

  // Class pointer.
  Fields.add(cast<llvm::Constant>(CFConstantStringClassRef));

  // Flags.
  if (IsSwiftABI) {
    Fields.addInt(IntPtrTy, IsSwift4_1 ? 0x05 : 0x01);
    Fields.addInt(Int64Ty, isUTF16 ? 0x07d0 : 0x07c8);
  } else {
    Fields.addInt(IntTy, isUTF16 ? 0x07d0 : 0x07C8);
  }

  // String pointer.
  llvm::Constant *C = nullptr;
  if (isUTF16) {
    auto Arr = llvm::ArrayRef(
        reinterpret_cast<uint16_t *>(const_cast<char *>(Entry.first().data())),
        Entry.first().size() / 2);
    C = llvm::ConstantDataArray::get(VMContext, Arr);
  } else {
    C = llvm::ConstantDataArray::getString(VMContext, Entry.first());
  }

  // Note: -fwritable-strings doesn't make the backing store strings of
  // CFStrings writable.
  auto *GV =
      new llvm::GlobalVariable(getModule(), C->getType(), /*isConstant=*/true,
                               llvm::GlobalValue::PrivateLinkage, C, ".str");
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  // Don't enforce the target's minimum global alignment, since the only use
  // of the string is via this class initializer.
  CharUnits Align = isUTF16 ? Context.getTypeAlignInChars(Context.ShortTy)
                            : Context.getTypeAlignInChars(Context.CharTy);
  GV->setAlignment(Align.getAsAlign());

  // FIXME: We set the section explicitly to avoid a bug in ld64 224.1.
  // Without it LLVM can merge the string with a non unnamed_addr one during
  // LTO.  Doing that changes the section it ends in, which surprises ld64.
  if (Triple.isOSBinFormatMachO())
    GV->setSection(isUTF16 ? "__TEXT,__ustring"
                           : "__TEXT,__cstring,cstring_literals");
  // Make sure the literal ends up in .rodata to allow for safe ICF and for
  // the static linker to adjust permissions to read-only later on.
  else if (Triple.isOSBinFormatELF())
    GV->setSection(".rodata");

  // String.
  Fields.add(GV);

  // String length.
  llvm::IntegerType *LengthTy =
      llvm::IntegerType::get(getModule().getContext(),
                             Context.getTargetInfo().getLongWidth());
  if (IsSwiftABI) {
    if (CFRuntime == LangOptions::CoreFoundationABI::Swift4_1 ||
        CFRuntime == LangOptions::CoreFoundationABI::Swift4_2)
      LengthTy = Int32Ty;
    else
      LengthTy = IntPtrTy;
  }
  Fields.addInt(LengthTy, StringLength);

  // Swift ABI requires 8-byte alignment to ensure that the _Atomic(uint64_t) is
  // properly aligned on 32-bit platforms.
  CharUnits Alignment =
      IsSwiftABI ? Context.toCharUnitsFromBits(64) : getPointerAlign();

  // The struct.
  GV = Fields.finishAndCreateGlobal("_unnamed_cfstring_", Alignment,
                                    /*isConstant=*/false,
                                    llvm::GlobalVariable::PrivateLinkage);
  GV->addAttribute("objc_arc_inert");
  switch (Triple.getObjectFormat()) {
  case llvm::Triple::UnknownObjectFormat:
    llvm_unreachable("unknown file format");
  case llvm::Triple::DXContainer:
  case llvm::Triple::GOFF:
  case llvm::Triple::SPIRV:
  case llvm::Triple::XCOFF:
    llvm_unreachable("unimplemented");
  case llvm::Triple::COFF:
  case llvm::Triple::ELF:
  case llvm::Triple::Wasm:
    GV->setSection("cfstring");
    break;
  case llvm::Triple::MachO:
    GV->setSection("__DATA,__cfstring");
    break;
  }
  Entry.second = GV;

  return ConstantAddress(GV, GV->getValueType(), Alignment);
}

bool CodeGenModule::getExpressionLocationsEnabled() const {
  return !CodeGenOpts.EmitCodeView || CodeGenOpts.DebugColumnInfo;
}

QualType CodeGenModule::getObjCFastEnumerationStateType() {
  if (ObjCFastEnumerationStateType.isNull()) {
    RecordDecl *D = Context.buildImplicitRecord("__objcFastEnumerationState");
    D->startDefinition();

    QualType FieldTypes[] = {
        Context.UnsignedLongTy, Context.getPointerType(Context.getObjCIdType()),
        Context.getPointerType(Context.UnsignedLongTy),
        Context.getConstantArrayType(Context.UnsignedLongTy, llvm::APInt(32, 5),
                                     nullptr, ArraySizeModifier::Normal, 0)};

    for (size_t i = 0; i < 4; ++i) {
      FieldDecl *Field = FieldDecl::Create(Context,
                                           D,
                                           SourceLocation(),
                                           SourceLocation(), nullptr,
                                           FieldTypes[i], /*TInfo=*/nullptr,
                                           /*BitWidth=*/nullptr,
                                           /*Mutable=*/false,
                                           ICIS_NoInit);
      Field->setAccess(AS_public);
      D->addDecl(Field);
    }

    D->completeDefinition();
    ObjCFastEnumerationStateType = Context.getTagDeclType(D);
  }

  return ObjCFastEnumerationStateType;
}

llvm::Constant *
CodeGenModule::GetConstantArrayFromStringLiteral(const StringLiteral *E) {
  assert(!E->getType()->isPointerType() && "Strings are always arrays");

  // Don't emit it as the address of the string, emit the string data itself
  // as an inline array.
  if (E->getCharByteWidth() == 1) {
    SmallString<64> Str(E->getString());

    // Resize the string to the right size, which is indicated by its type.
    const ConstantArrayType *CAT = Context.getAsConstantArrayType(E->getType());
    assert(CAT && "String literal not of constant array type!");
    Str.resize(CAT->getZExtSize());
    return llvm::ConstantDataArray::getString(VMContext, Str, false);
  }

  auto *AType = cast<llvm::ArrayType>(getTypes().ConvertType(E->getType()));
  llvm::Type *ElemTy = AType->getElementType();
  unsigned NumElements = AType->getNumElements();

  // Wide strings have either 2-byte or 4-byte elements.
  if (ElemTy->getPrimitiveSizeInBits() == 16) {
    SmallVector<uint16_t, 32> Elements;
    Elements.reserve(NumElements);

    for(unsigned i = 0, e = E->getLength(); i != e; ++i)
      Elements.push_back(E->getCodeUnit(i));
    Elements.resize(NumElements);
    return llvm::ConstantDataArray::get(VMContext, Elements);
  }

  assert(ElemTy->getPrimitiveSizeInBits() == 32);
  SmallVector<uint32_t, 32> Elements;
  Elements.reserve(NumElements);

  for(unsigned i = 0, e = E->getLength(); i != e; ++i)
    Elements.push_back(E->getCodeUnit(i));
  Elements.resize(NumElements);
  return llvm::ConstantDataArray::get(VMContext, Elements);
}

static llvm::GlobalVariable *
GenerateStringLiteral(llvm::Constant *C, llvm::GlobalValue::LinkageTypes LT,
                      CodeGenModule &CGM, StringRef GlobalName,
                      CharUnits Alignment) {
  unsigned AddrSpace = CGM.getContext().getTargetAddressSpace(
      CGM.GetGlobalConstantAddressSpace());

  llvm::Module &M = CGM.getModule();
  // Create a global variable for this string
  auto *GV = new llvm::GlobalVariable(
      M, C->getType(), !CGM.getLangOpts().WritableStrings, LT, C, GlobalName,
      nullptr, llvm::GlobalVariable::NotThreadLocal, AddrSpace);
  GV->setAlignment(Alignment.getAsAlign());
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  if (GV->isWeakForLinker()) {
    assert(CGM.supportsCOMDAT() && "Only COFF uses weak string literals");
    GV->setComdat(M.getOrInsertComdat(GV->getName()));
  }
  CGM.setDSOLocal(GV);

  return GV;
}

/// GetAddrOfConstantStringFromLiteral - Return a pointer to a
/// constant array for the given string literal.
ConstantAddress
CodeGenModule::GetAddrOfConstantStringFromLiteral(const StringLiteral *S,
                                                  StringRef Name) {
  CharUnits Alignment =
      getContext().getAlignOfGlobalVarInChars(S->getType(), /*VD=*/nullptr);

  llvm::Constant *C = GetConstantArrayFromStringLiteral(S);
  llvm::GlobalVariable **Entry = nullptr;
  if (!LangOpts.WritableStrings) {
    Entry = &ConstantStringMap[C];
    if (auto GV = *Entry) {
      if (uint64_t(Alignment.getQuantity()) > GV->getAlignment())
        GV->setAlignment(Alignment.getAsAlign());
      return ConstantAddress(castStringLiteralToDefaultAddressSpace(*this, GV),
                             GV->getValueType(), Alignment);
    }
  }

  SmallString<256> MangledNameBuffer;
  StringRef GlobalVariableName;
  llvm::GlobalValue::LinkageTypes LT;

  // Mangle the string literal if that's how the ABI merges duplicate strings.
  // Don't do it if they are writable, since we don't want writes in one TU to
  // affect strings in another.
  if (getCXXABI().getMangleContext().shouldMangleStringLiteral(S) &&
      !LangOpts.WritableStrings) {
    llvm::raw_svector_ostream Out(MangledNameBuffer);
    getCXXABI().getMangleContext().mangleStringLiteral(S, Out);
    LT = llvm::GlobalValue::LinkOnceODRLinkage;
    GlobalVariableName = MangledNameBuffer;
  } else {
    LT = llvm::GlobalValue::PrivateLinkage;
    GlobalVariableName = Name;
  }

  auto GV = GenerateStringLiteral(C, LT, *this, GlobalVariableName, Alignment);

  CGDebugInfo *DI = getModuleDebugInfo();
  if (DI && getCodeGenOpts().hasReducedDebugInfo())
    DI->AddStringLiteralDebugInfo(GV, S);

  if (Entry)
    *Entry = GV;

  SanitizerMD->reportGlobal(GV, S->getStrTokenLoc(0), "<string literal>");

  return ConstantAddress(castStringLiteralToDefaultAddressSpace(*this, GV),
                         GV->getValueType(), Alignment);
}

/// GetAddrOfConstantStringFromObjCEncode - Return a pointer to a constant
/// array for the given ObjCEncodeExpr node.
ConstantAddress
CodeGenModule::GetAddrOfConstantStringFromObjCEncode(const ObjCEncodeExpr *E) {
  std::string Str;
  getContext().getObjCEncodingForType(E->getEncodedType(), Str);

  return GetAddrOfConstantCString(Str);
}

/// GetAddrOfConstantCString - Returns a pointer to a character array containing
/// the literal and a terminating '\0' character.
/// The result has pointer to array type.
ConstantAddress CodeGenModule::GetAddrOfConstantCString(
    const std::string &Str, const char *GlobalName) {
  StringRef StrWithNull(Str.c_str(), Str.size() + 1);
  CharUnits Alignment = getContext().getAlignOfGlobalVarInChars(
      getContext().CharTy, /*VD=*/nullptr);

  llvm::Constant *C =
      llvm::ConstantDataArray::getString(getLLVMContext(), StrWithNull, false);

  // Don't share any string literals if strings aren't constant.
  llvm::GlobalVariable **Entry = nullptr;
  if (!LangOpts.WritableStrings) {
    Entry = &ConstantStringMap[C];
    if (auto GV = *Entry) {
      if (uint64_t(Alignment.getQuantity()) > GV->getAlignment())
        GV->setAlignment(Alignment.getAsAlign());
      return ConstantAddress(castStringLiteralToDefaultAddressSpace(*this, GV),
                             GV->getValueType(), Alignment);
    }
  }

  // Get the default prefix if a name wasn't specified.
  if (!GlobalName)
    GlobalName = ".str";
  // Create a global variable for this.
  auto GV = GenerateStringLiteral(C, llvm::GlobalValue::PrivateLinkage, *this,
                                  GlobalName, Alignment);
  if (Entry)
    *Entry = GV;

  return ConstantAddress(castStringLiteralToDefaultAddressSpace(*this, GV),
                         GV->getValueType(), Alignment);
}

ConstantAddress CodeGenModule::GetAddrOfGlobalTemporary(
    const MaterializeTemporaryExpr *E, const Expr *Init) {
  assert((E->getStorageDuration() == SD_Static ||
          E->getStorageDuration() == SD_Thread) && "not a global temporary");
  const auto *VD = cast<VarDecl>(E->getExtendingDecl());

  // If we're not materializing a subobject of the temporary, keep the
  // cv-qualifiers from the type of the MaterializeTemporaryExpr.
  QualType MaterializedType = Init->getType();
  if (Init == E->getSubExpr())
    MaterializedType = E->getType();

  CharUnits Align = getContext().getTypeAlignInChars(MaterializedType);

  auto InsertResult = MaterializedGlobalTemporaryMap.insert({E, nullptr});
  if (!InsertResult.second) {
    // We've seen this before: either we already created it or we're in the
    // process of doing so.
    if (!InsertResult.first->second) {
      // We recursively re-entered this function, probably during emission of
      // the initializer. Create a placeholder. We'll clean this up in the
      // outer call, at the end of this function.
      llvm::Type *Type = getTypes().ConvertTypeForMem(MaterializedType);
      InsertResult.first->second = new llvm::GlobalVariable(
          getModule(), Type, false, llvm::GlobalVariable::InternalLinkage,
          nullptr);
    }
    return ConstantAddress(InsertResult.first->second,
                           llvm::cast<llvm::GlobalVariable>(
                               InsertResult.first->second->stripPointerCasts())
                               ->getValueType(),
                           Align);
  }

  // FIXME: If an externally-visible declaration extends multiple temporaries,
  // we need to give each temporary the same name in every translation unit (and
  // we also need to make the temporaries externally-visible).
  SmallString<256> Name;
  llvm::raw_svector_ostream Out(Name);
  getCXXABI().getMangleContext().mangleReferenceTemporary(
      VD, E->getManglingNumber(), Out);

  APValue *Value = nullptr;
  if (E->getStorageDuration() == SD_Static && VD->evaluateValue()) {
    // If the initializer of the extending declaration is a constant
    // initializer, we should have a cached constant initializer for this
    // temporary. Note that this might have a different value from the value
    // computed by evaluating the initializer if the surrounding constant
    // expression modifies the temporary.
    Value = E->getOrCreateValue(false);
  }

  // Try evaluating it now, it might have a constant initializer.
  Expr::EvalResult EvalResult;
  if (!Value && Init->EvaluateAsRValue(EvalResult, getContext()) &&
      !EvalResult.hasSideEffects())
    Value = &EvalResult.Val;

  LangAS AddrSpace = GetGlobalVarAddressSpace(VD);

  std::optional<ConstantEmitter> emitter;
  llvm::Constant *InitialValue = nullptr;
  bool Constant = false;
  llvm::Type *Type;
  if (Value) {
    // The temporary has a constant initializer, use it.
    emitter.emplace(*this);
    InitialValue = emitter->emitForInitializer(*Value, AddrSpace,
                                               MaterializedType);
    Constant =
        MaterializedType.isConstantStorage(getContext(), /*ExcludeCtor*/ Value,
                                           /*ExcludeDtor*/ false);
    Type = InitialValue->getType();
  } else {
    // No initializer, the initialization will be provided when we
    // initialize the declaration which performed lifetime extension.
    Type = getTypes().ConvertTypeForMem(MaterializedType);
  }

  // Create a global variable for this lifetime-extended temporary.
  llvm::GlobalValue::LinkageTypes Linkage = getLLVMLinkageVarDefinition(VD);
  if (Linkage == llvm::GlobalVariable::ExternalLinkage) {
    const VarDecl *InitVD;
    if (VD->isStaticDataMember() && VD->getAnyInitializer(InitVD) &&
        isa<CXXRecordDecl>(InitVD->getLexicalDeclContext())) {
      // Temporaries defined inside a class get linkonce_odr linkage because the
      // class can be defined in multiple translation units.
      Linkage = llvm::GlobalVariable::LinkOnceODRLinkage;
    } else {
      // There is no need for this temporary to have external linkage if the
      // VarDecl has external linkage.
      Linkage = llvm::GlobalVariable::InternalLinkage;
    }
  }
  auto TargetAS = getContext().getTargetAddressSpace(AddrSpace);
  auto *GV = new llvm::GlobalVariable(
      getModule(), Type, Constant, Linkage, InitialValue, Name.c_str(),
      /*InsertBefore=*/nullptr, llvm::GlobalVariable::NotThreadLocal, TargetAS);
  if (emitter) emitter->finalize(GV);
  // Don't assign dllimport or dllexport to local linkage globals.
  if (!llvm::GlobalValue::isLocalLinkage(Linkage)) {
    setGVProperties(GV, VD);
    if (GV->getDLLStorageClass() == llvm::GlobalVariable::DLLExportStorageClass)
      // The reference temporary should never be dllexport.
      GV->setDLLStorageClass(llvm::GlobalVariable::DefaultStorageClass);
  }
  GV->setAlignment(Align.getAsAlign());
  if (supportsCOMDAT() && GV->isWeakForLinker())
    GV->setComdat(TheModule.getOrInsertComdat(GV->getName()));
  if (VD->getTLSKind())
    setTLSMode(GV, *VD);
  llvm::Constant *CV = GV;
  if (AddrSpace != LangAS::Default)
    CV = getTargetCodeGenInfo().performAddrSpaceCast(
        *this, GV, AddrSpace, LangAS::Default,
        llvm::PointerType::get(
            getLLVMContext(),
            getContext().getTargetAddressSpace(LangAS::Default)));

  // Update the map with the new temporary. If we created a placeholder above,
  // replace it with the new global now.
  llvm::Constant *&Entry = MaterializedGlobalTemporaryMap[E];
  if (Entry) {
    Entry->replaceAllUsesWith(CV);
    llvm::cast<llvm::GlobalVariable>(Entry)->eraseFromParent();
  }
  Entry = CV;

  return ConstantAddress(CV, Type, Align);
}

/// EmitObjCPropertyImplementations - Emit information for synthesized
/// properties for an implementation.
void CodeGenModule::EmitObjCPropertyImplementations(const
                                                    ObjCImplementationDecl *D) {
  for (const auto *PID : D->property_impls()) {
    // Dynamic is just for type-checking.
    if (PID->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize) {
      ObjCPropertyDecl *PD = PID->getPropertyDecl();

      // Determine which methods need to be implemented, some may have
      // been overridden. Note that ::isPropertyAccessor is not the method
      // we want, that just indicates if the decl came from a
      // property. What we want to know is if the method is defined in
      // this implementation.
      auto *Getter = PID->getGetterMethodDecl();
      if (!Getter || Getter->isSynthesizedAccessorStub())
        CodeGenFunction(*this).GenerateObjCGetter(
            const_cast<ObjCImplementationDecl *>(D), PID);
      auto *Setter = PID->getSetterMethodDecl();
      if (!PD->isReadOnly() && (!Setter || Setter->isSynthesizedAccessorStub()))
        CodeGenFunction(*this).GenerateObjCSetter(
                                 const_cast<ObjCImplementationDecl *>(D), PID);
    }
  }
}

static bool needsDestructMethod(ObjCImplementationDecl *impl) {
  const ObjCInterfaceDecl *iface = impl->getClassInterface();
  for (const ObjCIvarDecl *ivar = iface->all_declared_ivar_begin();
       ivar; ivar = ivar->getNextIvar())
    if (ivar->getType().isDestructedType())
      return true;

  return false;
}

static bool AllTrivialInitializers(CodeGenModule &CGM,
                                   ObjCImplementationDecl *D) {
  CodeGenFunction CGF(CGM);
  for (ObjCImplementationDecl::init_iterator B = D->init_begin(),
       E = D->init_end(); B != E; ++B) {
    CXXCtorInitializer *CtorInitExp = *B;
    Expr *Init = CtorInitExp->getInit();
    if (!CGF.isTrivialInitializer(Init))
      return false;
  }
  return true;
}

/// EmitObjCIvarInitializations - Emit information for ivar initialization
/// for an implementation.
void CodeGenModule::EmitObjCIvarInitializations(ObjCImplementationDecl *D) {
  // We might need a .cxx_destruct even if we don't have any ivar initializers.
  if (needsDestructMethod(D)) {
    const IdentifierInfo *II = &getContext().Idents.get(".cxx_destruct");
    Selector cxxSelector = getContext().Selectors.getSelector(0, &II);
    ObjCMethodDecl *DTORMethod = ObjCMethodDecl::Create(
        getContext(), D->getLocation(), D->getLocation(), cxxSelector,
        getContext().VoidTy, nullptr, D,
        /*isInstance=*/true, /*isVariadic=*/false,
        /*isPropertyAccessor=*/true, /*isSynthesizedAccessorStub=*/false,
        /*isImplicitlyDeclared=*/true,
        /*isDefined=*/false, ObjCImplementationControl::Required);
    D->addInstanceMethod(DTORMethod);
    CodeGenFunction(*this).GenerateObjCCtorDtorMethod(D, DTORMethod, false);
    D->setHasDestructors(true);
  }

  // If the implementation doesn't have any ivar initializers, we don't need
  // a .cxx_construct.
  if (D->getNumIvarInitializers() == 0 ||
      AllTrivialInitializers(*this, D))
    return;

  const IdentifierInfo *II = &getContext().Idents.get(".cxx_construct");
  Selector cxxSelector = getContext().Selectors.getSelector(0, &II);
  // The constructor returns 'self'.
  ObjCMethodDecl *CTORMethod = ObjCMethodDecl::Create(
      getContext(), D->getLocation(), D->getLocation(), cxxSelector,
      getContext().getObjCIdType(), nullptr, D, /*isInstance=*/true,
      /*isVariadic=*/false,
      /*isPropertyAccessor=*/true, /*isSynthesizedAccessorStub=*/false,
      /*isImplicitlyDeclared=*/true,
      /*isDefined=*/false, ObjCImplementationControl::Required);
  D->addInstanceMethod(CTORMethod);
  CodeGenFunction(*this).GenerateObjCCtorDtorMethod(D, CTORMethod, true);
  D->setHasNonZeroConstructors(true);
}

// EmitLinkageSpec - Emit all declarations in a linkage spec.
void CodeGenModule::EmitLinkageSpec(const LinkageSpecDecl *LSD) {
  if (LSD->getLanguage() != LinkageSpecLanguageIDs::C &&
      LSD->getLanguage() != LinkageSpecLanguageIDs::CXX) {
    ErrorUnsupported(LSD, "linkage spec");
    return;
  }

  EmitDeclContext(LSD);
}

void CodeGenModule::EmitTopLevelStmt(const TopLevelStmtDecl *D) {
  // Device code should not be at top level.
  if (LangOpts.CUDA && LangOpts.CUDAIsDevice)
    return;

  std::unique_ptr<CodeGenFunction> &CurCGF =
      GlobalTopLevelStmtBlockInFlight.first;

  // We emitted a top-level stmt but after it there is initialization.
  // Stop squashing the top-level stmts into a single function.
  if (CurCGF && CXXGlobalInits.back() != CurCGF->CurFn) {
    CurCGF->FinishFunction(D->getEndLoc());
    CurCGF = nullptr;
  }

  if (!CurCGF) {
    // void __stmts__N(void)
    // FIXME: Ask the ABI name mangler to pick a name.
    std::string Name = "__stmts__" + llvm::utostr(CXXGlobalInits.size());
    FunctionArgList Args;
    QualType RetTy = getContext().VoidTy;
    const CGFunctionInfo &FnInfo =
        getTypes().arrangeBuiltinFunctionDeclaration(RetTy, Args);
    llvm::FunctionType *FnTy = getTypes().GetFunctionType(FnInfo);
    llvm::Function *Fn = llvm::Function::Create(
        FnTy, llvm::GlobalValue::InternalLinkage, Name, &getModule());

    CurCGF.reset(new CodeGenFunction(*this));
    GlobalTopLevelStmtBlockInFlight.second = D;
    CurCGF->StartFunction(GlobalDecl(), RetTy, Fn, FnInfo, Args,
                          D->getBeginLoc(), D->getBeginLoc());
    CXXGlobalInits.push_back(Fn);
  }

  CurCGF->EmitStmt(D->getStmt());
}

void CodeGenModule::EmitDeclContext(const DeclContext *DC) {
  for (auto *I : DC->decls()) {
    // Unlike other DeclContexts, the contents of an ObjCImplDecl at TU scope
    // are themselves considered "top-level", so EmitTopLevelDecl on an
    // ObjCImplDecl does not recursively visit them. We need to do that in
    // case they're nested inside another construct (LinkageSpecDecl /
    // ExportDecl) that does stop them from being considered "top-level".
    if (auto *OID = dyn_cast<ObjCImplDecl>(I)) {
      for (auto *M : OID->methods())
        EmitTopLevelDecl(M);
    }

    EmitTopLevelDecl(I);
  }
}

/// EmitTopLevelDecl - Emit code for a single top level declaration.
void CodeGenModule::EmitTopLevelDecl(Decl *D) {
  // Ignore dependent declarations.
  if (D->isTemplated())
    return;

  // Consteval function shouldn't be emitted.
  if (auto *FD = dyn_cast<FunctionDecl>(D); FD && FD->isImmediateFunction())
    return;

  switch (D->getKind()) {
  case Decl::CXXConversion:
  case Decl::CXXMethod:
  case Decl::Function:
    EmitGlobal(cast<FunctionDecl>(D));
    // Always provide some coverage mapping
    // even for the functions that aren't emitted.
    AddDeferredUnusedCoverageMapping(D);
    break;

  case Decl::CXXDeductionGuide:
    // Function-like, but does not result in code emission.
    break;

  case Decl::Var:
  case Decl::Decomposition:
  case Decl::VarTemplateSpecialization:
    EmitGlobal(cast<VarDecl>(D));
    if (auto *DD = dyn_cast<DecompositionDecl>(D))
      for (auto *B : DD->bindings())
        if (auto *HD = B->getHoldingVar())
          EmitGlobal(HD);
    break;

  // Indirect fields from global anonymous structs and unions can be
  // ignored; only the actual variable requires IR gen support.
  case Decl::IndirectField:
    break;

  // C++ Decls
  case Decl::Namespace:
    EmitDeclContext(cast<NamespaceDecl>(D));
    break;
  case Decl::ClassTemplateSpecialization: {
    const auto *Spec = cast<ClassTemplateSpecializationDecl>(D);
    if (CGDebugInfo *DI = getModuleDebugInfo())
      if (Spec->getSpecializationKind() ==
              TSK_ExplicitInstantiationDefinition &&
          Spec->hasDefinition())
        DI->completeTemplateDefinition(*Spec);
  } [[fallthrough]];
  case Decl::CXXRecord: {
    CXXRecordDecl *CRD = cast<CXXRecordDecl>(D);
    if (CGDebugInfo *DI = getModuleDebugInfo()) {
      if (CRD->hasDefinition())
        DI->EmitAndRetainType(getContext().getRecordType(cast<RecordDecl>(D)));
      if (auto *ES = D->getASTContext().getExternalSource())
        if (ES->hasExternalDefinitions(D) == ExternalASTSource::EK_Never)
          DI->completeUnusedClass(*CRD);
    }
    // Emit any static data members, they may be definitions.
    for (auto *I : CRD->decls())
      if (isa<VarDecl>(I) || isa<CXXRecordDecl>(I))
        EmitTopLevelDecl(I);
    break;
  }
    // No code generation needed.
  case Decl::UsingShadow:
  case Decl::ClassTemplate:
  case Decl::VarTemplate:
  case Decl::Concept:
  case Decl::VarTemplatePartialSpecialization:
  case Decl::FunctionTemplate:
  case Decl::TypeAliasTemplate:
  case Decl::Block:
  case Decl::Empty:
  case Decl::Binding:
    break;
  case Decl::Using:          // using X; [C++]
    if (CGDebugInfo *DI = getModuleDebugInfo())
        DI->EmitUsingDecl(cast<UsingDecl>(*D));
    break;
  case Decl::UsingEnum: // using enum X; [C++]
    if (CGDebugInfo *DI = getModuleDebugInfo())
      DI->EmitUsingEnumDecl(cast<UsingEnumDecl>(*D));
    break;
  case Decl::NamespaceAlias:
    if (CGDebugInfo *DI = getModuleDebugInfo())
        DI->EmitNamespaceAlias(cast<NamespaceAliasDecl>(*D));
    break;
  case Decl::UsingDirective: // using namespace X; [C++]
    if (CGDebugInfo *DI = getModuleDebugInfo())
      DI->EmitUsingDirective(cast<UsingDirectiveDecl>(*D));
    break;
  case Decl::CXXConstructor:
    getCXXABI().EmitCXXConstructors(cast<CXXConstructorDecl>(D));
    break;
  case Decl::CXXDestructor:
    getCXXABI().EmitCXXDestructors(cast<CXXDestructorDecl>(D));
    break;

  case Decl::StaticAssert:
    // Nothing to do.
    break;

  // Objective-C Decls

  // Forward declarations, no (immediate) code generation.
  case Decl::ObjCInterface:
  case Decl::ObjCCategory:
    break;

  case Decl::ObjCProtocol: {
    auto *Proto = cast<ObjCProtocolDecl>(D);
    if (Proto->isThisDeclarationADefinition())
      ObjCRuntime->GenerateProtocol(Proto);
    break;
  }

  case Decl::ObjCCategoryImpl:
    // Categories have properties but don't support synthesize so we
    // can ignore them here.
    ObjCRuntime->GenerateCategory(cast<ObjCCategoryImplDecl>(D));
    break;

  case Decl::ObjCImplementation: {
    auto *OMD = cast<ObjCImplementationDecl>(D);
    EmitObjCPropertyImplementations(OMD);
    EmitObjCIvarInitializations(OMD);
    ObjCRuntime->GenerateClass(OMD);
    // Emit global variable debug information.
    if (CGDebugInfo *DI = getModuleDebugInfo())
      if (getCodeGenOpts().hasReducedDebugInfo())
        DI->getOrCreateInterfaceType(getContext().getObjCInterfaceType(
            OMD->getClassInterface()), OMD->getLocation());
    break;
  }
  case Decl::ObjCMethod: {
    auto *OMD = cast<ObjCMethodDecl>(D);
    // If this is not a prototype, emit the body.
    if (OMD->getBody())
      CodeGenFunction(*this).GenerateObjCMethod(OMD);
    break;
  }
  case Decl::ObjCCompatibleAlias:
    ObjCRuntime->RegisterAlias(cast<ObjCCompatibleAliasDecl>(D));
    break;

  case Decl::PragmaComment: {
    const auto *PCD = cast<PragmaCommentDecl>(D);
    switch (PCD->getCommentKind()) {
    case PCK_Unknown:
      llvm_unreachable("unexpected pragma comment kind");
    case PCK_Linker:
      AppendLinkerOptions(PCD->getArg());
      break;
    case PCK_Lib:
        AddDependentLib(PCD->getArg());
      break;
    case PCK_Compiler:
    case PCK_ExeStr:
    case PCK_User:
      break; // We ignore all of these.
    }
    break;
  }

  case Decl::PragmaDetectMismatch: {
    const auto *PDMD = cast<PragmaDetectMismatchDecl>(D);
    AddDetectMismatch(PDMD->getName(), PDMD->getValue());
    break;
  }

  case Decl::LinkageSpec:
    EmitLinkageSpec(cast<LinkageSpecDecl>(D));
    break;

  case Decl::FileScopeAsm: {
    // File-scope asm is ignored during device-side CUDA compilation.
    if (LangOpts.CUDA && LangOpts.CUDAIsDevice)
      break;
    // File-scope asm is ignored during device-side OpenMP compilation.
    if (LangOpts.OpenMPIsTargetDevice)
      break;
    // File-scope asm is ignored during device-side SYCL compilation.
    if (LangOpts.SYCLIsDevice)
      break;
    auto *AD = cast<FileScopeAsmDecl>(D);
    getModule().appendModuleInlineAsm(AD->getAsmString()->getString());
    break;
  }

  case Decl::TopLevelStmt:
    EmitTopLevelStmt(cast<TopLevelStmtDecl>(D));
    break;

  case Decl::Import: {
    auto *Import = cast<ImportDecl>(D);

    // If we've already imported this module, we're done.
    if (!ImportedModules.insert(Import->getImportedModule()))
      break;

    // Emit debug information for direct imports.
    if (!Import->getImportedOwningModule()) {
      if (CGDebugInfo *DI = getModuleDebugInfo())
        DI->EmitImportDecl(*Import);
    }

    // For C++ standard modules we are done - we will call the module
    // initializer for imported modules, and that will likewise call those for
    // any imports it has.
    if (CXX20ModuleInits && Import->getImportedModule() &&
        Import->getImportedModule()->isNamedModule())
      break;

    // For clang C++ module map modules the initializers for sub-modules are
    // emitted here.

    // Find all of the submodules and emit the module initializers.
    llvm::SmallPtrSet<clang::Module *, 16> Visited;
    SmallVector<clang::Module *, 16> Stack;
    Visited.insert(Import->getImportedModule());
    Stack.push_back(Import->getImportedModule());

    while (!Stack.empty()) {
      clang::Module *Mod = Stack.pop_back_val();
      if (!EmittedModuleInitializers.insert(Mod).second)
        continue;

      for (auto *D : Context.getModuleInitializers(Mod))
        EmitTopLevelDecl(D);

      // Visit the submodules of this module.
      for (auto *Submodule : Mod->submodules()) {
        // Skip explicit children; they need to be explicitly imported to emit
        // the initializers.
        if (Submodule->IsExplicit)
          continue;

        if (Visited.insert(Submodule).second)
          Stack.push_back(Submodule);
      }
    }
    break;
  }

  case Decl::Export:
    EmitDeclContext(cast<ExportDecl>(D));
    break;

  case Decl::OMPThreadPrivate:
    EmitOMPThreadPrivateDecl(cast<OMPThreadPrivateDecl>(D));
    break;

  case Decl::OMPAllocate:
    EmitOMPAllocateDecl(cast<OMPAllocateDecl>(D));
    break;

  case Decl::OMPDeclareReduction:
    EmitOMPDeclareReduction(cast<OMPDeclareReductionDecl>(D));
    break;

  case Decl::OMPDeclareMapper:
    EmitOMPDeclareMapper(cast<OMPDeclareMapperDecl>(D));
    break;

  case Decl::OMPRequires:
    EmitOMPRequiresDecl(cast<OMPRequiresDecl>(D));
    break;

  case Decl::Typedef:
  case Decl::TypeAlias: // using foo = bar; [C++11]
    if (CGDebugInfo *DI = getModuleDebugInfo())
      DI->EmitAndRetainType(
          getContext().getTypedefType(cast<TypedefNameDecl>(D)));
    break;

  case Decl::Record:
    if (CGDebugInfo *DI = getModuleDebugInfo())
      if (cast<RecordDecl>(D)->getDefinition())
        DI->EmitAndRetainType(getContext().getRecordType(cast<RecordDecl>(D)));
    break;

  case Decl::Enum:
    if (CGDebugInfo *DI = getModuleDebugInfo())
      if (cast<EnumDecl>(D)->getDefinition())
        DI->EmitAndRetainType(getContext().getEnumType(cast<EnumDecl>(D)));
    break;

  case Decl::HLSLBuffer:
    getHLSLRuntime().addBuffer(cast<HLSLBufferDecl>(D));
    break;

  default:
    // Make sure we handled everything we should, every other kind is a
    // non-top-level decl.  FIXME: Would be nice to have an isTopLevelDeclKind
    // function. Need to recode Decl::Kind to do that easily.
    assert(isa<TypeDecl>(D) && "Unsupported decl kind");
    break;
  }
}

void CodeGenModule::AddDeferredUnusedCoverageMapping(Decl *D) {
  // Do we need to generate coverage mapping?
  if (!CodeGenOpts.CoverageMapping)
    return;
  switch (D->getKind()) {
  case Decl::CXXConversion:
  case Decl::CXXMethod:
  case Decl::Function:
  case Decl::ObjCMethod:
  case Decl::CXXConstructor:
  case Decl::CXXDestructor: {
    if (!cast<FunctionDecl>(D)->doesThisDeclarationHaveABody())
      break;
    SourceManager &SM = getContext().getSourceManager();
    if (LimitedCoverage && SM.getMainFileID() != SM.getFileID(D->getBeginLoc()))
      break;
    if (!llvm::coverage::SystemHeadersCoverage &&
        SM.isInSystemHeader(D->getBeginLoc()))
      break;
    DeferredEmptyCoverageMappingDecls.try_emplace(D, true);
    break;
  }
  default:
    break;
  };
}

void CodeGenModule::ClearUnusedCoverageMapping(const Decl *D) {
  // Do we need to generate coverage mapping?
  if (!CodeGenOpts.CoverageMapping)
    return;
  if (const auto *Fn = dyn_cast<FunctionDecl>(D)) {
    if (Fn->isTemplateInstantiation())
      ClearUnusedCoverageMapping(Fn->getTemplateInstantiationPattern());
  }
  DeferredEmptyCoverageMappingDecls.insert_or_assign(D, false);
}

void CodeGenModule::EmitDeferredUnusedCoverageMappings() {
  // We call takeVector() here to avoid use-after-free.
  // FIXME: DeferredEmptyCoverageMappingDecls is getting mutated because
  // we deserialize function bodies to emit coverage info for them, and that
  // deserializes more declarations. How should we handle that case?
  for (const auto &Entry : DeferredEmptyCoverageMappingDecls.takeVector()) {
    if (!Entry.second)
      continue;
    const Decl *D = Entry.first;
    switch (D->getKind()) {
    case Decl::CXXConversion:
    case Decl::CXXMethod:
    case Decl::Function:
    case Decl::ObjCMethod: {
      CodeGenPGO PGO(*this);
      GlobalDecl GD(cast<FunctionDecl>(D));
      PGO.emitEmptyCounterMapping(D, getMangledName(GD),
                                  getFunctionLinkage(GD));
      break;
    }
    case Decl::CXXConstructor: {
      CodeGenPGO PGO(*this);
      GlobalDecl GD(cast<CXXConstructorDecl>(D), Ctor_Base);
      PGO.emitEmptyCounterMapping(D, getMangledName(GD),
                                  getFunctionLinkage(GD));
      break;
    }
    case Decl::CXXDestructor: {
      CodeGenPGO PGO(*this);
      GlobalDecl GD(cast<CXXDestructorDecl>(D), Dtor_Base);
      PGO.emitEmptyCounterMapping(D, getMangledName(GD),
                                  getFunctionLinkage(GD));
      break;
    }
    default:
      break;
    };
  }
}

void CodeGenModule::EmitMainVoidAlias() {
  // In order to transition away from "__original_main" gracefully, emit an
  // alias for "main" in the no-argument case so that libc can detect when
  // new-style no-argument main is in used.
  if (llvm::Function *F = getModule().getFunction("main")) {
    if (!F->isDeclaration() && F->arg_size() == 0 && !F->isVarArg() &&
        F->getReturnType()->isIntegerTy(Context.getTargetInfo().getIntWidth())) {
      auto *GA = llvm::GlobalAlias::create("__main_void", F);
      GA->setVisibility(llvm::GlobalValue::HiddenVisibility);
    }
  }
}

/// Turns the given pointer into a constant.
static llvm::Constant *GetPointerConstant(llvm::LLVMContext &Context,
                                          const void *Ptr) {
  uintptr_t PtrInt = reinterpret_cast<uintptr_t>(Ptr);
  llvm::Type *i64 = llvm::Type::getInt64Ty(Context);
  return llvm::ConstantInt::get(i64, PtrInt);
}

static void EmitGlobalDeclMetadata(CodeGenModule &CGM,
                                   llvm::NamedMDNode *&GlobalMetadata,
                                   GlobalDecl D,
                                   llvm::GlobalValue *Addr) {
  if (!GlobalMetadata)
    GlobalMetadata =
      CGM.getModule().getOrInsertNamedMetadata("clang.global.decl.ptrs");

  // TODO: should we report variant information for ctors/dtors?
  llvm::Metadata *Ops[] = {llvm::ConstantAsMetadata::get(Addr),
                           llvm::ConstantAsMetadata::get(GetPointerConstant(
                               CGM.getLLVMContext(), D.getDecl()))};
  GlobalMetadata->addOperand(llvm::MDNode::get(CGM.getLLVMContext(), Ops));
}

bool CodeGenModule::CheckAndReplaceExternCIFuncs(llvm::GlobalValue *Elem,
                                                 llvm::GlobalValue *CppFunc) {
  // Store the list of ifuncs we need to replace uses in.
  llvm::SmallVector<llvm::GlobalIFunc *> IFuncs;
  // List of ConstantExprs that we should be able to delete when we're done
  // here.
  llvm::SmallVector<llvm::ConstantExpr *> CEs;

  // It isn't valid to replace the extern-C ifuncs if all we find is itself!
  if (Elem == CppFunc)
    return false;

  // First make sure that all users of this are ifuncs (or ifuncs via a
  // bitcast), and collect the list of ifuncs and CEs so we can work on them
  // later.
  for (llvm::User *User : Elem->users()) {
    // Users can either be a bitcast ConstExpr that is used by the ifuncs, OR an
    // ifunc directly. In any other case, just give up, as we don't know what we
    // could break by changing those.
    if (auto *ConstExpr = dyn_cast<llvm::ConstantExpr>(User)) {
      if (ConstExpr->getOpcode() != llvm::Instruction::BitCast)
        return false;

      for (llvm::User *CEUser : ConstExpr->users()) {
        if (auto *IFunc = dyn_cast<llvm::GlobalIFunc>(CEUser)) {
          IFuncs.push_back(IFunc);
        } else {
          return false;
        }
      }
      CEs.push_back(ConstExpr);
    } else if (auto *IFunc = dyn_cast<llvm::GlobalIFunc>(User)) {
      IFuncs.push_back(IFunc);
    } else {
      // This user is one we don't know how to handle, so fail redirection. This
      // will result in an ifunc retaining a resolver name that will ultimately
      // fail to be resolved to a defined function.
      return false;
    }
  }

  // Now we know this is a valid case where we can do this alias replacement, we
  // need to remove all of the references to Elem (and the bitcasts!) so we can
  // delete it.
  for (llvm::GlobalIFunc *IFunc : IFuncs)
    IFunc->setResolver(nullptr);
  for (llvm::ConstantExpr *ConstExpr : CEs)
    ConstExpr->destroyConstant();

  // We should now be out of uses for the 'old' version of this function, so we
  // can erase it as well.
  Elem->eraseFromParent();

  for (llvm::GlobalIFunc *IFunc : IFuncs) {
    // The type of the resolver is always just a function-type that returns the
    // type of the IFunc, so create that here. If the type of the actual
    // resolver doesn't match, it just gets bitcast to the right thing.
    auto *ResolverTy =
        llvm::FunctionType::get(IFunc->getType(), /*isVarArg*/ false);
    llvm::Constant *Resolver = GetOrCreateLLVMFunction(
        CppFunc->getName(), ResolverTy, {}, /*ForVTable*/ false);
    IFunc->setResolver(Resolver);
  }
  return true;
}

/// For each function which is declared within an extern "C" region and marked
/// as 'used', but has internal linkage, create an alias from the unmangled
/// name to the mangled name if possible. People expect to be able to refer
/// to such functions with an unmangled name from inline assembly within the
/// same translation unit.
void CodeGenModule::EmitStaticExternCAliases() {
  if (!getTargetCodeGenInfo().shouldEmitStaticExternCAliases())
    return;
  for (auto &I : StaticExternCValues) {
    const IdentifierInfo *Name = I.first;
    llvm::GlobalValue *Val = I.second;

    // If Val is null, that implies there were multiple declarations that each
    // had a claim to the unmangled name. In this case, generation of the alias
    // is suppressed. See CodeGenModule::MaybeHandleStaticInExternC.
    if (!Val)
      break;

    llvm::GlobalValue *ExistingElem =
        getModule().getNamedValue(Name->getName());

    // If there is either not something already by this name, or we were able to
    // replace all uses from IFuncs, create the alias.
    if (!ExistingElem || CheckAndReplaceExternCIFuncs(ExistingElem, Val))
      addCompilerUsedGlobal(llvm::GlobalAlias::create(Name->getName(), Val));
  }
}

bool CodeGenModule::lookupRepresentativeDecl(StringRef MangledName,
                                             GlobalDecl &Result) const {
  auto Res = Manglings.find(MangledName);
  if (Res == Manglings.end())
    return false;
  Result = Res->getValue();
  return true;
}

/// Emits metadata nodes associating all the global values in the
/// current module with the Decls they came from.  This is useful for
/// projects using IR gen as a subroutine.
///
/// Since there's currently no way to associate an MDNode directly
/// with an llvm::GlobalValue, we create a global named metadata
/// with the name 'clang.global.decl.ptrs'.
void CodeGenModule::EmitDeclMetadata() {
  llvm::NamedMDNode *GlobalMetadata = nullptr;

  for (auto &I : MangledDeclNames) {
    llvm::GlobalValue *Addr = getModule().getNamedValue(I.second);
    // Some mangled names don't necessarily have an associated GlobalValue
    // in this module, e.g. if we mangled it for DebugInfo.
    if (Addr)
      EmitGlobalDeclMetadata(*this, GlobalMetadata, I.first, Addr);
  }
}

/// Emits metadata nodes for all the local variables in the current
/// function.
void CodeGenFunction::EmitDeclMetadata() {
  if (LocalDeclMap.empty()) return;

  llvm::LLVMContext &Context = getLLVMContext();

  // Find the unique metadata ID for this name.
  unsigned DeclPtrKind = Context.getMDKindID("clang.decl.ptr");

  llvm::NamedMDNode *GlobalMetadata = nullptr;

  for (auto &I : LocalDeclMap) {
    const Decl *D = I.first;
    llvm::Value *Addr = I.second.emitRawPointer(*this);
    if (auto *Alloca = dyn_cast<llvm::AllocaInst>(Addr)) {
      llvm::Value *DAddr = GetPointerConstant(getLLVMContext(), D);
      Alloca->setMetadata(
          DeclPtrKind, llvm::MDNode::get(
                           Context, llvm::ValueAsMetadata::getConstant(DAddr)));
    } else if (auto *GV = dyn_cast<llvm::GlobalValue>(Addr)) {
      GlobalDecl GD = GlobalDecl(cast<VarDecl>(D));
      EmitGlobalDeclMetadata(CGM, GlobalMetadata, GD, GV);
    }
  }
}

void CodeGenModule::EmitVersionIdentMetadata() {
  llvm::NamedMDNode *IdentMetadata =
    TheModule.getOrInsertNamedMetadata("llvm.ident");
  std::string Version = getClangFullVersion();
  llvm::LLVMContext &Ctx = TheModule.getContext();

  llvm::Metadata *IdentNode[] = {llvm::MDString::get(Ctx, Version)};
  IdentMetadata->addOperand(llvm::MDNode::get(Ctx, IdentNode));
}

void CodeGenModule::EmitCommandLineMetadata() {
  llvm::NamedMDNode *CommandLineMetadata =
    TheModule.getOrInsertNamedMetadata("llvm.commandline");
  std::string CommandLine = getCodeGenOpts().RecordCommandLine;
  llvm::LLVMContext &Ctx = TheModule.getContext();

  llvm::Metadata *CommandLineNode[] = {llvm::MDString::get(Ctx, CommandLine)};
  CommandLineMetadata->addOperand(llvm::MDNode::get(Ctx, CommandLineNode));
}

void CodeGenModule::EmitCoverageFile() {
  llvm::NamedMDNode *CUNode = TheModule.getNamedMetadata("llvm.dbg.cu");
  if (!CUNode)
    return;

  llvm::NamedMDNode *GCov = TheModule.getOrInsertNamedMetadata("llvm.gcov");
  llvm::LLVMContext &Ctx = TheModule.getContext();
  auto *CoverageDataFile =
      llvm::MDString::get(Ctx, getCodeGenOpts().CoverageDataFile);
  auto *CoverageNotesFile =
      llvm::MDString::get(Ctx, getCodeGenOpts().CoverageNotesFile);
  for (int i = 0, e = CUNode->getNumOperands(); i != e; ++i) {
    llvm::MDNode *CU = CUNode->getOperand(i);
    llvm::Metadata *Elts[] = {CoverageNotesFile, CoverageDataFile, CU};
    GCov->addOperand(llvm::MDNode::get(Ctx, Elts));
  }
}

llvm::Constant *CodeGenModule::GetAddrOfRTTIDescriptor(QualType Ty,
                                                       bool ForEH) {
  // Return a bogus pointer if RTTI is disabled, unless it's for EH.
  // FIXME: should we even be calling this method if RTTI is disabled
  // and it's not for EH?
  if (!shouldEmitRTTI(ForEH))
    return llvm::Constant::getNullValue(GlobalsInt8PtrTy);

  if (ForEH && Ty->isObjCObjectPointerType() &&
      LangOpts.ObjCRuntime.isGNUFamily())
    return ObjCRuntime->GetEHType(Ty);

  return getCXXABI().getAddrOfRTTIDescriptor(Ty);
}

void CodeGenModule::EmitOMPThreadPrivateDecl(const OMPThreadPrivateDecl *D) {
  // Do not emit threadprivates in simd-only mode.
  if (LangOpts.OpenMP && LangOpts.OpenMPSimd)
    return;
  for (auto RefExpr : D->varlists()) {
    auto *VD = cast<VarDecl>(cast<DeclRefExpr>(RefExpr)->getDecl());
    bool PerformInit =
        VD->getAnyInitializer() &&
        !VD->getAnyInitializer()->isConstantInitializer(getContext(),
                                                        /*ForRef=*/false);

    Address Addr(GetAddrOfGlobalVar(VD),
                 getTypes().ConvertTypeForMem(VD->getType()),
                 getContext().getDeclAlign(VD));
    if (auto InitFunction = getOpenMPRuntime().emitThreadPrivateVarDefinition(
            VD, Addr, RefExpr->getBeginLoc(), PerformInit))
      CXXGlobalInits.push_back(InitFunction);
  }
}

llvm::Metadata *
CodeGenModule::CreateMetadataIdentifierImpl(QualType T, MetadataTypeMap &Map,
                                            StringRef Suffix) {
  if (auto *FnType = T->getAs<FunctionProtoType>())
    T = getContext().getFunctionType(
        FnType->getReturnType(), FnType->getParamTypes(),
        FnType->getExtProtoInfo().withExceptionSpec(EST_None));

  llvm::Metadata *&InternalId = Map[T.getCanonicalType()];
  if (InternalId)
    return InternalId;

  if (isExternallyVisible(T->getLinkage())) {
    std::string OutName;
    llvm::raw_string_ostream Out(OutName);
    getCXXABI().getMangleContext().mangleCanonicalTypeName(
        T, Out, getCodeGenOpts().SanitizeCfiICallNormalizeIntegers);

    if (getCodeGenOpts().SanitizeCfiICallNormalizeIntegers)
      Out << ".normalized";

    Out << Suffix;

    InternalId = llvm::MDString::get(getLLVMContext(), Out.str());
  } else {
    InternalId = llvm::MDNode::getDistinct(getLLVMContext(),
                                           llvm::ArrayRef<llvm::Metadata *>());
  }

  return InternalId;
}

llvm::Metadata *CodeGenModule::CreateMetadataIdentifierForType(QualType T) {
  return CreateMetadataIdentifierImpl(T, MetadataIdMap, "");
}

llvm::Metadata *
CodeGenModule::CreateMetadataIdentifierForVirtualMemPtrType(QualType T) {
  return CreateMetadataIdentifierImpl(T, VirtualMetadataIdMap, ".virtual");
}

// Generalize pointer types to a void pointer with the qualifiers of the
// originally pointed-to type, e.g. 'const char *' and 'char * const *'
// generalize to 'const void *' while 'char *' and 'const char **' generalize to
// 'void *'.
static QualType GeneralizeType(ASTContext &Ctx, QualType Ty) {
  if (!Ty->isPointerType())
    return Ty;

  return Ctx.getPointerType(
      QualType(Ctx.VoidTy).withCVRQualifiers(
          Ty->getPointeeType().getCVRQualifiers()));
}

// Apply type generalization to a FunctionType's return and argument types
static QualType GeneralizeFunctionType(ASTContext &Ctx, QualType Ty) {
  if (auto *FnType = Ty->getAs<FunctionProtoType>()) {
    SmallVector<QualType, 8> GeneralizedParams;
    for (auto &Param : FnType->param_types())
      GeneralizedParams.push_back(GeneralizeType(Ctx, Param));

    return Ctx.getFunctionType(
        GeneralizeType(Ctx, FnType->getReturnType()),
        GeneralizedParams, FnType->getExtProtoInfo());
  }

  if (auto *FnType = Ty->getAs<FunctionNoProtoType>())
    return Ctx.getFunctionNoProtoType(
        GeneralizeType(Ctx, FnType->getReturnType()));

  llvm_unreachable("Encountered unknown FunctionType");
}

llvm::Metadata *CodeGenModule::CreateMetadataIdentifierGeneralized(QualType T) {
  return CreateMetadataIdentifierImpl(GeneralizeFunctionType(getContext(), T),
                                      GeneralizedMetadataIdMap, ".generalized");
}

/// Returns whether this module needs the "all-vtables" type identifier.
bool CodeGenModule::NeedAllVtablesTypeId() const {
  // Returns true if at least one of vtable-based CFI checkers is enabled and
  // is not in the trapping mode.
  return ((LangOpts.Sanitize.has(SanitizerKind::CFIVCall) &&
           !CodeGenOpts.SanitizeTrap.has(SanitizerKind::CFIVCall)) ||
          (LangOpts.Sanitize.has(SanitizerKind::CFINVCall) &&
           !CodeGenOpts.SanitizeTrap.has(SanitizerKind::CFINVCall)) ||
          (LangOpts.Sanitize.has(SanitizerKind::CFIDerivedCast) &&
           !CodeGenOpts.SanitizeTrap.has(SanitizerKind::CFIDerivedCast)) ||
          (LangOpts.Sanitize.has(SanitizerKind::CFIUnrelatedCast) &&
           !CodeGenOpts.SanitizeTrap.has(SanitizerKind::CFIUnrelatedCast)));
}

void CodeGenModule::AddVTableTypeMetadata(llvm::GlobalVariable *VTable,
                                          CharUnits Offset,
                                          const CXXRecordDecl *RD) {
  llvm::Metadata *MD =
      CreateMetadataIdentifierForType(QualType(RD->getTypeForDecl(), 0));
  VTable->addTypeMetadata(Offset.getQuantity(), MD);

  if (CodeGenOpts.SanitizeCfiCrossDso)
    if (auto CrossDsoTypeId = CreateCrossDsoCfiTypeId(MD))
      VTable->addTypeMetadata(Offset.getQuantity(),
                              llvm::ConstantAsMetadata::get(CrossDsoTypeId));

  if (NeedAllVtablesTypeId()) {
    llvm::Metadata *MD = llvm::MDString::get(getLLVMContext(), "all-vtables");
    VTable->addTypeMetadata(Offset.getQuantity(), MD);
  }
}

llvm::SanitizerStatReport &CodeGenModule::getSanStats() {
  if (!SanStats)
    SanStats = std::make_unique<llvm::SanitizerStatReport>(&getModule());

  return *SanStats;
}

llvm::Value *
CodeGenModule::createOpenCLIntToSamplerConversion(const Expr *E,
                                                  CodeGenFunction &CGF) {
  llvm::Constant *C = ConstantEmitter(CGF).emitAbstract(E, E->getType());
  auto *SamplerT = getOpenCLRuntime().getSamplerType(E->getType().getTypePtr());
  auto *FTy = llvm::FunctionType::get(SamplerT, {C->getType()}, false);
  auto *Call = CGF.EmitRuntimeCall(
      CreateRuntimeFunction(FTy, "__translate_sampler_initializer"), {C});
  return Call;
}

CharUnits CodeGenModule::getNaturalPointeeTypeAlignment(
    QualType T, LValueBaseInfo *BaseInfo, TBAAAccessInfo *TBAAInfo) {
  return getNaturalTypeAlignment(T->getPointeeType(), BaseInfo, TBAAInfo,
                                 /* forPointeeType= */ true);
}

CharUnits CodeGenModule::getNaturalTypeAlignment(QualType T,
                                                 LValueBaseInfo *BaseInfo,
                                                 TBAAAccessInfo *TBAAInfo,
                                                 bool forPointeeType) {
  if (TBAAInfo)
    *TBAAInfo = getTBAAAccessInfo(T);

  // FIXME: This duplicates logic in ASTContext::getTypeAlignIfKnown. But
  // that doesn't return the information we need to compute BaseInfo.

  // Honor alignment typedef attributes even on incomplete types.
  // We also honor them straight for C++ class types, even as pointees;
  // there's an expressivity gap here.
  if (auto TT = T->getAs<TypedefType>()) {
    if (auto Align = TT->getDecl()->getMaxAlignment()) {
      if (BaseInfo)
        *BaseInfo = LValueBaseInfo(AlignmentSource::AttributedType);
      return getContext().toCharUnitsFromBits(Align);
    }
  }

  bool AlignForArray = T->isArrayType();

  // Analyze the base element type, so we don't get confused by incomplete
  // array types.
  T = getContext().getBaseElementType(T);

  if (T->isIncompleteType()) {
    // We could try to replicate the logic from
    // ASTContext::getTypeAlignIfKnown, but nothing uses the alignment if the
    // type is incomplete, so it's impossible to test. We could try to reuse
    // getTypeAlignIfKnown, but that doesn't return the information we need
    // to set BaseInfo.  So just ignore the possibility that the alignment is
    // greater than one.
    if (BaseInfo)
      *BaseInfo = LValueBaseInfo(AlignmentSource::Type);
    return CharUnits::One();
  }

  if (BaseInfo)
    *BaseInfo = LValueBaseInfo(AlignmentSource::Type);

  CharUnits Alignment;
  const CXXRecordDecl *RD;
  if (T.getQualifiers().hasUnaligned()) {
    Alignment = CharUnits::One();
  } else if (forPointeeType && !AlignForArray &&
             (RD = T->getAsCXXRecordDecl())) {
    // For C++ class pointees, we don't know whether we're pointing at a
    // base or a complete object, so we generally need to use the
    // non-virtual alignment.
    Alignment = getClassPointerAlignment(RD);
  } else {
    Alignment = getContext().getTypeAlignInChars(T);
  }

  // Cap to the global maximum type alignment unless the alignment
  // was somehow explicit on the type.
  if (unsigned MaxAlign = getLangOpts().MaxTypeAlign) {
    if (Alignment.getQuantity() > MaxAlign &&
        !getContext().isAlignmentRequired(T))
      Alignment = CharUnits::fromQuantity(MaxAlign);
  }
  return Alignment;
}

bool CodeGenModule::stopAutoInit() {
  unsigned StopAfter = getContext().getLangOpts().TrivialAutoVarInitStopAfter;
  if (StopAfter) {
    // This number is positive only when -ftrivial-auto-var-init-stop-after=* is
    // used
    if (NumAutoVarInit >= StopAfter) {
      return true;
    }
    if (!NumAutoVarInit) {
      unsigned DiagID = getDiags().getCustomDiagID(
          DiagnosticsEngine::Warning,
          "-ftrivial-auto-var-init-stop-after=%0 has been enabled to limit the "
          "number of times ftrivial-auto-var-init=%1 gets applied.");
      getDiags().Report(DiagID)
          << StopAfter
          << (getContext().getLangOpts().getTrivialAutoVarInit() ==
                      LangOptions::TrivialAutoVarInitKind::Zero
                  ? "zero"
                  : "pattern");
    }
    ++NumAutoVarInit;
  }
  return false;
}

void CodeGenModule::printPostfixForExternalizedDecl(llvm::raw_ostream &OS,
                                                    const Decl *D) const {
  // ptxas does not allow '.' in symbol names. On the other hand, HIP prefers
  // postfix beginning with '.' since the symbol name can be demangled.
  if (LangOpts.HIP)
    OS << (isa<VarDecl>(D) ? ".static." : ".intern.");
  else
    OS << (isa<VarDecl>(D) ? "__static__" : "__intern__");

  // If the CUID is not specified we try to generate a unique postfix.
  if (getLangOpts().CUID.empty()) {
    SourceManager &SM = getContext().getSourceManager();
    PresumedLoc PLoc = SM.getPresumedLoc(D->getLocation());
    assert(PLoc.isValid() && "Source location is expected to be valid.");

    // Get the hash of the user defined macros.
    llvm::MD5 Hash;
    llvm::MD5::MD5Result Result;
    for (const auto &Arg : PreprocessorOpts.Macros)
      Hash.update(Arg.first);
    Hash.final(Result);

    // Get the UniqueID for the file containing the decl.
    llvm::sys::fs::UniqueID ID;
    if (llvm::sys::fs::getUniqueID(PLoc.getFilename(), ID)) {
      PLoc = SM.getPresumedLoc(D->getLocation(), /*UseLineDirectives=*/false);
      assert(PLoc.isValid() && "Source location is expected to be valid.");
      if (auto EC = llvm::sys::fs::getUniqueID(PLoc.getFilename(), ID))
        SM.getDiagnostics().Report(diag::err_cannot_open_file)
            << PLoc.getFilename() << EC.message();
    }
    OS << llvm::format("%x", ID.getFile()) << llvm::format("%x", ID.getDevice())
       << "_" << llvm::utohexstr(Result.low(), /*LowerCase=*/true, /*Width=*/8);
  } else {
    OS << getContext().getCUIDHash();
  }
}

void CodeGenModule::moveLazyEmissionStates(CodeGenModule *NewBuilder) {
  assert(DeferredDeclsToEmit.empty() &&
         "Should have emitted all decls deferred to emit.");
  assert(NewBuilder->DeferredDecls.empty() &&
         "Newly created module should not have deferred decls");
  NewBuilder->DeferredDecls = std::move(DeferredDecls);
  assert(EmittedDeferredDecls.empty() &&
         "Still have (unmerged) EmittedDeferredDecls deferred decls");

  assert(NewBuilder->DeferredVTables.empty() &&
         "Newly created module should not have deferred vtables");
  NewBuilder->DeferredVTables = std::move(DeferredVTables);

  assert(NewBuilder->MangledDeclNames.empty() &&
         "Newly created module should not have mangled decl names");
  assert(NewBuilder->Manglings.empty() &&
         "Newly created module should not have manglings");
  NewBuilder->Manglings = std::move(Manglings);

  NewBuilder->WeakRefReferences = std::move(WeakRefReferences);

  NewBuilder->ABI->MangleCtx = std::move(ABI->MangleCtx);
}
