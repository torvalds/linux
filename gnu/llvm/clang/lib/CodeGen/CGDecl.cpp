//===--- CGDecl.cpp - Emit LLVM Code for declarations ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Decl nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CGBlocks.h"
#include "CGCXXABI.h"
#include "CGCleanup.h"
#include "CGDebugInfo.h"
#include "CGOpenCLRuntime.h"
#include "CGOpenMPRuntime.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "EHScopeStack.h"
#include "PatternInit.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/Sema/Sema.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include <optional>

using namespace clang;
using namespace CodeGen;

static_assert(clang::Sema::MaximumAlignment <= llvm::Value::MaximumAlignment,
              "Clang max alignment greater than what LLVM supports?");

void CodeGenFunction::EmitDecl(const Decl &D) {
  switch (D.getKind()) {
  case Decl::BuiltinTemplate:
  case Decl::TranslationUnit:
  case Decl::ExternCContext:
  case Decl::Namespace:
  case Decl::UnresolvedUsingTypename:
  case Decl::ClassTemplateSpecialization:
  case Decl::ClassTemplatePartialSpecialization:
  case Decl::VarTemplateSpecialization:
  case Decl::VarTemplatePartialSpecialization:
  case Decl::TemplateTypeParm:
  case Decl::UnresolvedUsingValue:
  case Decl::NonTypeTemplateParm:
  case Decl::CXXDeductionGuide:
  case Decl::CXXMethod:
  case Decl::CXXConstructor:
  case Decl::CXXDestructor:
  case Decl::CXXConversion:
  case Decl::Field:
  case Decl::MSProperty:
  case Decl::IndirectField:
  case Decl::ObjCIvar:
  case Decl::ObjCAtDefsField:
  case Decl::ParmVar:
  case Decl::ImplicitParam:
  case Decl::ClassTemplate:
  case Decl::VarTemplate:
  case Decl::FunctionTemplate:
  case Decl::TypeAliasTemplate:
  case Decl::TemplateTemplateParm:
  case Decl::ObjCMethod:
  case Decl::ObjCCategory:
  case Decl::ObjCProtocol:
  case Decl::ObjCInterface:
  case Decl::ObjCCategoryImpl:
  case Decl::ObjCImplementation:
  case Decl::ObjCProperty:
  case Decl::ObjCCompatibleAlias:
  case Decl::PragmaComment:
  case Decl::PragmaDetectMismatch:
  case Decl::AccessSpec:
  case Decl::LinkageSpec:
  case Decl::Export:
  case Decl::ObjCPropertyImpl:
  case Decl::FileScopeAsm:
  case Decl::TopLevelStmt:
  case Decl::Friend:
  case Decl::FriendTemplate:
  case Decl::Block:
  case Decl::Captured:
  case Decl::UsingShadow:
  case Decl::ConstructorUsingShadow:
  case Decl::ObjCTypeParam:
  case Decl::Binding:
  case Decl::UnresolvedUsingIfExists:
  case Decl::HLSLBuffer:
    llvm_unreachable("Declaration should not be in declstmts!");
  case Decl::Record:    // struct/union/class X;
  case Decl::CXXRecord: // struct/union/class X; [C++]
    if (CGDebugInfo *DI = getDebugInfo())
      if (cast<RecordDecl>(D).getDefinition())
        DI->EmitAndRetainType(getContext().getRecordType(cast<RecordDecl>(&D)));
    return;
  case Decl::Enum:      // enum X;
    if (CGDebugInfo *DI = getDebugInfo())
      if (cast<EnumDecl>(D).getDefinition())
        DI->EmitAndRetainType(getContext().getEnumType(cast<EnumDecl>(&D)));
    return;
  case Decl::Function:     // void X();
  case Decl::EnumConstant: // enum ? { X = ? }
  case Decl::StaticAssert: // static_assert(X, ""); [C++0x]
  case Decl::Label:        // __label__ x;
  case Decl::Import:
  case Decl::MSGuid:    // __declspec(uuid("..."))
  case Decl::UnnamedGlobalConstant:
  case Decl::TemplateParamObject:
  case Decl::OMPThreadPrivate:
  case Decl::OMPAllocate:
  case Decl::OMPCapturedExpr:
  case Decl::OMPRequires:
  case Decl::Empty:
  case Decl::Concept:
  case Decl::ImplicitConceptSpecialization:
  case Decl::LifetimeExtendedTemporary:
  case Decl::RequiresExprBody:
    // None of these decls require codegen support.
    return;

  case Decl::NamespaceAlias:
    if (CGDebugInfo *DI = getDebugInfo())
        DI->EmitNamespaceAlias(cast<NamespaceAliasDecl>(D));
    return;
  case Decl::Using:          // using X; [C++]
    if (CGDebugInfo *DI = getDebugInfo())
        DI->EmitUsingDecl(cast<UsingDecl>(D));
    return;
  case Decl::UsingEnum: // using enum X; [C++]
    if (CGDebugInfo *DI = getDebugInfo())
      DI->EmitUsingEnumDecl(cast<UsingEnumDecl>(D));
    return;
  case Decl::UsingPack:
    for (auto *Using : cast<UsingPackDecl>(D).expansions())
      EmitDecl(*Using);
    return;
  case Decl::UsingDirective: // using namespace X; [C++]
    if (CGDebugInfo *DI = getDebugInfo())
      DI->EmitUsingDirective(cast<UsingDirectiveDecl>(D));
    return;
  case Decl::Var:
  case Decl::Decomposition: {
    const VarDecl &VD = cast<VarDecl>(D);
    assert(VD.isLocalVarDecl() &&
           "Should not see file-scope variables inside a function!");
    EmitVarDecl(VD);
    if (auto *DD = dyn_cast<DecompositionDecl>(&VD))
      for (auto *B : DD->bindings())
        if (auto *HD = B->getHoldingVar())
          EmitVarDecl(*HD);
    return;
  }

  case Decl::OMPDeclareReduction:
    return CGM.EmitOMPDeclareReduction(cast<OMPDeclareReductionDecl>(&D), this);

  case Decl::OMPDeclareMapper:
    return CGM.EmitOMPDeclareMapper(cast<OMPDeclareMapperDecl>(&D), this);

  case Decl::Typedef:      // typedef int X;
  case Decl::TypeAlias: {  // using X = int; [C++0x]
    QualType Ty = cast<TypedefNameDecl>(D).getUnderlyingType();
    if (CGDebugInfo *DI = getDebugInfo())
      DI->EmitAndRetainType(Ty);
    if (Ty->isVariablyModifiedType())
      EmitVariablyModifiedType(Ty);
    return;
  }
  }
}

/// EmitVarDecl - This method handles emission of any variable declaration
/// inside a function, including static vars etc.
void CodeGenFunction::EmitVarDecl(const VarDecl &D) {
  if (D.hasExternalStorage())
    // Don't emit it now, allow it to be emitted lazily on its first use.
    return;

  // Some function-scope variable does not have static storage but still
  // needs to be emitted like a static variable, e.g. a function-scope
  // variable in constant address space in OpenCL.
  if (D.getStorageDuration() != SD_Automatic) {
    // Static sampler variables translated to function calls.
    if (D.getType()->isSamplerT())
      return;

    llvm::GlobalValue::LinkageTypes Linkage =
        CGM.getLLVMLinkageVarDefinition(&D);

    // FIXME: We need to force the emission/use of a guard variable for
    // some variables even if we can constant-evaluate them because
    // we can't guarantee every translation unit will constant-evaluate them.

    return EmitStaticVarDecl(D, Linkage);
  }

  if (D.getType().getAddressSpace() == LangAS::opencl_local)
    return CGM.getOpenCLRuntime().EmitWorkGroupLocalVarDecl(*this, D);

  assert(D.hasLocalStorage());
  return EmitAutoVarDecl(D);
}

static std::string getStaticDeclName(CodeGenModule &CGM, const VarDecl &D) {
  if (CGM.getLangOpts().CPlusPlus)
    return CGM.getMangledName(&D).str();

  // If this isn't C++, we don't need a mangled name, just a pretty one.
  assert(!D.isExternallyVisible() && "name shouldn't matter");
  std::string ContextName;
  const DeclContext *DC = D.getDeclContext();
  if (auto *CD = dyn_cast<CapturedDecl>(DC))
    DC = cast<DeclContext>(CD->getNonClosureContext());
  if (const auto *FD = dyn_cast<FunctionDecl>(DC))
    ContextName = std::string(CGM.getMangledName(FD));
  else if (const auto *BD = dyn_cast<BlockDecl>(DC))
    ContextName = std::string(CGM.getBlockMangledName(GlobalDecl(), BD));
  else if (const auto *OMD = dyn_cast<ObjCMethodDecl>(DC))
    ContextName = OMD->getSelector().getAsString();
  else
    llvm_unreachable("Unknown context for static var decl");

  ContextName += "." + D.getNameAsString();
  return ContextName;
}

llvm::Constant *CodeGenModule::getOrCreateStaticVarDecl(
    const VarDecl &D, llvm::GlobalValue::LinkageTypes Linkage) {
  // In general, we don't always emit static var decls once before we reference
  // them. It is possible to reference them before emitting the function that
  // contains them, and it is possible to emit the containing function multiple
  // times.
  if (llvm::Constant *ExistingGV = StaticLocalDeclMap[&D])
    return ExistingGV;

  QualType Ty = D.getType();
  assert(Ty->isConstantSizeType() && "VLAs can't be static");

  // Use the label if the variable is renamed with the asm-label extension.
  std::string Name;
  if (D.hasAttr<AsmLabelAttr>())
    Name = std::string(getMangledName(&D));
  else
    Name = getStaticDeclName(*this, D);

  llvm::Type *LTy = getTypes().ConvertTypeForMem(Ty);
  LangAS AS = GetGlobalVarAddressSpace(&D);
  unsigned TargetAS = getContext().getTargetAddressSpace(AS);

  // OpenCL variables in local address space and CUDA shared
  // variables cannot have an initializer.
  llvm::Constant *Init = nullptr;
  if (Ty.getAddressSpace() == LangAS::opencl_local ||
      D.hasAttr<CUDASharedAttr>() || D.hasAttr<LoaderUninitializedAttr>())
    Init = llvm::UndefValue::get(LTy);
  else
    Init = EmitNullConstant(Ty);

  llvm::GlobalVariable *GV = new llvm::GlobalVariable(
      getModule(), LTy, Ty.isConstant(getContext()), Linkage, Init, Name,
      nullptr, llvm::GlobalVariable::NotThreadLocal, TargetAS);
  GV->setAlignment(getContext().getDeclAlign(&D).getAsAlign());

  if (supportsCOMDAT() && GV->isWeakForLinker())
    GV->setComdat(TheModule.getOrInsertComdat(GV->getName()));

  if (D.getTLSKind())
    setTLSMode(GV, D);

  setGVProperties(GV, &D);
  getTargetCodeGenInfo().setTargetAttributes(cast<Decl>(&D), GV, *this);

  // Make sure the result is of the correct type.
  LangAS ExpectedAS = Ty.getAddressSpace();
  llvm::Constant *Addr = GV;
  if (AS != ExpectedAS) {
    Addr = getTargetCodeGenInfo().performAddrSpaceCast(
        *this, GV, AS, ExpectedAS,
        llvm::PointerType::get(getLLVMContext(),
                               getContext().getTargetAddressSpace(ExpectedAS)));
  }

  setStaticLocalDeclAddress(&D, Addr);

  // Ensure that the static local gets initialized by making sure the parent
  // function gets emitted eventually.
  const Decl *DC = cast<Decl>(D.getDeclContext());

  // We can't name blocks or captured statements directly, so try to emit their
  // parents.
  if (isa<BlockDecl>(DC) || isa<CapturedDecl>(DC)) {
    DC = DC->getNonClosureContext();
    // FIXME: Ensure that global blocks get emitted.
    if (!DC)
      return Addr;
  }

  GlobalDecl GD;
  if (const auto *CD = dyn_cast<CXXConstructorDecl>(DC))
    GD = GlobalDecl(CD, Ctor_Base);
  else if (const auto *DD = dyn_cast<CXXDestructorDecl>(DC))
    GD = GlobalDecl(DD, Dtor_Base);
  else if (const auto *FD = dyn_cast<FunctionDecl>(DC))
    GD = GlobalDecl(FD);
  else {
    // Don't do anything for Obj-C method decls or global closures. We should
    // never defer them.
    assert(isa<ObjCMethodDecl>(DC) && "unexpected parent code decl");
  }
  if (GD.getDecl()) {
    // Disable emission of the parent function for the OpenMP device codegen.
    CGOpenMPRuntime::DisableAutoDeclareTargetRAII NoDeclTarget(*this);
    (void)GetAddrOfGlobal(GD);
  }

  return Addr;
}

/// AddInitializerToStaticVarDecl - Add the initializer for 'D' to the
/// global variable that has already been created for it.  If the initializer
/// has a different type than GV does, this may free GV and return a different
/// one.  Otherwise it just returns GV.
llvm::GlobalVariable *
CodeGenFunction::AddInitializerToStaticVarDecl(const VarDecl &D,
                                               llvm::GlobalVariable *GV) {
  ConstantEmitter emitter(*this);
  llvm::Constant *Init = emitter.tryEmitForInitializer(D);

  // If constant emission failed, then this should be a C++ static
  // initializer.
  if (!Init) {
    if (!getLangOpts().CPlusPlus)
      CGM.ErrorUnsupported(D.getInit(), "constant l-value expression");
    else if (D.hasFlexibleArrayInit(getContext()))
      CGM.ErrorUnsupported(D.getInit(), "flexible array initializer");
    else if (HaveInsertPoint()) {
      // Since we have a static initializer, this global variable can't
      // be constant.
      GV->setConstant(false);

      EmitCXXGuardedInit(D, GV, /*PerformInit*/true);
    }
    return GV;
  }

#ifndef NDEBUG
  CharUnits VarSize = CGM.getContext().getTypeSizeInChars(D.getType()) +
                      D.getFlexibleArrayInitChars(getContext());
  CharUnits CstSize = CharUnits::fromQuantity(
      CGM.getDataLayout().getTypeAllocSize(Init->getType()));
  assert(VarSize == CstSize && "Emitted constant has unexpected size");
#endif

  // The initializer may differ in type from the global. Rewrite
  // the global to match the initializer.  (We have to do this
  // because some types, like unions, can't be completely represented
  // in the LLVM type system.)
  if (GV->getValueType() != Init->getType()) {
    llvm::GlobalVariable *OldGV = GV;

    GV = new llvm::GlobalVariable(
        CGM.getModule(), Init->getType(), OldGV->isConstant(),
        OldGV->getLinkage(), Init, "",
        /*InsertBefore*/ OldGV, OldGV->getThreadLocalMode(),
        OldGV->getType()->getPointerAddressSpace());
    GV->setVisibility(OldGV->getVisibility());
    GV->setDSOLocal(OldGV->isDSOLocal());
    GV->setComdat(OldGV->getComdat());

    // Steal the name of the old global
    GV->takeName(OldGV);

    // Replace all uses of the old global with the new global
    OldGV->replaceAllUsesWith(GV);

    // Erase the old global, since it is no longer used.
    OldGV->eraseFromParent();
  }

  bool NeedsDtor =
      D.needsDestruction(getContext()) == QualType::DK_cxx_destructor;

  GV->setConstant(
      D.getType().isConstantStorage(getContext(), true, !NeedsDtor));
  GV->setInitializer(Init);

  emitter.finalize(GV);

  if (NeedsDtor && HaveInsertPoint()) {
    // We have a constant initializer, but a nontrivial destructor. We still
    // need to perform a guarded "initialization" in order to register the
    // destructor.
    EmitCXXGuardedInit(D, GV, /*PerformInit*/false);
  }

  return GV;
}

