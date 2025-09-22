//===--- CGDeclCXX.cpp - Emit LLVM Code for C++ declarations --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with code generation of C++ declarations
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGHLSLRuntime.h"
#include "CGObjCRuntime.h"
#include "CGOpenMPRuntime.h"
#include "CodeGenFunction.h"
#include "TargetInfo.h"
#include "clang/AST/Attr.h"
#include "clang/Basic/LangOptions.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace CodeGen;

static void EmitDeclInit(CodeGenFunction &CGF, const VarDecl &D,
                         ConstantAddress DeclPtr) {
  assert(
      (D.hasGlobalStorage() ||
       (D.hasLocalStorage() && CGF.getContext().getLangOpts().OpenCLCPlusPlus)) &&
      "VarDecl must have global or local (in the case of OpenCL) storage!");
  assert(!D.getType()->isReferenceType() &&
         "Should not call EmitDeclInit on a reference!");

  QualType type = D.getType();
  LValue lv = CGF.MakeAddrLValue(DeclPtr, type);

  const Expr *Init = D.getInit();
  switch (CGF.getEvaluationKind(type)) {
  case TEK_Scalar: {
    CodeGenModule &CGM = CGF.CGM;
    if (lv.isObjCStrong())
      CGM.getObjCRuntime().EmitObjCGlobalAssign(CGF, CGF.EmitScalarExpr(Init),
                                                DeclPtr, D.getTLSKind());
    else if (lv.isObjCWeak())
      CGM.getObjCRuntime().EmitObjCWeakAssign(CGF, CGF.EmitScalarExpr(Init),
                                              DeclPtr);
    else
      CGF.EmitScalarInit(Init, &D, lv, false);
    return;
  }
  case TEK_Complex:
    CGF.EmitComplexExprIntoLValue(Init, lv, /*isInit*/ true);
    return;
  case TEK_Aggregate:
    CGF.EmitAggExpr(Init,
                    AggValueSlot::forLValue(lv, AggValueSlot::IsDestructed,
                                            AggValueSlot::DoesNotNeedGCBarriers,
                                            AggValueSlot::IsNotAliased,
                                            AggValueSlot::DoesNotOverlap));
    return;
  }
  llvm_unreachable("bad evaluation kind");
}

/// Emit code to cause the destruction of the given variable with
/// static storage duration.
static void EmitDeclDestroy(CodeGenFunction &CGF, const VarDecl &D,
                            ConstantAddress Addr) {
  // Honor __attribute__((no_destroy)) and bail instead of attempting
  // to emit a reference to a possibly nonexistent destructor, which
  // in turn can cause a crash. This will result in a global constructor
  // that isn't balanced out by a destructor call as intended by the
  // attribute. This also checks for -fno-c++-static-destructors and
  // bails even if the attribute is not present.
  QualType::DestructionKind DtorKind = D.needsDestruction(CGF.getContext());

  // FIXME:  __attribute__((cleanup)) ?

  switch (DtorKind) {
  case QualType::DK_none:
    return;

  case QualType::DK_cxx_destructor:
    break;

  case QualType::DK_objc_strong_lifetime:
  case QualType::DK_objc_weak_lifetime:
  case QualType::DK_nontrivial_c_struct:
    // We don't care about releasing objects during process teardown.
    assert(!D.getTLSKind() && "should have rejected this");
    return;
  }

  llvm::FunctionCallee Func;
  llvm::Constant *Argument;

  CodeGenModule &CGM = CGF.CGM;
  QualType Type = D.getType();

  // Special-case non-array C++ destructors, if they have the right signature.
  // Under some ABIs, destructors return this instead of void, and cannot be
  // passed directly to __cxa_atexit if the target does not allow this
  // mismatch.
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  bool CanRegisterDestructor =
      Record && (!CGM.getCXXABI().HasThisReturn(
                     GlobalDecl(Record->getDestructor(), Dtor_Complete)) ||
                 CGM.getCXXABI().canCallMismatchedFunctionType());
  // If __cxa_atexit is disabled via a flag, a different helper function is
  // generated elsewhere which uses atexit instead, and it takes the destructor
  // directly.
  bool UsingExternalHelper = !CGM.getCodeGenOpts().CXAAtExit;
  if (Record && (CanRegisterDestructor || UsingExternalHelper)) {
    assert(!Record->hasTrivialDestructor());
    CXXDestructorDecl *Dtor = Record->getDestructor();

    Func = CGM.getAddrAndTypeOfCXXStructor(GlobalDecl(Dtor, Dtor_Complete));
    if (CGF.getContext().getLangOpts().OpenCL) {
      auto DestAS =
          CGM.getTargetCodeGenInfo().getAddrSpaceOfCxaAtexitPtrParam();
      auto DestTy = llvm::PointerType::get(
          CGM.getLLVMContext(), CGM.getContext().getTargetAddressSpace(DestAS));
      auto SrcAS = D.getType().getQualifiers().getAddressSpace();
      if (DestAS == SrcAS)
        Argument = Addr.getPointer();
      else
        // FIXME: On addr space mismatch we are passing NULL. The generation
        // of the global destructor function should be adjusted accordingly.
        Argument = llvm::ConstantPointerNull::get(DestTy);
    } else {
      Argument = Addr.getPointer();
    }
  // Otherwise, the standard logic requires a helper function.
  } else {
    Addr = Addr.withElementType(CGF.ConvertTypeForMem(Type));
    Func = CodeGenFunction(CGM)
           .generateDestroyHelper(Addr, Type, CGF.getDestroyer(DtorKind),
                                  CGF.needsEHCleanup(DtorKind), &D);
    Argument = llvm::Constant::getNullValue(CGF.Int8PtrTy);
  }

  CGM.getCXXABI().registerGlobalDtor(CGF, D, Func, Argument);
}

/// Emit code to cause the variable at the given address to be considered as
/// constant from this point onwards.
static void EmitDeclInvariant(CodeGenFunction &CGF, const VarDecl &D,
                              llvm::Constant *Addr) {
  return CGF.EmitInvariantStart(
      Addr, CGF.getContext().getTypeSizeInChars(D.getType()));
}

