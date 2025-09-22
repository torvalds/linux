//===--------- LLJIT.cpp - An ORC-based JIT for compiling LLVM IR ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/JITLink/EHFrameSupport.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/COFFPlatform.h"
#include "llvm/ExecutionEngine/Orc/ELFNixPlatform.h"
#include "llvm/ExecutionEngine/Orc/EPCDynamicLibrarySearchGenerator.h"
#include "llvm/ExecutionEngine/Orc/EPCEHFrameRegistrar.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/MachOPlatform.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcError.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/DynamicLibrary.h"

#define DEBUG_TYPE "orc"

using namespace llvm;
using namespace llvm::orc;

namespace {

/// Adds helper function decls and wrapper functions that call the helper with
/// some additional prefix arguments.
///
/// E.g. For wrapper "foo" with type i8(i8, i64), helper "bar", and prefix
/// args i32 4 and i16 12345, this function will add:
///
/// declare i8 @bar(i32, i16, i8, i64)
///
/// define i8 @foo(i8, i64) {
/// entry:
///   %2 = call i8 @bar(i32 4, i16 12345, i8 %0, i64 %1)
///   ret i8 %2
/// }
///
Function *addHelperAndWrapper(Module &M, StringRef WrapperName,
                              FunctionType *WrapperFnType,
                              GlobalValue::VisibilityTypes WrapperVisibility,
                              StringRef HelperName,
                              ArrayRef<Value *> HelperPrefixArgs) {
  std::vector<Type *> HelperArgTypes;
  for (auto *Arg : HelperPrefixArgs)
    HelperArgTypes.push_back(Arg->getType());
  for (auto *T : WrapperFnType->params())
    HelperArgTypes.push_back(T);
  auto *HelperFnType =
      FunctionType::get(WrapperFnType->getReturnType(), HelperArgTypes, false);
  auto *HelperFn = Function::Create(HelperFnType, GlobalValue::ExternalLinkage,
                                    HelperName, M);

  auto *WrapperFn = Function::Create(
      WrapperFnType, GlobalValue::ExternalLinkage, WrapperName, M);
  WrapperFn->setVisibility(WrapperVisibility);

  auto *EntryBlock = BasicBlock::Create(M.getContext(), "entry", WrapperFn);
  IRBuilder<> IB(EntryBlock);

  std::vector<Value *> HelperArgs;
  for (auto *Arg : HelperPrefixArgs)
    HelperArgs.push_back(Arg);
  for (auto &Arg : WrapperFn->args())
    HelperArgs.push_back(&Arg);
  auto *HelperResult = IB.CreateCall(HelperFn, HelperArgs);
  if (HelperFn->getReturnType()->isVoidTy())
    IB.CreateRetVoid();
  else
    IB.CreateRet(HelperResult);

  return WrapperFn;
}

class GenericLLVMIRPlatformSupport;

/// orc::Platform component of Generic LLVM IR Platform support.
/// Just forwards calls to the GenericLLVMIRPlatformSupport class below.
class GenericLLVMIRPlatform : public Platform {
public:
  GenericLLVMIRPlatform(GenericLLVMIRPlatformSupport &S) : S(S) {}
  Error setupJITDylib(JITDylib &JD) override;
  Error teardownJITDylib(JITDylib &JD) override;
  Error notifyAdding(ResourceTracker &RT,
                     const MaterializationUnit &MU) override;
  Error notifyRemoving(ResourceTracker &RT) override {
    // Noop -- Nothing to do (yet).
    return Error::success();
  }

private:
  GenericLLVMIRPlatformSupport &S;
};

/// This transform parses llvm.global_ctors to produce a single initialization
/// function for the module, records the function, then deletes
/// llvm.global_ctors.
class GlobalCtorDtorScraper {
public:
  GlobalCtorDtorScraper(GenericLLVMIRPlatformSupport &PS,
                        StringRef InitFunctionPrefix,
                        StringRef DeInitFunctionPrefix)
      : PS(PS), InitFunctionPrefix(InitFunctionPrefix),
        DeInitFunctionPrefix(DeInitFunctionPrefix) {}
  Expected<ThreadSafeModule> operator()(ThreadSafeModule TSM,
                                        MaterializationResponsibility &R);

private:
  GenericLLVMIRPlatformSupport &PS;
  StringRef InitFunctionPrefix;
  StringRef DeInitFunctionPrefix;
};

/// Generic IR Platform Support
///
/// Scrapes llvm.global_ctors and llvm.global_dtors and replaces them with
/// specially named 'init' and 'deinit'. Injects definitions / interposes for
/// some runtime API, including __cxa_atexit, dlopen, and dlclose.
class GenericLLVMIRPlatformSupport : public LLJIT::PlatformSupport {
public:
  GenericLLVMIRPlatformSupport(LLJIT &J, JITDylib &PlatformJD)
      : J(J), InitFunctionPrefix(J.mangle("__orc_init_func.")),
        DeInitFunctionPrefix(J.mangle("__orc_deinit_func.")) {

    getExecutionSession().setPlatform(
        std::make_unique<GenericLLVMIRPlatform>(*this));

    setInitTransform(J, GlobalCtorDtorScraper(*this, InitFunctionPrefix,
                                              DeInitFunctionPrefix));

    SymbolMap StdInterposes;

    StdInterposes[J.mangleAndIntern("__lljit.platform_support_instance")] = {
        ExecutorAddr::fromPtr(this), JITSymbolFlags::Exported};
    StdInterposes[J.mangleAndIntern("__lljit.cxa_atexit_helper")] = {
        ExecutorAddr::fromPtr(registerCxaAtExitHelper), JITSymbolFlags()};

    cantFail(PlatformJD.define(absoluteSymbols(std::move(StdInterposes))));
    cantFail(setupJITDylib(PlatformJD));
    cantFail(J.addIRModule(PlatformJD, createPlatformRuntimeModule()));
  }

  ExecutionSession &getExecutionSession() { return J.getExecutionSession(); }