void CodeGenFunction::EmitStaticVarDecl(const VarDecl &D,
                                      llvm::GlobalValue::LinkageTypes Linkage) {
  // Check to see if we already have a global variable for this
  // declaration.  This can happen when double-emitting function
  // bodies, e.g. with complete and base constructors.
  llvm::Constant *addr = CGM.getOrCreateStaticVarDecl(D, Linkage);
  CharUnits alignment = getContext().getDeclAlign(&D);

  // Store into LocalDeclMap before generating initializer to handle
  // circular references.
  llvm::Type *elemTy = ConvertTypeForMem(D.getType());
  setAddrOfLocalVar(&D, Address(addr, elemTy, alignment));

  // We can't have a VLA here, but we can have a pointer to a VLA,
  // even though that doesn't really make any sense.
  // Make sure to evaluate VLA bounds now so that we have them for later.
  if (D.getType()->isVariablyModifiedType())
    EmitVariablyModifiedType(D.getType());

  // Save the type in case adding the initializer forces a type change.
  llvm::Type *expectedType = addr->getType();

  llvm::GlobalVariable *var =
    cast<llvm::GlobalVariable>(addr->stripPointerCasts());

  // CUDA's local and local static __shared__ variables should not
  // have any non-empty initializers. This is ensured by Sema.
  // Whatever initializer such variable may have when it gets here is
  // a no-op and should not be emitted.
  bool isCudaSharedVar = getLangOpts().CUDA && getLangOpts().CUDAIsDevice &&
                         D.hasAttr<CUDASharedAttr>();
  // If this value has an initializer, emit it.
  if (D.getInit() && !isCudaSharedVar)
    var = AddInitializerToStaticVarDecl(D, var);

  var->setAlignment(alignment.getAsAlign());

  if (D.hasAttr<AnnotateAttr>())
    CGM.AddGlobalAnnotations(&D, var);

  if (auto *SA = D.getAttr<PragmaClangBSSSectionAttr>())
    var->addAttribute("bss-section", SA->getName());
  if (auto *SA = D.getAttr<PragmaClangDataSectionAttr>())
    var->addAttribute("data-section", SA->getName());
  if (auto *SA = D.getAttr<PragmaClangRodataSectionAttr>())
    var->addAttribute("rodata-section", SA->getName());
  if (auto *SA = D.getAttr<PragmaClangRelroSectionAttr>())
    var->addAttribute("relro-section", SA->getName());

  if (const SectionAttr *SA = D.getAttr<SectionAttr>())
    var->setSection(SA->getName());

  if (D.hasAttr<RetainAttr>())
    CGM.addUsedGlobal(var);
  else if (D.hasAttr<UsedAttr>())
    CGM.addUsedOrCompilerUsedGlobal(var);

  if (CGM.getCodeGenOpts().KeepPersistentStorageVariables)
    CGM.addUsedOrCompilerUsedGlobal(var);

  // We may have to cast the constant because of the initializer
  // mismatch above.
  //
  // FIXME: It is really dangerous to store this in the map; if anyone
  // RAUW's the GV uses of this constant will be invalid.
  llvm::Constant *castedAddr =
    llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(var, expectedType);
  LocalDeclMap.find(&D)->second = Address(castedAddr, elemTy, alignment);
  CGM.setStaticLocalDeclAddress(&D, castedAddr);

  CGM.getSanitizerMetadata()->reportGlobal(var, D);

  // Emit global variable debug descriptor for static vars.
  CGDebugInfo *DI = getDebugInfo();
  if (DI && CGM.getCodeGenOpts().hasReducedDebugInfo()) {
    DI->setLocation(D.getLocation());
    DI->EmitGlobalVariable(var, &D);
  }
}

namespace {
  struct DestroyObject final : EHScopeStack::Cleanup {
    DestroyObject(Address addr, QualType type,
                  CodeGenFunction::Destroyer *destroyer,
                  bool useEHCleanupForArray)
      : addr(addr), type(type), destroyer(destroyer),
        useEHCleanupForArray(useEHCleanupForArray) {}

    Address addr;
    QualType type;
    CodeGenFunction::Destroyer *destroyer;
    bool useEHCleanupForArray;

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      // Don't use an EH cleanup recursively from an EH cleanup.
      bool useEHCleanupForArray =
        flags.isForNormalCleanup() && this->useEHCleanupForArray;

      CGF.emitDestroy(addr, type, destroyer, useEHCleanupForArray);
    }
  };

  template <class Derived>
  struct DestroyNRVOVariable : EHScopeStack::Cleanup {
    DestroyNRVOVariable(Address addr, QualType type, llvm::Value *NRVOFlag)
        : NRVOFlag(NRVOFlag), Loc(addr), Ty(type) {}

    llvm::Value *NRVOFlag;
    Address Loc;
    QualType Ty;

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      // Along the exceptions path we always execute the dtor.
      bool NRVO = flags.isForNormalCleanup() && NRVOFlag;

      llvm::BasicBlock *SkipDtorBB = nullptr;
      if (NRVO) {
        // If we exited via NRVO, we skip the destructor call.
        llvm::BasicBlock *RunDtorBB = CGF.createBasicBlock("nrvo.unused");
        SkipDtorBB = CGF.createBasicBlock("nrvo.skipdtor");
        llvm::Value *DidNRVO =
          CGF.Builder.CreateFlagLoad(NRVOFlag, "nrvo.val");
        CGF.Builder.CreateCondBr(DidNRVO, SkipDtorBB, RunDtorBB);
        CGF.EmitBlock(RunDtorBB);
      }

      static_cast<Derived *>(this)->emitDestructorCall(CGF);

      if (NRVO) CGF.EmitBlock(SkipDtorBB);
    }

    virtual ~DestroyNRVOVariable() = default;
  };

  struct DestroyNRVOVariableCXX final
      : DestroyNRVOVariable<DestroyNRVOVariableCXX> {
    DestroyNRVOVariableCXX(Address addr, QualType type,
                           const CXXDestructorDecl *Dtor, llvm::Value *NRVOFlag)
        : DestroyNRVOVariable<DestroyNRVOVariableCXX>(addr, type, NRVOFlag),
          Dtor(Dtor) {}

    const CXXDestructorDecl *Dtor;

    void emitDestructorCall(CodeGenFunction &CGF) {
      CGF.EmitCXXDestructorCall(Dtor, Dtor_Complete,
                                /*ForVirtualBase=*/false,
                                /*Delegating=*/false, Loc, Ty);
    }
  };

  struct DestroyNRVOVariableC final
      : DestroyNRVOVariable<DestroyNRVOVariableC> {
    DestroyNRVOVariableC(Address addr, llvm::Value *NRVOFlag, QualType Ty)
        : DestroyNRVOVariable<DestroyNRVOVariableC>(addr, Ty, NRVOFlag) {}

    void emitDestructorCall(CodeGenFunction &CGF) {
      CGF.destroyNonTrivialCStruct(CGF, Loc, Ty);
    }
  };

  struct CallStackRestore final : EHScopeStack::Cleanup {
    Address Stack;
    CallStackRestore(Address Stack) : Stack(Stack) {}
    bool isRedundantBeforeReturn() override { return true; }
    void Emit(CodeGenFunction &CGF, Flags flags) override {
      llvm::Value *V = CGF.Builder.CreateLoad(Stack);
      CGF.Builder.CreateStackRestore(V);
    }
  };

  struct KmpcAllocFree final : EHScopeStack::Cleanup {
    std::pair<llvm::Value *, llvm::Value *> AddrSizePair;
    KmpcAllocFree(const std::pair<llvm::Value *, llvm::Value *> &AddrSizePair)
        : AddrSizePair(AddrSizePair) {}
    void Emit(CodeGenFunction &CGF, Flags EmissionFlags) override {
      auto &RT = CGF.CGM.getOpenMPRuntime();
      RT.getKmpcFreeShared(CGF, AddrSizePair);
    }
  };

  struct ExtendGCLifetime final : EHScopeStack::Cleanup {
    const VarDecl &Var;
    ExtendGCLifetime(const VarDecl *var) : Var(*var) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      // Compute the address of the local variable, in case it's a
      // byref or something.
      DeclRefExpr DRE(CGF.getContext(), const_cast<VarDecl *>(&Var), false,
                      Var.getType(), VK_LValue, SourceLocation());
      llvm::Value *value = CGF.EmitLoadOfScalar(CGF.EmitDeclRefLValue(&DRE),
                                                SourceLocation());
      CGF.EmitExtendGCLifetime(value);
    }
  };

  struct CallCleanupFunction final : EHScopeStack::Cleanup {
    llvm::Constant *CleanupFn;
    const CGFunctionInfo &FnInfo;
    const VarDecl &Var;

    CallCleanupFunction(llvm::Constant *CleanupFn, const CGFunctionInfo *Info,
                        const VarDecl *Var)
      : CleanupFn(CleanupFn), FnInfo(*Info), Var(*Var) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      DeclRefExpr DRE(CGF.getContext(), const_cast<VarDecl *>(&Var), false,
                      Var.getType(), VK_LValue, SourceLocation());
      // Compute the address of the local variable, in case it's a byref
      // or something.
      llvm::Value *Addr = CGF.EmitDeclRefLValue(&DRE).getPointer(CGF);

      // In some cases, the type of the function argument will be different from
      // the type of the pointer. An example of this is
      // void f(void* arg);
      // __attribute__((cleanup(f))) void *g;
      //
      // To fix this we insert a bitcast here.
      QualType ArgTy = FnInfo.arg_begin()->type;
      llvm::Value *Arg =
        CGF.Builder.CreateBitCast(Addr, CGF.ConvertType(ArgTy));

      CallArgList Args;
      Args.add(RValue::get(Arg),
               CGF.getContext().getPointerType(Var.getType()));
      auto Callee = CGCallee::forDirect(CleanupFn);
      CGF.EmitCall(FnInfo, Callee, ReturnValueSlot(), Args);
    }
  };
} // end anonymous namespace

/// EmitAutoVarWithLifetime - Does the setup required for an automatic
/// variable with lifetime.
static void EmitAutoVarWithLifetime(CodeGenFunction &CGF, const VarDecl &var,
                                    Address addr,
                                    Qualifiers::ObjCLifetime lifetime) {
  switch (lifetime) {
  case Qualifiers::OCL_None:
    llvm_unreachable("present but none");

  case Qualifiers::OCL_ExplicitNone:
    // nothing to do
    break;

  case Qualifiers::OCL_Strong: {
    CodeGenFunction::Destroyer *destroyer =
      (var.hasAttr<ObjCPreciseLifetimeAttr>()
       ? CodeGenFunction::destroyARCStrongPrecise
       : CodeGenFunction::destroyARCStrongImprecise);

    CleanupKind cleanupKind = CGF.getARCCleanupKind();
    CGF.pushDestroy(cleanupKind, addr, var.getType(), destroyer,
                    cleanupKind & EHCleanup);
    break;
  }
  case Qualifiers::OCL_Autoreleasing:
    // nothing to do
    break;

  case Qualifiers::OCL_Weak:
    // __weak objects always get EH cleanups; otherwise, exceptions
    // could cause really nasty crashes instead of mere leaks.
    CGF.pushDestroy(NormalAndEHCleanup, addr, var.getType(),
                    CodeGenFunction::destroyARCWeak,
                    /*useEHCleanup*/ true);
    break;
  }
}

static bool isAccessedBy(const VarDecl &var, const Stmt *s) {
  if (const Expr *e = dyn_cast<Expr>(s)) {
    // Skip the most common kinds of expressions that make
    // hierarchy-walking expensive.
    s = e = e->IgnoreParenCasts();

    if (const DeclRefExpr *ref = dyn_cast<DeclRefExpr>(e))
      return (ref->getDecl() == &var);
    if (const BlockExpr *be = dyn_cast<BlockExpr>(e)) {
      const BlockDecl *block = be->getBlockDecl();
      for (const auto &I : block->captures()) {
        if (I.getVariable() == &var)
          return true;
      }
    }
  }

  for (const Stmt *SubStmt : s->children())
    // SubStmt might be null; as in missing decl or conditional of an if-stmt.
    if (SubStmt && isAccessedBy(var, SubStmt))
      return true;

  return false;
}

static bool isAccessedBy(const ValueDecl *decl, const Expr *e) {
  if (!decl) return false;
  if (!isa<VarDecl>(decl)) return false;
  const VarDecl *var = cast<VarDecl>(decl);
  return isAccessedBy(*var, e);
}

static bool tryEmitARCCopyWeakInit(CodeGenFunction &CGF,
                                   const LValue &destLV, const Expr *init) {
  bool needsCast = false;

  while (auto castExpr = dyn_cast<CastExpr>(init->IgnoreParens())) {
    switch (castExpr->getCastKind()) {
    // Look through casts that don't require representation changes.
    case CK_NoOp:
    case CK_BitCast:
    case CK_BlockPointerToObjCPointerCast:
      needsCast = true;
      break;

    // If we find an l-value to r-value cast from a __weak variable,
    // emit this operation as a copy or move.
    case CK_LValueToRValue: {
      const Expr *srcExpr = castExpr->getSubExpr();
      if (srcExpr->getType().getObjCLifetime() != Qualifiers::OCL_Weak)
        return false;

      // Emit the source l-value.
      LValue srcLV = CGF.EmitLValue(srcExpr);

      // Handle a formal type change to avoid asserting.
      auto srcAddr = srcLV.getAddress();
      if (needsCast) {
        srcAddr = srcAddr.withElementType(destLV.getAddress().getElementType());
      }

      // If it was an l-value, use objc_copyWeak.
      if (srcExpr->isLValue()) {
        CGF.EmitARCCopyWeak(destLV.getAddress(), srcAddr);
      } else {
        assert(srcExpr->isXValue());
        CGF.EmitARCMoveWeak(destLV.getAddress(), srcAddr);
      }
      return true;
    }

    // Stop at anything else.
    default:
      return false;
    }

    init = castExpr->getSubExpr();
  }
  return false;
}

static void drillIntoBlockVariable(CodeGenFunction &CGF,
                                   LValue &lvalue,
                                   const VarDecl *var) {
  lvalue.setAddress(CGF.emitBlockByrefAddress(lvalue.getAddress(), var));
}

