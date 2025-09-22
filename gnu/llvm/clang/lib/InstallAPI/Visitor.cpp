//===- Visitor.cpp ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/InstallAPI/Visitor.h"
#include "clang/AST/Availability.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/VTableBuilder.h"
#include "clang/Basic/Linkage.h"
#include "clang/InstallAPI/DylibVerifier.h"
#include "clang/InstallAPI/FrontendRecords.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"

using namespace llvm;
using namespace llvm::MachO;

namespace {
enum class CXXLinkage {
  ExternalLinkage,
  LinkOnceODRLinkage,
  WeakODRLinkage,
  PrivateLinkage,
};
}

namespace clang::installapi {

// Exported NamedDecl needs to have external linkage and
// default visibility from LinkageComputer.
static bool isExported(const NamedDecl *D) {
  auto LV = D->getLinkageAndVisibility();
  return isExternallyVisible(LV.getLinkage()) &&
         (LV.getVisibility() == DefaultVisibility);
}

static bool isInlined(const FunctionDecl *D) {
  bool HasInlineAttribute = false;
  bool NoCXXAttr =
      (!D->getASTContext().getLangOpts().CPlusPlus &&
       !D->getASTContext().getTargetInfo().getCXXABI().isMicrosoft() &&
       !D->hasAttr<DLLExportAttr>());

  // Check all redeclarations to find an inline attribute or keyword.
  for (const auto *RD : D->redecls()) {
    if (!RD->isInlined())
      continue;
    HasInlineAttribute = true;
    if (!(NoCXXAttr || RD->hasAttr<GNUInlineAttr>()))
      continue;
    if (RD->doesThisDeclarationHaveABody() &&
        RD->isInlineDefinitionExternallyVisible())
      return false;
  }

  if (!HasInlineAttribute)
    return false;

  return true;
}

static SymbolFlags getFlags(bool WeakDef, bool ThreadLocal = false) {
  SymbolFlags Result = SymbolFlags::None;
  if (WeakDef)
    Result |= SymbolFlags::WeakDefined;
  if (ThreadLocal)
    Result |= SymbolFlags::ThreadLocalValue;

  return Result;
}

void InstallAPIVisitor::HandleTranslationUnit(ASTContext &ASTCtx) {
  if (ASTCtx.getDiagnostics().hasErrorOccurred())
    return;

  auto *D = ASTCtx.getTranslationUnitDecl();
  TraverseDecl(D);
}

std::string InstallAPIVisitor::getMangledName(const NamedDecl *D) const {
  SmallString<256> Name;
  if (MC->shouldMangleDeclName(D)) {
    raw_svector_ostream NStream(Name);
    MC->mangleName(D, NStream);
  } else
    Name += D->getNameAsString();

  return getBackendMangledName(Name);
}

std::string InstallAPIVisitor::getBackendMangledName(Twine Name) const {
  SmallString<256> FinalName;
  Mangler::getNameWithPrefix(FinalName, Name, DataLayout(Layout));
  return std::string(FinalName);
}

std::optional<HeaderType>
InstallAPIVisitor::getAccessForDecl(const NamedDecl *D) const {
  SourceLocation Loc = D->getLocation();
  if (Loc.isInvalid())
    return std::nullopt;

  // If the loc refers to a macro expansion, InstallAPI needs to first get the
  // file location of the expansion.
  auto FileLoc = SrcMgr.getFileLoc(Loc);
  FileID ID = SrcMgr.getFileID(FileLoc);
  if (ID.isInvalid())
    return std::nullopt;

  const FileEntry *FE = SrcMgr.getFileEntryForID(ID);
  if (!FE)
    return std::nullopt;

  auto Header = Ctx.findAndRecordFile(FE, PP);
  if (!Header.has_value())
    return std::nullopt;

  HeaderType Access = Header.value();
  assert(Access != HeaderType::Unknown && "unexpected access level for global");
  return Access;
}

/// Check if the interface itself or any of its super classes have an
/// exception attribute. InstallAPI needs to export an additional symbol
/// ("OBJC_EHTYPE_$CLASS_NAME") if any of the classes have the exception
/// attribute.
static bool hasObjCExceptionAttribute(const ObjCInterfaceDecl *D) {
  for (; D != nullptr; D = D->getSuperClass())
    if (D->hasAttr<ObjCExceptionAttr>())
      return true;

  return false;
}
void InstallAPIVisitor::recordObjCInstanceVariables(
    const ASTContext &ASTCtx, ObjCContainerRecord *Record, StringRef SuperClass,
    const llvm::iterator_range<
        DeclContext::specific_decl_iterator<ObjCIvarDecl>>
        Ivars) {
  RecordLinkage Linkage = RecordLinkage::Exported;
  const RecordLinkage ContainerLinkage = Record->getLinkage();
  // If fragile, set to unknown.
  if (ASTCtx.getLangOpts().ObjCRuntime.isFragile())
    Linkage = RecordLinkage::Unknown;
  // Linkage should be inherited from container.
  else if (ContainerLinkage != RecordLinkage::Unknown)
    Linkage = ContainerLinkage;
  for (const auto *IV : Ivars) {
    auto Access = getAccessForDecl(IV);
    if (!Access)
      continue;
    StringRef Name = IV->getName();
    const AvailabilityInfo Avail = AvailabilityInfo::createFromDecl(IV);
    auto AC = IV->getCanonicalAccessControl();
    auto [ObjCIVR, FA] =
        Ctx.Slice->addObjCIVar(Record, Name, Linkage, Avail, IV, *Access, AC);
    Ctx.Verifier->verify(ObjCIVR, FA, SuperClass);
  }
}

bool InstallAPIVisitor::VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D) {
  // Skip forward declaration for classes (@class)
  if (!D->isThisDeclarationADefinition())
    return true;

  // Skip over declarations that access could not be collected for.
  auto Access = getAccessForDecl(D);
  if (!Access)
    return true;

  StringRef Name = D->getObjCRuntimeNameAsString();
  const RecordLinkage Linkage =
      isExported(D) ? RecordLinkage::Exported : RecordLinkage::Internal;
  const AvailabilityInfo Avail = AvailabilityInfo::createFromDecl(D);
  const bool IsEHType =
      (!D->getASTContext().getLangOpts().ObjCRuntime.isFragile() &&
       hasObjCExceptionAttribute(D));

  auto [Class, FA] =
      Ctx.Slice->addObjCInterface(Name, Linkage, Avail, D, *Access, IsEHType);
  Ctx.Verifier->verify(Class, FA);

  // Get base class.
  StringRef SuperClassName;
  if (const auto *SuperClass = D->getSuperClass())
    SuperClassName = SuperClass->getObjCRuntimeNameAsString();

  recordObjCInstanceVariables(D->getASTContext(), Class, Class->getName(),
                              D->ivars());
  return true;
}

