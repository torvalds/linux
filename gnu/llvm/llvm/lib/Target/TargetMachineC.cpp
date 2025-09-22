//===-- TargetMachine.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LLVM-C part of TargetMachine.h
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Core.h"
#include "llvm-c/TargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/CodeGenCWrappers.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <cstring>
#include <optional>

using namespace llvm;

namespace llvm {

/// Options for LLVMCreateTargetMachine().
struct LLVMTargetMachineOptions {
  std::string CPU;
  std::string Features;
  std::string ABI;
  CodeGenOptLevel OL = CodeGenOptLevel::Default;
  std::optional<Reloc::Model> RM;
  std::optional<CodeModel::Model> CM;
  bool JIT;
};

} // namespace llvm

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(LLVMTargetMachineOptions,
                                   LLVMTargetMachineOptionsRef)

static TargetMachine *unwrap(LLVMTargetMachineRef P) {
  return reinterpret_cast<TargetMachine *>(P);
}
static Target *unwrap(LLVMTargetRef P) {
  return reinterpret_cast<Target*>(P);
}
static LLVMTargetMachineRef wrap(const TargetMachine *P) {
  return reinterpret_cast<LLVMTargetMachineRef>(const_cast<TargetMachine *>(P));
}
static LLVMTargetRef wrap(const Target * P) {
  return reinterpret_cast<LLVMTargetRef>(const_cast<Target*>(P));
}

LLVMTargetRef LLVMGetFirstTarget() {
  if (TargetRegistry::targets().begin() == TargetRegistry::targets().end()) {
    return nullptr;
  }

  const Target *target = &*TargetRegistry::targets().begin();
  return wrap(target);
}
LLVMTargetRef LLVMGetNextTarget(LLVMTargetRef T) {
  return wrap(unwrap(T)->getNext());
}

LLVMTargetRef LLVMGetTargetFromName(const char *Name) {
  StringRef NameRef = Name;
  auto I = find_if(TargetRegistry::targets(),
                   [&](const Target &T) { return T.getName() == NameRef; });
  return I != TargetRegistry::targets().end() ? wrap(&*I) : nullptr;
}

LLVMBool LLVMGetTargetFromTriple(const char* TripleStr, LLVMTargetRef *T,
                                 char **ErrorMessage) {
  std::string Error;

  *T = wrap(TargetRegistry::lookupTarget(TripleStr, Error));

  if (!*T) {
    if (ErrorMessage)
      *ErrorMessage = strdup(Error.c_str());

    return 1;
  }

  return 0;
}

const char * LLVMGetTargetName(LLVMTargetRef T) {
  return unwrap(T)->getName();
}

const char * LLVMGetTargetDescription(LLVMTargetRef T) {
  return unwrap(T)->getShortDescription();
}

LLVMBool LLVMTargetHasJIT(LLVMTargetRef T) {
  return unwrap(T)->hasJIT();
}

LLVMBool LLVMTargetHasTargetMachine(LLVMTargetRef T) {
  return unwrap(T)->hasTargetMachine();
}

LLVMBool LLVMTargetHasAsmBackend(LLVMTargetRef T) {
  return unwrap(T)->hasMCAsmBackend();
}

LLVMTargetMachineOptionsRef LLVMCreateTargetMachineOptions(void) {
  return wrap(new LLVMTargetMachineOptions());
}

void LLVMDisposeTargetMachineOptions(LLVMTargetMachineOptionsRef Options) {
  delete unwrap(Options);
}

void LLVMTargetMachineOptionsSetCPU(LLVMTargetMachineOptionsRef Options,
                                    const char *CPU) {
  unwrap(Options)->CPU = CPU;
}

void LLVMTargetMachineOptionsSetFeatures(LLVMTargetMachineOptionsRef Options,
                                         const char *Features) {
  unwrap(Options)->Features = Features;
}

void LLVMTargetMachineOptionsSetABI(LLVMTargetMachineOptionsRef Options,
                                    const char *ABI) {
  unwrap(Options)->ABI = ABI;
}