void CodeGenFunction::EmitInvariantStart(llvm::Constant *Addr, CharUnits Size) {
  // Do not emit the intrinsic if we're not optimizing.
  if (!CGM.getCodeGenOpts().OptimizationLevel)
    return;

  // Grab the llvm.invariant.start intrinsic.
  llvm::Intrinsic::ID InvStartID = llvm::Intrinsic::invariant_start;
  // Overloaded address space type.
  assert(Addr->getType()->isPointerTy() && "Address must be a pointer");
  llvm::Type *ObjectPtr[1] = {Addr->getType()};
  llvm::Function *InvariantStart = CGM.getIntrinsic(InvStartID, ObjectPtr);

  // Emit a call with the size in bytes of the object.
  uint64_t Width = Size.getQuantity();
  llvm::Value *Args[2] = {llvm::ConstantInt::getSigned(Int64Ty, Width), Addr};
  Builder.CreateCall(InvariantStart, Args);
}

void CodeGenFunction::EmitCXXGlobalVarDeclInit(const VarDecl &D,
                                               llvm::GlobalVariable *GV,
                                               bool PerformInit) {

  const Expr *Init = D.getInit();
  QualType T = D.getType();

  // The address space of a static local variable (DeclPtr) may be different
  // from the address space of the "this" argument of the constructor. In that
  // case, we need an addrspacecast before calling the constructor.
  //
  // struct StructWithCtor {
  //   __device__ StructWithCtor() {...}
  // };
  // __device__ void foo() {
  //   __shared__ StructWithCtor s;
  //   ...
  // }
  //
  // For example, in the above CUDA code, the static local variable s has a
  // "shared" address space qualifier, but the constructor of StructWithCtor
  // expects "this" in the "generic" address space.
  unsigned ExpectedAddrSpace = getTypes().getTargetAddressSpace(T);
  unsigned ActualAddrSpace = GV->getAddressSpace();
  llvm::Constant *DeclPtr = GV;
  if (ActualAddrSpace != ExpectedAddrSpace) {
    llvm::PointerType *PTy =
        llvm::PointerType::get(getLLVMContext(), ExpectedAddrSpace);
    DeclPtr = llvm::ConstantExpr::getAddrSpaceCast(DeclPtr, PTy);
  }

  ConstantAddress DeclAddr(
      DeclPtr, GV->getValueType(), getContext().getDeclAlign(&D));

  if (!T->isReferenceType()) {
    if (getLangOpts().OpenMP && !getLangOpts().OpenMPSimd &&
        D.hasAttr<OMPThreadPrivateDeclAttr>()) {
      (void)CGM.getOpenMPRuntime().emitThreadPrivateVarDefinition(
          &D, DeclAddr, D.getAttr<OMPThreadPrivateDeclAttr>()->getLocation(),
          PerformInit, this);
    }
    bool NeedsDtor =
        D.needsDestruction(getContext()) == QualType::DK_cxx_destructor;
    if (PerformInit)
      EmitDeclInit(*this, D, DeclAddr);
    if (D.getType().isConstantStorage(getContext(), true, !NeedsDtor))
      EmitDeclInvariant(*this, D, DeclPtr);
    else
      EmitDeclDestroy(*this, D, DeclAddr);
    return;
  }

  assert(PerformInit && "cannot have constant initializer which needs "
         "destruction for reference");
  RValue RV = EmitReferenceBindingToExpr(Init);
  EmitStoreOfScalar(RV.getScalarVal(), DeclAddr, false, T);
}

/// Create a stub function, suitable for being passed to atexit,
/// which passes the given address to the given destructor function.
llvm::Constant *CodeGenFunction::createAtExitStub(const VarDecl &VD,
                                                  llvm::FunctionCallee dtor,
                                                  llvm::Constant *addr) {
  // Get the destructor function type, void(*)(void).
  llvm::FunctionType *ty = llvm::FunctionType::get(CGM.VoidTy, false);
  SmallString<256> FnName;
  {
    llvm::raw_svector_ostream Out(FnName);
    CGM.getCXXABI().getMangleContext().mangleDynamicAtExitDestructor(&VD, Out);
  }

  const CGFunctionInfo &FI = CGM.getTypes().arrangeNullaryFunction();
  llvm::Function *fn = CGM.CreateGlobalInitOrCleanUpFunction(
      ty, FnName.str(), FI, VD.getLocation());

  CodeGenFunction CGF(CGM);

  CGF.StartFunction(GlobalDecl(&VD, DynamicInitKind::AtExit),
                    CGM.getContext().VoidTy, fn, FI, FunctionArgList(),
                    VD.getLocation(), VD.getInit()->getExprLoc());
  // Emit an artificial location for this function.
  auto AL = ApplyDebugLocation::CreateArtificial(CGF);

  llvm::CallInst *call = CGF.Builder.CreateCall(dtor, addr);

  // Make sure the call and the callee agree on calling convention.
  if (auto *dtorFn = dyn_cast<llvm::Function>(
          dtor.getCallee()->stripPointerCastsAndAliases()))
    call->setCallingConv(dtorFn->getCallingConv());

  CGF.FinishFunction();

  // Get a proper function pointer.
  FunctionProtoType::ExtProtoInfo EPI(getContext().getDefaultCallingConvention(
      /*IsVariadic=*/false, /*IsCXXMethod=*/false));
  QualType fnType = getContext().getFunctionType(getContext().VoidTy,
                                                 {getContext().VoidPtrTy}, EPI);
  return CGM.getFunctionPointer(fn, fnType);
}

/// Create a stub function, suitable for being passed to __pt_atexit_np,
/// which passes the given address to the given destructor function.
llvm::Function *CodeGenFunction::createTLSAtExitStub(
    const VarDecl &D, llvm::FunctionCallee Dtor, llvm::Constant *Addr,
    llvm::FunctionCallee &AtExit) {
  SmallString<256> FnName;
  {
    llvm::raw_svector_ostream Out(FnName);
    CGM.getCXXABI().getMangleContext().mangleDynamicAtExitDestructor(&D, Out);
  }

  const CGFunctionInfo &FI = CGM.getTypes().arrangeLLVMFunctionInfo(
      getContext().IntTy, FnInfoOpts::None, {getContext().IntTy},
      FunctionType::ExtInfo(), {}, RequiredArgs::All);

  // Get the stub function type, int(*)(int,...).
  llvm::FunctionType *StubTy =
      llvm::FunctionType::get(CGM.IntTy, {CGM.IntTy}, true);

  llvm::Function *DtorStub = CGM.CreateGlobalInitOrCleanUpFunction(
      StubTy, FnName.str(), FI, D.getLocation());

  CodeGenFunction CGF(CGM);

  FunctionArgList Args;
  ImplicitParamDecl IPD(CGM.getContext(), CGM.getContext().IntTy,
                        ImplicitParamKind::Other);
  Args.push_back(&IPD);
  QualType ResTy = CGM.getContext().IntTy;

  CGF.StartFunction(GlobalDecl(&D, DynamicInitKind::AtExit), ResTy, DtorStub,
                    FI, Args, D.getLocation(), D.getInit()->getExprLoc());

  // Emit an artificial location for this function.
  auto AL = ApplyDebugLocation::CreateArtificial(CGF);

  llvm::CallInst *call = CGF.Builder.CreateCall(Dtor, Addr);

  // Make sure the call and the callee agree on calling convention.
  if (auto *DtorFn = dyn_cast<llvm::Function>(
          Dtor.getCallee()->stripPointerCastsAndAliases()))
    call->setCallingConv(DtorFn->getCallingConv());

  // Return 0 from function
  CGF.Builder.CreateStore(llvm::Constant::getNullValue(CGM.IntTy),
                          CGF.ReturnValue);

  CGF.FinishFunction();

  return DtorStub;
}