bool InstallAPIVisitor::VisitObjCCategoryDecl(const ObjCCategoryDecl *D) {
  StringRef CategoryName = D->getName();
  // Skip over declarations that access could not be collected for.
  auto Access = getAccessForDecl(D);
  if (!Access)
    return true;
  const AvailabilityInfo Avail = AvailabilityInfo::createFromDecl(D);
  const ObjCInterfaceDecl *InterfaceD = D->getClassInterface();
  const StringRef InterfaceName = InterfaceD->getName();

  ObjCCategoryRecord *CategoryRecord =
      Ctx.Slice->addObjCCategory(InterfaceName, CategoryName, Avail, D, *Access)
          .first;
  recordObjCInstanceVariables(D->getASTContext(), CategoryRecord, InterfaceName,
                              D->ivars());
  return true;
}

bool InstallAPIVisitor::VisitVarDecl(const VarDecl *D) {
  // Skip function parameters.
  if (isa<ParmVarDecl>(D))
    return true;

  // Skip variables in records. They are handled separately for C++.
  if (D->getDeclContext()->isRecord())
    return true;

  // Skip anything inside functions or methods.
  if (!D->isDefinedOutsideFunctionOrMethod())
    return true;

  // If this is a template but not specialization or instantiation, skip.
  if (D->getASTContext().getTemplateOrSpecializationInfo(D) &&
      D->getTemplateSpecializationKind() == TSK_Undeclared)
    return true;

  // Skip over declarations that access could not collected for.
  auto Access = getAccessForDecl(D);
  if (!Access)
    return true;

  const RecordLinkage Linkage =
      isExported(D) ? RecordLinkage::Exported : RecordLinkage::Internal;
  const bool WeakDef = D->hasAttr<WeakAttr>();
  const bool ThreadLocal = D->getTLSKind() != VarDecl::TLS_None;
  const AvailabilityInfo Avail = AvailabilityInfo::createFromDecl(D);
  auto [GR, FA] = Ctx.Slice->addGlobal(getMangledName(D), Linkage,
                                       GlobalRecord::Kind::Variable, Avail, D,
                                       *Access, getFlags(WeakDef, ThreadLocal));
  Ctx.Verifier->verify(GR, FA);
  return true;
}