  /// Adds a module that defines the __dso_handle global.
  Error setupJITDylib(JITDylib &JD) {

    // Add per-jitdylib standard interposes.
    SymbolMap PerJDInterposes;
    PerJDInterposes[J.mangleAndIntern("__lljit.run_atexits_helper")] = {
        ExecutorAddr::fromPtr(runAtExitsHelper), JITSymbolFlags()};
    PerJDInterposes[J.mangleAndIntern("__lljit.atexit_helper")] = {
        ExecutorAddr::fromPtr(registerAtExitHelper), JITSymbolFlags()};
    cantFail(JD.define(absoluteSymbols(std::move(PerJDInterposes))));

    auto Ctx = std::make_unique<LLVMContext>();
    auto M = std::make_unique<Module>("__standard_lib", *Ctx);
    M->setDataLayout(J.getDataLayout());

    auto *Int64Ty = Type::getInt64Ty(*Ctx);
    auto *DSOHandle = new GlobalVariable(
        *M, Int64Ty, true, GlobalValue::ExternalLinkage,
        ConstantInt::get(Int64Ty, reinterpret_cast<uintptr_t>(&JD)),
        "__dso_handle");
    DSOHandle->setVisibility(GlobalValue::DefaultVisibility);
    DSOHandle->setInitializer(
        ConstantInt::get(Int64Ty, ExecutorAddr::fromPtr(&JD).getValue()));

    auto *GenericIRPlatformSupportTy =
        StructType::create(*Ctx, "lljit.GenericLLJITIRPlatformSupport");

    auto *PlatformInstanceDecl = new GlobalVariable(
        *M, GenericIRPlatformSupportTy, true, GlobalValue::ExternalLinkage,
        nullptr, "__lljit.platform_support_instance");

    auto *VoidTy = Type::getVoidTy(*Ctx);
    addHelperAndWrapper(
        *M, "__lljit_run_atexits", FunctionType::get(VoidTy, {}, false),
        GlobalValue::HiddenVisibility, "__lljit.run_atexits_helper",
        {PlatformInstanceDecl, DSOHandle});

    auto *IntTy = Type::getIntNTy(*Ctx, sizeof(int) * CHAR_BIT);
    auto *AtExitCallbackTy = FunctionType::get(VoidTy, {}, false);
    auto *AtExitCallbackPtrTy = PointerType::getUnqual(AtExitCallbackTy);
    addHelperAndWrapper(*M, "atexit",
                        FunctionType::get(IntTy, {AtExitCallbackPtrTy}, false),
                        GlobalValue::HiddenVisibility, "__lljit.atexit_helper",
                        {PlatformInstanceDecl, DSOHandle});

    return J.addIRModule(JD, ThreadSafeModule(std::move(M), std::move(Ctx)));
  }

  Error notifyAdding(ResourceTracker &RT, const MaterializationUnit &MU) {
    auto &JD = RT.getJITDylib();
    if (auto &InitSym = MU.getInitializerSymbol())
      InitSymbols[&JD].add(InitSym, SymbolLookupFlags::WeaklyReferencedSymbol);
    else {
      // If there's no identified init symbol attached, but there is a symbol
      // with the GenericIRPlatform::InitFunctionPrefix, then treat that as
      // an init function. Add the symbol to both the InitSymbols map (which
      // will trigger a lookup to materialize the module) and the InitFunctions
      // map (which holds the names of the symbols to execute).
      for (auto &KV : MU.getSymbols())
        if ((*KV.first).starts_with(InitFunctionPrefix)) {
          InitSymbols[&JD].add(KV.first,
                               SymbolLookupFlags::WeaklyReferencedSymbol);
          InitFunctions[&JD].add(KV.first);
        } else if ((*KV.first).starts_with(DeInitFunctionPrefix)) {
          DeInitFunctions[&JD].add(KV.first);
        }
    }
    return Error::success();
  }

  Error initialize(JITDylib &JD) override {
    LLVM_DEBUG({
      dbgs() << "GenericLLVMIRPlatformSupport getting initializers to run\n";
    });
    if (auto Initializers = getInitializers(JD)) {
      LLVM_DEBUG(
          { dbgs() << "GenericLLVMIRPlatformSupport running initializers\n"; });
      for (auto InitFnAddr : *Initializers) {
        LLVM_DEBUG({
          dbgs() << "  Running init " << formatv("{0:x16}", InitFnAddr)
                 << "...\n";
        });
        auto *InitFn = InitFnAddr.toPtr<void (*)()>();
        InitFn();
      }
    } else
      return Initializers.takeError();
    return Error::success();
  }

  Error deinitialize(JITDylib &JD) override {
    LLVM_DEBUG({
      dbgs() << "GenericLLVMIRPlatformSupport getting deinitializers to run\n";
    });
    if (auto Deinitializers = getDeinitializers(JD)) {
      LLVM_DEBUG({
        dbgs() << "GenericLLVMIRPlatformSupport running deinitializers\n";
      });
      for (auto DeinitFnAddr : *Deinitializers) {
        LLVM_DEBUG({
          dbgs() << "  Running deinit " << formatv("{0:x16}", DeinitFnAddr)
                 << "...\n";
        });
        auto *DeinitFn = DeinitFnAddr.toPtr<void (*)()>();
        DeinitFn();
      }
    } else
      return Deinitializers.takeError();

    return Error::success();
  }

  void registerInitFunc(JITDylib &JD, SymbolStringPtr InitName) {
    getExecutionSession().runSessionLocked([&]() {
        InitFunctions[&JD].add(InitName);
      });
  }

  void registerDeInitFunc(JITDylib &JD, SymbolStringPtr DeInitName) {
    getExecutionSession().runSessionLocked(
        [&]() { DeInitFunctions[&JD].add(DeInitName); });
  }

private:
  Expected<std::vector<ExecutorAddr>> getInitializers(JITDylib &JD) {
    if (auto Err = issueInitLookups(JD))
      return std::move(Err);

    DenseMap<JITDylib *, SymbolLookupSet> LookupSymbols;
    std::vector<JITDylibSP> DFSLinkOrder;

    if (auto Err = getExecutionSession().runSessionLocked([&]() -> Error {
          if (auto DFSLinkOrderOrErr = JD.getDFSLinkOrder())
            DFSLinkOrder = std::move(*DFSLinkOrderOrErr);
          else
            return DFSLinkOrderOrErr.takeError();

          for (auto &NextJD : DFSLinkOrder) {
            auto IFItr = InitFunctions.find(NextJD.get());
            if (IFItr != InitFunctions.end()) {
              LookupSymbols[NextJD.get()] = std::move(IFItr->second);
              InitFunctions.erase(IFItr);
            }
          }
          return Error::success();
        }))
      return std::move(Err);

    LLVM_DEBUG({
      dbgs() << "JITDylib init order is [ ";
      for (auto &JD : llvm::reverse(DFSLinkOrder))
        dbgs() << "\"" << JD->getName() << "\" ";
      dbgs() << "]\n";
      dbgs() << "Looking up init functions:\n";
      for (auto &KV : LookupSymbols)
        dbgs() << "  \"" << KV.first->getName() << "\": " << KV.second << "\n";
    });

    auto &ES = getExecutionSession();
    auto LookupResult = Platform::lookupInitSymbols(ES, LookupSymbols);

    if (!LookupResult)
      return LookupResult.takeError();

    std::vector<ExecutorAddr> Initializers;
    while (!DFSLinkOrder.empty()) {
      auto &NextJD = *DFSLinkOrder.back();
      DFSLinkOrder.pop_back();
      auto InitsItr = LookupResult->find(&NextJD);
      if (InitsItr == LookupResult->end())
        continue;
      for (auto &KV : InitsItr->second)
        Initializers.push_back(KV.second.getAddress());
    }

    return Initializers;
  }

