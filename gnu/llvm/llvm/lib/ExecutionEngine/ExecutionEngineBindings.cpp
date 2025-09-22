//===-- ExecutionEngineBindings.cpp - C bindings for EEs ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the C bindings for the ExecutionEngine library.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/ExecutionEngine.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/CodeGenCWrappers.h"
#include "llvm/Target/TargetOptions.h"
#include <cstring>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "jit"

// Wrapping the C bindings types.
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(GenericValue, LLVMGenericValueRef)


static LLVMTargetMachineRef wrap(const TargetMachine *P) {
  return
  reinterpret_cast<LLVMTargetMachineRef>(const_cast<TargetMachine*>(P));
}

/*===-- Operations on generic values --------------------------------------===*/

LLVMGenericValueRef LLVMCreateGenericValueOfInt(LLVMTypeRef Ty,
                                                unsigned long long N,
                                                LLVMBool IsSigned) {
  GenericValue *GenVal = new GenericValue();
  GenVal->IntVal = APInt(unwrap<IntegerType>(Ty)->getBitWidth(), N, IsSigned);
  return wrap(GenVal);
}

LLVMGenericValueRef LLVMCreateGenericValueOfPointer(void *P) {
  GenericValue *GenVal = new GenericValue();
  GenVal->PointerVal = P;
  return wrap(GenVal);
}

LLVMGenericValueRef LLVMCreateGenericValueOfFloat(LLVMTypeRef TyRef, double N) {
  GenericValue *GenVal = new GenericValue();
  switch (unwrap(TyRef)->getTypeID()) {
  case Type::FloatTyID:
    GenVal->FloatVal = N;
    break;
  case Type::DoubleTyID:
    GenVal->DoubleVal = N;
    break;
  default:
    llvm_unreachable("LLVMGenericValueToFloat supports only float and double.");
  }
  return wrap(GenVal);
}

unsigned LLVMGenericValueIntWidth(LLVMGenericValueRef GenValRef) {
  return unwrap(GenValRef)->IntVal.getBitWidth();
}

unsigned long long LLVMGenericValueToInt(LLVMGenericValueRef GenValRef,
                                         LLVMBool IsSigned) {
  GenericValue *GenVal = unwrap(GenValRef);
  if (IsSigned)
    return GenVal->IntVal.getSExtValue();
  else
    return GenVal->IntVal.getZExtValue();
}

void *LLVMGenericValueToPointer(LLVMGenericValueRef GenVal) {
  return unwrap(GenVal)->PointerVal;
}

double LLVMGenericValueToFloat(LLVMTypeRef TyRef, LLVMGenericValueRef GenVal) {
  switch (unwrap(TyRef)->getTypeID()) {
  case Type::FloatTyID:
    return unwrap(GenVal)->FloatVal;
  case Type::DoubleTyID:
    return unwrap(GenVal)->DoubleVal;
  default:
    llvm_unreachable("LLVMGenericValueToFloat supports only float and double.");
  }
}

void LLVMDisposeGenericValue(LLVMGenericValueRef GenVal) {
  delete unwrap(GenVal);
}

/*===-- Operations on execution engines -----------------------------------===*/