bool InstallAPIVisitor::VisitFunctionDecl(const FunctionDecl *D) {
  if (const CXXMethodDecl *M = dyn_cast<CXXMethodDecl>(D)) {
    // Skip member function in class templates.
    if (M->getParent()->getDescribedClassTemplate() != nullptr)
      return true;

    // Skip methods in CXX RecordDecls.
    for (const DynTypedNode &P : D->getASTContext().getParents(*M)) {
      if (P.get<CXXRecordDecl>())
        return true;
    }

    // Skip CXX ConstructorDecls and DestructorDecls.
    if (isa<CXXConstructorDecl>(M) || isa<CXXDestructorDecl>(M))
      return true;
  }

  // Skip templated functions.
  switch (D->getTemplatedKind()) {
  case FunctionDecl::TK_NonTemplate:
  case FunctionDecl::TK_DependentNonTemplate:
    break;
  case FunctionDecl::TK_MemberSpecialization:
  case FunctionDecl::TK_FunctionTemplateSpecialization:
    if (auto *TempInfo = D->getTemplateSpecializationInfo()) {
      if (!TempInfo->isExplicitInstantiationOrSpecialization())
        return true;
    }
    break;
  case FunctionDecl::TK_FunctionTemplate:
  case FunctionDecl::TK_DependentFunctionTemplateSpecialization:
    return true;
  }

  auto Access = getAccessForDecl(D);
  if (!Access)
    return true;
  auto Name = getMangledName(D);
  const AvailabilityInfo Avail = AvailabilityInfo::createFromDecl(D);
  const bool ExplicitInstantiation = D->getTemplateSpecializationKind() ==
                                     TSK_ExplicitInstantiationDeclaration;
  const bool WeakDef = ExplicitInstantiation || D->hasAttr<WeakAttr>();
  const bool Inlined = isInlined(D);
  const RecordLinkage Linkage = (Inlined || !isExported(D))
                                    ? RecordLinkage::Internal
                                    : RecordLinkage::Exported;
  auto [GR, FA] =
      Ctx.Slice->addGlobal(Name, Linkage, GlobalRecord::Kind::Function, Avail,
                           D, *Access, getFlags(WeakDef), Inlined);
  Ctx.Verifier->verify(GR, FA);
  return true;
}