  Expected<std::vector<ExecutorAddr>> getDeinitializers(JITDylib &JD) {
    auto &ES = getExecutionSession();

    auto LLJITRunAtExits = J.mangleAndIntern("__lljit_run_atexits");

    DenseMap<JITDylib *, SymbolLookupSet> LookupSymbols;
    std::vector<JITDylibSP> DFSLinkOrder;

    if (auto Err = ES.runSessionLocked([&]() -> Error {
          if (auto DFSLinkOrderOrErr = JD.getDFSLinkOrder())
            DFSLinkOrder = std::move(*DFSLinkOrderOrErr);
          else
            return DFSLinkOrderOrErr.takeError();

          for (auto &NextJD : DFSLinkOrder) {
            auto &JDLookupSymbols = LookupSymbols[NextJD.get()];
            auto DIFItr = DeInitFunctions.find(NextJD.get());
            if (DIFItr != DeInitFunctions.end()) {
              LookupSymbols[NextJD.get()] = std::move(DIFItr->second);
              DeInitFunctions.erase(DIFItr);
            }
            JDLookupSymbols.add(LLJITRunAtExits,
                                SymbolLookupFlags::WeaklyReferencedSymbol);
          }
          return Error::success();
        }))
      return std::move(Err);

    LLVM_DEBUG({
      dbgs() << "JITDylib deinit order is [ ";
      for (auto &JD : DFSLinkOrder)
        dbgs() << "\"" << JD->getName() << "\" ";
      dbgs() << "]\n";
      dbgs() << "Looking up deinit functions:\n";
      for (auto &KV : LookupSymbols)
        dbgs() << "  \"" << KV.first->getName() << "\": " << KV.second << "\n";
    });

    auto LookupResult = Platform::lookupInitSymbols(ES, LookupSymbols);

    if (!LookupResult)
      return LookupResult.takeError();

    std::vector<ExecutorAddr> DeInitializers;
    for (auto &NextJD : DFSLinkOrder) {
      auto DeInitsItr = LookupResult->find(NextJD.get());
      assert(DeInitsItr != LookupResult->end() &&
             "Every JD should have at least __lljit_run_atexits");

      auto RunAtExitsItr = DeInitsItr->second.find(LLJITRunAtExits);
      if (RunAtExitsItr != DeInitsItr->second.end())
        DeInitializers.push_back(RunAtExitsItr->second.getAddress());

      for (auto &KV : DeInitsItr->second)
        if (KV.first != LLJITRunAtExits)
          DeInitializers.push_back(KV.second.getAddress());
    }

    return DeInitializers;
  }

  /// Issue lookups for all init symbols required to initialize JD (and any
  /// JITDylibs that it depends on).
  Error issueInitLookups(JITDylib &JD) {
    DenseMap<JITDylib *, SymbolLookupSet> RequiredInitSymbols;
    std::vector<JITDylibSP> DFSLinkOrder;

    if (auto Err = getExecutionSession().runSessionLocked([&]() -> Error {
          if (auto DFSLinkOrderOrErr = JD.getDFSLinkOrder())
            DFSLinkOrder = std::move(*DFSLinkOrderOrErr);
          else
            return DFSLinkOrderOrErr.takeError();

          for (auto &NextJD : DFSLinkOrder) {
            auto ISItr = InitSymbols.find(NextJD.get());
            if (ISItr != InitSymbols.end()) {
              RequiredInitSymbols[NextJD.get()] = std::move(ISItr->second);
              InitSymbols.erase(ISItr);
            }
          }
          return Error::success();
        }))
      return Err;

    return Platform::lookupInitSymbols(getExecutionSession(),
                                       RequiredInitSymbols)
        .takeError();
  }

  static void registerCxaAtExitHelper(void *Self, void (*F)(void *), void *Ctx,
                                      void *DSOHandle) {
    LLVM_DEBUG({
      dbgs() << "Registering cxa atexit function " << (void *)F << " for JD "
             << (*static_cast<JITDylib **>(DSOHandle))->getName() << "\n";
    });
    static_cast<GenericLLVMIRPlatformSupport *>(Self)->AtExitMgr.registerAtExit(
        F, Ctx, DSOHandle);
  }

  static void registerAtExitHelper(void *Self, void *DSOHandle, void (*F)()) {
    LLVM_DEBUG({
      dbgs() << "Registering atexit function " << (void *)F << " for JD "
             << (*static_cast<JITDylib **>(DSOHandle))->getName() << "\n";
    });
    static_cast<GenericLLVMIRPlatformSupport *>(Self)->AtExitMgr.registerAtExit(
        reinterpret_cast<void (*)(void *)>(F), nullptr, DSOHandle);
  }

  static void runAtExitsHelper(void *Self, void *DSOHandle) {
    LLVM_DEBUG({
      dbgs() << "Running atexit functions for JD "
             << (*static_cast<JITDylib **>(DSOHandle))->getName() << "\n";
    });
    static_cast<GenericLLVMIRPlatformSupport *>(Self)->AtExitMgr.runAtExits(
        DSOHandle);
  }