/// Register a global destructor using the C atexit runtime function.
void CodeGenFunction::registerGlobalDtorWithAtExit(const VarDecl &VD,
                                                   llvm::FunctionCallee dtor,
                                                   llvm::Constant *addr) {
  // Create a function which calls the destructor.
  llvm::Constant *dtorStub = createAtExitStub(VD, dtor, addr);
  registerGlobalDtorWithAtExit(dtorStub);
}

/// Register a global destructor using the LLVM 'llvm.global_dtors' global.
void CodeGenFunction::registerGlobalDtorWithLLVM(const VarDecl &VD,
                                                 llvm::FunctionCallee Dtor,
                                                 llvm::Constant *Addr) {
  // Create a function which calls the destructor.
  llvm::Function *dtorStub =
      cast<llvm::Function>(createAtExitStub(VD, Dtor, Addr));
  CGM.AddGlobalDtor(dtorStub);
}

void CodeGenFunction::registerGlobalDtorWithAtExit(llvm::Constant *dtorStub) {
  // extern "C" int atexit(void (*f)(void));
  assert(dtorStub->getType() ==
             llvm::PointerType::get(
                 llvm::FunctionType::get(CGM.VoidTy, false),
                 dtorStub->getType()->getPointerAddressSpace()) &&
         "Argument to atexit has a wrong type.");

  llvm::FunctionType *atexitTy =
      llvm::FunctionType::get(IntTy, dtorStub->getType(), false);

  llvm::FunctionCallee atexit =
      CGM.CreateRuntimeFunction(atexitTy, "atexit", llvm::AttributeList(),
                                /*Local=*/true);
  if (llvm::Function *atexitFn = dyn_cast<llvm::Function>(atexit.getCallee()))
    atexitFn->setDoesNotThrow();

  EmitNounwindRuntimeCall(atexit, dtorStub);
}

llvm::Value *
CodeGenFunction::unregisterGlobalDtorWithUnAtExit(llvm::Constant *dtorStub) {
  // The unatexit subroutine unregisters __dtor functions that were previously
  // registered by the atexit subroutine. If the referenced function is found,
  // it is removed from the list of functions that are called at normal program
  // termination and the unatexit returns a value of 0, otherwise a non-zero
  // value is returned.
  //
  // extern "C" int unatexit(void (*f)(void));
  assert(dtorStub->getType() ==
             llvm::PointerType::get(
                 llvm::FunctionType::get(CGM.VoidTy, false),
                 dtorStub->getType()->getPointerAddressSpace()) &&
         "Argument to unatexit has a wrong type.");

  llvm::FunctionType *unatexitTy =
      llvm::FunctionType::get(IntTy, {dtorStub->getType()}, /*isVarArg=*/false);

  llvm::FunctionCallee unatexit =
      CGM.CreateRuntimeFunction(unatexitTy, "unatexit", llvm::AttributeList());

  cast<llvm::Function>(unatexit.getCallee())->setDoesNotThrow();

  return EmitNounwindRuntimeCall(unatexit, dtorStub);
}

void CodeGenFunction::EmitCXXGuardedInit(const VarDecl &D,
                                         llvm::GlobalVariable *DeclPtr,
                                         bool PerformInit) {
  // If we've been asked to forbid guard variables, emit an error now.
  // This diagnostic is hard-coded for Darwin's use case;  we can find
  // better phrasing if someone else needs it.
  if (CGM.getCodeGenOpts().ForbidGuardVariables)
    CGM.Error(D.getLocation(),
              "this initialization requires a guard variable, which "
              "the kernel does not support");

  CGM.getCXXABI().EmitGuardedInit(*this, D, DeclPtr, PerformInit);
}

void CodeGenFunction::EmitCXXGuardedInitBranch(llvm::Value *NeedsInit,
                                               llvm::BasicBlock *InitBlock,
                                               llvm::BasicBlock *NoInitBlock,
                                               GuardKind Kind,
                                               const VarDecl *D) {
  assert((Kind == GuardKind::TlsGuard || D) && "no guarded variable");

  // A guess at how many times we will enter the initialization of a
  // variable, depending on the kind of variable.
  static const uint64_t InitsPerTLSVar = 1024;
  static const uint64_t InitsPerLocalVar = 1024 * 1024;

  llvm::MDNode *Weights;
  if (Kind == GuardKind::VariableGuard && !D->isLocalVarDecl()) {
    // For non-local variables, don't apply any weighting for now. Due to our
    // use of COMDATs, we expect there to be at most one initialization of the
    // variable per DSO, but we have no way to know how many DSOs will try to
    // initialize the variable.
    Weights = nullptr;
  } else {
    uint64_t NumInits;
    // FIXME: For the TLS case, collect and use profiling information to
    // determine a more accurate brach weight.
    if (Kind == GuardKind::TlsGuard || D->getTLSKind())
      NumInits = InitsPerTLSVar;
    else
      NumInits = InitsPerLocalVar;

    // The probability of us entering the initializer is
    //   1 / (total number of times we attempt to initialize the variable).
    llvm::MDBuilder MDHelper(CGM.getLLVMContext());
    Weights = MDHelper.createBranchWeights(1, NumInits - 1);
  }

  Builder.CreateCondBr(NeedsInit, InitBlock, NoInitBlock, Weights);
}