static bool hasVTable(const CXXRecordDecl *D) {
  // Check if vtable symbols should be emitted, only dynamic classes need
  // vtables.
  if (!D->hasDefinition() || !D->isDynamicClass())
    return false;

  assert(D->isExternallyVisible() && "Should be externally visible");
  assert(D->isCompleteDefinition() && "Only works on complete definitions");

  const CXXMethodDecl *KeyFunctionD =
      D->getASTContext().getCurrentKeyFunction(D);
  // If this class has a key function, then there is a vtable, possibly internal
  // though.
  if (KeyFunctionD) {
    switch (KeyFunctionD->getTemplateSpecializationKind()) {
    case TSK_Undeclared:
    case TSK_ExplicitSpecialization:
    case TSK_ImplicitInstantiation:
    case TSK_ExplicitInstantiationDefinition:
      return true;
    case TSK_ExplicitInstantiationDeclaration:
      llvm_unreachable(
          "Unexpected TemplateSpecializationKind for key function");
    }
  } else if (D->isAbstract()) {
    // If the class is abstract and it doesn't have a key function, it is a
    // 'pure' virtual class. It doesn't need a vtable.
    return false;
  }

  switch (D->getTemplateSpecializationKind()) {
  case TSK_Undeclared:
  case TSK_ExplicitSpecialization:
  case TSK_ImplicitInstantiation:
    return false;

  case TSK_ExplicitInstantiationDeclaration:
  case TSK_ExplicitInstantiationDefinition:
    return true;
  }

  llvm_unreachable("Invalid TemplateSpecializationKind!");
}

static CXXLinkage getVTableLinkage(const CXXRecordDecl *D) {
  assert((D->hasDefinition() && D->isDynamicClass()) && "Record has no vtable");
  assert(D->isExternallyVisible() && "Record should be externally visible");
  if (D->getVisibility() == HiddenVisibility)
    return CXXLinkage::PrivateLinkage;

  const CXXMethodDecl *KeyFunctionD =
      D->getASTContext().getCurrentKeyFunction(D);
  if (KeyFunctionD) {
    // If this class has a key function, use that to determine the
    // linkage of the vtable.
    switch (KeyFunctionD->getTemplateSpecializationKind()) {
    case TSK_Undeclared:
    case TSK_ExplicitSpecialization:
      if (isInlined(KeyFunctionD))
        return CXXLinkage::LinkOnceODRLinkage;
      return CXXLinkage::ExternalLinkage;
    case TSK_ImplicitInstantiation:
      llvm_unreachable("No external vtable for implicit instantiations");
    case TSK_ExplicitInstantiationDefinition:
      return CXXLinkage::WeakODRLinkage;
    case TSK_ExplicitInstantiationDeclaration:
      llvm_unreachable(
          "Unexpected TemplateSpecializationKind for key function");
    }
  }

  switch (D->getTemplateSpecializationKind()) {
  case TSK_Undeclared:
  case TSK_ExplicitSpecialization:
  case TSK_ImplicitInstantiation:
    return CXXLinkage::LinkOnceODRLinkage;
  case TSK_ExplicitInstantiationDeclaration:
  case TSK_ExplicitInstantiationDefinition:
    return CXXLinkage::WeakODRLinkage;
  }

  llvm_unreachable("Invalid TemplateSpecializationKind!");
}

static bool isRTTIWeakDef(const CXXRecordDecl *D) {
  if (D->hasAttr<WeakAttr>())
    return true;

  if (D->isAbstract() && D->getASTContext().getCurrentKeyFunction(D) == nullptr)
    return true;

  if (D->isDynamicClass())
    return getVTableLinkage(D) != CXXLinkage::ExternalLinkage;

  return false;
}

static bool hasRTTI(const CXXRecordDecl *D) {
  if (!D->getASTContext().getLangOpts().RTTI)
    return false;

  if (!D->hasDefinition())
    return false;

  if (!D->isDynamicClass())
    return false;

  // Don't emit weak-def RTTI information. InstallAPI cannot reliably determine
  // if the final binary will have those weak defined RTTI symbols. This depends
  // on the optimization level and if the class has been instantiated and used.
  //
  // Luckily, the Apple static linker doesn't need those weak defined RTTI
  // symbols for linking. They are only needed by the runtime linker. That means
  // they can be safely dropped.
  if (isRTTIWeakDef(D))
    return false;

  return true;
}