void CodeGenFunction::EmitNullabilityCheck(LValue LHS, llvm::Value *RHS,
                                           SourceLocation Loc) {
  if (!SanOpts.has(SanitizerKind::NullabilityAssign))
    return;

  auto Nullability = LHS.getType()->getNullability();
  if (!Nullability || *Nullability != NullabilityKind::NonNull)
    return;

  // Check if the right hand side of the assignment is nonnull, if the left
  // hand side must be nonnull.
  SanitizerScope SanScope(this);
  llvm::Value *IsNotNull = Builder.CreateIsNotNull(RHS);
  llvm::Constant *StaticData[] = {
      EmitCheckSourceLocation(Loc), EmitCheckTypeDescriptor(LHS.getType()),
      llvm::ConstantInt::get(Int8Ty, 0), // The LogAlignment info is unused.
      llvm::ConstantInt::get(Int8Ty, TCK_NonnullAssign)};
  EmitCheck({{IsNotNull, SanitizerKind::NullabilityAssign}},
            SanitizerHandler::TypeMismatch, StaticData, RHS);
}

void CodeGenFunction::EmitScalarInit(const Expr *init, const ValueDecl *D,
                                     LValue lvalue, bool capturedByInit) {
  Qualifiers::ObjCLifetime lifetime = lvalue.getObjCLifetime();
  if (!lifetime) {
    llvm::Value *value = EmitScalarExpr(init);
    if (capturedByInit)
      drillIntoBlockVariable(*this, lvalue, cast<VarDecl>(D));
    EmitNullabilityCheck(lvalue, value, init->getExprLoc());
    EmitStoreThroughLValue(RValue::get(value), lvalue, true);
    return;
  }

  if (const CXXDefaultInitExpr *DIE = dyn_cast<CXXDefaultInitExpr>(init))
    init = DIE->getExpr();

  // If we're emitting a value with lifetime, we have to do the
  // initialization *before* we leave the cleanup scopes.
  if (auto *EWC = dyn_cast<ExprWithCleanups>(init)) {
    CodeGenFunction::RunCleanupsScope Scope(*this);
    return EmitScalarInit(EWC->getSubExpr(), D, lvalue, capturedByInit);
  }

  // We have to maintain the illusion that the variable is
  // zero-initialized.  If the variable might be accessed in its
  // initializer, zero-initialize before running the initializer, then
  // actually perform the initialization with an assign.
  bool accessedByInit = false;
  if (lifetime != Qualifiers::OCL_ExplicitNone)
    accessedByInit = (capturedByInit || isAccessedBy(D, init));
  if (accessedByInit) {
    LValue tempLV = lvalue;
    // Drill down to the __block object if necessary.
    if (capturedByInit) {
      // We can use a simple GEP for this because it can't have been
      // moved yet.
      tempLV.setAddress(emitBlockByrefAddress(tempLV.getAddress(),
                                              cast<VarDecl>(D),
                                              /*follow*/ false));
    }

    auto ty = cast<llvm::PointerType>(tempLV.getAddress().getElementType());
    llvm::Value *zero = CGM.getNullPointer(ty, tempLV.getType());

    // If __weak, we want to use a barrier under certain conditions.
    if (lifetime == Qualifiers::OCL_Weak)
      EmitARCInitWeak(tempLV.getAddress(), zero);

    // Otherwise just do a simple store.
    else
      EmitStoreOfScalar(zero, tempLV, /* isInitialization */ true);
  }

  // Emit the initializer.
  llvm::Value *value = nullptr;

  switch (lifetime) {
  case Qualifiers::OCL_None:
    llvm_unreachable("present but none");

  case Qualifiers::OCL_Strong: {
    if (!D || !isa<VarDecl>(D) || !cast<VarDecl>(D)->isARCPseudoStrong()) {
      value = EmitARCRetainScalarExpr(init);
      break;
    }
    // If D is pseudo-strong, treat it like __unsafe_unretained here. This means
    // that we omit the retain, and causes non-autoreleased return values to be
    // immediately released.
    [[fallthrough]];
  }

  case Qualifiers::OCL_ExplicitNone:
    value = EmitARCUnsafeUnretainedScalarExpr(init);
    break;

  case Qualifiers::OCL_Weak: {
    // If it's not accessed by the initializer, try to emit the
    // initialization with a copy or move.
    if (!accessedByInit && tryEmitARCCopyWeakInit(*this, lvalue, init)) {
      return;
    }

    // No way to optimize a producing initializer into this.  It's not
    // worth optimizing for, because the value will immediately
    // disappear in the common case.
    value = EmitScalarExpr(init);

    if (capturedByInit) drillIntoBlockVariable(*this, lvalue, cast<VarDecl>(D));
    if (accessedByInit)
      EmitARCStoreWeak(lvalue.getAddress(), value, /*ignored*/ true);
    else
      EmitARCInitWeak(lvalue.getAddress(), value);
    return;
  }

  case Qualifiers::OCL_Autoreleasing:
    value = EmitARCRetainAutoreleaseScalarExpr(init);
    break;
  }

  if (capturedByInit) drillIntoBlockVariable(*this, lvalue, cast<VarDecl>(D));

  EmitNullabilityCheck(lvalue, value, init->getExprLoc());

  // If the variable might have been accessed by its initializer, we
  // might have to initialize with a barrier.  We have to do this for
  // both __weak and __strong, but __weak got filtered out above.
  if (accessedByInit && lifetime == Qualifiers::OCL_Strong) {
    llvm::Value *oldValue = EmitLoadOfScalar(lvalue, init->getExprLoc());
    EmitStoreOfScalar(value, lvalue, /* isInitialization */ true);
    EmitARCRelease(oldValue, ARCImpreciseLifetime);
    return;
  }

  EmitStoreOfScalar(value, lvalue, /* isInitialization */ true);
}

/// Decide whether we can emit the non-zero parts of the specified initializer
/// with equal or fewer than NumStores scalar stores.
static bool canEmitInitWithFewStoresAfterBZero(llvm::Constant *Init,
                                               unsigned &NumStores) {
  // Zero and Undef never requires any extra stores.
  if (isa<llvm::ConstantAggregateZero>(Init) ||
      isa<llvm::ConstantPointerNull>(Init) ||
      isa<llvm::UndefValue>(Init))
    return true;
  if (isa<llvm::ConstantInt>(Init) || isa<llvm::ConstantFP>(Init) ||
      isa<llvm::ConstantVector>(Init) || isa<llvm::BlockAddress>(Init) ||
      isa<llvm::ConstantExpr>(Init))
    return Init->isNullValue() || NumStores--;

  // See if we can emit each element.
  if (isa<llvm::ConstantArray>(Init) || isa<llvm::ConstantStruct>(Init)) {
    for (unsigned i = 0, e = Init->getNumOperands(); i != e; ++i) {
      llvm::Constant *Elt = cast<llvm::Constant>(Init->getOperand(i));
      if (!canEmitInitWithFewStoresAfterBZero(Elt, NumStores))
        return false;
    }
    return true;
  }

  if (llvm::ConstantDataSequential *CDS =
        dyn_cast<llvm::ConstantDataSequential>(Init)) {
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      llvm::Constant *Elt = CDS->getElementAsConstant(i);
      if (!canEmitInitWithFewStoresAfterBZero(Elt, NumStores))
        return false;
    }
    return true;
  }

  // Anything else is hard and scary.
  return false;
}

/// For inits that canEmitInitWithFewStoresAfterBZero returned true for, emit
/// the scalar stores that would be required.
static void emitStoresForInitAfterBZero(CodeGenModule &CGM,
                                        llvm::Constant *Init, Address Loc,
                                        bool isVolatile, CGBuilderTy &Builder,
                                        bool IsAutoInit) {
  assert(!Init->isNullValue() && !isa<llvm::UndefValue>(Init) &&
         "called emitStoresForInitAfterBZero for zero or undef value.");

  if (isa<llvm::ConstantInt>(Init) || isa<llvm::ConstantFP>(Init) ||
      isa<llvm::ConstantVector>(Init) || isa<llvm::BlockAddress>(Init) ||
      isa<llvm::ConstantExpr>(Init)) {
    auto *I = Builder.CreateStore(Init, Loc, isVolatile);
    if (IsAutoInit)
      I->addAnnotationMetadata("auto-init");
    return;
  }

  if (llvm::ConstantDataSequential *CDS =
          dyn_cast<llvm::ConstantDataSequential>(Init)) {
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      llvm::Constant *Elt = CDS->getElementAsConstant(i);

      // If necessary, get a pointer to the element and emit it.
      if (!Elt->isNullValue() && !isa<llvm::UndefValue>(Elt))
        emitStoresForInitAfterBZero(
            CGM, Elt, Builder.CreateConstInBoundsGEP2_32(Loc, 0, i), isVolatile,
            Builder, IsAutoInit);
    }
    return;
  }

  assert((isa<llvm::ConstantStruct>(Init) || isa<llvm::ConstantArray>(Init)) &&
         "Unknown value type!");

  for (unsigned i = 0, e = Init->getNumOperands(); i != e; ++i) {
    llvm::Constant *Elt = cast<llvm::Constant>(Init->getOperand(i));

    // If necessary, get a pointer to the element and emit it.
    if (!Elt->isNullValue() && !isa<llvm::UndefValue>(Elt))
      emitStoresForInitAfterBZero(CGM, Elt,
                                  Builder.CreateConstInBoundsGEP2_32(Loc, 0, i),
                                  isVolatile, Builder, IsAutoInit);
  }
}

/// Decide whether we should use bzero plus some stores to initialize a local
/// variable instead of using a memcpy from a constant global.  It is beneficial
/// to use bzero if the global is all zeros, or mostly zeros and large.
static bool shouldUseBZeroPlusStoresToInitialize(llvm::Constant *Init,
                                                 uint64_t GlobalSize) {
  // If a global is all zeros, always use a bzero.
  if (isa<llvm::ConstantAggregateZero>(Init)) return true;

  // If a non-zero global is <= 32 bytes, always use a memcpy.  If it is large,
  // do it if it will require 6 or fewer scalar stores.
  // TODO: Should budget depends on the size?  Avoiding a large global warrants
  // plopping in more stores.
  unsigned StoreBudget = 6;
  uint64_t SizeLimit = 32;

  return GlobalSize > SizeLimit &&
         canEmitInitWithFewStoresAfterBZero(Init, StoreBudget);
}

/// Decide whether we should use memset to initialize a local variable instead
/// of using a memcpy from a constant global. Assumes we've already decided to
/// not user bzero.
/// FIXME We could be more clever, as we are for bzero above, and generate
///       memset followed by stores. It's unclear that's worth the effort.
static llvm::Value *shouldUseMemSetToInitialize(llvm::Constant *Init,
                                                uint64_t GlobalSize,
                                                const llvm::DataLayout &DL) {
  uint64_t SizeLimit = 32;
  if (GlobalSize <= SizeLimit)
    return nullptr;
  return llvm::isBytewiseValue(Init, DL);
}

/// Decide whether we want to split a constant structure or array store into a
/// sequence of its fields' stores. This may cost us code size and compilation
/// speed, but plays better with store optimizations.
static bool shouldSplitConstantStore(CodeGenModule &CGM,
                                     uint64_t GlobalByteSize) {
  // Don't break things that occupy more than one cacheline.
  uint64_t ByteSizeLimit = 64;
  if (CGM.getCodeGenOpts().OptimizationLevel == 0)
    return false;
  if (GlobalByteSize <= ByteSizeLimit)
    return true;
  return false;
}

enum class IsPattern { No, Yes };

/// Generate a constant filled with either a pattern or zeroes.
static llvm::Constant *patternOrZeroFor(CodeGenModule &CGM, IsPattern isPattern,
                                        llvm::Type *Ty) {
  if (isPattern == IsPattern::Yes)
    return initializationPatternFor(CGM, Ty);
  else
    return llvm::Constant::getNullValue(Ty);
}

static llvm::Constant *constWithPadding(CodeGenModule &CGM, IsPattern isPattern,
                                        llvm::Constant *constant);

/// Helper function for constWithPadding() to deal with padding in structures.
static llvm::Constant *constStructWithPadding(CodeGenModule &CGM,
                                              IsPattern isPattern,
                                              llvm::StructType *STy,
                                              llvm::Constant *constant) {
  const llvm::DataLayout &DL = CGM.getDataLayout();
  const llvm::StructLayout *Layout = DL.getStructLayout(STy);
  llvm::Type *Int8Ty = llvm::IntegerType::getInt8Ty(CGM.getLLVMContext());
  unsigned SizeSoFar = 0;
  SmallVector<llvm::Constant *, 8> Values;
  bool NestedIntact = true;
  for (unsigned i = 0, e = STy->getNumElements(); i != e; i++) {
    unsigned CurOff = Layout->getElementOffset(i);
    if (SizeSoFar < CurOff) {
      assert(!STy->isPacked());
      auto *PadTy = llvm::ArrayType::get(Int8Ty, CurOff - SizeSoFar);
      Values.push_back(patternOrZeroFor(CGM, isPattern, PadTy));
    }
    llvm::Constant *CurOp;
    if (constant->isZeroValue())
      CurOp = llvm::Constant::getNullValue(STy->getElementType(i));
    else
      CurOp = cast<llvm::Constant>(constant->getAggregateElement(i));
    auto *NewOp = constWithPadding(CGM, isPattern, CurOp);
    if (CurOp != NewOp)
      NestedIntact = false;
    Values.push_back(NewOp);
    SizeSoFar = CurOff + DL.getTypeAllocSize(CurOp->getType());
  }
  unsigned TotalSize = Layout->getSizeInBytes();
  if (SizeSoFar < TotalSize) {
    auto *PadTy = llvm::ArrayType::get(Int8Ty, TotalSize - SizeSoFar);
    Values.push_back(patternOrZeroFor(CGM, isPattern, PadTy));
  }
  if (NestedIntact && Values.size() == STy->getNumElements())
    return constant;
  return llvm::ConstantStruct::getAnon(Values, STy->isPacked());
}

/// Replace all padding bytes in a given constant with either a pattern byte or
/// 0x00.
static llvm::Constant *constWithPadding(CodeGenModule &CGM, IsPattern isPattern,
                                        llvm::Constant *constant) {
  llvm::Type *OrigTy = constant->getType();
  if (const auto STy = dyn_cast<llvm::StructType>(OrigTy))
    return constStructWithPadding(CGM, isPattern, STy, constant);
  if (auto *ArrayTy = dyn_cast<llvm::ArrayType>(OrigTy)) {
    llvm::SmallVector<llvm::Constant *, 8> Values;
    uint64_t Size = ArrayTy->getNumElements();
    if (!Size)
      return constant;
    llvm::Type *ElemTy = ArrayTy->getElementType();
    bool ZeroInitializer = constant->isNullValue();
    llvm::Constant *OpValue, *PaddedOp;
    if (ZeroInitializer) {
      OpValue = llvm::Constant::getNullValue(ElemTy);
      PaddedOp = constWithPadding(CGM, isPattern, OpValue);
    }
    for (unsigned Op = 0; Op != Size; ++Op) {
      if (!ZeroInitializer) {
        OpValue = constant->getAggregateElement(Op);
        PaddedOp = constWithPadding(CGM, isPattern, OpValue);
      }
      Values.push_back(PaddedOp);
    }
    auto *NewElemTy = Values[0]->getType();
    if (NewElemTy == ElemTy)
      return constant;
    auto *NewArrayTy = llvm::ArrayType::get(NewElemTy, Size);
    return llvm::ConstantArray::get(NewArrayTy, Values);
  }
  // FIXME: Add handling for tail padding in vectors. Vectors don't
  // have padding between or inside elements, but the total amount of
  // data can be less than the allocated size.
  return constant;
}