void LLVMTargetMachineOptionsSetCodeGenOptLevel(
    LLVMTargetMachineOptionsRef Options, LLVMCodeGenOptLevel Level) {
  CodeGenOptLevel OL;

  switch (Level) {
  case LLVMCodeGenLevelNone:
    OL = CodeGenOptLevel::None;
    break;
  case LLVMCodeGenLevelLess:
    OL = CodeGenOptLevel::Less;
    break;
  case LLVMCodeGenLevelAggressive:
    OL = CodeGenOptLevel::Aggressive;
    break;
  case LLVMCodeGenLevelDefault:
    OL = CodeGenOptLevel::Default;
    break;
  }

  unwrap(Options)->OL = OL;
}

void LLVMTargetMachineOptionsSetRelocMode(LLVMTargetMachineOptionsRef Options,
                                          LLVMRelocMode Reloc) {
  std::optional<Reloc::Model> RM;

  switch (Reloc) {
  case LLVMRelocStatic:
    RM = Reloc::Static;
    break;
  case LLVMRelocPIC:
    RM = Reloc::PIC_;
    break;
  case LLVMRelocDynamicNoPic:
    RM = Reloc::DynamicNoPIC;
    break;
  case LLVMRelocROPI:
    RM = Reloc::ROPI;
    break;
  case LLVMRelocRWPI:
    RM = Reloc::RWPI;
    break;
  case LLVMRelocROPI_RWPI:
    RM = Reloc::ROPI_RWPI;
    break;
  case LLVMRelocDefault:
    break;
  }

  unwrap(Options)->RM = RM;
}

void LLVMTargetMachineOptionsSetCodeModel(LLVMTargetMachineOptionsRef Options,
                                          LLVMCodeModel CodeModel) {
  auto CM = unwrap(CodeModel, unwrap(Options)->JIT);
  unwrap(Options)->CM = CM;
}

LLVMTargetMachineRef
LLVMCreateTargetMachineWithOptions(LLVMTargetRef T, const char *Triple,
                                   LLVMTargetMachineOptionsRef Options) {
  auto *Opt = unwrap(Options);
  TargetOptions TO;
  TO.MCOptions.ABIName = Opt->ABI;
  return wrap(unwrap(T)->createTargetMachine(Triple, Opt->CPU, Opt->Features,
                                             TO, Opt->RM, Opt->CM, Opt->OL,
                                             Opt->JIT));
}

LLVMTargetMachineRef
LLVMCreateTargetMachine(LLVMTargetRef T, const char *Triple, const char *CPU,
                        const char *Features, LLVMCodeGenOptLevel Level,
                        LLVMRelocMode Reloc, LLVMCodeModel CodeModel) {
  auto *Options = LLVMCreateTargetMachineOptions();

  LLVMTargetMachineOptionsSetCPU(Options, CPU);
  LLVMTargetMachineOptionsSetFeatures(Options, Features);
  LLVMTargetMachineOptionsSetCodeGenOptLevel(Options, Level);
  LLVMTargetMachineOptionsSetRelocMode(Options, Reloc);
  LLVMTargetMachineOptionsSetCodeModel(Options, CodeModel);

  auto *Machine = LLVMCreateTargetMachineWithOptions(T, Triple, Options);

  LLVMDisposeTargetMachineOptions(Options);
  return Machine;
}

void LLVMDisposeTargetMachine(LLVMTargetMachineRef T) { delete unwrap(T); }

LLVMTargetRef LLVMGetTargetMachineTarget(LLVMTargetMachineRef T) {
  const Target* target = &(unwrap(T)->getTarget());
  return wrap(target);
}

char* LLVMGetTargetMachineTriple(LLVMTargetMachineRef T) {
  std::string StringRep = unwrap(T)->getTargetTriple().str();
  return strdup(StringRep.c_str());
}

char* LLVMGetTargetMachineCPU(LLVMTargetMachineRef T) {
  std::string StringRep = std::string(unwrap(T)->getTargetCPU());
  return strdup(StringRep.c_str());
}

char* LLVMGetTargetMachineFeatureString(LLVMTargetMachineRef T) {
  std::string StringRep = std::string(unwrap(T)->getTargetFeatureString());
  return strdup(StringRep.c_str());
}

void LLVMSetTargetMachineAsmVerbosity(LLVMTargetMachineRef T,
                                      LLVMBool VerboseAsm) {
  unwrap(T)->Options.MCOptions.AsmVerbose = VerboseAsm;
}

void LLVMSetTargetMachineFastISel(LLVMTargetMachineRef T, LLVMBool Enable) {
  unwrap(T)->setFastISel(Enable);
}