std::string
InstallAPIVisitor::getMangledCXXRTTIName(const CXXRecordDecl *D) const {
  SmallString<256> Name;
  raw_svector_ostream NameStream(Name);
  MC->mangleCXXRTTIName(QualType(D->getTypeForDecl(), 0), NameStream);

  return getBackendMangledName(Name);
}

std::string InstallAPIVisitor::getMangledCXXRTTI(const CXXRecordDecl *D) const {
  SmallString<256> Name;
  raw_svector_ostream NameStream(Name);
  MC->mangleCXXRTTI(QualType(D->getTypeForDecl(), 0), NameStream);

  return getBackendMangledName(Name);
}

std::string
InstallAPIVisitor::getMangledCXXVTableName(const CXXRecordDecl *D) const {
  SmallString<256> Name;
  raw_svector_ostream NameStream(Name);
  MC->mangleCXXVTable(D, NameStream);

  return getBackendMangledName(Name);
}

std::string InstallAPIVisitor::getMangledCXXThunk(
    const GlobalDecl &D, const ThunkInfo &Thunk, bool ElideOverrideInfo) const {
  SmallString<256> Name;
  raw_svector_ostream NameStream(Name);
  const auto *Method = cast<CXXMethodDecl>(D.getDecl());
  if (const auto *Dtor = dyn_cast<CXXDestructorDecl>(Method))
    MC->mangleCXXDtorThunk(Dtor, D.getDtorType(), Thunk, ElideOverrideInfo,
                           NameStream);
  else
    MC->mangleThunk(Method, Thunk, ElideOverrideInfo, NameStream);

  return getBackendMangledName(Name);
}

std::string InstallAPIVisitor::getMangledCtorDtor(const CXXMethodDecl *D,
                                                  int Type) const {
  SmallString<256> Name;
  raw_svector_ostream NameStream(Name);
  GlobalDecl GD;
  if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(D))
    GD = GlobalDecl(Ctor, CXXCtorType(Type));
  else {
    const auto *Dtor = cast<CXXDestructorDecl>(D);
    GD = GlobalDecl(Dtor, CXXDtorType(Type));
  }
  MC->mangleName(GD, NameStream);
  return getBackendMangledName(Name);
}