Address CodeGenModule::createUnnamedGlobalFrom(const VarDecl &D,
                                               llvm::Constant *Constant,
                                               CharUnits Align) {
  auto FunctionName = [&](const DeclContext *DC) -> std::string {
    if (const auto *FD = dyn_cast<FunctionDecl>(DC)) {
      if (const auto *CC = dyn_cast<CXXConstructorDecl>(FD))
        return CC->getNameAsString();
      if (const auto *CD = dyn_cast<CXXDestructorDecl>(FD))
        return CD->getNameAsString();
      return std::string(getMangledName(FD));
    } else if (const auto *OM = dyn_cast<ObjCMethodDecl>(DC)) {
      return OM->getNameAsString();
    } else if (isa<BlockDecl>(DC)) {
      return "<block>";
    } else if (isa<CapturedDecl>(DC)) {
      return "<captured>";
    } else {
      llvm_unreachable("expected a function or method");
    }
  };

  // Form a simple per-variable cache of these values in case we find we
  // want to reuse them.
  llvm::GlobalVariable *&CacheEntry = InitializerConstants[&D];
  if (!CacheEntry || CacheEntry->getInitializer() != Constant) {
    auto *Ty = Constant->getType();
    bool isConstant = true;
    llvm::GlobalVariable *InsertBefore = nullptr;
    unsigned AS =
        getContext().getTargetAddressSpace(GetGlobalConstantAddressSpace());
    std::string Name;
    if (D.hasGlobalStorage())
      Name = getMangledName(&D).str() + ".const";
    else if (const DeclContext *DC = D.getParentFunctionOrMethod())
      Name = ("__const." + FunctionName(DC) + "." + D.getName()).str();
    else
      llvm_unreachable("local variable has no parent function or method");
    llvm::GlobalVariable *GV = new llvm::GlobalVariable(
        getModule(), Ty, isConstant, llvm::GlobalValue::PrivateLinkage,
        Constant, Name, InsertBefore, llvm::GlobalValue::NotThreadLocal, AS);
    GV->setAlignment(Align.getAsAlign());
    GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    CacheEntry = GV;
  } else if (CacheEntry->getAlignment() < uint64_t(Align.getQuantity())) {
    CacheEntry->setAlignment(Align.getAsAlign());
  }

  return Address(CacheEntry, CacheEntry->getValueType(), Align);
}

static Address createUnnamedGlobalForMemcpyFrom(CodeGenModule &CGM,
                                                const VarDecl &D,
                                                CGBuilderTy &Builder,
                                                llvm::Constant *Constant,
                                                CharUnits Align) {
  Address SrcPtr = CGM.createUnnamedGlobalFrom(D, Constant, Align);
  return SrcPtr.withElementType(CGM.Int8Ty);
}

static void emitStoresForConstant(CodeGenModule &CGM, const VarDecl &D,
                                  Address Loc, bool isVolatile,
                                  CGBuilderTy &Builder,
                                  llvm::Constant *constant, bool IsAutoInit) {
  auto *Ty = constant->getType();
  uint64_t ConstantSize = CGM.getDataLayout().getTypeAllocSize(Ty);
  if (!ConstantSize)
    return;

  bool canDoSingleStore = Ty->isIntOrIntVectorTy() ||
                          Ty->isPtrOrPtrVectorTy() || Ty->isFPOrFPVectorTy();
  if (canDoSingleStore) {
    auto *I = Builder.CreateStore(constant, Loc, isVolatile);
    if (IsAutoInit)
      I->addAnnotationMetadata("auto-init");
    return;
  }

  auto *SizeVal = llvm::ConstantInt::get(CGM.IntPtrTy, ConstantSize);

  // If the initializer is all or mostly the same, codegen with bzero / memset
  // then do a few stores afterward.
  if (shouldUseBZeroPlusStoresToInitialize(constant, ConstantSize)) {
    auto *I = Builder.CreateMemSet(Loc, llvm::ConstantInt::get(CGM.Int8Ty, 0),
                                   SizeVal, isVolatile);
    if (IsAutoInit)
      I->addAnnotationMetadata("auto-init");

    bool valueAlreadyCorrect =
        constant->isNullValue() || isa<llvm::UndefValue>(constant);
    if (!valueAlreadyCorrect) {
      Loc = Loc.withElementType(Ty);
      emitStoresForInitAfterBZero(CGM, constant, Loc, isVolatile, Builder,
                                  IsAutoInit);
    }
    return;
  }

  // If the initializer is a repeated byte pattern, use memset.
  llvm::Value *Pattern =
      shouldUseMemSetToInitialize(constant, ConstantSize, CGM.getDataLayout());
  if (Pattern) {
    uint64_t Value = 0x00;
    if (!isa<llvm::UndefValue>(Pattern)) {
      const llvm::APInt &AP = cast<llvm::ConstantInt>(Pattern)->getValue();
      assert(AP.getBitWidth() <= 8);
      Value = AP.getLimitedValue();
    }
    auto *I = Builder.CreateMemSet(
        Loc, llvm::ConstantInt::get(CGM.Int8Ty, Value), SizeVal, isVolatile);
    if (IsAutoInit)
      I->addAnnotationMetadata("auto-init");
    return;
  }

  // If the initializer is small or trivialAutoVarInit is set, use a handful of
  // stores.
  bool IsTrivialAutoVarInitPattern =
      CGM.getContext().getLangOpts().getTrivialAutoVarInit() ==
      LangOptions::TrivialAutoVarInitKind::Pattern;
  if (shouldSplitConstantStore(CGM, ConstantSize)) {
    if (auto *STy = dyn_cast<llvm::StructType>(Ty)) {
      if (STy == Loc.getElementType() ||
          (STy != Loc.getElementType() && IsTrivialAutoVarInitPattern)) {
        const llvm::StructLayout *Layout =
            CGM.getDataLayout().getStructLayout(STy);
        for (unsigned i = 0; i != constant->getNumOperands(); i++) {
          CharUnits CurOff =
              CharUnits::fromQuantity(Layout->getElementOffset(i));
          Address EltPtr = Builder.CreateConstInBoundsByteGEP(
              Loc.withElementType(CGM.Int8Ty), CurOff);
          emitStoresForConstant(CGM, D, EltPtr, isVolatile, Builder,
                                constant->getAggregateElement(i), IsAutoInit);
        }
        return;
      }
    } else if (auto *ATy = dyn_cast<llvm::ArrayType>(Ty)) {
      if (ATy == Loc.getElementType() ||
          (ATy != Loc.getElementType() && IsTrivialAutoVarInitPattern)) {
        for (unsigned i = 0; i != ATy->getNumElements(); i++) {
          Address EltPtr = Builder.CreateConstGEP(
              Loc.withElementType(ATy->getElementType()), i);
          emitStoresForConstant(CGM, D, EltPtr, isVolatile, Builder,
                                constant->getAggregateElement(i), IsAutoInit);
        }
        return;
      }
    }
  }

  // Copy from a global.
  auto *I =
      Builder.CreateMemCpy(Loc,
                           createUnnamedGlobalForMemcpyFrom(
                               CGM, D, Builder, constant, Loc.getAlignment()),
                           SizeVal, isVolatile);
  if (IsAutoInit)
    I->addAnnotationMetadata("auto-init");
}

static void emitStoresForZeroInit(CodeGenModule &CGM, const VarDecl &D,
                                  Address Loc, bool isVolatile,
                                  CGBuilderTy &Builder) {
  llvm::Type *ElTy = Loc.getElementType();
  llvm::Constant *constant =
      constWithPadding(CGM, IsPattern::No, llvm::Constant::getNullValue(ElTy));
  emitStoresForConstant(CGM, D, Loc, isVolatile, Builder, constant,
                        /*IsAutoInit=*/true);
}

static void emitStoresForPatternInit(CodeGenModule &CGM, const VarDecl &D,
                                     Address Loc, bool isVolatile,
                                     CGBuilderTy &Builder) {
  llvm::Type *ElTy = Loc.getElementType();
  llvm::Constant *constant = constWithPadding(
      CGM, IsPattern::Yes, initializationPatternFor(CGM, ElTy));
  assert(!isa<llvm::UndefValue>(constant));
  emitStoresForConstant(CGM, D, Loc, isVolatile, Builder, constant,
                        /*IsAutoInit=*/true);
}

static bool containsUndef(llvm::Constant *constant) {
  auto *Ty = constant->getType();
  if (isa<llvm::UndefValue>(constant))
    return true;
  if (Ty->isStructTy() || Ty->isArrayTy() || Ty->isVectorTy())
    for (llvm::Use &Op : constant->operands())
      if (containsUndef(cast<llvm::Constant>(Op)))
        return true;
  return false;
}

static llvm::Constant *replaceUndef(CodeGenModule &CGM, IsPattern isPattern,
                                    llvm::Constant *constant) {
  auto *Ty = constant->getType();
  if (isa<llvm::UndefValue>(constant))
    return patternOrZeroFor(CGM, isPattern, Ty);
  if (!(Ty->isStructTy() || Ty->isArrayTy() || Ty->isVectorTy()))
    return constant;
  if (!containsUndef(constant))
    return constant;
  llvm::SmallVector<llvm::Constant *, 8> Values(constant->getNumOperands());
  for (unsigned Op = 0, NumOp = constant->getNumOperands(); Op != NumOp; ++Op) {
    auto *OpValue = cast<llvm::Constant>(constant->getOperand(Op));
    Values[Op] = replaceUndef(CGM, isPattern, OpValue);
  }
  if (Ty->isStructTy())
    return llvm::ConstantStruct::get(cast<llvm::StructType>(Ty), Values);
  if (Ty->isArrayTy())
    return llvm::ConstantArray::get(cast<llvm::ArrayType>(Ty), Values);
  assert(Ty->isVectorTy());
  return llvm::ConstantVector::get(Values);
}

/// EmitAutoVarDecl - Emit code and set up an entry in LocalDeclMap for a
/// variable declaration with auto, register, or no storage class specifier.
/// These turn into simple stack objects, or GlobalValues depending on target.
void CodeGenFunction::EmitAutoVarDecl(const VarDecl &D) {
  AutoVarEmission emission = EmitAutoVarAlloca(D);
  EmitAutoVarInit(emission);
  EmitAutoVarCleanups(emission);
}

/// Emit a lifetime.begin marker if some criteria are satisfied.
/// \return a pointer to the temporary size Value if a marker was emitted, null
/// otherwise
llvm::Value *CodeGenFunction::EmitLifetimeStart(llvm::TypeSize Size,
                                                llvm::Value *Addr) {
  if (!ShouldEmitLifetimeMarkers)
    return nullptr;

  assert(Addr->getType()->getPointerAddressSpace() ==
             CGM.getDataLayout().getAllocaAddrSpace() &&
         "Pointer should be in alloca address space");
  llvm::Value *SizeV = llvm::ConstantInt::get(
      Int64Ty, Size.isScalable() ? -1 : Size.getFixedValue());
  llvm::CallInst *C =
      Builder.CreateCall(CGM.getLLVMLifetimeStartFn(), {SizeV, Addr});
  C->setDoesNotThrow();
  return SizeV;
}

void CodeGenFunction::EmitLifetimeEnd(llvm::Value *Size, llvm::Value *Addr) {
  assert(Addr->getType()->getPointerAddressSpace() ==
             CGM.getDataLayout().getAllocaAddrSpace() &&
         "Pointer should be in alloca address space");
  llvm::CallInst *C =
      Builder.CreateCall(CGM.getLLVMLifetimeEndFn(), {Size, Addr});
  C->setDoesNotThrow();
}

void CodeGenFunction::EmitAndRegisterVariableArrayDimensions(
    CGDebugInfo *DI, const VarDecl &D, bool EmitDebugInfo) {
  // For each dimension stores its QualType and corresponding
  // size-expression Value.
  SmallVector<CodeGenFunction::VlaSizePair, 4> Dimensions;
  SmallVector<const IdentifierInfo *, 4> VLAExprNames;

  // Break down the array into individual dimensions.
  QualType Type1D = D.getType();
  while (getContext().getAsVariableArrayType(Type1D)) {
    auto VlaSize = getVLAElements1D(Type1D);
    if (auto *C = dyn_cast<llvm::ConstantInt>(VlaSize.NumElts))
      Dimensions.emplace_back(C, Type1D.getUnqualifiedType());
    else {
      // Generate a locally unique name for the size expression.
      Twine Name = Twine("__vla_expr") + Twine(VLAExprCounter++);
      SmallString<12> Buffer;
      StringRef NameRef = Name.toStringRef(Buffer);
      auto &Ident = getContext().Idents.getOwn(NameRef);
      VLAExprNames.push_back(&Ident);
      auto SizeExprAddr =
          CreateDefaultAlignTempAlloca(VlaSize.NumElts->getType(), NameRef);
      Builder.CreateStore(VlaSize.NumElts, SizeExprAddr);
      Dimensions.emplace_back(SizeExprAddr.getPointer(),
                              Type1D.getUnqualifiedType());
    }
    Type1D = VlaSize.Type;
  }

  if (!EmitDebugInfo)
    return;

  // Register each dimension's size-expression with a DILocalVariable,
  // so that it can be used by CGDebugInfo when instantiating a DISubrange
  // to describe this array.
  unsigned NameIdx = 0;
  for (auto &VlaSize : Dimensions) {
    llvm::Metadata *MD;
    if (auto *C = dyn_cast<llvm::ConstantInt>(VlaSize.NumElts))
      MD = llvm::ConstantAsMetadata::get(C);
    else {
      // Create an artificial VarDecl to generate debug info for.
      const IdentifierInfo *NameIdent = VLAExprNames[NameIdx++];
      auto QT = getContext().getIntTypeForBitwidth(
          SizeTy->getScalarSizeInBits(), false);
      auto *ArtificialDecl = VarDecl::Create(
          getContext(), const_cast<DeclContext *>(D.getDeclContext()),
          D.getLocation(), D.getLocation(), NameIdent, QT,
          getContext().CreateTypeSourceInfo(QT), SC_Auto);
      ArtificialDecl->setImplicit();

      MD = DI->EmitDeclareOfAutoVariable(ArtificialDecl, VlaSize.NumElts,
                                         Builder);
    }
    assert(MD && "No Size expression debug node created");
    DI->registerVLASizeExpression(VlaSize.Type, MD);
  }
}

