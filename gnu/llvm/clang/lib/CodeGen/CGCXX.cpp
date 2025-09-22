//===--- CGCXX.cpp - Emit LLVM Code for declarations ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation.
//
//===----------------------------------------------------------------------===//

// We might split this into multiple files if it gets too unwieldy

#include "CGCXXABI.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/CodeGenOptions.h"
#include "llvm/ADT/StringExtras.h"
using namespace clang;
using namespace CodeGen;


/// Try to emit a base destructor as an alias to its primary
/// base-class destructor.
bool CodeGenModule::TryEmitBaseDestructorAsAlias(const CXXDestructorDecl *D) {
  if (!getCodeGenOpts().CXXCtorDtorAliases)
    return true;

  // Producing an alias to a base class ctor/dtor can degrade debug quality
  // as the debugger cannot tell them apart.
  if (getCodeGenOpts().OptimizationLevel == 0)
    return true;

  // Disable this optimization for ARM64EC.  FIXME: This probably should work,
  // but getting the symbol table correct is complicated.
  if (getTarget().getTriple().isWindowsArm64EC())
    return true;

  // If sanitizing memory to check for use-after-dtor, do not emit as
  //  an alias, unless this class owns no members.
  if (getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
      !D->getParent()->field_empty())
    return true;

  // If the destructor doesn't have a trivial body, we have to emit it
  // separately.
  if (!D->hasTrivialBody())
    return true;

  const CXXRecordDecl *Class = D->getParent();

  // We are going to instrument this destructor, so give up even if it is
  // currently empty.
  if (Class->mayInsertExtraPadding())
    return true;

  // If we need to manipulate a VTT parameter, give up.
  if (Class->getNumVBases()) {
    // Extra Credit:  passing extra parameters is perfectly safe
    // in many calling conventions, so only bail out if the ctor's
    // calling convention is nonstandard.
    return true;
  }

  // If any field has a non-trivial destructor, we have to emit the
  // destructor separately.
  for (const auto *I : Class->fields())
    if (I->getType().isDestructedType())
      return true;

  // Try to find a unique base class with a non-trivial destructor.
  const CXXRecordDecl *UniqueBase = nullptr;
  for (const auto &I : Class->bases()) {

    // We're in the base destructor, so skip virtual bases.
    if (I.isVirtual()) continue;

    // Skip base classes with trivial destructors.
    const auto *Base =
        cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());
    if (Base->hasTrivialDestructor()) continue;

    // If we've already found a base class with a non-trivial
    // destructor, give up.
    if (UniqueBase) return true;
    UniqueBase = Base;
  }

  // If we didn't find any bases with a non-trivial destructor, then
  // the base destructor is actually effectively trivial, which can
  // happen if it was needlessly user-defined or if there are virtual
  // bases with non-trivial destructors.
  if (!UniqueBase)
    return true;

  // If the base is at a non-zero offset, give up.
  const ASTRecordLayout &ClassLayout = Context.getASTRecordLayout(Class);
  if (!ClassLayout.getBaseClassOffset(UniqueBase).isZero())
    return true;

  // Give up if the calling conventions don't match. We could update the call,
  // but it is probably not worth it.
  const CXXDestructorDecl *BaseD = UniqueBase->getDestructor();
  if (BaseD->getType()->castAs<FunctionType>()->getCallConv() !=
      D->getType()->castAs<FunctionType>()->getCallConv())
    return true;

  GlobalDecl AliasDecl(D, Dtor_Base);
  GlobalDecl TargetDecl(BaseD, Dtor_Base);

  // The alias will use the linkage of the referent.  If we can't
  // support aliases with that linkage, fail.
  llvm::GlobalValue::LinkageTypes Linkage = getFunctionLinkage(AliasDecl);

  // We can't use an alias if the linkage is not valid for one.
  if (!llvm::GlobalAlias::isValidLinkage(Linkage))
    return true;

  llvm::GlobalValue::LinkageTypes TargetLinkage =
      getFunctionLinkage(TargetDecl);

  // Check if we have it already.
  StringRef MangledName = getMangledName(AliasDecl);
  llvm::GlobalValue *Entry = GetGlobalValue(MangledName);
  if (Entry && !Entry->isDeclaration())
    return false;
  if (Replacements.count(MangledName))
    return false;

  llvm::Type *AliasValueType = getTypes().GetFunctionType(AliasDecl);

  // Find the referent.
  auto *Aliasee = cast<llvm::GlobalValue>(GetAddrOfGlobal(TargetDecl));

  // Instead of creating as alias to a linkonce_odr, replace all of the uses
  // of the aliasee.
  if (llvm::GlobalValue::isDiscardableIfUnused(Linkage) &&
      !(TargetLinkage == llvm::GlobalValue::AvailableExternallyLinkage &&
        TargetDecl.getDecl()->hasAttr<AlwaysInlineAttr>())) {
    // FIXME: An extern template instantiation will create functions with
    // linkage "AvailableExternally". In libc++, some classes also define
    // members with attribute "AlwaysInline" and expect no reference to
    // be generated. It is desirable to reenable this optimisation after
    // corresponding LLVM changes.
    addReplacement(MangledName, Aliasee);
    return false;
  }

  // If we have a weak, non-discardable alias (weak, weak_odr), like an extern
  // template instantiation or a dllexported class, avoid forming it on COFF.
  // A COFF weak external alias cannot satisfy a normal undefined symbol
  // reference from another TU. The other TU must also mark the referenced
  // symbol as weak, which we cannot rely on.
  if (llvm::GlobalValue::isWeakForLinker(Linkage) &&
      getTriple().isOSBinFormatCOFF()) {
    return true;
  }

  // If we don't have a definition for the destructor yet or the definition is
  // avaialable_externally, don't emit an alias.  We can't emit aliases to
  // declarations; that's just not how aliases work.
  if (Aliasee->isDeclarationForLinker())
    return true;

  // Don't create an alias to a linker weak symbol. This avoids producing
  // different COMDATs in different TUs. Another option would be to
  // output the alias both for weak_odr and linkonce_odr, but that
  // requires explicit comdat support in the IL.
  if (llvm::GlobalValue::isWeakForLinker(TargetLinkage))
    return true;

  // Create the alias with no name.
  auto *Alias = llvm::GlobalAlias::create(AliasValueType, 0, Linkage, "",
                                          Aliasee, &getModule());

  // Destructors are always unnamed_addr.
  Alias->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // Switch any previous uses to the alias.
  if (Entry) {
    assert(Entry->getValueType() == AliasValueType &&
           Entry->getAddressSpace() == Alias->getAddressSpace() &&
           "declaration exists with different type");
    Alias->takeName(Entry);
    Entry->replaceAllUsesWith(Alias);
    Entry->eraseFromParent();
  } else {
    Alias->setName(MangledName);
  }

  // Finally, set up the alias with its proper name and attributes.
  SetCommonAttributes(AliasDecl, Alias);

  return false;
}