void InstallAPIVisitor::emitVTableSymbols(const CXXRecordDecl *D,
                                          const AvailabilityInfo &Avail,
                                          const HeaderType Access,
                                          bool EmittedVTable) {
  if (hasVTable(D)) {
    EmittedVTable = true;
    const CXXLinkage VTableLinkage = getVTableLinkage(D);
    if (VTableLinkage == CXXLinkage::ExternalLinkage ||
        VTableLinkage == CXXLinkage::WeakODRLinkage) {
      const std::string Name = getMangledCXXVTableName(D);
      const bool WeakDef = VTableLinkage == CXXLinkage::WeakODRLinkage;
      auto [GR, FA] = Ctx.Slice->addGlobal(Name, RecordLinkage::Exported,
                                           GlobalRecord::Kind::Variable, Avail,
                                           D, Access, getFlags(WeakDef));
      Ctx.Verifier->verify(GR, FA);
      if (!D->getDescribedClassTemplate() && !D->isInvalidDecl()) {
        VTableContextBase *VTable = D->getASTContext().getVTableContext();
        auto AddThunk = [&](GlobalDecl GD) {
          const ItaniumVTableContext::ThunkInfoVectorTy *Thunks =
              VTable->getThunkInfo(GD);
          if (!Thunks)
            return;

          for (const auto &Thunk : *Thunks) {
            const std::string Name =
                getMangledCXXThunk(GD, Thunk, /*ElideOverrideInfo=*/true);
            auto [GR, FA] = Ctx.Slice->addGlobal(Name, RecordLinkage::Exported,
                                                 GlobalRecord::Kind::Function,
                                                 Avail, GD.getDecl(), Access);
            Ctx.Verifier->verify(GR, FA);
          }
        };

        for (const auto *Method : D->methods()) {
          if (isa<CXXConstructorDecl>(Method) || !Method->isVirtual())
            continue;

          if (auto Dtor = dyn_cast<CXXDestructorDecl>(Method)) {
            // Skip default destructor.
            if (Dtor->isDefaulted())
              continue;
            AddThunk({Dtor, Dtor_Deleting});
            AddThunk({Dtor, Dtor_Complete});
          } else
            AddThunk(Method);
        }
      }
    }
  }

  if (!EmittedVTable)
    return;

  if (hasRTTI(D)) {
    std::string Name = getMangledCXXRTTI(D);
    auto [GR, FA] =
        Ctx.Slice->addGlobal(Name, RecordLinkage::Exported,
                             GlobalRecord::Kind::Variable, Avail, D, Access);
    Ctx.Verifier->verify(GR, FA);

    Name = getMangledCXXRTTIName(D);
    auto [NamedGR, NamedFA] =
        Ctx.Slice->addGlobal(Name, RecordLinkage::Exported,
                             GlobalRecord::Kind::Variable, Avail, D, Access);
    Ctx.Verifier->verify(NamedGR, NamedFA);
  }

  for (const auto &It : D->bases()) {
    const CXXRecordDecl *Base =
        cast<CXXRecordDecl>(It.getType()->castAs<RecordType>()->getDecl());
    const auto BaseAccess = getAccessForDecl(Base);
    if (!BaseAccess)
      continue;
    const AvailabilityInfo BaseAvail = AvailabilityInfo::createFromDecl(Base);
    emitVTableSymbols(Base, BaseAvail, *BaseAccess, /*EmittedVTable=*/true);
  }
}