/// EmitAutoVarAlloca - Emit the alloca and debug information for a
/// local variable.  Does not emit initialization or destruction.
CodeGenFunction::AutoVarEmission
CodeGenFunction::EmitAutoVarAlloca(const VarDecl &D) {
  QualType Ty = D.getType();
  assert(
      Ty.getAddressSpace() == LangAS::Default ||
      (Ty.getAddressSpace() == LangAS::opencl_private && getLangOpts().OpenCL));

  AutoVarEmission emission(D);

  bool isEscapingByRef = D.isEscapingByref();
  emission.IsEscapingByRef = isEscapingByRef;

  CharUnits alignment = getContext().getDeclAlign(&D);

  // If the type is variably-modified, emit all the VLA sizes for it.
  if (Ty->isVariablyModifiedType())
    EmitVariablyModifiedType(Ty);

  auto *DI = getDebugInfo();
  bool EmitDebugInfo = DI && CGM.getCodeGenOpts().hasReducedDebugInfo();

  Address address = Address::invalid();
  RawAddress AllocaAddr = RawAddress::invalid();
  Address OpenMPLocalAddr = Address::invalid();
  if (CGM.getLangOpts().OpenMPIRBuilder)
    OpenMPLocalAddr = OMPBuilderCBHelpers::getAddressOfLocalVariable(*this, &D);
  else
    OpenMPLocalAddr =
        getLangOpts().OpenMP
            ? CGM.getOpenMPRuntime().getAddressOfLocalVariable(*this, &D)
            : Address::invalid();

  bool NRVO = getLangOpts().ElideConstructors && D.isNRVOVariable();

  if (getLangOpts().OpenMP && OpenMPLocalAddr.isValid()) {
    address = OpenMPLocalAddr;
    AllocaAddr = OpenMPLocalAddr;
  } else if (Ty->isConstantSizeType()) {
    // If this value is an array or struct with a statically determinable
    // constant initializer, there are optimizations we can do.
    //
    // TODO: We should constant-evaluate the initializer of any variable,
    // as long as it is initialized by a constant expression. Currently,
    // isConstantInitializer produces wrong answers for structs with
    // reference or bitfield members, and a few other cases, and checking
    // for POD-ness protects us from some of these.
    if (D.getInit() && (Ty->isArrayType() || Ty->isRecordType()) &&
        (D.isConstexpr() ||
         ((Ty.isPODType(getContext()) ||
           getContext().getBaseElementType(Ty)->isObjCObjectPointerType()) &&
          D.getInit()->isConstantInitializer(getContext(), false)))) {

      // If the variable's a const type, and it's neither an NRVO
      // candidate nor a __block variable and has no mutable members,
      // emit it as a global instead.
      // Exception is if a variable is located in non-constant address space
      // in OpenCL.
      bool NeedsDtor =
          D.needsDestruction(getContext()) == QualType::DK_cxx_destructor;
      if ((!getLangOpts().OpenCL ||
           Ty.getAddressSpace() == LangAS::opencl_constant) &&
          (CGM.getCodeGenOpts().MergeAllConstants && !NRVO &&
           !isEscapingByRef &&
           Ty.isConstantStorage(getContext(), true, !NeedsDtor))) {
        EmitStaticVarDecl(D, llvm::GlobalValue::InternalLinkage);

        // Signal this condition to later callbacks.
        emission.Addr = Address::invalid();
        assert(emission.wasEmittedAsGlobal());
        return emission;
      }

      // Otherwise, tell the initialization code that we're in this case.
      emission.IsConstantAggregate = true;
    }

    // A normal fixed sized variable becomes an alloca in the entry block,
    // unless:
    // - it's an NRVO variable.
    // - we are compiling OpenMP and it's an OpenMP local variable.
    if (NRVO) {
      // The named return value optimization: allocate this variable in the
      // return slot, so that we can elide the copy when returning this
      // variable (C++0x [class.copy]p34).
      address = ReturnValue;
      AllocaAddr =
          RawAddress(ReturnValue.emitRawPointer(*this),
                     ReturnValue.getElementType(), ReturnValue.getAlignment());
      ;

      if (const RecordType *RecordTy = Ty->getAs<RecordType>()) {
        const auto *RD = RecordTy->getDecl();
        const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD);
        if ((CXXRD && !CXXRD->hasTrivialDestructor()) ||
            RD->isNonTrivialToPrimitiveDestroy()) {
          // Create a flag that is used to indicate when the NRVO was applied
          // to this variable. Set it to zero to indicate that NRVO was not
          // applied.
          llvm::Value *Zero = Builder.getFalse();
          RawAddress NRVOFlag =
              CreateTempAlloca(Zero->getType(), CharUnits::One(), "nrvo");
          EnsureInsertPoint();
          Builder.CreateStore(Zero, NRVOFlag);

          // Record the NRVO flag for this variable.
          NRVOFlags[&D] = NRVOFlag.getPointer();
          emission.NRVOFlag = NRVOFlag.getPointer();
        }
      }
    } else {
      CharUnits allocaAlignment;
      llvm::Type *allocaTy;
      if (isEscapingByRef) {
        auto &byrefInfo = getBlockByrefInfo(&D);
        allocaTy = byrefInfo.Type;
        allocaAlignment = byrefInfo.ByrefAlignment;
      } else {
        allocaTy = ConvertTypeForMem(Ty);
        allocaAlignment = alignment;
      }

      // Create the alloca.  Note that we set the name separately from
      // building the instruction so that it's there even in no-asserts
      // builds.
      address = CreateTempAlloca(allocaTy, allocaAlignment, D.getName(),
                                 /*ArraySize=*/nullptr, &AllocaAddr);

      // Don't emit lifetime markers for MSVC catch parameters. The lifetime of
      // the catch parameter starts in the catchpad instruction, and we can't
      // insert code in those basic blocks.
      bool IsMSCatchParam =
          D.isExceptionVariable() && getTarget().getCXXABI().isMicrosoft();

      // Emit a lifetime intrinsic if meaningful. There's no point in doing this
      // if we don't have a valid insertion point (?).
      if (HaveInsertPoint() && !IsMSCatchParam) {
        // If there's a jump into the lifetime of this variable, its lifetime
        // gets broken up into several regions in IR, which requires more work
        // to handle correctly. For now, just omit the intrinsics; this is a
        // rare case, and it's better to just be conservatively correct.
        // PR28267.
        //
        // We have to do this in all language modes if there's a jump past the
        // declaration. We also have to do it in C if there's a jump to an
        // earlier point in the current block because non-VLA lifetimes begin as
        // soon as the containing block is entered, not when its variables
        // actually come into scope; suppressing the lifetime annotations
        // completely in this case is unnecessarily pessimistic, but again, this
        // is rare.
        if (!Bypasses.IsBypassed(&D) &&
            !(!getLangOpts().CPlusPlus && hasLabelBeenSeenInCurrentScope())) {
          llvm::TypeSize Size = CGM.getDataLayout().getTypeAllocSize(allocaTy);
          emission.SizeForLifetimeMarkers =
              EmitLifetimeStart(Size, AllocaAddr.getPointer());
        }
      } else {
        assert(!emission.useLifetimeMarkers());
      }
    }
  } else {
    EnsureInsertPoint();

    // Delayed globalization for variable length declarations. This ensures that
    // the expression representing the length has been emitted and can be used
    // by the definition of the VLA. Since this is an escaped declaration, in
    // OpenMP we have to use a call to __kmpc_alloc_shared(). The matching
    // deallocation call to __kmpc_free_shared() is emitted later.
    bool VarAllocated = false;
    if (getLangOpts().OpenMPIsTargetDevice) {
      auto &RT = CGM.getOpenMPRuntime();
      if (RT.isDelayedVariableLengthDecl(*this, &D)) {
        // Emit call to __kmpc_alloc_shared() instead of the alloca.
        std::pair<llvm::Value *, llvm::Value *> AddrSizePair =
            RT.getKmpcAllocShared(*this, &D);

        // Save the address of the allocation:
        LValue Base = MakeAddrLValue(AddrSizePair.first, D.getType(),
                                     CGM.getContext().getDeclAlign(&D),
                                     AlignmentSource::Decl);
        address = Base.getAddress();

        // Push a cleanup block to emit the call to __kmpc_free_shared in the
        // appropriate location at the end of the scope of the
        // __kmpc_alloc_shared functions:
        pushKmpcAllocFree(NormalCleanup, AddrSizePair);

        // Mark variable as allocated:
        VarAllocated = true;
      }
    }

    if (!VarAllocated) {
      if (!DidCallStackSave) {
        // Save the stack.
        Address Stack =
            CreateDefaultAlignTempAlloca(AllocaInt8PtrTy, "saved_stack");

        llvm::Value *V = Builder.CreateStackSave();
        assert(V->getType() == AllocaInt8PtrTy);
        Builder.CreateStore(V, Stack);

        DidCallStackSave = true;

        // Push a cleanup block and restore the stack there.
        // FIXME: in general circumstances, this should be an EH cleanup.
        pushStackRestore(NormalCleanup, Stack);
      }

      auto VlaSize = getVLASize(Ty);
      llvm::Type *llvmTy = ConvertTypeForMem(VlaSize.Type);

      // Allocate memory for the array.
      address = CreateTempAlloca(llvmTy, alignment, "vla", VlaSize.NumElts,
                                 &AllocaAddr);
    }

    // If we have debug info enabled, properly describe the VLA dimensions for
    // this type by registering the vla size expression for each of the
    // dimensions.
    EmitAndRegisterVariableArrayDimensions(DI, D, EmitDebugInfo);
  }

  setAddrOfLocalVar(&D, address);
  emission.Addr = address;
  emission.AllocaAddr = AllocaAddr;

  // Emit debug info for local var declaration.
  if (EmitDebugInfo && HaveInsertPoint()) {
    Address DebugAddr = address;
    bool UsePointerValue = NRVO && ReturnValuePointer.isValid();
    DI->setLocation(D.getLocation());

    // If NRVO, use a pointer to the return address.
    if (UsePointerValue) {
      DebugAddr = ReturnValuePointer;
      AllocaAddr = ReturnValuePointer;
    }
    (void)DI->EmitDeclareOfAutoVariable(&D, AllocaAddr.getPointer(), Builder,
                                        UsePointerValue);
  }

  if (D.hasAttr<AnnotateAttr>() && HaveInsertPoint())
    EmitVarAnnotations(&D, address.emitRawPointer(*this));

  // Make sure we call @llvm.lifetime.end.
  if (emission.useLifetimeMarkers())
    EHStack.pushCleanup<CallLifetimeEnd>(NormalEHLifetimeMarker,
                                         emission.getOriginalAllocatedAddress(),
                                         emission.getSizeForLifetimeMarkers());

  return emission;
}

static bool isCapturedBy(const VarDecl &, const Expr *);

/// Determines whether the given __block variable is potentially
/// captured by the given statement.
static bool isCapturedBy(const VarDecl &Var, const Stmt *S) {
  if (const Expr *E = dyn_cast<Expr>(S))
    return isCapturedBy(Var, E);
  for (const Stmt *SubStmt : S->children())
    if (isCapturedBy(Var, SubStmt))
      return true;
  return false;
}

/// Determines whether the given __block variable is potentially
/// captured by the given expression.
static bool isCapturedBy(const VarDecl &Var, const Expr *E) {
  // Skip the most common kinds of expressions that make
  // hierarchy-walking expensive.
  E = E->IgnoreParenCasts();

  if (const BlockExpr *BE = dyn_cast<BlockExpr>(E)) {
    const BlockDecl *Block = BE->getBlockDecl();
    for (const auto &I : Block->captures()) {
      if (I.getVariable() == &Var)
        return true;
    }

    // No need to walk into the subexpressions.
    return false;
  }

  if (const StmtExpr *SE = dyn_cast<StmtExpr>(E)) {
    const CompoundStmt *CS = SE->getSubStmt();
    for (const auto *BI : CS->body())
      if (const auto *BIE = dyn_cast<Expr>(BI)) {
        if (isCapturedBy(Var, BIE))
          return true;
      }
      else if (const auto *DS = dyn_cast<DeclStmt>(BI)) {
          // special case declarations
          for (const auto *I : DS->decls()) {
              if (const auto *VD = dyn_cast<VarDecl>((I))) {
                const Expr *Init = VD->getInit();
                if (Init && isCapturedBy(Var, Init))
                  return true;
              }
          }
      }
      else
        // FIXME. Make safe assumption assuming arbitrary statements cause capturing.
        // Later, provide code to poke into statements for capture analysis.
        return true;
    return false;
  }

  for (const Stmt *SubStmt : E->children())
    if (isCapturedBy(Var, SubStmt))
      return true;

  return false;
}

/// Determine whether the given initializer is trivial in the sense
/// that it requires no code to be generated.
bool CodeGenFunction::isTrivialInitializer(const Expr *Init) {
  if (!Init)
    return true;

  if (const CXXConstructExpr *Construct = dyn_cast<CXXConstructExpr>(Init))
    if (CXXConstructorDecl *Constructor = Construct->getConstructor())
      if (Constructor->isTrivial() &&
          Constructor->isDefaultConstructor() &&
          !Construct->requiresZeroInitialization())
        return true;

  return false;
}