llvm::Function *CodeGenModule::codegenCXXStructor(GlobalDecl GD) {
  const CGFunctionInfo &FnInfo = getTypes().arrangeCXXStructorDeclaration(GD);
  auto *Fn = cast<llvm::Function>(
      getAddrOfCXXStructor(GD, &FnInfo, /*FnType=*/nullptr,
                           /*DontDefer=*/true, ForDefinition));

  setFunctionLinkage(GD, Fn);

  CodeGenFunction(*this).GenerateCode(GD, Fn, FnInfo);
  setNonAliasAttributes(GD, Fn);
  SetLLVMFunctionAttributesForDefinition(cast<CXXMethodDecl>(GD.getDecl()), Fn);
  return Fn;
}

llvm::FunctionCallee CodeGenModule::getAddrAndTypeOfCXXStructor(
    GlobalDecl GD, const CGFunctionInfo *FnInfo, llvm::FunctionType *FnType,
    bool DontDefer, ForDefinition_t IsForDefinition) {
  auto *MD = cast<CXXMethodDecl>(GD.getDecl());

  if (isa<CXXDestructorDecl>(MD)) {
    // Always alias equivalent complete destructors to base destructors in the
    // MS ABI.
    if (getTarget().getCXXABI().isMicrosoft() &&
        GD.getDtorType() == Dtor_Complete &&
        MD->getParent()->getNumVBases() == 0)
      GD = GD.getWithDtorType(Dtor_Base);
  }

  if (!FnType) {
    if (!FnInfo)
      FnInfo = &getTypes().arrangeCXXStructorDeclaration(GD);
    FnType = getTypes().GetFunctionType(*FnInfo);
  }

  llvm::Constant *Ptr = GetOrCreateLLVMFunction(
      getMangledName(GD), FnType, GD, /*ForVTable=*/false, DontDefer,
      /*IsThunk=*/false, /*ExtraAttrs=*/llvm::AttributeList(), IsForDefinition);
  return {FnType, Ptr};
}