bool InstallAPIVisitor::VisitCXXRecordDecl(const CXXRecordDecl *D) {
  if (!D->isCompleteDefinition())
    return true;

  // Skip templated classes.
  if (D->getDescribedClassTemplate() != nullptr)
    return true;

  // Skip partial templated classes too.
  if (isa<ClassTemplatePartialSpecializationDecl>(D))
    return true;

  auto Access = getAccessForDecl(D);
  if (!Access)
    return true;
  const AvailabilityInfo Avail = AvailabilityInfo::createFromDecl(D);

  // Check whether to emit the vtable/rtti symbols.
  if (isExported(D))
    emitVTableSymbols(D, Avail, *Access);

  TemplateSpecializationKind ClassSK = TSK_Undeclared;
  bool KeepInlineAsWeak = false;
  if (auto *Templ = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
    ClassSK = Templ->getTemplateSpecializationKind();
    if (ClassSK == TSK_ExplicitInstantiationDeclaration)
      KeepInlineAsWeak = true;
  }

  // Record the class methods.
  for (const auto *M : D->methods()) {
    // Inlined methods are usually not emitted, except when it comes from a
    // specialized template.
    bool WeakDef = false;
    if (isInlined(M)) {
      if (!KeepInlineAsWeak)
        continue;

      WeakDef = true;
    }

    if (!isExported(M))
      continue;

    switch (M->getTemplateSpecializationKind()) {
    case TSK_Undeclared:
    case TSK_ExplicitSpecialization:
      break;
    case TSK_ImplicitInstantiation:
      continue;
    case TSK_ExplicitInstantiationDeclaration:
      if (ClassSK == TSK_ExplicitInstantiationDeclaration)
        WeakDef = true;
      break;
    case TSK_ExplicitInstantiationDefinition:
      WeakDef = true;
      break;
    }

    if (!M->isUserProvided())
      continue;

    // Methods that are deleted are not exported.
    if (M->isDeleted())
      continue;

    const auto Access = getAccessForDecl(M);
    if (!Access)
      return true;
    const AvailabilityInfo Avail = AvailabilityInfo::createFromDecl(M);

    if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(M)) {
      // Defaulted constructors are not exported.
      if (Ctor->isDefaulted())
        continue;

      std::string Name = getMangledCtorDtor(M, Ctor_Base);
      auto [GR, FA] = Ctx.Slice->addGlobal(Name, RecordLinkage::Exported,
                                           GlobalRecord::Kind::Function, Avail,
                                           D, *Access, getFlags(WeakDef));
      Ctx.Verifier->verify(GR, FA);

      if (!D->isAbstract()) {
        std::string Name = getMangledCtorDtor(M, Ctor_Complete);
        auto [GR, FA] = Ctx.Slice->addGlobal(
            Name, RecordLinkage::Exported, GlobalRecord::Kind::Function, Avail,
            D, *Access, getFlags(WeakDef));
        Ctx.Verifier->verify(GR, FA);
      }

      continue;
    }

    if (const auto *Dtor = dyn_cast<CXXDestructorDecl>(M)) {
      // Defaulted destructors are not exported.
      if (Dtor->isDefaulted())
        continue;

      std::string Name = getMangledCtorDtor(M, Dtor_Base);
      auto [GR, FA] = Ctx.Slice->addGlobal(Name, RecordLinkage::Exported,
                                           GlobalRecord::Kind::Function, Avail,
                                           D, *Access, getFlags(WeakDef));
      Ctx.Verifier->verify(GR, FA);

      Name = getMangledCtorDtor(M, Dtor_Complete);
      auto [CompleteGR, CompleteFA] = Ctx.Slice->addGlobal(
          Name, RecordLinkage::Exported, GlobalRecord::Kind::Function, Avail, D,
          *Access, getFlags(WeakDef));
      Ctx.Verifier->verify(CompleteGR, CompleteFA);

      if (Dtor->isVirtual()) {
        Name = getMangledCtorDtor(M, Dtor_Deleting);
        auto [VirtualGR, VirtualFA] = Ctx.Slice->addGlobal(
            Name, RecordLinkage::Exported, GlobalRecord::Kind::Function, Avail,
            D, *Access, getFlags(WeakDef));
        Ctx.Verifier->verify(VirtualGR, VirtualFA);
      }

      continue;
    }

    // Though abstract methods can map to exports, this is generally unexpected.
    // Except in the case of destructors. Only ignore pure virtuals after
    // checking if the member function was a destructor.
    if (M->isPureVirtual())
      continue;

    std::string Name = getMangledName(M);
    auto [GR, FA] = Ctx.Slice->addGlobal(Name, RecordLinkage::Exported,
                                         GlobalRecord::Kind::Function, Avail, M,
                                         *Access, getFlags(WeakDef));
    Ctx.Verifier->verify(GR, FA);
  }

  if (auto *Templ = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
    if (!Templ->isExplicitInstantiationOrSpecialization())
      return true;
  }

  using var_iter = CXXRecordDecl::specific_decl_iterator<VarDecl>;
  using var_range = iterator_range<var_iter>;
  for (const auto *Var : var_range(D->decls())) {
    // Skip const static member variables.
    // \code
    // struct S {
    //   static const int x = 0;
    // };
    // \endcode
    if (Var->isStaticDataMember() && Var->hasInit())
      continue;

    // Skip unexported var decls.
    if (!isExported(Var))
      continue;

    const std::string Name = getMangledName(Var);
    const auto Access = getAccessForDecl(Var);
    if (!Access)
      return true;
    const AvailabilityInfo Avail = AvailabilityInfo::createFromDecl(Var);
    const bool WeakDef = Var->hasAttr<WeakAttr>() || KeepInlineAsWeak;

    auto [GR, FA] = Ctx.Slice->addGlobal(Name, RecordLinkage::Exported,
                                         GlobalRecord::Kind::Variable, Avail, D,
                                         *Access, getFlags(WeakDef));
    Ctx.Verifier->verify(GR, FA);
  }

  return true;
}

} // namespace clang::installapi