void CodeGenFunction::emitZeroOrPatternForAutoVarInit(QualType type,
                                                      const VarDecl &D,
                                                      Address Loc) {
  auto trivialAutoVarInit = getContext().getLangOpts().getTrivialAutoVarInit();
  auto trivialAutoVarInitMaxSize =
      getContext().getLangOpts().TrivialAutoVarInitMaxSize;
  CharUnits Size = getContext().getTypeSizeInChars(type);
  bool isVolatile = type.isVolatileQualified();
  if (!Size.isZero()) {
    // We skip auto-init variables by their alloc size. Take this as an example:
    // "struct Foo {int x; char buff[1024];}" Assume the max-size flag is 1023.
    // All Foo type variables will be skipped. Ideally, we only skip the buff
    // array and still auto-init X in this example.
    // TODO: Improve the size filtering to by member size.
    auto allocSize = CGM.getDataLayout().getTypeAllocSize(Loc.getElementType());
    switch (trivialAutoVarInit) {
    case LangOptions::TrivialAutoVarInitKind::Uninitialized:
      llvm_unreachable("Uninitialized handled by caller");
    case LangOptions::TrivialAutoVarInitKind::Zero:
      if (CGM.stopAutoInit())
        return;
      if (trivialAutoVarInitMaxSize > 0 &&
          allocSize > trivialAutoVarInitMaxSize)
        return;
      emitStoresForZeroInit(CGM, D, Loc, isVolatile, Builder);
      break;
    case LangOptions::TrivialAutoVarInitKind::Pattern:
      if (CGM.stopAutoInit())
        return;
      if (trivialAutoVarInitMaxSize > 0 &&
          allocSize > trivialAutoVarInitMaxSize)
        return;
      emitStoresForPatternInit(CGM, D, Loc, isVolatile, Builder);
      break;
    }
    return;
  }

  // VLAs look zero-sized to getTypeInfo. We can't emit constant stores to
  // them, so emit a memcpy with the VLA size to initialize each element.
  // Technically zero-sized or negative-sized VLAs are undefined, and UBSan
  // will catch that code, but there exists code which generates zero-sized
  // VLAs. Be nice and initialize whatever they requested.
  const auto *VlaType = getContext().getAsVariableArrayType(type);
  if (!VlaType)
    return;
  auto VlaSize = getVLASize(VlaType);
  auto SizeVal = VlaSize.NumElts;
  CharUnits EltSize = getContext().getTypeSizeInChars(VlaSize.Type);
  switch (trivialAutoVarInit) {
  case LangOptions::TrivialAutoVarInitKind::Uninitialized:
    llvm_unreachable("Uninitialized handled by caller");

  case LangOptions::TrivialAutoVarInitKind::Zero: {
    if (CGM.stopAutoInit())
      return;
    if (!EltSize.isOne())
      SizeVal = Builder.CreateNUWMul(SizeVal, CGM.getSize(EltSize));
    auto *I = Builder.CreateMemSet(Loc, llvm::ConstantInt::get(Int8Ty, 0),
                                   SizeVal, isVolatile);
    I->addAnnotationMetadata("auto-init");
    break;
  }

  case LangOptions::TrivialAutoVarInitKind::Pattern: {
    if (CGM.stopAutoInit())
      return;
    llvm::Type *ElTy = Loc.getElementType();
    llvm::Constant *Constant = constWithPadding(
        CGM, IsPattern::Yes, initializationPatternFor(CGM, ElTy));
    CharUnits ConstantAlign = getContext().getTypeAlignInChars(VlaSize.Type);
    llvm::BasicBlock *SetupBB = createBasicBlock("vla-setup.loop");
    llvm::BasicBlock *LoopBB = createBasicBlock("vla-init.loop");
    llvm::BasicBlock *ContBB = createBasicBlock("vla-init.cont");
    llvm::Value *IsZeroSizedVLA = Builder.CreateICmpEQ(
        SizeVal, llvm::ConstantInt::get(SizeVal->getType(), 0),
        "vla.iszerosized");
    Builder.CreateCondBr(IsZeroSizedVLA, ContBB, SetupBB);
    EmitBlock(SetupBB);
    if (!EltSize.isOne())
      SizeVal = Builder.CreateNUWMul(SizeVal, CGM.getSize(EltSize));
    llvm::Value *BaseSizeInChars =
        llvm::ConstantInt::get(IntPtrTy, EltSize.getQuantity());
    Address Begin = Loc.withElementType(Int8Ty);
    llvm::Value *End = Builder.CreateInBoundsGEP(Begin.getElementType(),
                                                 Begin.emitRawPointer(*this),
                                                 SizeVal, "vla.end");
    llvm::BasicBlock *OriginBB = Builder.GetInsertBlock();
    EmitBlock(LoopBB);
    llvm::PHINode *Cur = Builder.CreatePHI(Begin.getType(), 2, "vla.cur");
    Cur->addIncoming(Begin.emitRawPointer(*this), OriginBB);
    CharUnits CurAlign = Loc.getAlignment().alignmentOfArrayElement(EltSize);
    auto *I =
        Builder.CreateMemCpy(Address(Cur, Int8Ty, CurAlign),
                             createUnnamedGlobalForMemcpyFrom(
                                 CGM, D, Builder, Constant, ConstantAlign),
                             BaseSizeInChars, isVolatile);
    I->addAnnotationMetadata("auto-init");
    llvm::Value *Next =
        Builder.CreateInBoundsGEP(Int8Ty, Cur, BaseSizeInChars, "vla.next");
    llvm::Value *Done = Builder.CreateICmpEQ(Next, End, "vla-init.isdone");
    Builder.CreateCondBr(Done, ContBB, LoopBB);
    Cur->addIncoming(Next, LoopBB);
    EmitBlock(ContBB);
  } break;
  }
}

void CodeGenFunction::EmitAutoVarInit(const AutoVarEmission &emission) {
  assert(emission.Variable && "emission was not valid!");

  // If this was emitted as a global constant, we're done.
  if (emission.wasEmittedAsGlobal()) return;

  const VarDecl &D = *emission.Variable;
  auto DL = ApplyDebugLocation::CreateDefaultArtificial(*this, D.getLocation());
  QualType type = D.getType();

  // If this local has an initializer, emit it now.
  const Expr *Init = D.getInit();

  // If we are at an unreachable point, we don't need to emit the initializer
  // unless it contains a label.
  if (!HaveInsertPoint()) {
    if (!Init || !ContainsLabel(Init)) return;
    EnsureInsertPoint();
  }

  // Initialize the structure of a __block variable.
  if (emission.IsEscapingByRef)
    emitByrefStructureInit(emission);

  // Initialize the variable here if it doesn't have a initializer and it is a
  // C struct that is non-trivial to initialize or an array containing such a
  // struct.
  if (!Init &&
      type.isNonTrivialToPrimitiveDefaultInitialize() ==
          QualType::PDIK_Struct) {
    LValue Dst = MakeAddrLValue(emission.getAllocatedAddress(), type);
    if (emission.IsEscapingByRef)
      drillIntoBlockVariable(*this, Dst, &D);
    defaultInitNonTrivialCStructVar(Dst);
    return;
  }

  // Check whether this is a byref variable that's potentially
  // captured and moved by its own initializer.  If so, we'll need to
  // emit the initializer first, then copy into the variable.
  bool capturedByInit =
      Init && emission.IsEscapingByRef && isCapturedBy(D, Init);

  bool locIsByrefHeader = !capturedByInit;
  const Address Loc =
      locIsByrefHeader ? emission.getObjectAddress(*this) : emission.Addr;

  // Note: constexpr already initializes everything correctly.
  LangOptions::TrivialAutoVarInitKind trivialAutoVarInit =
      (D.isConstexpr()
           ? LangOptions::TrivialAutoVarInitKind::Uninitialized
           : (D.getAttr<UninitializedAttr>()
                  ? LangOptions::TrivialAutoVarInitKind::Uninitialized
                  : getContext().getLangOpts().getTrivialAutoVarInit()));

  auto initializeWhatIsTechnicallyUninitialized = [&](Address Loc) {
    if (trivialAutoVarInit ==
        LangOptions::TrivialAutoVarInitKind::Uninitialized)
      return;

    // Only initialize a __block's storage: we always initialize the header.
    if (emission.IsEscapingByRef && !locIsByrefHeader)
      Loc = emitBlockByrefAddress(Loc, &D, /*follow=*/false);

    return emitZeroOrPatternForAutoVarInit(type, D, Loc);
  };

  if (isTrivialInitializer(Init))
    return initializeWhatIsTechnicallyUninitialized(Loc);

  llvm::Constant *constant = nullptr;
  if (emission.IsConstantAggregate ||
      D.mightBeUsableInConstantExpressions(getContext())) {
    assert(!capturedByInit && "constant init contains a capturing block?");
    constant = ConstantEmitter(*this).tryEmitAbstractForInitializer(D);
    if (constant && !constant->isZeroValue() &&
        (trivialAutoVarInit !=
         LangOptions::TrivialAutoVarInitKind::Uninitialized)) {
      IsPattern isPattern =
          (trivialAutoVarInit == LangOptions::TrivialAutoVarInitKind::Pattern)
              ? IsPattern::Yes
              : IsPattern::No;
      // C guarantees that brace-init with fewer initializers than members in
      // the aggregate will initialize the rest of the aggregate as-if it were
      // static initialization. In turn static initialization guarantees that
      // padding is initialized to zero bits. We could instead pattern-init if D
      // has any ImplicitValueInitExpr, but that seems to be unintuitive
      // behavior.
      constant = constWithPadding(CGM, IsPattern::No,
                                  replaceUndef(CGM, isPattern, constant));
    }

    if (D.getType()->isBitIntType() &&
        CGM.getTypes().typeRequiresSplitIntoByteArray(D.getType())) {
      // Constants for long _BitInt types are split into individual bytes.
      // Try to fold these back into an integer constant so it can be stored
      // properly.
      llvm::Type *LoadType = CGM.getTypes().convertTypeForLoadStore(
          D.getType(), constant->getType());
      constant = llvm::ConstantFoldLoadFromConst(
          constant, LoadType, llvm::APInt::getZero(32), CGM.getDataLayout());
    }
  }

  if (!constant) {
    if (trivialAutoVarInit !=
        LangOptions::TrivialAutoVarInitKind::Uninitialized) {
      // At this point, we know D has an Init expression, but isn't a constant.
      // - If D is not a scalar, auto-var-init conservatively (members may be
      // left uninitialized by constructor Init expressions for example).
      // - If D is a scalar, we only need to auto-var-init if there is a
      // self-reference. Otherwise, the Init expression should be sufficient.
      // It may be that the Init expression uses other uninitialized memory,
      // but auto-var-init here would not help, as auto-init would get
      // overwritten by Init.
      if (!D.getType()->isScalarType() || capturedByInit ||
          isAccessedBy(D, Init)) {
        initializeWhatIsTechnicallyUninitialized(Loc);
      }
    }
    LValue lv = MakeAddrLValue(Loc, type);
    lv.setNonGC(true);
    return EmitExprAsInit(Init, &D, lv, capturedByInit);
  }

  if (!emission.IsConstantAggregate) {
    // For simple scalar/complex initialization, store the value directly.
    LValue lv = MakeAddrLValue(Loc, type);
    lv.setNonGC(true);
    return EmitStoreThroughLValue(RValue::get(constant), lv, true);
  }

  emitStoresForConstant(CGM, D, Loc.withElementType(CGM.Int8Ty),
                        type.isVolatileQualified(), Builder, constant,
                        /*IsAutoInit=*/false);
}

/// Emit an expression as an initializer for an object (variable, field, etc.)
/// at the given location.  The expression is not necessarily the normal
/// initializer for the object, and the address is not necessarily
/// its normal location.
///
/// \param init the initializing expression
/// \param D the object to act as if we're initializing
/// \param lvalue the lvalue to initialize
/// \param capturedByInit true if \p D is a __block variable
///   whose address is potentially changed by the initializer
void CodeGenFunction::EmitExprAsInit(const Expr *init, const ValueDecl *D,
                                     LValue lvalue, bool capturedByInit) {
  QualType type = D->getType();

  if (type->isReferenceType()) {
    RValue rvalue = EmitReferenceBindingToExpr(init);
    if (capturedByInit)
      drillIntoBlockVariable(*this, lvalue, cast<VarDecl>(D));
    EmitStoreThroughLValue(rvalue, lvalue, true);
    return;
  }
  switch (getEvaluationKind(type)) {
  case TEK_Scalar:
    EmitScalarInit(init, D, lvalue, capturedByInit);
    return;
  case TEK_Complex: {
    ComplexPairTy complex = EmitComplexExpr(init);
    if (capturedByInit)
      drillIntoBlockVariable(*this, lvalue, cast<VarDecl>(D));
    EmitStoreOfComplex(complex, lvalue, /*init*/ true);
    return;
  }
  case TEK_Aggregate:
    if (type->isAtomicType()) {
      EmitAtomicInit(const_cast<Expr*>(init), lvalue);
    } else {
      AggValueSlot::Overlap_t Overlap = AggValueSlot::MayOverlap;
      if (isa<VarDecl>(D))
        Overlap = AggValueSlot::DoesNotOverlap;
      else if (auto *FD = dyn_cast<FieldDecl>(D))
        Overlap = getOverlapForFieldInit(FD);
      // TODO: how can we delay here if D is captured by its initializer?
      EmitAggExpr(init,
                  AggValueSlot::forLValue(lvalue, AggValueSlot::IsDestructed,
                                          AggValueSlot::DoesNotNeedGCBarriers,
                                          AggValueSlot::IsNotAliased, Overlap));
    }
    return;
  }
  llvm_unreachable("bad evaluation kind");
}

/// Enter a destroy cleanup for the given local variable.
void CodeGenFunction::emitAutoVarTypeCleanup(
                            const CodeGenFunction::AutoVarEmission &emission,
                            QualType::DestructionKind dtorKind) {
  assert(dtorKind != QualType::DK_none);

  // Note that for __block variables, we want to destroy the
  // original stack object, not the possibly forwarded object.
  Address addr = emission.getObjectAddress(*this);

  const VarDecl *var = emission.Variable;
  QualType type = var->getType();

  CleanupKind cleanupKind = NormalAndEHCleanup;
  CodeGenFunction::Destroyer *destroyer = nullptr;

  switch (dtorKind) {
  case QualType::DK_none:
    llvm_unreachable("no cleanup for trivially-destructible variable");

  case QualType::DK_cxx_destructor:
    // If there's an NRVO flag on the emission, we need a different
    // cleanup.
    if (emission.NRVOFlag) {
      assert(!type->isArrayType());
      CXXDestructorDecl *dtor = type->getAsCXXRecordDecl()->getDestructor();
      EHStack.pushCleanup<DestroyNRVOVariableCXX>(cleanupKind, addr, type, dtor,
                                                  emission.NRVOFlag);
      return;
    }
    break;

  case QualType::DK_objc_strong_lifetime:
    // Suppress cleanups for pseudo-strong variables.
    if (var->isARCPseudoStrong()) return;

    // Otherwise, consider whether to use an EH cleanup or not.
    cleanupKind = getARCCleanupKind();

    // Use the imprecise destroyer by default.
    if (!var->hasAttr<ObjCPreciseLifetimeAttr>())
      destroyer = CodeGenFunction::destroyARCStrongImprecise;
    break;

  case QualType::DK_objc_weak_lifetime:
    break;

  case QualType::DK_nontrivial_c_struct:
    destroyer = CodeGenFunction::destroyNonTrivialCStruct;
    if (emission.NRVOFlag) {
      assert(!type->isArrayType());
      EHStack.pushCleanup<DestroyNRVOVariableC>(cleanupKind, addr,
                                                emission.NRVOFlag, type);
      return;
    }
    break;
  }

  // If we haven't chosen a more specific destroyer, use the default.
  if (!destroyer) destroyer = getDestroyer(dtorKind);

  // Use an EH cleanup in array destructors iff the destructor itself
  // is being pushed as an EH cleanup.
  bool useEHCleanup = (cleanupKind & EHCleanup);
  EHStack.pushCleanup<DestroyObject>(cleanupKind, addr, type, destroyer,
                                     useEHCleanup);
}

void CodeGenFunction::EmitAutoVarCleanups(const AutoVarEmission &emission) {
  assert(emission.Variable && "emission was not valid!");

  // If this was emitted as a global constant, we're done.
  if (emission.wasEmittedAsGlobal()) return;

  // If we don't have an insertion point, we're done.  Sema prevents
  // us from jumping into any of these scopes anyway.
  if (!HaveInsertPoint()) return;

  const VarDecl &D = *emission.Variable;

  // Check the type for a cleanup.
  if (QualType::DestructionKind dtorKind = D.needsDestruction(getContext()))
    emitAutoVarTypeCleanup(emission, dtorKind);

  // In GC mode, honor objc_precise_lifetime.
  if (getLangOpts().getGC() != LangOptions::NonGC &&
      D.hasAttr<ObjCPreciseLifetimeAttr>()) {
    EHStack.pushCleanup<ExtendGCLifetime>(NormalCleanup, &D);
  }

  // Handle the cleanup attribute.
  if (const CleanupAttr *CA = D.getAttr<CleanupAttr>()) {
    const FunctionDecl *FD = CA->getFunctionDecl();

    llvm::Constant *F = CGM.GetAddrOfFunction(FD);
    assert(F && "Could not find function!");

    const CGFunctionInfo &Info = CGM.getTypes().arrangeFunctionDeclaration(FD);
    EHStack.pushCleanup<CallCleanupFunction>(NormalAndEHCleanup, F, &Info, &D);
  }

  // If this is a block variable, call _Block_object_destroy
  // (on the unforwarded address). Don't enter this cleanup if we're in pure-GC
  // mode.
  if (emission.IsEscapingByRef &&
      CGM.getLangOpts().getGC() != LangOptions::GCOnly) {
    BlockFieldFlags Flags = BLOCK_FIELD_IS_BYREF;
    if (emission.Variable->getType().isObjCGCWeak())
      Flags |= BLOCK_FIELD_IS_WEAK;
    enterByrefCleanup(NormalAndEHCleanup, emission.Addr, Flags,
                      /*LoadBlockVarAddr*/ false,
                      cxxDestructorCanThrow(emission.Variable->getType()));
  }
}