void LLVMSetTargetMachineGlobalISel(LLVMTargetMachineRef T, LLVMBool Enable) {
  unwrap(T)->setGlobalISel(Enable);
}

void LLVMSetTargetMachineGlobalISelAbort(LLVMTargetMachineRef T,
                                         LLVMGlobalISelAbortMode Mode) {
  GlobalISelAbortMode AM = GlobalISelAbortMode::Enable;
  switch (Mode) {
  case LLVMGlobalISelAbortDisable:
    AM = GlobalISelAbortMode::Disable;
    break;
  case LLVMGlobalISelAbortEnable:
    AM = GlobalISelAbortMode::Enable;
    break;
  case LLVMGlobalISelAbortDisableWithDiag:
    AM = GlobalISelAbortMode::DisableWithDiag;
    break;
  }

  unwrap(T)->setGlobalISelAbort(AM);
}

void LLVMSetTargetMachineMachineOutliner(LLVMTargetMachineRef T,
                                         LLVMBool Enable) {
  unwrap(T)->setMachineOutliner(Enable);
}

LLVMTargetDataRef LLVMCreateTargetDataLayout(LLVMTargetMachineRef T) {
  return wrap(new DataLayout(unwrap(T)->createDataLayout()));
}

static LLVMBool LLVMTargetMachineEmit(LLVMTargetMachineRef T, LLVMModuleRef M,
                                      raw_pwrite_stream &OS,
                                      LLVMCodeGenFileType codegen,
                                      char **ErrorMessage) {
  TargetMachine* TM = unwrap(T);
  Module* Mod = unwrap(M);

  legacy::PassManager pass;

  std::string error;

  Mod->setDataLayout(TM->createDataLayout());

  CodeGenFileType ft;
  switch (codegen) {
    case LLVMAssemblyFile:
      ft = CodeGenFileType::AssemblyFile;
      break;
    default:
      ft = CodeGenFileType::ObjectFile;
      break;
  }
  if (TM->addPassesToEmitFile(pass, OS, nullptr, ft)) {
    error = "TargetMachine can't emit a file of this type";
    *ErrorMessage = strdup(error.c_str());
    return true;
  }

  pass.run(*Mod);

  OS.flush();
  return false;
}

LLVMBool LLVMTargetMachineEmitToFile(LLVMTargetMachineRef T, LLVMModuleRef M,
                                     const char *Filename,
                                     LLVMCodeGenFileType codegen,
                                     char **ErrorMessage) {
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);
  if (EC) {
    *ErrorMessage = strdup(EC.message().c_str());
    return true;
  }
  bool Result = LLVMTargetMachineEmit(T, M, dest, codegen, ErrorMessage);
  dest.flush();
  return Result;
}

LLVMBool LLVMTargetMachineEmitToMemoryBuffer(LLVMTargetMachineRef T,
  LLVMModuleRef M, LLVMCodeGenFileType codegen, char** ErrorMessage,
  LLVMMemoryBufferRef *OutMemBuf) {
  SmallString<0> CodeString;
  raw_svector_ostream OStream(CodeString);
  bool Result = LLVMTargetMachineEmit(T, M, OStream, codegen, ErrorMessage);

  StringRef Data = OStream.str();
  *OutMemBuf =
      LLVMCreateMemoryBufferWithMemoryRangeCopy(Data.data(), Data.size(), "");
  return Result;
}

char *LLVMGetDefaultTargetTriple(void) {
  return strdup(sys::getDefaultTargetTriple().c_str());
}

char *LLVMNormalizeTargetTriple(const char* triple) {
  return strdup(Triple::normalize(StringRef(triple)).c_str());
}

char *LLVMGetHostCPUName(void) {
  return strdup(sys::getHostCPUName().data());
}

char *LLVMGetHostCPUFeatures(void) {
  SubtargetFeatures Features;
  for (const auto &[Feature, IsEnabled] : sys::getHostCPUFeatures())
    Features.AddFeature(Feature, IsEnabled);

  return strdup(Features.getString().c_str());
}

void LLVMAddAnalysisPasses(LLVMTargetMachineRef T, LLVMPassManagerRef PM) {
  unwrap(PM)->add(
      createTargetTransformInfoWrapperPass(unwrap(T)->getTargetIRAnalysis()));
}