LLVMBool LLVMCreateExecutionEngineForModule(LLVMExecutionEngineRef *OutEE,
                                            LLVMModuleRef M,
                                            char **OutError) {
  std::string Error;
  EngineBuilder builder(std::unique_ptr<Module>(unwrap(M)));
  builder.setEngineKind(EngineKind::Either)
         .setErrorStr(&Error);
  if (ExecutionEngine *EE = builder.create()){
    *OutEE = wrap(EE);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}

LLVMBool LLVMCreateInterpreterForModule(LLVMExecutionEngineRef *OutInterp,
                                        LLVMModuleRef M,
                                        char **OutError) {
  std::string Error;
  EngineBuilder builder(std::unique_ptr<Module>(unwrap(M)));
  builder.setEngineKind(EngineKind::Interpreter)
         .setErrorStr(&Error);
  if (ExecutionEngine *Interp = builder.create()) {
    *OutInterp = wrap(Interp);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}

LLVMBool LLVMCreateJITCompilerForModule(LLVMExecutionEngineRef *OutJIT,
                                        LLVMModuleRef M,
                                        unsigned OptLevel,
                                        char **OutError) {
  std::string Error;
  EngineBuilder builder(std::unique_ptr<Module>(unwrap(M)));
  builder.setEngineKind(EngineKind::JIT)
      .setErrorStr(&Error)
      .setOptLevel((CodeGenOptLevel)OptLevel);
  if (ExecutionEngine *JIT = builder.create()) {
    *OutJIT = wrap(JIT);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}

void LLVMInitializeMCJITCompilerOptions(LLVMMCJITCompilerOptions *PassedOptions,
                                        size_t SizeOfPassedOptions) {
  LLVMMCJITCompilerOptions options;
  memset(&options, 0, sizeof(options)); // Most fields are zero by default.
  options.CodeModel = LLVMCodeModelJITDefault;

  memcpy(PassedOptions, &options,
         std::min(sizeof(options), SizeOfPassedOptions));
}

LLVMBool LLVMCreateMCJITCompilerForModule(
    LLVMExecutionEngineRef *OutJIT, LLVMModuleRef M,
    LLVMMCJITCompilerOptions *PassedOptions, size_t SizeOfPassedOptions,
    char **OutError) {
  LLVMMCJITCompilerOptions options;
  // If the user passed a larger sized options struct, then they were compiled
  // against a newer LLVM. Tell them that something is wrong.
  if (SizeOfPassedOptions > sizeof(options)) {
    *OutError = strdup(
      "Refusing to use options struct that is larger than my own; assuming "
      "LLVM library mismatch.");
    return 1;
  }

  // Defend against the user having an old version of the API by ensuring that
  // any fields they didn't see are cleared. We must defend against fields being
  // set to the bitwise equivalent of zero, and assume that this means "do the
  // default" as if that option hadn't been available.
  LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
  memcpy(&options, PassedOptions, SizeOfPassedOptions);

  TargetOptions targetOptions;
  targetOptions.EnableFastISel = options.EnableFastISel;
  std::unique_ptr<Module> Mod(unwrap(M));

  if (Mod)
    // Set function attribute "frame-pointer" based on
    // NoFramePointerElim.
    for (auto &F : *Mod) {
      auto Attrs = F.getAttributes();
      StringRef Value = options.NoFramePointerElim ? "all" : "none";
      Attrs = Attrs.addFnAttribute(F.getContext(), "frame-pointer", Value);
      F.setAttributes(Attrs);
    }

  std::string Error;
  EngineBuilder builder(std::move(Mod));
  builder.setEngineKind(EngineKind::JIT)
      .setErrorStr(&Error)
      .setOptLevel((CodeGenOptLevel)options.OptLevel)
      .setTargetOptions(targetOptions);
  bool JIT;
  if (std::optional<CodeModel::Model> CM = unwrap(options.CodeModel, JIT))
    builder.setCodeModel(*CM);
  if (options.MCJMM)
    builder.setMCJITMemoryManager(
      std::unique_ptr<RTDyldMemoryManager>(unwrap(options.MCJMM)));
  if (ExecutionEngine *JIT = builder.create()) {
    *OutJIT = wrap(JIT);
    return 0;
  }
  *OutError = strdup(Error.c_str());
  return 1;
}

void LLVMDisposeExecutionEngine(LLVMExecutionEngineRef EE) {
  delete unwrap(EE);
}

void LLVMRunStaticConstructors(LLVMExecutionEngineRef EE) {
  unwrap(EE)->finalizeObject();
  unwrap(EE)->runStaticConstructorsDestructors(false);
}

void LLVMRunStaticDestructors(LLVMExecutionEngineRef EE) {
  unwrap(EE)->finalizeObject();
  unwrap(EE)->runStaticConstructorsDestructors(true);
}

int LLVMRunFunctionAsMain(LLVMExecutionEngineRef EE, LLVMValueRef F,
                          unsigned ArgC, const char * const *ArgV,
                          const char * const *EnvP) {
  unwrap(EE)->finalizeObject();

  std::vector<std::string> ArgVec(ArgV, ArgV + ArgC);
  return unwrap(EE)->runFunctionAsMain(unwrap<Function>(F), ArgVec, EnvP);
}

LLVMGenericValueRef LLVMRunFunction(LLVMExecutionEngineRef EE, LLVMValueRef F,
                                    unsigned NumArgs,
                                    LLVMGenericValueRef *Args) {
  unwrap(EE)->finalizeObject();

  std::vector<GenericValue> ArgVec;
  ArgVec.reserve(NumArgs);
  for (unsigned I = 0; I != NumArgs; ++I)
    ArgVec.push_back(*unwrap(Args[I]));

  GenericValue *Result = new GenericValue();
  *Result = unwrap(EE)->runFunction(unwrap<Function>(F), ArgVec);
  return wrap(Result);
}

void LLVMFreeMachineCodeForFunction(LLVMExecutionEngineRef EE, LLVMValueRef F) {
}

void LLVMAddModule(LLVMExecutionEngineRef EE, LLVMModuleRef M){
  unwrap(EE)->addModule(std::unique_ptr<Module>(unwrap(M)));
}

LLVMBool LLVMRemoveModule(LLVMExecutionEngineRef EE, LLVMModuleRef M,
                          LLVMModuleRef *OutMod, char **OutError) {
  Module *Mod = unwrap(M);
  unwrap(EE)->removeModule(Mod);
  *OutMod = wrap(Mod);
  return 0;
}

LLVMBool LLVMFindFunction(LLVMExecutionEngineRef EE, const char *Name,
                          LLVMValueRef *OutFn) {
  if (Function *F = unwrap(EE)->FindFunctionNamed(Name)) {
    *OutFn = wrap(F);
    return 0;
  }
  return 1;
}

void *LLVMRecompileAndRelinkFunction(LLVMExecutionEngineRef EE,
                                     LLVMValueRef Fn) {
  return nullptr;
}

LLVMTargetDataRef LLVMGetExecutionEngineTargetData(LLVMExecutionEngineRef EE) {
  return wrap(&unwrap(EE)->getDataLayout());
}

LLVMTargetMachineRef
LLVMGetExecutionEngineTargetMachine(LLVMExecutionEngineRef EE) {
  return wrap(unwrap(EE)->getTargetMachine());
}

void LLVMAddGlobalMapping(LLVMExecutionEngineRef EE, LLVMValueRef Global,
                          void* Addr) {
  unwrap(EE)->addGlobalMapping(unwrap<GlobalValue>(Global), Addr);
}

void *LLVMGetPointerToGlobal(LLVMExecutionEngineRef EE, LLVMValueRef Global) {
  unwrap(EE)->finalizeObject();

  return unwrap(EE)->getPointerToGlobal(unwrap<GlobalValue>(Global));
}

uint64_t LLVMGetGlobalValueAddress(LLVMExecutionEngineRef EE, const char *Name) {
  return unwrap(EE)->getGlobalValueAddress(Name);
}

uint64_t LLVMGetFunctionAddress(LLVMExecutionEngineRef EE, const char *Name) {
  return unwrap(EE)->getFunctionAddress(Name);
}

LLVMBool LLVMExecutionEngineGetErrMsg(LLVMExecutionEngineRef EE,
                                      char **OutError) {
  assert(OutError && "OutError must be non-null");
  auto *ExecEngine = unwrap(EE);
  if (ExecEngine->hasError()) {
    *OutError = strdup(ExecEngine->getErrorMessage().c_str());
    ExecEngine->clearErrorMessage();
    return true;
  }
  return false;
}

/*===-- Operations on memory managers -------------------------------------===*/

namespace {

struct SimpleBindingMMFunctions {
  LLVMMemoryManagerAllocateCodeSectionCallback AllocateCodeSection;
  LLVMMemoryManagerAllocateDataSectionCallback AllocateDataSection;
  LLVMMemoryManagerFinalizeMemoryCallback FinalizeMemory;
  LLVMMemoryManagerDestroyCallback Destroy;
};

class SimpleBindingMemoryManager : public RTDyldMemoryManager {
public:
  SimpleBindingMemoryManager(const SimpleBindingMMFunctions& Functions,
                             void *Opaque);
  ~SimpleBindingMemoryManager() override;

  uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID,
                               StringRef SectionName) override;

  uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID, StringRef SectionName,
                               bool isReadOnly) override;

  bool finalizeMemory(std::string *ErrMsg) override;

private:
  SimpleBindingMMFunctions Functions;
  void *Opaque;
};

SimpleBindingMemoryManager::SimpleBindingMemoryManager(
  const SimpleBindingMMFunctions& Functions,
  void *Opaque)
  : Functions(Functions), Opaque(Opaque) {
  assert(Functions.AllocateCodeSection &&
         "No AllocateCodeSection function provided!");
  assert(Functions.AllocateDataSection &&
         "No AllocateDataSection function provided!");
  assert(Functions.FinalizeMemory &&
         "No FinalizeMemory function provided!");
  assert(Functions.Destroy &&
         "No Destroy function provided!");
}

SimpleBindingMemoryManager::~SimpleBindingMemoryManager() {
  Functions.Destroy(Opaque);
}

uint8_t *SimpleBindingMemoryManager::allocateCodeSection(
  uintptr_t Size, unsigned Alignment, unsigned SectionID,
  StringRef SectionName) {
  return Functions.AllocateCodeSection(Opaque, Size, Alignment, SectionID,
                                       SectionName.str().c_str());
}

uint8_t *SimpleBindingMemoryManager::allocateDataSection(
  uintptr_t Size, unsigned Alignment, unsigned SectionID,
  StringRef SectionName, bool isReadOnly) {
  return Functions.AllocateDataSection(Opaque, Size, Alignment, SectionID,
                                       SectionName.str().c_str(),
                                       isReadOnly);
}

bool SimpleBindingMemoryManager::finalizeMemory(std::string *ErrMsg) {
  char *errMsgCString = nullptr;
  bool result = Functions.FinalizeMemory(Opaque, &errMsgCString);
  assert((result || !errMsgCString) &&
         "Did not expect an error message if FinalizeMemory succeeded");
  if (errMsgCString) {
    if (ErrMsg)
      *ErrMsg = errMsgCString;
    free(errMsgCString);
  }
  return result;
}

} // anonymous namespace

LLVMMCJITMemoryManagerRef LLVMCreateSimpleMCJITMemoryManager(
  void *Opaque,
  LLVMMemoryManagerAllocateCodeSectionCallback AllocateCodeSection,
  LLVMMemoryManagerAllocateDataSectionCallback AllocateDataSection,
  LLVMMemoryManagerFinalizeMemoryCallback FinalizeMemory,
  LLVMMemoryManagerDestroyCallback Destroy) {

  if (!AllocateCodeSection || !AllocateDataSection || !FinalizeMemory ||
      !Destroy)
    return nullptr;

  SimpleBindingMMFunctions functions;
  functions.AllocateCodeSection = AllocateCodeSection;
  functions.AllocateDataSection = AllocateDataSection;
  functions.FinalizeMemory = FinalizeMemory;
  functions.Destroy = Destroy;
  return wrap(new SimpleBindingMemoryManager(functions, Opaque));
}

void LLVMDisposeMCJITMemoryManager(LLVMMCJITMemoryManagerRef MM) {
  delete unwrap(MM);
}

/*===-- JIT Event Listener functions -------------------------------------===*/


#if !LLVM_USE_INTEL_JITEVENTS
LLVMJITEventListenerRef LLVMCreateIntelJITEventListener(void)
{
  return nullptr;
}
#endif

#if !LLVM_USE_OPROFILE
LLVMJITEventListenerRef LLVMCreateOProfileJITEventListener(void)
{
  return nullptr;
}
#endif

#if !LLVM_USE_PERF
LLVMJITEventListenerRef LLVMCreatePerfJITEventListener(void)
{
  return nullptr;
}
#endif