  // Constructs an LLVM IR module containing platform runtime globals,
  // functions, and interposes.
  ThreadSafeModule createPlatformRuntimeModule() {
    auto Ctx = std::make_unique<LLVMContext>();
    auto M = std::make_unique<Module>("__standard_lib", *Ctx);
    M->setDataLayout(J.getDataLayout());

    auto *GenericIRPlatformSupportTy =
        StructType::create(*Ctx, "lljit.GenericLLJITIRPlatformSupport");

    auto *PlatformInstanceDecl = new GlobalVariable(
        *M, GenericIRPlatformSupportTy, true, GlobalValue::ExternalLinkage,
        nullptr, "__lljit.platform_support_instance");

    auto *Int8Ty = Type::getInt8Ty(*Ctx);
    auto *IntTy = Type::getIntNTy(*Ctx, sizeof(int) * CHAR_BIT);
    auto *VoidTy = Type::getVoidTy(*Ctx);
    auto *BytePtrTy = PointerType::getUnqual(Int8Ty);
    auto *CxaAtExitCallbackTy = FunctionType::get(VoidTy, {BytePtrTy}, false);
    auto *CxaAtExitCallbackPtrTy = PointerType::getUnqual(CxaAtExitCallbackTy);

    addHelperAndWrapper(
        *M, "__cxa_atexit",
        FunctionType::get(IntTy, {CxaAtExitCallbackPtrTy, BytePtrTy, BytePtrTy},
                          false),
        GlobalValue::DefaultVisibility, "__lljit.cxa_atexit_helper",
        {PlatformInstanceDecl});

    return ThreadSafeModule(std::move(M), std::move(Ctx));
  }

  LLJIT &J;
  std::string InitFunctionPrefix;
  std::string DeInitFunctionPrefix;
  DenseMap<JITDylib *, SymbolLookupSet> InitSymbols;
  DenseMap<JITDylib *, SymbolLookupSet> InitFunctions;
  DenseMap<JITDylib *, SymbolLookupSet> DeInitFunctions;
  ItaniumCXAAtExitSupport AtExitMgr;
};

Error GenericLLVMIRPlatform::setupJITDylib(JITDylib &JD) {
  return S.setupJITDylib(JD);
}

Error GenericLLVMIRPlatform::teardownJITDylib(JITDylib &JD) {
  return Error::success();
}

Error GenericLLVMIRPlatform::notifyAdding(ResourceTracker &RT,
                                          const MaterializationUnit &MU) {
  return S.notifyAdding(RT, MU);
}

Expected<ThreadSafeModule>
GlobalCtorDtorScraper::operator()(ThreadSafeModule TSM,
                                  MaterializationResponsibility &R) {
  auto Err = TSM.withModuleDo([&](Module &M) -> Error {
    auto &Ctx = M.getContext();
    auto *GlobalCtors = M.getNamedGlobal("llvm.global_ctors");
    auto *GlobalDtors = M.getNamedGlobal("llvm.global_dtors");

    auto RegisterCOrDtors = [&](GlobalVariable *GlobalCOrDtors,
                                bool isCtor) -> Error {
      // If there's no llvm.global_c/dtor or it's just a decl then skip.
      if (!GlobalCOrDtors || GlobalCOrDtors->isDeclaration())
        return Error::success();
      std::string InitOrDeInitFunctionName;
      if (isCtor)
        raw_string_ostream(InitOrDeInitFunctionName)
            << InitFunctionPrefix << M.getModuleIdentifier();
      else
        raw_string_ostream(InitOrDeInitFunctionName)
            << DeInitFunctionPrefix << M.getModuleIdentifier();

      MangleAndInterner Mangle(PS.getExecutionSession(), M.getDataLayout());
      auto InternedInitOrDeInitName = Mangle(InitOrDeInitFunctionName);
      if (auto Err = R.defineMaterializing(
              {{InternedInitOrDeInitName, JITSymbolFlags::Callable}}))
        return Err;

      auto *InitOrDeInitFunc = Function::Create(
          FunctionType::get(Type::getVoidTy(Ctx), {}, false),
          GlobalValue::ExternalLinkage, InitOrDeInitFunctionName, &M);
      InitOrDeInitFunc->setVisibility(GlobalValue::HiddenVisibility);
      std::vector<std::pair<Function *, unsigned>> InitsOrDeInits;
      auto COrDtors = isCtor ? getConstructors(M) : getDestructors(M);

      for (auto E : COrDtors)
        InitsOrDeInits.push_back(std::make_pair(E.Func, E.Priority));
      llvm::stable_sort(InitsOrDeInits, llvm::less_second());

      auto *InitOrDeInitFuncEntryBlock =
          BasicBlock::Create(Ctx, "entry", InitOrDeInitFunc);
      IRBuilder<> IB(InitOrDeInitFuncEntryBlock);
      for (auto &KV : InitsOrDeInits)
        IB.CreateCall(KV.first);
      IB.CreateRetVoid();

      if (isCtor)
        PS.registerInitFunc(R.getTargetJITDylib(), InternedInitOrDeInitName);
      else
        PS.registerDeInitFunc(R.getTargetJITDylib(), InternedInitOrDeInitName);

      GlobalCOrDtors->eraseFromParent();
      return Error::success();
    };

    if (auto Err = RegisterCOrDtors(GlobalCtors, true))
      return Err;
    if (auto Err = RegisterCOrDtors(GlobalDtors, false))
      return Err;

    return Error::success();
  });

  if (Err)
    return std::move(Err);

  return std::move(TSM);
}

/// Inactive Platform Support
///
/// Explicitly disables platform support. JITDylibs are not scanned for special
/// init/deinit symbols. No runtime API interposes are injected.
class InactivePlatformSupport : public LLJIT::PlatformSupport {
public:
  InactivePlatformSupport() = default;

  Error initialize(JITDylib &JD) override {
    LLVM_DEBUG(dbgs() << "InactivePlatformSupport: no initializers running for "
                      << JD.getName() << "\n");
    return Error::success();
  }

  Error deinitialize(JITDylib &JD) override {
    LLVM_DEBUG(
        dbgs() << "InactivePlatformSupport: no deinitializers running for "
               << JD.getName() << "\n");
    return Error::success();
  }
};

} // end anonymous namespace