CodeGenFunction::Destroyer *
CodeGenFunction::getDestroyer(QualType::DestructionKind kind) {
  switch (kind) {
  case QualType::DK_none: llvm_unreachable("no destroyer for trivial dtor");
  case QualType::DK_cxx_destructor:
    return destroyCXXObject;
  case QualType::DK_objc_strong_lifetime:
    return destroyARCStrongPrecise;
  case QualType::DK_objc_weak_lifetime:
    return destroyARCWeak;
  case QualType::DK_nontrivial_c_struct:
    return destroyNonTrivialCStruct;
  }
  llvm_unreachable("Unknown DestructionKind");
}

/// pushEHDestroy - Push the standard destructor for the given type as
/// an EH-only cleanup.
void CodeGenFunction::pushEHDestroy(QualType::DestructionKind dtorKind,
                                    Address addr, QualType type) {
  assert(dtorKind && "cannot push destructor for trivial type");
  assert(needsEHCleanup(dtorKind));

  pushDestroy(EHCleanup, addr, type, getDestroyer(dtorKind), true);
}

/// pushDestroy - Push the standard destructor for the given type as
/// at least a normal cleanup.
void CodeGenFunction::pushDestroy(QualType::DestructionKind dtorKind,
                                  Address addr, QualType type) {
  assert(dtorKind && "cannot push destructor for trivial type");

  CleanupKind cleanupKind = getCleanupKind(dtorKind);
  pushDestroy(cleanupKind, addr, type, getDestroyer(dtorKind),
              cleanupKind & EHCleanup);
}

void CodeGenFunction::pushDestroy(CleanupKind cleanupKind, Address addr,
                                  QualType type, Destroyer *destroyer,
                                  bool useEHCleanupForArray) {
  pushFullExprCleanup<DestroyObject>(cleanupKind, addr, type,
                                     destroyer, useEHCleanupForArray);
}

// Pushes a destroy and defers its deactivation until its
// CleanupDeactivationScope is exited.
void CodeGenFunction::pushDestroyAndDeferDeactivation(
    QualType::DestructionKind dtorKind, Address addr, QualType type) {
  assert(dtorKind && "cannot push destructor for trivial type");

  CleanupKind cleanupKind = getCleanupKind(dtorKind);
  pushDestroyAndDeferDeactivation(
      cleanupKind, addr, type, getDestroyer(dtorKind), cleanupKind & EHCleanup);
}

void CodeGenFunction::pushDestroyAndDeferDeactivation(
    CleanupKind cleanupKind, Address addr, QualType type, Destroyer *destroyer,
    bool useEHCleanupForArray) {
  llvm::Instruction *DominatingIP =
      Builder.CreateFlagLoad(llvm::Constant::getNullValue(Int8PtrTy));
  pushDestroy(cleanupKind, addr, type, destroyer, useEHCleanupForArray);
  DeferredDeactivationCleanupStack.push_back(
      {EHStack.stable_begin(), DominatingIP});
}

void CodeGenFunction::pushStackRestore(CleanupKind Kind, Address SPMem) {
  EHStack.pushCleanup<CallStackRestore>(Kind, SPMem);
}

void CodeGenFunction::pushKmpcAllocFree(
    CleanupKind Kind, std::pair<llvm::Value *, llvm::Value *> AddrSizePair) {
  EHStack.pushCleanup<KmpcAllocFree>(Kind, AddrSizePair);
}

void CodeGenFunction::pushLifetimeExtendedDestroy(CleanupKind cleanupKind,
                                                  Address addr, QualType type,
                                                  Destroyer *destroyer,
                                                  bool useEHCleanupForArray) {
  // If we're not in a conditional branch, we don't need to bother generating a
  // conditional cleanup.
  if (!isInConditionalBranch()) {
    // FIXME: When popping normal cleanups, we need to keep this EH cleanup
    // around in case a temporary's destructor throws an exception.

    // Add the cleanup to the EHStack. After the full-expr, this would be
    // deactivated before being popped from the stack.
    pushDestroyAndDeferDeactivation(cleanupKind, addr, type, destroyer,
                                    useEHCleanupForArray);

    // Since this is lifetime-extended, push it once again to the EHStack after
    // the full expression.
    return pushCleanupAfterFullExprWithActiveFlag<DestroyObject>(
        cleanupKind, Address::invalid(), addr, type, destroyer,
        useEHCleanupForArray);
  }

  // Otherwise, we should only destroy the object if it's been initialized.

  using ConditionalCleanupType =
      EHScopeStack::ConditionalCleanup<DestroyObject, Address, QualType,
                                       Destroyer *, bool>;
  DominatingValue<Address>::saved_type SavedAddr = saveValueInCond(addr);

  // Remember to emit cleanup if we branch-out before end of full-expression
  // (eg: through stmt-expr or coro suspensions).
  AllocaTrackerRAII DeactivationAllocas(*this);
  Address ActiveFlagForDeactivation = createCleanupActiveFlag();

  pushCleanupAndDeferDeactivation<ConditionalCleanupType>(
      cleanupKind, SavedAddr, type, destroyer, useEHCleanupForArray);
  initFullExprCleanupWithFlag(ActiveFlagForDeactivation);
  EHCleanupScope &cleanup = cast<EHCleanupScope>(*EHStack.begin());
  // Erase the active flag if the cleanup was not emitted.
  cleanup.AddAuxAllocas(std::move(DeactivationAllocas).Take());

  // Since this is lifetime-extended, push it once again to the EHStack after
  // the full expression.
  // The previous active flag would always be 'false' due to forced deferred
  // deactivation. Use a separate flag for lifetime-extension to correctly
  // remember if this branch was taken and the object was initialized.
  Address ActiveFlagForLifetimeExt = createCleanupActiveFlag();
  pushCleanupAfterFullExprWithActiveFlag<ConditionalCleanupType>(
      cleanupKind, ActiveFlagForLifetimeExt, SavedAddr, type, destroyer,
      useEHCleanupForArray);
}

/// emitDestroy - Immediately perform the destruction of the given
/// object.
///
/// \param addr - the address of the object; a type*
/// \param type - the type of the object; if an array type, all
///   objects are destroyed in reverse order
/// \param destroyer - the function to call to destroy individual
///   elements
/// \param useEHCleanupForArray - whether an EH cleanup should be
///   used when destroying array elements, in case one of the
///   destructions throws an exception
void CodeGenFunction::emitDestroy(Address addr, QualType type,
                                  Destroyer *destroyer,
                                  bool useEHCleanupForArray) {
  const ArrayType *arrayType = getContext().getAsArrayType(type);
  if (!arrayType)
    return destroyer(*this, addr, type);

  llvm::Value *length = emitArrayLength(arrayType, type, addr);

  CharUnits elementAlign =
    addr.getAlignment()
        .alignmentOfArrayElement(getContext().getTypeSizeInChars(type));

  // Normally we have to check whether the array is zero-length.
  bool checkZeroLength = true;

  // But if the array length is constant, we can suppress that.
  if (llvm::ConstantInt *constLength = dyn_cast<llvm::ConstantInt>(length)) {
    // ...and if it's constant zero, we can just skip the entire thing.
    if (constLength->isZero()) return;
    checkZeroLength = false;
  }

  llvm::Value *begin = addr.emitRawPointer(*this);
  llvm::Value *end =
      Builder.CreateInBoundsGEP(addr.getElementType(), begin, length);
  emitArrayDestroy(begin, end, type, elementAlign, destroyer,
                   checkZeroLength, useEHCleanupForArray);
}

/// emitArrayDestroy - Destroys all the elements of the given array,
/// beginning from last to first.  The array cannot be zero-length.
///
/// \param begin - a type* denoting the first element of the array
/// \param end - a type* denoting one past the end of the array
/// \param elementType - the element type of the array
/// \param destroyer - the function to call to destroy elements
/// \param useEHCleanup - whether to push an EH cleanup to destroy
///   the remaining elements in case the destruction of a single
///   element throws
void CodeGenFunction::emitArrayDestroy(llvm::Value *begin,
                                       llvm::Value *end,
                                       QualType elementType,
                                       CharUnits elementAlign,
                                       Destroyer *destroyer,
                                       bool checkZeroLength,
                                       bool useEHCleanup) {
  assert(!elementType->isArrayType());

  // The basic structure here is a do-while loop, because we don't
  // need to check for the zero-element case.
  llvm::BasicBlock *bodyBB = createBasicBlock("arraydestroy.body");
  llvm::BasicBlock *doneBB = createBasicBlock("arraydestroy.done");

  if (checkZeroLength) {
    llvm::Value *isEmpty = Builder.CreateICmpEQ(begin, end,
                                                "arraydestroy.isempty");
    Builder.CreateCondBr(isEmpty, doneBB, bodyBB);
  }

  // Enter the loop body, making that address the current address.
  llvm::BasicBlock *entryBB = Builder.GetInsertBlock();
  EmitBlock(bodyBB);
  llvm::PHINode *elementPast =
    Builder.CreatePHI(begin->getType(), 2, "arraydestroy.elementPast");
  elementPast->addIncoming(end, entryBB);

  // Shift the address back by one element.
  llvm::Value *negativeOne = llvm::ConstantInt::get(SizeTy, -1, true);
  llvm::Type *llvmElementType = ConvertTypeForMem(elementType);
  llvm::Value *element = Builder.CreateInBoundsGEP(
      llvmElementType, elementPast, negativeOne, "arraydestroy.element");

  if (useEHCleanup)
    pushRegularPartialArrayCleanup(begin, element, elementType, elementAlign,
                                   destroyer);

  // Perform the actual destruction there.
  destroyer(*this, Address(element, llvmElementType, elementAlign),
            elementType);

  if (useEHCleanup)
    PopCleanupBlock();

  // Check whether we've reached the end.
  llvm::Value *done = Builder.CreateICmpEQ(element, begin, "arraydestroy.done");
  Builder.CreateCondBr(done, doneBB, bodyBB);
  elementPast->addIncoming(element, Builder.GetInsertBlock());

  // Done.
  EmitBlock(doneBB);
}

/// Perform partial array destruction as if in an EH cleanup.  Unlike
/// emitArrayDestroy, the element type here may still be an array type.
static void emitPartialArrayDestroy(CodeGenFunction &CGF,
                                    llvm::Value *begin, llvm::Value *end,
                                    QualType type, CharUnits elementAlign,
                                    CodeGenFunction::Destroyer *destroyer) {
  llvm::Type *elemTy = CGF.ConvertTypeForMem(type);

  // If the element type is itself an array, drill down.
  unsigned arrayDepth = 0;
  while (const ArrayType *arrayType = CGF.getContext().getAsArrayType(type)) {
    // VLAs don't require a GEP index to walk into.
    if (!isa<VariableArrayType>(arrayType))
      arrayDepth++;
    type = arrayType->getElementType();
  }

  if (arrayDepth) {
    llvm::Value *zero = llvm::ConstantInt::get(CGF.SizeTy, 0);

    SmallVector<llvm::Value*,4> gepIndices(arrayDepth+1, zero);
    begin = CGF.Builder.CreateInBoundsGEP(
        elemTy, begin, gepIndices, "pad.arraybegin");
    end = CGF.Builder.CreateInBoundsGEP(
        elemTy, end, gepIndices, "pad.arrayend");
  }

  // Destroy the array.  We don't ever need an EH cleanup because we
  // assume that we're in an EH cleanup ourselves, so a throwing
  // destructor causes an immediate terminate.
  CGF.emitArrayDestroy(begin, end, type, elementAlign, destroyer,
                       /*checkZeroLength*/ true, /*useEHCleanup*/ false);
}

namespace {
  /// RegularPartialArrayDestroy - a cleanup which performs a partial
  /// array destroy where the end pointer is regularly determined and
  /// does not need to be loaded from a local.
  class RegularPartialArrayDestroy final : public EHScopeStack::Cleanup {
    llvm::Value *ArrayBegin;
    llvm::Value *ArrayEnd;
    QualType ElementType;
    CodeGenFunction::Destroyer *Destroyer;
    CharUnits ElementAlign;
  public:
    RegularPartialArrayDestroy(llvm::Value *arrayBegin, llvm::Value *arrayEnd,
                               QualType elementType, CharUnits elementAlign,
                               CodeGenFunction::Destroyer *destroyer)
      : ArrayBegin(arrayBegin), ArrayEnd(arrayEnd),
        ElementType(elementType), Destroyer(destroyer),
        ElementAlign(elementAlign) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      emitPartialArrayDestroy(CGF, ArrayBegin, ArrayEnd,
                              ElementType, ElementAlign, Destroyer);
    }
  };

  /// IrregularPartialArrayDestroy - a cleanup which performs a
  /// partial array destroy where the end pointer is irregularly
  /// determined and must be loaded from a local.
  class IrregularPartialArrayDestroy final : public EHScopeStack::Cleanup {
    llvm::Value *ArrayBegin;
    Address ArrayEndPointer;
    QualType ElementType;
    CodeGenFunction::Destroyer *Destroyer;
    CharUnits ElementAlign;
  public:
    IrregularPartialArrayDestroy(llvm::Value *arrayBegin,
                                 Address arrayEndPointer,
                                 QualType elementType,
                                 CharUnits elementAlign,
                                 CodeGenFunction::Destroyer *destroyer)
      : ArrayBegin(arrayBegin), ArrayEndPointer(arrayEndPointer),
        ElementType(elementType), Destroyer(destroyer),
        ElementAlign(elementAlign) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      llvm::Value *arrayEnd = CGF.Builder.CreateLoad(ArrayEndPointer);
      emitPartialArrayDestroy(CGF, ArrayBegin, arrayEnd,
                              ElementType, ElementAlign, Destroyer);
    }
  };
} // end anonymous namespace

/// pushIrregularPartialArrayCleanup - Push a NormalAndEHCleanup to
/// destroy already-constructed elements of the given array.  The cleanup may be
/// popped with DeactivateCleanupBlock or PopCleanupBlock.
///
/// \param elementType - the immediate element type of the array;
///   possibly still an array type
void CodeGenFunction::pushIrregularPartialArrayCleanup(llvm::Value *arrayBegin,
                                                       Address arrayEndPointer,
                                                       QualType elementType,
                                                       CharUnits elementAlign,
                                                       Destroyer *destroyer) {
  pushFullExprCleanup<IrregularPartialArrayDestroy>(
      NormalAndEHCleanup, arrayBegin, arrayEndPointer, elementType,
      elementAlign, destroyer);
}

/// pushRegularPartialArrayCleanup - Push an EH cleanup to destroy
/// already-constructed elements of the given array.  The cleanup
/// may be popped with DeactivateCleanupBlock or PopCleanupBlock.
///
/// \param elementType - the immediate element type of the array;
///   possibly still an array type
void CodeGenFunction::pushRegularPartialArrayCleanup(llvm::Value *arrayBegin,
                                                     llvm::Value *arrayEnd,
                                                     QualType elementType,
                                                     CharUnits elementAlign,
                                                     Destroyer *destroyer) {
  pushFullExprCleanup<RegularPartialArrayDestroy>(EHCleanup,
                                                  arrayBegin, arrayEnd,
                                                  elementType, elementAlign,
                                                  destroyer);
}