llvm::Function *CodeGenModule::CreateGlobalInitOrCleanUpFunction(
    llvm::FunctionType *FTy, const Twine &Name, const CGFunctionInfo &FI,
    SourceLocation Loc, bool TLS, llvm::GlobalVariable::LinkageTypes Linkage) {
  llvm::Function *Fn = llvm::Function::Create(FTy, Linkage, Name, &getModule());

  if (!getLangOpts().AppleKext && !TLS) {
    // Set the section if needed.
    if (const char *Section = getTarget().getStaticInitSectionSpecifier())
      Fn->setSection(Section);
  }

  if (Linkage == llvm::GlobalVariable::InternalLinkage)
    SetInternalFunctionAttributes(GlobalDecl(), Fn, FI);

  Fn->setCallingConv(getRuntimeCC());

  if (!getLangOpts().Exceptions)
    Fn->setDoesNotThrow();

  if (getLangOpts().Sanitize.has(SanitizerKind::Address) &&
      !isInNoSanitizeList(SanitizerKind::Address, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeAddress);

  if (getLangOpts().Sanitize.has(SanitizerKind::KernelAddress) &&
      !isInNoSanitizeList(SanitizerKind::KernelAddress, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeAddress);

  if (getLangOpts().Sanitize.has(SanitizerKind::HWAddress) &&
      !isInNoSanitizeList(SanitizerKind::HWAddress, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeHWAddress);

  if (getLangOpts().Sanitize.has(SanitizerKind::KernelHWAddress) &&
      !isInNoSanitizeList(SanitizerKind::KernelHWAddress, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeHWAddress);

  if (getLangOpts().Sanitize.has(SanitizerKind::MemtagStack) &&
      !isInNoSanitizeList(SanitizerKind::MemtagStack, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeMemTag);

  if (getLangOpts().Sanitize.has(SanitizerKind::Thread) &&
      !isInNoSanitizeList(SanitizerKind::Thread, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeThread);

  if (getLangOpts().Sanitize.has(SanitizerKind::NumericalStability) &&
      !isInNoSanitizeList(SanitizerKind::NumericalStability, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeNumericalStability);

  if (getLangOpts().Sanitize.has(SanitizerKind::Memory) &&
      !isInNoSanitizeList(SanitizerKind::Memory, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeMemory);

  if (getLangOpts().Sanitize.has(SanitizerKind::KernelMemory) &&
      !isInNoSanitizeList(SanitizerKind::KernelMemory, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SanitizeMemory);

  if (getLangOpts().Sanitize.has(SanitizerKind::SafeStack) &&
      !isInNoSanitizeList(SanitizerKind::SafeStack, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::SafeStack);

  if (getLangOpts().Sanitize.has(SanitizerKind::ShadowCallStack) &&
      !isInNoSanitizeList(SanitizerKind::ShadowCallStack, Fn, Loc))
    Fn->addFnAttr(llvm::Attribute::ShadowCallStack);

  return Fn;
}

/// Create a global pointer to a function that will initialize a global
/// variable.  The user has requested that this pointer be emitted in a specific
/// section.
void CodeGenModule::EmitPointerToInitFunc(const VarDecl *D,
                                          llvm::GlobalVariable *GV,
                                          llvm::Function *InitFunc,
                                          InitSegAttr *ISA) {
  llvm::GlobalVariable *PtrArray = new llvm::GlobalVariable(
      TheModule, InitFunc->getType(), /*isConstant=*/true,
      llvm::GlobalValue::PrivateLinkage, InitFunc, "__cxx_init_fn_ptr");
  PtrArray->setSection(ISA->getSection());
  addUsedGlobal(PtrArray);

  // If the GV is already in a comdat group, then we have to join it.
  if (llvm::Comdat *C = GV->getComdat())
    PtrArray->setComdat(C);
}

void
CodeGenModule::EmitCXXGlobalVarDeclInitFunc(const VarDecl *D,
                                            llvm::GlobalVariable *Addr,
                                            bool PerformInit) {

  // According to E.2.3.1 in CUDA-7.5 Programming guide: __device__,
  // __constant__ and __shared__ variables defined in namespace scope,
  // that are of class type, cannot have a non-empty constructor. All
  // the checks have been done in Sema by now. Whatever initializers
  // are allowed are empty and we just need to ignore them here.
  if (getLangOpts().CUDAIsDevice && !getLangOpts().GPUAllowDeviceInit &&
      (D->hasAttr<CUDADeviceAttr>() || D->hasAttr<CUDAConstantAttr>() ||
       D->hasAttr<CUDASharedAttr>()))
    return;

  // Check if we've already initialized this decl.
  auto I = DelayedCXXInitPosition.find(D);
  if (I != DelayedCXXInitPosition.end() && I->second == ~0U)
    return;

  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
  SmallString<256> FnName;
  {
    llvm::raw_svector_ostream Out(FnName);
    getCXXABI().getMangleContext().mangleDynamicInitializer(D, Out);
  }

  // Create a variable initialization function.
  llvm::Function *Fn = CreateGlobalInitOrCleanUpFunction(
      FTy, FnName.str(), getTypes().arrangeNullaryFunction(), D->getLocation());

  auto *ISA = D->getAttr<InitSegAttr>();
  CodeGenFunction(*this).GenerateCXXGlobalVarDeclInitFunc(Fn, D, Addr,
                                                          PerformInit);

  llvm::GlobalVariable *COMDATKey =
      supportsCOMDAT() && D->isExternallyVisible() ? Addr : nullptr;

  if (D->getTLSKind()) {
    // FIXME: Should we support init_priority for thread_local?
    // FIXME: We only need to register one __cxa_thread_atexit function for the
    // entire TU.
    CXXThreadLocalInits.push_back(Fn);
    CXXThreadLocalInitVars.push_back(D);
  } else if (PerformInit && ISA) {
    // Contract with backend that "init_seg(compiler)" corresponds to priority
    // 200 and "init_seg(lib)" corresponds to priority 400.
    int Priority = -1;
    if (ISA->getSection() == ".CRT$XCC")
      Priority = 200;
    else if (ISA->getSection() == ".CRT$XCL")
      Priority = 400;

    if (Priority != -1)
      AddGlobalCtor(Fn, Priority, ~0U, COMDATKey);
    else
      EmitPointerToInitFunc(D, Addr, Fn, ISA);
  } else if (auto *IPA = D->getAttr<InitPriorityAttr>()) {
    OrderGlobalInitsOrStermFinalizers Key(IPA->getPriority(),
                                          PrioritizedCXXGlobalInits.size());
    PrioritizedCXXGlobalInits.push_back(std::make_pair(Key, Fn));
  } else if (isTemplateInstantiation(D->getTemplateSpecializationKind()) ||
             getContext().GetGVALinkageForVariable(D) == GVA_DiscardableODR ||
             D->hasAttr<SelectAnyAttr>()) {
    // C++ [basic.start.init]p2:
    //   Definitions of explicitly specialized class template static data
    //   members have ordered initialization. Other class template static data
    //   members (i.e., implicitly or explicitly instantiated specializations)
    //   have unordered initialization.
    //
    // As a consequence, we can put them into their own llvm.global_ctors entry.
    //
    // If the global is externally visible, put the initializer into a COMDAT
    // group with the global being initialized.  On most platforms, this is a
    // minor startup time optimization.  In the MS C++ ABI, there are no guard
    // variables, so this COMDAT key is required for correctness.
    //
    // SelectAny globals will be comdat-folded. Put the initializer into a
    // COMDAT group associated with the global, so the initializers get folded
    // too.
    I = DelayedCXXInitPosition.find(D);
    // CXXGlobalInits.size() is the lex order number for the next deferred
    // VarDecl. Use it when the current VarDecl is non-deferred. Although this
    // lex order number is shared between current VarDecl and some following
    // VarDecls, their order of insertion into `llvm.global_ctors` is the same
    // as the lexing order and the following stable sort would preserve such
    // order.
    unsigned LexOrder =
        I == DelayedCXXInitPosition.end() ? CXXGlobalInits.size() : I->second;
    AddGlobalCtor(Fn, 65535, LexOrder, COMDATKey);
    if (COMDATKey && (getTriple().isOSBinFormatELF() ||
                      getTarget().getCXXABI().isMicrosoft())) {
      // When COMDAT is used on ELF or in the MS C++ ABI, the key must be in
      // llvm.used to prevent linker GC.
      addUsedGlobal(COMDATKey);
    }

    // If we used a COMDAT key for the global ctor, the init function can be
    // discarded if the global ctor entry is discarded.
    // FIXME: Do we need to restrict this to ELF and Wasm?
    llvm::Comdat *C = Addr->getComdat();
    if (COMDATKey && C &&
        (getTarget().getTriple().isOSBinFormatELF() ||
         getTarget().getTriple().isOSBinFormatWasm())) {
      Fn->setComdat(C);
    }
  } else {
    I = DelayedCXXInitPosition.find(D); // Re-do lookup in case of re-hash.
    if (I == DelayedCXXInitPosition.end()) {
      CXXGlobalInits.push_back(Fn);
    } else if (I->second != ~0U) {
      assert(I->second < CXXGlobalInits.size() &&
             CXXGlobalInits[I->second] == nullptr);
      CXXGlobalInits[I->second] = Fn;
    }
  }

  // Remember that we already emitted the initializer for this global.
  DelayedCXXInitPosition[D] = ~0U;
}

void CodeGenModule::EmitCXXThreadLocalInitFunc() {
  getCXXABI().EmitThreadLocalInitFuncs(
      *this, CXXThreadLocals, CXXThreadLocalInits, CXXThreadLocalInitVars);

  CXXThreadLocalInits.clear();
  CXXThreadLocalInitVars.clear();
  CXXThreadLocals.clear();
}

/* Build the initializer for a C++20 module:
   This is arranged to be run only once regardless of how many times the module
   might be included transitively.  This arranged by using a guard variable.

   If there are no initializers at all (and also no imported modules) we reduce
   this to an empty function (since the Itanium ABI requires that this function
   be available to a caller, which might be produced by a different
   implementation).

   First we call any initializers for imported modules.
   We then call initializers for the Global Module Fragment (if present)
   We then call initializers for the current module.
   We then call initializers for the Private Module Fragment (if present)
*/

void CodeGenModule::EmitCXXModuleInitFunc(Module *Primary) {
  assert(Primary->isInterfaceOrPartition() &&
         "The function should only be called for C++20 named module interface"
         " or partition.");

  while (!CXXGlobalInits.empty() && !CXXGlobalInits.back())
    CXXGlobalInits.pop_back();

  // As noted above, we create the function, even if it is empty.
  // Module initializers for imported modules are emitted first.

  // Collect all the modules that we import
  llvm::SmallSetVector<Module *, 8> AllImports;
  // Ones that we export
  for (auto I : Primary->Exports)
    AllImports.insert(I.getPointer());
  // Ones that we only import.
  for (Module *M : Primary->Imports)
    AllImports.insert(M);
  // Ones that we import in the global module fragment or the private module
  // fragment.
  for (Module *SubM : Primary->submodules()) {
    assert((SubM->isGlobalModule() || SubM->isPrivateModule()) &&
           "The sub modules of C++20 module unit should only be global module "
           "fragments or private module framents.");
    assert(SubM->Exports.empty() &&
           "The global mdoule fragments and the private module fragments are "
           "not allowed to export import modules.");
    for (Module *M : SubM->Imports)
      AllImports.insert(M);
  }

  SmallVector<llvm::Function *, 8> ModuleInits;
  for (Module *M : AllImports) {
    // No Itanium initializer in header like modules.
    if (M->isHeaderLikeModule())
      continue; // TODO: warn of mixed use of module map modules and C++20?
    // We're allowed to skip the initialization if we are sure it doesn't
    // do any thing.
    if (!M->isNamedModuleInterfaceHasInit())
      continue;
    llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
    SmallString<256> FnName;
    {
      llvm::raw_svector_ostream Out(FnName);
      cast<ItaniumMangleContext>(getCXXABI().getMangleContext())
          .mangleModuleInitializer(M, Out);
    }
    assert(!GetGlobalValue(FnName.str()) &&
           "We should only have one use of the initializer call");
    llvm::Function *Fn = llvm::Function::Create(
        FTy, llvm::Function::ExternalLinkage, FnName.str(), &getModule());
    ModuleInits.push_back(Fn);
  }

  // Add any initializers with specified priority; this uses the same  approach
  // as EmitCXXGlobalInitFunc().
  if (!PrioritizedCXXGlobalInits.empty()) {
    SmallVector<llvm::Function *, 8> LocalCXXGlobalInits;
    llvm::array_pod_sort(PrioritizedCXXGlobalInits.begin(),
                         PrioritizedCXXGlobalInits.end());
    for (SmallVectorImpl<GlobalInitData>::iterator
             I = PrioritizedCXXGlobalInits.begin(),
             E = PrioritizedCXXGlobalInits.end();
         I != E;) {
      SmallVectorImpl<GlobalInitData>::iterator PrioE =
          std::upper_bound(I + 1, E, *I, GlobalInitPriorityCmp());

      for (; I < PrioE; ++I)
        ModuleInits.push_back(I->second);
    }
  }

  // Now append the ones without specified priority.
  for (auto *F : CXXGlobalInits)
    ModuleInits.push_back(F);

  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
  const CGFunctionInfo &FI = getTypes().arrangeNullaryFunction();

  // We now build the initializer for this module, which has a mangled name
  // as per the Itanium ABI .  The action of the initializer is guarded so that
  // each init is run just once (even though a module might be imported
  // multiple times via nested use).
  llvm::Function *Fn;
  {
    SmallString<256> InitFnName;
    llvm::raw_svector_ostream Out(InitFnName);
    cast<ItaniumMangleContext>(getCXXABI().getMangleContext())
        .mangleModuleInitializer(Primary, Out);
    Fn = CreateGlobalInitOrCleanUpFunction(
        FTy, llvm::Twine(InitFnName), FI, SourceLocation(), false,
        llvm::GlobalVariable::ExternalLinkage);

    // If we have a completely empty initializer then we do not want to create
    // the guard variable.
    ConstantAddress GuardAddr = ConstantAddress::invalid();
    if (!ModuleInits.empty()) {
      // Create the guard var.
      llvm::GlobalVariable *Guard = new llvm::GlobalVariable(
          getModule(), Int8Ty, /*isConstant=*/false,
          llvm::GlobalVariable::InternalLinkage,
          llvm::ConstantInt::get(Int8Ty, 0), InitFnName.str() + "__in_chrg");
      CharUnits GuardAlign = CharUnits::One();
      Guard->setAlignment(GuardAlign.getAsAlign());
      GuardAddr = ConstantAddress(Guard, Int8Ty, GuardAlign);
    }
    CodeGenFunction(*this).GenerateCXXGlobalInitFunc(Fn, ModuleInits,
                                                     GuardAddr);
  }

  // We allow for the case that a module object is added to a linked binary
  // without a specific call to the the initializer.  This also ensures that
  // implementation partition initializers are called when the partition
  // is not imported as an interface.
  AddGlobalCtor(Fn);

  // See the comment in EmitCXXGlobalInitFunc about OpenCL global init
  // functions.
  if (getLangOpts().OpenCL) {
    GenKernelArgMetadata(Fn);
    Fn->setCallingConv(llvm::CallingConv::SPIR_KERNEL);
  }

  assert(!getLangOpts().CUDA || !getLangOpts().CUDAIsDevice ||
         getLangOpts().GPUAllowDeviceInit);
  if (getLangOpts().HIP && getLangOpts().CUDAIsDevice) {
    Fn->setCallingConv(llvm::CallingConv::AMDGPU_KERNEL);
    Fn->addFnAttr("device-init");
  }

  // We are done with the inits.
  AllImports.clear();
  PrioritizedCXXGlobalInits.clear();
  CXXGlobalInits.clear();
  ModuleInits.clear();
}

static SmallString<128> getTransformedFileName(llvm::Module &M) {
  SmallString<128> FileName = llvm::sys::path::filename(M.getName());

  if (FileName.empty())
    FileName = "<null>";

  for (size_t i = 0; i < FileName.size(); ++i) {
    // Replace everything that's not [a-zA-Z0-9._] with a _. This set happens
    // to be the set of C preprocessing numbers.
    if (!isPreprocessingNumberBody(FileName[i]))
      FileName[i] = '_';
  }

  return FileName;
}

static std::string getPrioritySuffix(unsigned int Priority) {
  assert(Priority <= 65535 && "Priority should always be <= 65535.");

  // Compute the function suffix from priority. Prepend with zeroes to make
  // sure the function names are also ordered as priorities.
  std::string PrioritySuffix = llvm::utostr(Priority);
  PrioritySuffix = std::string(6 - PrioritySuffix.size(), '0') + PrioritySuffix;

  return PrioritySuffix;
}

void
CodeGenModule::EmitCXXGlobalInitFunc() {
  while (!CXXGlobalInits.empty() && !CXXGlobalInits.back())
    CXXGlobalInits.pop_back();

  // When we import C++20 modules, we must run their initializers first.
  SmallVector<llvm::Function *, 8> ModuleInits;
  if (CXX20ModuleInits)
    for (Module *M : ImportedModules) {
      // No Itanium initializer in header like modules.
      if (M->isHeaderLikeModule())
        continue;
      // We're allowed to skip the initialization if we are sure it doesn't
      // do any thing.
      if (!M->isNamedModuleInterfaceHasInit())
        continue;
      llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
      SmallString<256> FnName;
      {
        llvm::raw_svector_ostream Out(FnName);
        cast<ItaniumMangleContext>(getCXXABI().getMangleContext())
            .mangleModuleInitializer(M, Out);
      }
      assert(!GetGlobalValue(FnName.str()) &&
             "We should only have one use of the initializer call");
      llvm::Function *Fn = llvm::Function::Create(
          FTy, llvm::Function::ExternalLinkage, FnName.str(), &getModule());
      ModuleInits.push_back(Fn);
    }

  if (ModuleInits.empty() && CXXGlobalInits.empty() &&
      PrioritizedCXXGlobalInits.empty())
    return;

  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
  const CGFunctionInfo &FI = getTypes().arrangeNullaryFunction();

  // Create our global prioritized initialization function.
  if (!PrioritizedCXXGlobalInits.empty()) {
    SmallVector<llvm::Function *, 8> LocalCXXGlobalInits;
    llvm::array_pod_sort(PrioritizedCXXGlobalInits.begin(),
                         PrioritizedCXXGlobalInits.end());
    // Iterate over "chunks" of ctors with same priority and emit each chunk
    // into separate function. Note - everything is sorted first by priority,
    // second - by lex order, so we emit ctor functions in proper order.
    for (SmallVectorImpl<GlobalInitData >::iterator
           I = PrioritizedCXXGlobalInits.begin(),
           E = PrioritizedCXXGlobalInits.end(); I != E; ) {
      SmallVectorImpl<GlobalInitData >::iterator
        PrioE = std::upper_bound(I + 1, E, *I, GlobalInitPriorityCmp());

      LocalCXXGlobalInits.clear();

      unsigned int Priority = I->first.priority;
      llvm::Function *Fn = CreateGlobalInitOrCleanUpFunction(
          FTy, "_GLOBAL__I_" + getPrioritySuffix(Priority), FI);

      // Prepend the module inits to the highest priority set.
      if (!ModuleInits.empty()) {
        for (auto *F : ModuleInits)
          LocalCXXGlobalInits.push_back(F);
        ModuleInits.clear();
      }

      for (; I < PrioE; ++I)
        LocalCXXGlobalInits.push_back(I->second);

      CodeGenFunction(*this).GenerateCXXGlobalInitFunc(Fn, LocalCXXGlobalInits);
      AddGlobalCtor(Fn, Priority);
    }
    PrioritizedCXXGlobalInits.clear();
  }

  if (getCXXABI().useSinitAndSterm() && ModuleInits.empty() &&
      CXXGlobalInits.empty())
    return;

  for (auto *F : CXXGlobalInits)
    ModuleInits.push_back(F);
  CXXGlobalInits.clear();

  // Include the filename in the symbol name. Including "sub_" matches gcc
  // and makes sure these symbols appear lexicographically behind the symbols
  // with priority emitted above.  Module implementation units behave the same
  // way as a non-modular TU with imports.
  llvm::Function *Fn;
  if (CXX20ModuleInits && getContext().getCurrentNamedModule() &&
      !getContext().getCurrentNamedModule()->isModuleImplementation()) {
    SmallString<256> InitFnName;
    llvm::raw_svector_ostream Out(InitFnName);
    cast<ItaniumMangleContext>(getCXXABI().getMangleContext())
        .mangleModuleInitializer(getContext().getCurrentNamedModule(), Out);
    Fn = CreateGlobalInitOrCleanUpFunction(
        FTy, llvm::Twine(InitFnName), FI, SourceLocation(), false,
        llvm::GlobalVariable::ExternalLinkage);
  } else
    Fn = CreateGlobalInitOrCleanUpFunction(
        FTy,
        llvm::Twine("_GLOBAL__sub_I_", getTransformedFileName(getModule())),
        FI);

  CodeGenFunction(*this).GenerateCXXGlobalInitFunc(Fn, ModuleInits);
  AddGlobalCtor(Fn);

  // In OpenCL global init functions must be converted to kernels in order to
  // be able to launch them from the host.
  // FIXME: Some more work might be needed to handle destructors correctly.
  // Current initialization function makes use of function pointers callbacks.
  // We can't support function pointers especially between host and device.
  // However it seems global destruction has little meaning without any
  // dynamic resource allocation on the device and program scope variables are
  // destroyed by the runtime when program is released.
  if (getLangOpts().OpenCL) {
    GenKernelArgMetadata(Fn);
    Fn->setCallingConv(llvm::CallingConv::SPIR_KERNEL);
  }

  assert(!getLangOpts().CUDA || !getLangOpts().CUDAIsDevice ||
         getLangOpts().GPUAllowDeviceInit);
  if (getLangOpts().HIP && getLangOpts().CUDAIsDevice) {
    Fn->setCallingConv(llvm::CallingConv::AMDGPU_KERNEL);
    Fn->addFnAttr("device-init");
  }

  ModuleInits.clear();
}

void CodeGenModule::EmitCXXGlobalCleanUpFunc() {
  if (CXXGlobalDtorsOrStermFinalizers.empty() &&
      PrioritizedCXXStermFinalizers.empty())
    return;

  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
  const CGFunctionInfo &FI = getTypes().arrangeNullaryFunction();

  // Create our global prioritized cleanup function.
  if (!PrioritizedCXXStermFinalizers.empty()) {
    SmallVector<CXXGlobalDtorsOrStermFinalizer_t, 8> LocalCXXStermFinalizers;
    llvm::array_pod_sort(PrioritizedCXXStermFinalizers.begin(),
                         PrioritizedCXXStermFinalizers.end());
    // Iterate over "chunks" of dtors with same priority and emit each chunk
    // into separate function. Note - everything is sorted first by priority,
    // second - by lex order, so we emit dtor functions in proper order.
    for (SmallVectorImpl<StermFinalizerData>::iterator
             I = PrioritizedCXXStermFinalizers.begin(),
             E = PrioritizedCXXStermFinalizers.end();
         I != E;) {
      SmallVectorImpl<StermFinalizerData>::iterator PrioE =
          std::upper_bound(I + 1, E, *I, StermFinalizerPriorityCmp());

      LocalCXXStermFinalizers.clear();

      unsigned int Priority = I->first.priority;
      llvm::Function *Fn = CreateGlobalInitOrCleanUpFunction(
          FTy, "_GLOBAL__a_" + getPrioritySuffix(Priority), FI);

      for (; I < PrioE; ++I) {
        llvm::FunctionCallee DtorFn = I->second;
        LocalCXXStermFinalizers.emplace_back(DtorFn.getFunctionType(),
                                             DtorFn.getCallee(), nullptr);
      }

      CodeGenFunction(*this).GenerateCXXGlobalCleanUpFunc(
          Fn, LocalCXXStermFinalizers);
      AddGlobalDtor(Fn, Priority);
    }
    PrioritizedCXXStermFinalizers.clear();
  }

  if (CXXGlobalDtorsOrStermFinalizers.empty())
    return;

  // Create our global cleanup function.
  llvm::Function *Fn =
      CreateGlobalInitOrCleanUpFunction(FTy, "_GLOBAL__D_a", FI);

  CodeGenFunction(*this).GenerateCXXGlobalCleanUpFunc(
      Fn, CXXGlobalDtorsOrStermFinalizers);
  AddGlobalDtor(Fn);
  CXXGlobalDtorsOrStermFinalizers.clear();
}

/// Emit the code necessary to initialize the given global variable.
void CodeGenFunction::GenerateCXXGlobalVarDeclInitFunc(llvm::Function *Fn,
                                                       const VarDecl *D,
                                                 llvm::GlobalVariable *Addr,
                                                       bool PerformInit) {
  // Check if we need to emit debug info for variable initializer.
  if (D->hasAttr<NoDebugAttr>())
    DebugInfo = nullptr; // disable debug info indefinitely for this function

  CurEHLocation = D->getBeginLoc();

  StartFunction(GlobalDecl(D, DynamicInitKind::Initializer),
                getContext().VoidTy, Fn, getTypes().arrangeNullaryFunction(),
                FunctionArgList());
  // Emit an artificial location for this function.
  auto AL = ApplyDebugLocation::CreateArtificial(*this);

  // Use guarded initialization if the global variable is weak. This
  // occurs for, e.g., instantiated static data members and
  // definitions explicitly marked weak.
  //
  // Also use guarded initialization for a variable with dynamic TLS and
  // unordered initialization. (If the initialization is ordered, the ABI
  // layer will guard the whole-TU initialization for us.)
  if (Addr->hasWeakLinkage() || Addr->hasLinkOnceLinkage() ||
      (D->getTLSKind() == VarDecl::TLS_Dynamic &&
       isTemplateInstantiation(D->getTemplateSpecializationKind()))) {
    EmitCXXGuardedInit(*D, Addr, PerformInit);
  } else {
    EmitCXXGlobalVarDeclInit(*D, Addr, PerformInit);
  }

  if (getLangOpts().HLSL)
    CGM.getHLSLRuntime().annotateHLSLResource(D, Addr);

  FinishFunction();
}

void
CodeGenFunction::GenerateCXXGlobalInitFunc(llvm::Function *Fn,
                                           ArrayRef<llvm::Function *> Decls,
                                           ConstantAddress Guard) {
  {
    auto NL = ApplyDebugLocation::CreateEmpty(*this);
    StartFunction(GlobalDecl(), getContext().VoidTy, Fn,
                  getTypes().arrangeNullaryFunction(), FunctionArgList());
    // Emit an artificial location for this function.
    auto AL = ApplyDebugLocation::CreateArtificial(*this);

    llvm::BasicBlock *ExitBlock = nullptr;
    if (Guard.isValid()) {
      // If we have a guard variable, check whether we've already performed
      // these initializations. This happens for TLS initialization functions.
      llvm::Value *GuardVal = Builder.CreateLoad(Guard);
      llvm::Value *Uninit = Builder.CreateIsNull(GuardVal,
                                                 "guard.uninitialized");
      llvm::BasicBlock *InitBlock = createBasicBlock("init");
      ExitBlock = createBasicBlock("exit");
      EmitCXXGuardedInitBranch(Uninit, InitBlock, ExitBlock,
                               GuardKind::TlsGuard, nullptr);
      EmitBlock(InitBlock);
      // Mark as initialized before initializing anything else. If the
      // initializers use previously-initialized thread_local vars, that's
      // probably supposed to be OK, but the standard doesn't say.
      Builder.CreateStore(llvm::ConstantInt::get(GuardVal->getType(),1), Guard);

      // The guard variable can't ever change again.
      EmitInvariantStart(
          Guard.getPointer(),
          CharUnits::fromQuantity(
              CGM.getDataLayout().getTypeAllocSize(GuardVal->getType())));
    }

    RunCleanupsScope Scope(*this);

    // When building in Objective-C++ ARC mode, create an autorelease pool
    // around the global initializers.
    if (getLangOpts().ObjCAutoRefCount && getLangOpts().CPlusPlus) {
      llvm::Value *token = EmitObjCAutoreleasePoolPush();
      EmitObjCAutoreleasePoolCleanup(token);
    }

    for (unsigned i = 0, e = Decls.size(); i != e; ++i)
      if (Decls[i])
        EmitRuntimeCall(Decls[i]);

    Scope.ForceCleanup();

    if (ExitBlock) {
      Builder.CreateBr(ExitBlock);
      EmitBlock(ExitBlock);
    }
  }

  FinishFunction();
}

void CodeGenFunction::GenerateCXXGlobalCleanUpFunc(
    llvm::Function *Fn,
    ArrayRef<std::tuple<llvm::FunctionType *, llvm::WeakTrackingVH,
                        llvm::Constant *>>
        DtorsOrStermFinalizers) {
  {
    auto NL = ApplyDebugLocation::CreateEmpty(*this);
    StartFunction(GlobalDecl(), getContext().VoidTy, Fn,
                  getTypes().arrangeNullaryFunction(), FunctionArgList());
    // Emit an artificial location for this function.
    auto AL = ApplyDebugLocation::CreateArtificial(*this);

    // Emit the cleanups, in reverse order from construction.
    for (unsigned i = 0, e = DtorsOrStermFinalizers.size(); i != e; ++i) {
      llvm::FunctionType *CalleeTy;
      llvm::Value *Callee;
      llvm::Constant *Arg;
      std::tie(CalleeTy, Callee, Arg) = DtorsOrStermFinalizers[e - i - 1];

      llvm::CallInst *CI = nullptr;
      if (Arg == nullptr) {
        assert(
            CGM.getCXXABI().useSinitAndSterm() &&
            "Arg could not be nullptr unless using sinit and sterm functions.");
        CI = Builder.CreateCall(CalleeTy, Callee);
      } else
        CI = Builder.CreateCall(CalleeTy, Callee, Arg);

      // Make sure the call and the callee agree on calling convention.
      if (llvm::Function *F = dyn_cast<llvm::Function>(Callee))
        CI->setCallingConv(F->getCallingConv());
    }
  }

  FinishFunction();
}

/// generateDestroyHelper - Generates a helper function which, when
/// invoked, destroys the given object.  The address of the object
/// should be in global memory.
llvm::Function *CodeGenFunction::generateDestroyHelper(
    Address addr, QualType type, Destroyer *destroyer,
    bool useEHCleanupForArray, const VarDecl *VD) {
  FunctionArgList args;
  ImplicitParamDecl Dst(getContext(), getContext().VoidPtrTy,
                        ImplicitParamKind::Other);
  args.push_back(&Dst);

  const CGFunctionInfo &FI =
    CGM.getTypes().arrangeBuiltinFunctionDeclaration(getContext().VoidTy, args);
  llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);
  llvm::Function *fn = CGM.CreateGlobalInitOrCleanUpFunction(
      FTy, "__cxx_global_array_dtor", FI, VD->getLocation());

  CurEHLocation = VD->getBeginLoc();

  StartFunction(GlobalDecl(VD, DynamicInitKind::GlobalArrayDestructor),
                getContext().VoidTy, fn, FI, args);
  // Emit an artificial location for this function.
  auto AL = ApplyDebugLocation::CreateArtificial(*this);

  emitDestroy(addr, type, destroyer, useEHCleanupForArray);

  FinishFunction();

  return fn;
}