namespace llvm {
namespace orc {

Error ORCPlatformSupport::initialize(orc::JITDylib &JD) {
  using llvm::orc::shared::SPSExecutorAddr;
  using llvm::orc::shared::SPSString;
  using SPSDLOpenSig = SPSExecutorAddr(SPSString, int32_t);
  enum dlopen_mode : int32_t {
    ORC_RT_RTLD_LAZY = 0x1,
    ORC_RT_RTLD_NOW = 0x2,
    ORC_RT_RTLD_LOCAL = 0x4,
    ORC_RT_RTLD_GLOBAL = 0x8
  };

  auto &ES = J.getExecutionSession();
  auto MainSearchOrder = J.getMainJITDylib().withLinkOrderDo(
      [](const JITDylibSearchOrder &SO) { return SO; });

  if (auto WrapperAddr = ES.lookup(
          MainSearchOrder, J.mangleAndIntern("__orc_rt_jit_dlopen_wrapper"))) {
    return ES.callSPSWrapper<SPSDLOpenSig>(WrapperAddr->getAddress(),
                                           DSOHandles[&JD], JD.getName(),
                                           int32_t(ORC_RT_RTLD_LAZY));
  } else
    return WrapperAddr.takeError();
}

Error ORCPlatformSupport::deinitialize(orc::JITDylib &JD) {
  using llvm::orc::shared::SPSExecutorAddr;
  using SPSDLCloseSig = int32_t(SPSExecutorAddr);

  auto &ES = J.getExecutionSession();
  auto MainSearchOrder = J.getMainJITDylib().withLinkOrderDo(
      [](const JITDylibSearchOrder &SO) { return SO; });

  if (auto WrapperAddr = ES.lookup(
          MainSearchOrder, J.mangleAndIntern("__orc_rt_jit_dlclose_wrapper"))) {
    int32_t result;
    auto E = J.getExecutionSession().callSPSWrapper<SPSDLCloseSig>(
        WrapperAddr->getAddress(), result, DSOHandles[&JD]);
    if (E)
      return E;
    else if (result)
      return make_error<StringError>("dlclose failed",
                                     inconvertibleErrorCode());
    DSOHandles.erase(&JD);
  } else
    return WrapperAddr.takeError();
  return Error::success();
}

void LLJIT::PlatformSupport::setInitTransform(
    LLJIT &J, IRTransformLayer::TransformFunction T) {
  J.InitHelperTransformLayer->setTransform(std::move(T));
}

LLJIT::PlatformSupport::~PlatformSupport() = default;

Error LLJITBuilderState::prepareForConstruction() {

  LLVM_DEBUG(dbgs() << "Preparing to create LLJIT instance...\n");

  if (!JTMB) {
    LLVM_DEBUG({
      dbgs() << "  No explicitly set JITTargetMachineBuilder. "
                "Detecting host...\n";
    });
    if (auto JTMBOrErr = JITTargetMachineBuilder::detectHost())
      JTMB = std::move(*JTMBOrErr);
    else
      return JTMBOrErr.takeError();
  }

  if ((ES || EPC) && NumCompileThreads)
    return make_error<StringError>(
        "NumCompileThreads cannot be used with a custom ExecutionSession or "
        "ExecutorProcessControl",
        inconvertibleErrorCode());

#if !LLVM_ENABLE_THREADS
  if (NumCompileThreads)
    return make_error<StringError>(
        "LLJIT num-compile-threads is " + Twine(NumCompileThreads) +
            " but LLVM was compiled with LLVM_ENABLE_THREADS=Off",
        inconvertibleErrorCode());
#endif // !LLVM_ENABLE_THREADS

  // Only used in debug builds.
  [[maybe_unused]] bool ConcurrentCompilationSettingDefaulted =
      !SupportConcurrentCompilation;

  if (!SupportConcurrentCompilation) {
#if LLVM_ENABLE_THREADS
    SupportConcurrentCompilation = NumCompileThreads || ES || EPC;
#else
    SupportConcurrentCompilation = false;
#endif // LLVM_ENABLE_THREADS
  } else {
#if !LLVM_ENABLE_THREADS
    if (*SupportConcurrentCompilation)
      return make_error<StringError>(
          "LLJIT concurrent compilation support requested, but LLVM was built "
          "with LLVM_ENABLE_THREADS=Off",
          inconvertibleErrorCode());
#endif // !LLVM_ENABLE_THREADS
  }

  LLVM_DEBUG({
    dbgs() << "  JITTargetMachineBuilder is "
           << JITTargetMachineBuilderPrinter(*JTMB, "  ")
           << "  Pre-constructed ExecutionSession: " << (ES ? "Yes" : "No")
           << "\n"
           << "  DataLayout: ";
    if (DL)
      dbgs() << DL->getStringRepresentation() << "\n";
    else
      dbgs() << "None (will be created by JITTargetMachineBuilder)\n";

    dbgs() << "  Custom object-linking-layer creator: "
           << (CreateObjectLinkingLayer ? "Yes" : "No") << "\n"
           << "  Custom compile-function creator: "
           << (CreateCompileFunction ? "Yes" : "No") << "\n"
           << "  Custom platform-setup function: "
           << (SetUpPlatform ? "Yes" : "No") << "\n"
           << "  Support concurrent compilation: "
           << (*SupportConcurrentCompilation ? "Yes" : "No");
    if (ConcurrentCompilationSettingDefaulted)
      dbgs() << " (defaulted based on ES / EPC / NumCompileThreads)\n";
    else
      dbgs() << "\n";
    dbgs() << "  Number of compile threads: " << NumCompileThreads << "\n";
  });

  // Create DL if not specified.
  if (!DL) {
    if (auto DLOrErr = JTMB->getDefaultDataLayoutForTarget())
      DL = std::move(*DLOrErr);
    else
      return DLOrErr.takeError();
  }

  // If neither ES nor EPC has been set then create an EPC instance.
  if (!ES && !EPC) {
    LLVM_DEBUG({
      dbgs() << "ExecutorProcessControl not specified, "
                "Creating SelfExecutorProcessControl instance\n";
    });

    std::unique_ptr<TaskDispatcher> D = nullptr;
#if LLVM_ENABLE_THREADS
    if (*SupportConcurrentCompilation) {
      std::optional<size_t> NumThreads = std ::nullopt;
      if (NumCompileThreads)
        NumThreads = NumCompileThreads;
      D = std::make_unique<DynamicThreadPoolTaskDispatcher>(NumThreads);
    } else
      D = std::make_unique<InPlaceTaskDispatcher>();
#endif // LLVM_ENABLE_THREADS
    if (auto EPCOrErr =
            SelfExecutorProcessControl::Create(nullptr, std::move(D), nullptr))
      EPC = std::move(*EPCOrErr);
    else
      return EPCOrErr.takeError();
  } else if (EPC) {
    LLVM_DEBUG({
      dbgs() << "Using explicitly specified ExecutorProcessControl instance "
             << EPC.get() << "\n";
    });
  } else {
    LLVM_DEBUG({
      dbgs() << "Using explicitly specified ExecutionSession instance "
             << ES.get() << "\n";
    });
  }

  // If the client didn't configure any linker options then auto-configure the
  // JIT linker.
  if (!CreateObjectLinkingLayer) {
    auto &TT = JTMB->getTargetTriple();
    bool UseJITLink = false;
    switch (TT.getArch()) {
    case Triple::riscv64:
    case Triple::loongarch64:
      UseJITLink = true;
      break;
    case Triple::aarch64:
      UseJITLink = !TT.isOSBinFormatCOFF();
      break;
    case Triple::arm:
    case Triple::armeb:
    case Triple::thumb:
    case Triple::thumbeb:
      UseJITLink = TT.isOSBinFormatELF();
      break;
    case Triple::x86_64:
      UseJITLink = !TT.isOSBinFormatCOFF();
      break;
    case Triple::ppc64:
      UseJITLink = TT.isPPC64ELFv2ABI();
      break;
    case Triple::ppc64le:
      UseJITLink = TT.isOSBinFormatELF();
      break;
    default:
      break;
    }
    if (UseJITLink) {
      if (!JTMB->getCodeModel())
        JTMB->setCodeModel(CodeModel::Small);
      JTMB->setRelocationModel(Reloc::PIC_);
      CreateObjectLinkingLayer =
          [](ExecutionSession &ES,
             const Triple &) -> Expected<std::unique_ptr<ObjectLayer>> {
        auto ObjLinkingLayer = std::make_unique<ObjectLinkingLayer>(ES);
        if (auto EHFrameRegistrar = EPCEHFrameRegistrar::Create(ES))
          ObjLinkingLayer->addPlugin(
              std::make_unique<EHFrameRegistrationPlugin>(
                  ES, std::move(*EHFrameRegistrar)));
        else
          return EHFrameRegistrar.takeError();
        return std::move(ObjLinkingLayer);
      };
    }
  }

  // If we need a process JITDylib but no setup function has been given then
  // create a default one.
  if (!SetupProcessSymbolsJITDylib && LinkProcessSymbolsByDefault) {
    LLVM_DEBUG(dbgs() << "Creating default Process JD setup function\n");
    SetupProcessSymbolsJITDylib = [](LLJIT &J) -> Expected<JITDylibSP> {
      auto &JD =
          J.getExecutionSession().createBareJITDylib("<Process Symbols>");
      auto G = EPCDynamicLibrarySearchGenerator::GetForTargetProcess(
          J.getExecutionSession());
      if (!G)
        return G.takeError();
      JD.addGenerator(std::move(*G));
      return &JD;
    };
  }

  return Error::success();
}

LLJIT::~LLJIT() {
  if (auto Err = ES->endSession())
    ES->reportError(std::move(Err));
}

JITDylibSP LLJIT::getProcessSymbolsJITDylib() { return ProcessSymbols; }

JITDylibSP LLJIT::getPlatformJITDylib() { return Platform; }

Expected<JITDylib &> LLJIT::createJITDylib(std::string Name) {
  auto JD = ES->createJITDylib(std::move(Name));
  if (!JD)
    return JD.takeError();

  JD->addToLinkOrder(DefaultLinks);
  return JD;
}

Expected<JITDylib &> LLJIT::loadPlatformDynamicLibrary(const char *Path) {
  auto G = EPCDynamicLibrarySearchGenerator::Load(*ES, Path);
  if (!G)
    return G.takeError();

  if (auto *ExistingJD = ES->getJITDylibByName(Path))
    return *ExistingJD;

  auto &JD = ES->createBareJITDylib(Path);
  JD.addGenerator(std::move(*G));
  return JD;
}

Error LLJIT::linkStaticLibraryInto(JITDylib &JD,
                                   std::unique_ptr<MemoryBuffer> LibBuffer) {
  auto G = StaticLibraryDefinitionGenerator::Create(*ObjLinkingLayer,
                                                    std::move(LibBuffer));
  if (!G)
    return G.takeError();

  JD.addGenerator(std::move(*G));

  return Error::success();
}

Error LLJIT::linkStaticLibraryInto(JITDylib &JD, const char *Path) {
  auto G = StaticLibraryDefinitionGenerator::Load(*ObjLinkingLayer, Path);
  if (!G)
    return G.takeError();

  JD.addGenerator(std::move(*G));

  return Error::success();
}

Error LLJIT::addIRModule(ResourceTrackerSP RT, ThreadSafeModule TSM) {
  assert(TSM && "Can not add null module");

  if (auto Err =
          TSM.withModuleDo([&](Module &M) { return applyDataLayout(M); }))
    return Err;

  return InitHelperTransformLayer->add(std::move(RT), std::move(TSM));
}

Error LLJIT::addIRModule(JITDylib &JD, ThreadSafeModule TSM) {
  return addIRModule(JD.getDefaultResourceTracker(), std::move(TSM));
}

Error LLJIT::addObjectFile(ResourceTrackerSP RT,
                           std::unique_ptr<MemoryBuffer> Obj) {
  assert(Obj && "Can not add null object");

  return ObjTransformLayer->add(std::move(RT), std::move(Obj));
}

Error LLJIT::addObjectFile(JITDylib &JD, std::unique_ptr<MemoryBuffer> Obj) {
  return addObjectFile(JD.getDefaultResourceTracker(), std::move(Obj));
}

Expected<ExecutorAddr> LLJIT::lookupLinkerMangled(JITDylib &JD,
                                                  SymbolStringPtr Name) {
  if (auto Sym = ES->lookup(
        makeJITDylibSearchOrder(&JD, JITDylibLookupFlags::MatchAllSymbols),
        Name))
    return Sym->getAddress();
  else
    return Sym.takeError();
}

Expected<std::unique_ptr<ObjectLayer>>
LLJIT::createObjectLinkingLayer(LLJITBuilderState &S, ExecutionSession &ES) {

  // If the config state provided an ObjectLinkingLayer factory then use it.
  if (S.CreateObjectLinkingLayer)
    return S.CreateObjectLinkingLayer(ES, S.JTMB->getTargetTriple());

  // Otherwise default to creating an RTDyldObjectLinkingLayer that constructs
  // a new SectionMemoryManager for each object.
  auto GetMemMgr = []() { return std::make_unique<SectionMemoryManager>(); };
  auto Layer =
      std::make_unique<RTDyldObjectLinkingLayer>(ES, std::move(GetMemMgr));

  if (S.JTMB->getTargetTriple().isOSBinFormatCOFF()) {
    Layer->setOverrideObjectFlagsWithResponsibilityFlags(true);
    Layer->setAutoClaimResponsibilityForObjectSymbols(true);
  }

  if (S.JTMB->getTargetTriple().isOSBinFormatELF() &&
      (S.JTMB->getTargetTriple().getArch() == Triple::ArchType::ppc64 ||
       S.JTMB->getTargetTriple().getArch() == Triple::ArchType::ppc64le))
    Layer->setAutoClaimResponsibilityForObjectSymbols(true);

  // FIXME: Explicit conversion to std::unique_ptr<ObjectLayer> added to silence
  //        errors from some GCC / libstdc++ bots. Remove this conversion (i.e.
  //        just return ObjLinkingLayer) once those bots are upgraded.
  return std::unique_ptr<ObjectLayer>(std::move(Layer));
}

Expected<std::unique_ptr<IRCompileLayer::IRCompiler>>
LLJIT::createCompileFunction(LLJITBuilderState &S,
                             JITTargetMachineBuilder JTMB) {

  /// If there is a custom compile function creator set then use it.
  if (S.CreateCompileFunction)
    return S.CreateCompileFunction(std::move(JTMB));

  // If using a custom EPC then use a ConcurrentIRCompiler by default.
  if (*S.SupportConcurrentCompilation)
    return std::make_unique<ConcurrentIRCompiler>(std::move(JTMB));

  auto TM = JTMB.createTargetMachine();
  if (!TM)
    return TM.takeError();

  return std::make_unique<TMOwningSimpleCompiler>(std::move(*TM));
}

LLJIT::LLJIT(LLJITBuilderState &S, Error &Err)
    : DL(std::move(*S.DL)), TT(S.JTMB->getTargetTriple()) {

  ErrorAsOutParameter _(&Err);

  assert(!(S.EPC && S.ES) && "EPC and ES should not both be set");

  if (S.EPC) {
    ES = std::make_unique<ExecutionSession>(std::move(S.EPC));
  } else if (S.ES)
    ES = std::move(S.ES);
  else {
    if (auto EPC = SelfExecutorProcessControl::Create()) {
      ES = std::make_unique<ExecutionSession>(std::move(*EPC));
    } else {
      Err = EPC.takeError();
      return;
    }
  }

  auto ObjLayer = createObjectLinkingLayer(S, *ES);
  if (!ObjLayer) {
    Err = ObjLayer.takeError();
    return;
  }
  ObjLinkingLayer = std::move(*ObjLayer);
  ObjTransformLayer =
      std::make_unique<ObjectTransformLayer>(*ES, *ObjLinkingLayer);

  {
    auto CompileFunction = createCompileFunction(S, std::move(*S.JTMB));
    if (!CompileFunction) {
      Err = CompileFunction.takeError();
      return;
    }
    CompileLayer = std::make_unique<IRCompileLayer>(
        *ES, *ObjTransformLayer, std::move(*CompileFunction));
    TransformLayer = std::make_unique<IRTransformLayer>(*ES, *CompileLayer);
    InitHelperTransformLayer =
        std::make_unique<IRTransformLayer>(*ES, *TransformLayer);
  }

  if (*S.SupportConcurrentCompilation)
    InitHelperTransformLayer->setCloneToNewContextOnEmit(true);

  if (S.SetupProcessSymbolsJITDylib) {
    if (auto ProcSymsJD = S.SetupProcessSymbolsJITDylib(*this)) {
      ProcessSymbols = ProcSymsJD->get();
    } else {
      Err = ProcSymsJD.takeError();
      return;
    }
  }

  if (S.PrePlatformSetup) {
    if (auto Err2 = S.PrePlatformSetup(*this)) {
      Err = std::move(Err2);
      return;
    }
  }

  if (!S.SetUpPlatform)
    S.SetUpPlatform = setUpGenericLLVMIRPlatform;

  if (auto PlatformJDOrErr = S.SetUpPlatform(*this)) {
    Platform = PlatformJDOrErr->get();
    if (Platform)
      DefaultLinks.push_back(
          {Platform, JITDylibLookupFlags::MatchExportedSymbolsOnly});
  } else {
    Err = PlatformJDOrErr.takeError();
    return;
  }

  if (S.LinkProcessSymbolsByDefault)
    DefaultLinks.push_back(
        {ProcessSymbols, JITDylibLookupFlags::MatchExportedSymbolsOnly});

  if (auto MainOrErr = createJITDylib("main"))
    Main = &*MainOrErr;
  else {
    Err = MainOrErr.takeError();
    return;
  }
}

std::string LLJIT::mangle(StringRef UnmangledName) const {
  std::string MangledName;
  {
    raw_string_ostream MangledNameStream(MangledName);
    Mangler::getNameWithPrefix(MangledNameStream, UnmangledName, DL);
  }
  return MangledName;
}

Error LLJIT::applyDataLayout(Module &M) {
  if (M.getDataLayout().isDefault())
    M.setDataLayout(DL);

  if (M.getDataLayout() != DL)
    return make_error<StringError>(
        "Added modules have incompatible data layouts: " +
            M.getDataLayout().getStringRepresentation() + " (module) vs " +
            DL.getStringRepresentation() + " (jit)",
        inconvertibleErrorCode());

  return Error::success();
}

Error setUpOrcPlatformManually(LLJIT &J) {
  LLVM_DEBUG({ dbgs() << "Setting up orc platform support for LLJIT\n"; });
  J.setPlatformSupport(std::make_unique<ORCPlatformSupport>(J));
  return Error::success();
}

class LoadAndLinkDynLibrary {
public:
  LoadAndLinkDynLibrary(LLJIT &J) : J(J) {}
  Error operator()(JITDylib &JD, StringRef DLLName) {
    if (!DLLName.ends_with_insensitive(".dll"))
      return make_error<StringError>("DLLName not ending with .dll",
                                     inconvertibleErrorCode());
    auto DLLNameStr = DLLName.str(); // Guarantees null-termination.
    auto DLLJD = J.loadPlatformDynamicLibrary(DLLNameStr.c_str());
    if (!DLLJD)
      return DLLJD.takeError();
    JD.addToLinkOrder(*DLLJD);
    return Error::success();
  }

private:
  LLJIT &J;
};

Expected<JITDylibSP> ExecutorNativePlatform::operator()(LLJIT &J) {
  auto ProcessSymbolsJD = J.getProcessSymbolsJITDylib();
  if (!ProcessSymbolsJD)
    return make_error<StringError>(
        "Native platforms require a process symbols JITDylib",
        inconvertibleErrorCode());

  const Triple &TT = J.getTargetTriple();
  ObjectLinkingLayer *ObjLinkingLayer =
      dyn_cast<ObjectLinkingLayer>(&J.getObjLinkingLayer());

  if (!ObjLinkingLayer)
    return make_error<StringError>(
        "ExecutorNativePlatform requires ObjectLinkingLayer",
        inconvertibleErrorCode());

  std::unique_ptr<MemoryBuffer> RuntimeArchiveBuffer;
  if (OrcRuntime.index() == 0) {
    auto A = errorOrToExpected(MemoryBuffer::getFile(std::get<0>(OrcRuntime)));
    if (!A)
      return A.takeError();
    RuntimeArchiveBuffer = std::move(*A);
  } else
    RuntimeArchiveBuffer = std::move(std::get<1>(OrcRuntime));

  auto &ES = J.getExecutionSession();
  auto &PlatformJD = ES.createBareJITDylib("<Platform>");
  PlatformJD.addToLinkOrder(*ProcessSymbolsJD);

  J.setPlatformSupport(std::make_unique<ORCPlatformSupport>(J));

  switch (TT.getObjectFormat()) {
  case Triple::COFF: {
    const char *VCRuntimePath = nullptr;
    bool StaticVCRuntime = false;
    if (VCRuntime) {
      VCRuntimePath = VCRuntime->first.c_str();
      StaticVCRuntime = VCRuntime->second;
    }
    if (auto P = COFFPlatform::Create(
            ES, *ObjLinkingLayer, PlatformJD, std::move(RuntimeArchiveBuffer),
            LoadAndLinkDynLibrary(J), StaticVCRuntime, VCRuntimePath))
      J.getExecutionSession().setPlatform(std::move(*P));
    else
      return P.takeError();
    break;
  }
  case Triple::ELF: {
    auto G = StaticLibraryDefinitionGenerator::Create(
        *ObjLinkingLayer, std::move(RuntimeArchiveBuffer));
    if (!G)
      return G.takeError();

    if (auto P = ELFNixPlatform::Create(ES, *ObjLinkingLayer, PlatformJD,
                                        std::move(*G)))
      J.getExecutionSession().setPlatform(std::move(*P));
    else
      return P.takeError();
    break;
  }
  case Triple::MachO: {
    auto G = StaticLibraryDefinitionGenerator::Create(
        *ObjLinkingLayer, std::move(RuntimeArchiveBuffer));
    if (!G)
      return G.takeError();

    if (auto P = MachOPlatform::Create(ES, *ObjLinkingLayer, PlatformJD,
                                       std::move(*G)))
      ES.setPlatform(std::move(*P));
    else
      return P.takeError();
    break;
  }
  default:
    return make_error<StringError>("Unsupported object format in triple " +
                                       TT.str(),
                                   inconvertibleErrorCode());
  }

  return &PlatformJD;
}

Expected<JITDylibSP> setUpGenericLLVMIRPlatform(LLJIT &J) {
  LLVM_DEBUG(
      { dbgs() << "Setting up GenericLLVMIRPlatform support for LLJIT\n"; });
  auto ProcessSymbolsJD = J.getProcessSymbolsJITDylib();
  if (!ProcessSymbolsJD)
    return make_error<StringError>(
        "Native platforms require a process symbols JITDylib",
        inconvertibleErrorCode());

  auto &PlatformJD = J.getExecutionSession().createBareJITDylib("<Platform>");
  PlatformJD.addToLinkOrder(*ProcessSymbolsJD);

  J.setPlatformSupport(
      std::make_unique<GenericLLVMIRPlatformSupport>(J, PlatformJD));

  return &PlatformJD;
}

Expected<JITDylibSP> setUpInactivePlatform(LLJIT &J) {
  LLVM_DEBUG(
      { dbgs() << "Explicitly deactivated platform support for LLJIT\n"; });
  J.setPlatformSupport(std::make_unique<InactivePlatformSupport>());
  return nullptr;
}

Error LLLazyJITBuilderState::prepareForConstruction() {
  if (auto Err = LLJITBuilderState::prepareForConstruction())
    return Err;
  TT = JTMB->getTargetTriple();
  return Error::success();
}

Error LLLazyJIT::addLazyIRModule(JITDylib &JD, ThreadSafeModule TSM) {
  assert(TSM && "Can not add null module");

  if (auto Err = TSM.withModuleDo(
          [&](Module &M) -> Error { return applyDataLayout(M); }))
    return Err;

  return CODLayer->add(JD, std::move(TSM));
}

LLLazyJIT::LLLazyJIT(LLLazyJITBuilderState &S, Error &Err) : LLJIT(S, Err) {

  // If LLJIT construction failed then bail out.
  if (Err)
    return;

  ErrorAsOutParameter _(&Err);

  /// Take/Create the lazy-compile callthrough manager.
  if (S.LCTMgr)
    LCTMgr = std::move(S.LCTMgr);
  else {
    if (auto LCTMgrOrErr = createLocalLazyCallThroughManager(
            S.TT, *ES, S.LazyCompileFailureAddr))
      LCTMgr = std::move(*LCTMgrOrErr);
    else {
      Err = LCTMgrOrErr.takeError();
      return;
    }
  }

  // Take/Create the indirect stubs manager builder.
  auto ISMBuilder = std::move(S.ISMBuilder);

  // If none was provided, try to build one.
  if (!ISMBuilder)
    ISMBuilder = createLocalIndirectStubsManagerBuilder(S.TT);

  // No luck. Bail out.
  if (!ISMBuilder) {
    Err = make_error<StringError>("Could not construct "
                                  "IndirectStubsManagerBuilder for target " +
                                      S.TT.str(),
                                  inconvertibleErrorCode());
    return;
  }

  // Create the COD layer.
  CODLayer = std::make_unique<CompileOnDemandLayer>(
      *ES, *InitHelperTransformLayer, *LCTMgr, std::move(ISMBuilder));

  if (*S.SupportConcurrentCompilation)
    CODLayer->setCloneToNewContextOnEmit(true);
}

// In-process LLJIT uses eh-frame section wrappers via EPC, so we need to force
// them to be linked in.
LLVM_ATTRIBUTE_USED void linkComponents() {
  errs() << (void *)&llvm_orc_registerEHFrameSectionWrapper
         << (void *)&llvm_orc_deregisterEHFrameSectionWrapper;
}

} // End namespace orc.
} // End namespace llvm.