/// Lazily declare the @llvm.lifetime.start intrinsic.
llvm::Function *CodeGenModule::getLLVMLifetimeStartFn() {
  if (LifetimeStartFn)
    return LifetimeStartFn;
  LifetimeStartFn = llvm::Intrinsic::getDeclaration(&getModule(),
    llvm::Intrinsic::lifetime_start, AllocaInt8PtrTy);
  return LifetimeStartFn;
}

/// Lazily declare the @llvm.lifetime.end intrinsic.
llvm::Function *CodeGenModule::getLLVMLifetimeEndFn() {
  if (LifetimeEndFn)
    return LifetimeEndFn;
  LifetimeEndFn = llvm::Intrinsic::getDeclaration(&getModule(),
    llvm::Intrinsic::lifetime_end, AllocaInt8PtrTy);
  return LifetimeEndFn;
}

namespace {
  /// A cleanup to perform a release of an object at the end of a
  /// function.  This is used to balance out the incoming +1 of a
  /// ns_consumed argument when we can't reasonably do that just by
  /// not doing the initial retain for a __block argument.
  struct ConsumeARCParameter final : EHScopeStack::Cleanup {
    ConsumeARCParameter(llvm::Value *param,
                        ARCPreciseLifetime_t precise)
      : Param(param), Precise(precise) {}

    llvm::Value *Param;
    ARCPreciseLifetime_t Precise;

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      CGF.EmitARCRelease(Param, Precise);
    }
  };
} // end anonymous namespace

/// Emit an alloca (or GlobalValue depending on target)
/// for the specified parameter and set up LocalDeclMap.
void CodeGenFunction::EmitParmDecl(const VarDecl &D, ParamValue Arg,
                                   unsigned ArgNo) {
  bool NoDebugInfo = false;
  // FIXME: Why isn't ImplicitParamDecl a ParmVarDecl?
  assert((isa<ParmVarDecl>(D) || isa<ImplicitParamDecl>(D)) &&
         "Invalid argument to EmitParmDecl");

  // Set the name of the parameter's initial value to make IR easier to
  // read. Don't modify the names of globals.
  if (!isa<llvm::GlobalValue>(Arg.getAnyValue()))
    Arg.getAnyValue()->setName(D.getName());

  QualType Ty = D.getType();

  // Use better IR generation for certain implicit parameters.
  if (auto IPD = dyn_cast<ImplicitParamDecl>(&D)) {
    // The only implicit argument a block has is its literal.
    // This may be passed as an inalloca'ed value on Windows x86.
    if (BlockInfo) {
      llvm::Value *V = Arg.isIndirect()
                           ? Builder.CreateLoad(Arg.getIndirectAddress())
                           : Arg.getDirectValue();
      setBlockContextParameter(IPD, ArgNo, V);
      return;
    }
    // Suppressing debug info for ThreadPrivateVar parameters, else it hides
    // debug info of TLS variables.
    NoDebugInfo =
        (IPD->getParameterKind() == ImplicitParamKind::ThreadPrivateVar);
  }

  Address DeclPtr = Address::invalid();
  RawAddress AllocaPtr = Address::invalid();
  bool DoStore = false;
  bool IsScalar = hasScalarEvaluationKind(Ty);
  bool UseIndirectDebugAddress = false;

  // If we already have a pointer to the argument, reuse the input pointer.
  if (Arg.isIndirect()) {
    DeclPtr = Arg.getIndirectAddress();
    DeclPtr = DeclPtr.withElementType(ConvertTypeForMem(Ty));
    // Indirect argument is in alloca address space, which may be different
    // from the default address space.
    auto AllocaAS = CGM.getASTAllocaAddressSpace();
    auto *V = DeclPtr.emitRawPointer(*this);
    AllocaPtr = RawAddress(V, DeclPtr.getElementType(), DeclPtr.getAlignment());

    // For truly ABI indirect arguments -- those that are not `byval` -- store
    // the address of the argument on the stack to preserve debug information.
    ABIArgInfo ArgInfo = CurFnInfo->arguments()[ArgNo - 1].info;
    if (ArgInfo.isIndirect())
      UseIndirectDebugAddress = !ArgInfo.getIndirectByVal();
    if (UseIndirectDebugAddress) {
      auto PtrTy = getContext().getPointerType(Ty);
      AllocaPtr = CreateMemTemp(PtrTy, getContext().getTypeAlignInChars(PtrTy),
                                D.getName() + ".indirect_addr");
      EmitStoreOfScalar(V, AllocaPtr, /* Volatile */ false, PtrTy);
    }

    auto SrcLangAS = getLangOpts().OpenCL ? LangAS::opencl_private : AllocaAS;
    auto DestLangAS =
        getLangOpts().OpenCL ? LangAS::opencl_private : LangAS::Default;
    if (SrcLangAS != DestLangAS) {
      assert(getContext().getTargetAddressSpace(SrcLangAS) ==
             CGM.getDataLayout().getAllocaAddrSpace());
      auto DestAS = getContext().getTargetAddressSpace(DestLangAS);
      auto *T = llvm::PointerType::get(getLLVMContext(), DestAS);
      DeclPtr =
          DeclPtr.withPointer(getTargetHooks().performAddrSpaceCast(
                                  *this, V, SrcLangAS, DestLangAS, T, true),
                              DeclPtr.isKnownNonNull());
    }

    // Push a destructor cleanup for this parameter if the ABI requires it.
    // Don't push a cleanup in a thunk for a method that will also emit a
    // cleanup.
    if (Ty->isRecordType() && !CurFuncIsThunk &&
        Ty->castAs<RecordType>()->getDecl()->isParamDestroyedInCallee()) {
      if (QualType::DestructionKind DtorKind =
              D.needsDestruction(getContext())) {
        assert((DtorKind == QualType::DK_cxx_destructor ||
                DtorKind == QualType::DK_nontrivial_c_struct) &&
               "unexpected destructor type");
        pushDestroy(DtorKind, DeclPtr, Ty);
        CalleeDestructedParamCleanups[cast<ParmVarDecl>(&D)] =
            EHStack.stable_begin();
      }
    }
  } else {
    // Check if the parameter address is controlled by OpenMP runtime.
    Address OpenMPLocalAddr =
        getLangOpts().OpenMP
            ? CGM.getOpenMPRuntime().getAddressOfLocalVariable(*this, &D)
            : Address::invalid();
    if (getLangOpts().OpenMP && OpenMPLocalAddr.isValid()) {
      DeclPtr = OpenMPLocalAddr;
      AllocaPtr = DeclPtr;
    } else {
      // Otherwise, create a temporary to hold the value.
      DeclPtr = CreateMemTemp(Ty, getContext().getDeclAlign(&D),
                              D.getName() + ".addr", &AllocaPtr);
    }
    DoStore = true;
  }

  llvm::Value *ArgVal = (DoStore ? Arg.getDirectValue() : nullptr);

  LValue lv = MakeAddrLValue(DeclPtr, Ty);
  if (IsScalar) {
    Qualifiers qs = Ty.getQualifiers();
    if (Qualifiers::ObjCLifetime lt = qs.getObjCLifetime()) {
      // We honor __attribute__((ns_consumed)) for types with lifetime.
      // For __strong, it's handled by just skipping the initial retain;
      // otherwise we have to balance out the initial +1 with an extra
      // cleanup to do the release at the end of the function.
      bool isConsumed = D.hasAttr<NSConsumedAttr>();

      // If a parameter is pseudo-strong then we can omit the implicit retain.
      if (D.isARCPseudoStrong()) {
        assert(lt == Qualifiers::OCL_Strong &&
               "pseudo-strong variable isn't strong?");
        assert(qs.hasConst() && "pseudo-strong variable should be const!");
        lt = Qualifiers::OCL_ExplicitNone;
      }

      // Load objects passed indirectly.
      if (Arg.isIndirect() && !ArgVal)
        ArgVal = Builder.CreateLoad(DeclPtr);

      if (lt == Qualifiers::OCL_Strong) {
        if (!isConsumed) {
          if (CGM.getCodeGenOpts().OptimizationLevel == 0) {
            // use objc_storeStrong(&dest, value) for retaining the
            // object. But first, store a null into 'dest' because
            // objc_storeStrong attempts to release its old value.
            llvm::Value *Null = CGM.EmitNullConstant(D.getType());
            EmitStoreOfScalar(Null, lv, /* isInitialization */ true);
            EmitARCStoreStrongCall(lv.getAddress(), ArgVal, true);
            DoStore = false;
          }
          else
          // Don't use objc_retainBlock for block pointers, because we
          // don't want to Block_copy something just because we got it
          // as a parameter.
            ArgVal = EmitARCRetainNonBlock(ArgVal);
        }
      } else {
        // Push the cleanup for a consumed parameter.
        if (isConsumed) {
          ARCPreciseLifetime_t precise = (D.hasAttr<ObjCPreciseLifetimeAttr>()
                                ? ARCPreciseLifetime : ARCImpreciseLifetime);
          EHStack.pushCleanup<ConsumeARCParameter>(getARCCleanupKind(), ArgVal,
                                                   precise);
        }

        if (lt == Qualifiers::OCL_Weak) {
          EmitARCInitWeak(DeclPtr, ArgVal);
          DoStore = false; // The weak init is a store, no need to do two.
        }
      }

      // Enter the cleanup scope.
      EmitAutoVarWithLifetime(*this, D, DeclPtr, lt);
    }
  }

  // Store the initial value into the alloca.
  if (DoStore)
    EmitStoreOfScalar(ArgVal, lv, /* isInitialization */ true);

  setAddrOfLocalVar(&D, DeclPtr);

  // Emit debug info for param declarations in non-thunk functions.
  if (CGDebugInfo *DI = getDebugInfo()) {
    if (CGM.getCodeGenOpts().hasReducedDebugInfo() && !CurFuncIsThunk &&
        !NoDebugInfo) {
      llvm::DILocalVariable *DILocalVar = DI->EmitDeclareOfArgVariable(
          &D, AllocaPtr.getPointer(), ArgNo, Builder, UseIndirectDebugAddress);
      if (const auto *Var = dyn_cast_or_null<ParmVarDecl>(&D))
        DI->getParamDbgMappings().insert({Var, DILocalVar});
    }
  }

  if (D.hasAttr<AnnotateAttr>())
    EmitVarAnnotations(&D, DeclPtr.emitRawPointer(*this));

  // We can only check return value nullability if all arguments to the
  // function satisfy their nullability preconditions. This makes it necessary
  // to emit null checks for args in the function body itself.
  if (requiresReturnValueNullabilityCheck()) {
    auto Nullability = Ty->getNullability();
    if (Nullability && *Nullability == NullabilityKind::NonNull) {
      SanitizerScope SanScope(this);
      RetValNullabilityPrecondition =
          Builder.CreateAnd(RetValNullabilityPrecondition,
                            Builder.CreateIsNotNull(Arg.getAnyValue()));
    }
  }
}

void CodeGenModule::EmitOMPDeclareReduction(const OMPDeclareReductionDecl *D,
                                            CodeGenFunction *CGF) {
  if (!LangOpts.OpenMP || (!LangOpts.EmitAllDecls && !D->isUsed()))
    return;
  getOpenMPRuntime().emitUserDefinedReduction(CGF, D);
}

void CodeGenModule::EmitOMPDeclareMapper(const OMPDeclareMapperDecl *D,
                                         CodeGenFunction *CGF) {
  if (!LangOpts.OpenMP || LangOpts.OpenMPSimd ||
      (!LangOpts.EmitAllDecls && !D->isUsed()))
    return;
  getOpenMPRuntime().emitUserDefinedMapper(D, CGF);
}

void CodeGenModule::EmitOMPRequiresDecl(const OMPRequiresDecl *D) {
  getOpenMPRuntime().processRequiresDirective(D);
}

void CodeGenModule::EmitOMPAllocateDecl(const OMPAllocateDecl *D) {
  for (const Expr *E : D->varlists()) {
    const auto *DE = cast<DeclRefExpr>(E);
    const auto *VD = cast<VarDecl>(DE->getDecl());

    // Skip all but globals.
    if (!VD->hasGlobalStorage())
      continue;

    // Check if the global has been materialized yet or not. If not, we are done
    // as any later generation will utilize the OMPAllocateDeclAttr. However, if
    // we already emitted the global we might have done so before the
    // OMPAllocateDeclAttr was attached, leading to the wrong address space
    // (potentially). While not pretty, common practise is to remove the old IR
    // global and generate a new one, so we do that here too. Uses are replaced
    // properly.
    StringRef MangledName = getMangledName(VD);
    llvm::GlobalValue *Entry = GetGlobalValue(MangledName);
    if (!Entry)
      continue;

    // We can also keep the existing global if the address space is what we
    // expect it to be, if not, it is replaced.
    QualType ASTTy = VD->getType();
    clang::LangAS GVAS = GetGlobalVarAddressSpace(VD);
    auto TargetAS = getContext().getTargetAddressSpace(GVAS);
    if (Entry->getType()->getAddressSpace() == TargetAS)
      continue;

    // Make a new global with the correct type / address space.
    llvm::Type *Ty = getTypes().ConvertTypeForMem(ASTTy);
    llvm::PointerType *PTy = llvm::PointerType::get(Ty, TargetAS);

    // Replace all uses of the old global with a cast. Since we mutate the type
    // in place we neeed an intermediate that takes the spot of the old entry
    // until we can create the cast.
    llvm::GlobalVariable *DummyGV = new llvm::GlobalVariable(
        getModule(), Entry->getValueType(), false,
        llvm::GlobalValue::CommonLinkage, nullptr, "dummy", nullptr,
        llvm::GlobalVariable::NotThreadLocal, Entry->getAddressSpace());
    Entry->replaceAllUsesWith(DummyGV);

    Entry->mutateType(PTy);
    llvm::Constant *NewPtrForOldDecl =
        llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(
            Entry, DummyGV->getType());

    // Now we have a casted version of the changed global, the dummy can be
    // replaced and deleted.
    DummyGV->replaceAllUsesWith(NewPtrForOldDecl);
    DummyGV->eraseFromParent();
  }
}

std::optional<CharUnits>
CodeGenModule::getOMPAllocateAlignment(const VarDecl *VD) {
  if (const auto *AA = VD->getAttr<OMPAllocateDeclAttr>()) {
    if (Expr *Alignment = AA->getAlignment()) {
      unsigned UserAlign =
          Alignment->EvaluateKnownConstInt(getContext()).getExtValue();
      CharUnits NaturalAlign =
          getNaturalTypeAlignment(VD->getType().getNonReferenceType());

      // OpenMP5.1 pg 185 lines 7-10
      //   Each item in the align modifier list must be aligned to the maximum
      //   of the specified alignment and the type's natural alignment.
      return CharUnits::fromQuantity(
          std::max<unsigned>(UserAlign, NaturalAlign.getQuantity()));
    }
  }
  return std::nullopt;
}