static CGCallee BuildAppleKextVirtualCall(CodeGenFunction &CGF,
                                          GlobalDecl GD,
                                          llvm::Type *Ty,
                                          const CXXRecordDecl *RD) {
  assert(!CGF.CGM.getTarget().getCXXABI().isMicrosoft() &&
         "No kext in Microsoft ABI");
  CodeGenModule &CGM = CGF.CGM;
  llvm::Value *VTable = CGM.getCXXABI().getAddrOfVTable(RD, CharUnits());
  Ty = llvm::PointerType::getUnqual(CGM.getLLVMContext());
  assert(VTable && "BuildVirtualCall = kext vtbl pointer is null");
  uint64_t VTableIndex = CGM.getItaniumVTableContext().getMethodVTableIndex(GD);
  const VTableLayout &VTLayout = CGM.getItaniumVTableContext().getVTableLayout(RD);
  VTableLayout::AddressPointLocation AddressPoint =
      VTLayout.getAddressPoint(BaseSubobject(RD, CharUnits::Zero()));
  VTableIndex += VTLayout.getVTableOffset(AddressPoint.VTableIndex) +
                 AddressPoint.AddressPointIndex;
  llvm::Value *VFuncPtr =
    CGF.Builder.CreateConstInBoundsGEP1_64(Ty, VTable, VTableIndex, "vfnkxt");
  llvm::Value *VFunc = CGF.Builder.CreateAlignedLoad(
      Ty, VFuncPtr, llvm::Align(CGF.PointerAlignInBytes));

  CGPointerAuthInfo PointerAuth;
  if (auto &Schema =
          CGM.getCodeGenOpts().PointerAuth.CXXVirtualFunctionPointers) {
    GlobalDecl OrigMD =
        CGM.getItaniumVTableContext().findOriginalMethod(GD.getCanonicalDecl());
    PointerAuth = CGF.EmitPointerAuthInfo(Schema, VFuncPtr, OrigMD, QualType());
  }

  CGCallee Callee(GD, VFunc, PointerAuth);
  return Callee;
}

/// BuildAppleKextVirtualCall - This routine is to support gcc's kext ABI making
/// indirect call to virtual functions. It makes the call through indexing
/// into the vtable.
CGCallee
CodeGenFunction::BuildAppleKextVirtualCall(const CXXMethodDecl *MD,
                                           NestedNameSpecifier *Qual,
                                           llvm::Type *Ty) {
  assert((Qual->getKind() == NestedNameSpecifier::TypeSpec) &&
         "BuildAppleKextVirtualCall - bad Qual kind");

  const Type *QTy = Qual->getAsType();
  QualType T = QualType(QTy, 0);
  const RecordType *RT = T->getAs<RecordType>();
  assert(RT && "BuildAppleKextVirtualCall - Qual type must be record");
  const auto *RD = cast<CXXRecordDecl>(RT->getDecl());

  if (const auto *DD = dyn_cast<CXXDestructorDecl>(MD))
    return BuildAppleKextVirtualDestructorCall(DD, Dtor_Complete, RD);

  return ::BuildAppleKextVirtualCall(*this, MD, Ty, RD);
}

/// BuildVirtualCall - This routine makes indirect vtable call for
/// call to virtual destructors. It returns 0 if it could not do it.
CGCallee
CodeGenFunction::BuildAppleKextVirtualDestructorCall(
                                            const CXXDestructorDecl *DD,
                                            CXXDtorType Type,
                                            const CXXRecordDecl *RD) {
  assert(DD->isVirtual() && Type != Dtor_Base);
  // Compute the function type we're calling.
  const CGFunctionInfo &FInfo = CGM.getTypes().arrangeCXXStructorDeclaration(
      GlobalDecl(DD, Dtor_Complete));
  llvm::Type *Ty = CGM.getTypes().GetFunctionType(FInfo);
  return ::BuildAppleKextVirtualCall(*this, GlobalDecl(DD, Type), Ty, RD);
}
