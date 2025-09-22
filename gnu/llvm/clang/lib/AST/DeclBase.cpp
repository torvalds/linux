//===- DeclBase.cpp - Declaration AST Node Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Decl and DeclContext classes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclBase.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/Attr.h"
#include "clang/AST/AttrIterator.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DependentDiagnostic.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <tuple>
#include <utility>

using namespace clang;

//===----------------------------------------------------------------------===//
//  Statistics
//===----------------------------------------------------------------------===//

#define DECL(DERIVED, BASE) static int n##DERIVED##s = 0;
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"

void Decl::updateOutOfDate(IdentifierInfo &II) const {
  getASTContext().getExternalSource()->updateOutOfDateIdentifier(II);
}

#define DECL(DERIVED, BASE)                                                    \
  static_assert(alignof(Decl) >= alignof(DERIVED##Decl),                       \
                "Alignment sufficient after objects prepended to " #DERIVED);
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"

void *Decl::operator new(std::size_t Size, const ASTContext &Context,
                         GlobalDeclID ID, std::size_t Extra) {
  // Allocate an extra 8 bytes worth of storage, which ensures that the
  // resulting pointer will still be 8-byte aligned.
  static_assert(sizeof(uint64_t) >= alignof(Decl), "Decl won't be misaligned");
  void *Start = Context.Allocate(Size + Extra + 8);
  void *Result = (char*)Start + 8;

  uint64_t *PrefixPtr = (uint64_t *)Result - 1;

  *PrefixPtr = ID.getRawValue();

  // We leave the upper 16 bits to store the module IDs. 48 bits should be
  // sufficient to store a declaration ID.
  assert(*PrefixPtr < llvm::maskTrailingOnes<uint64_t>(48));

  return Result;
}

void *Decl::operator new(std::size_t Size, const ASTContext &Ctx,
                         DeclContext *Parent, std::size_t Extra) {
  assert(!Parent || &Parent->getParentASTContext() == &Ctx);
  // With local visibility enabled, we track the owning module even for local
  // declarations. We create the TU decl early and may not yet know what the
  // LangOpts are, so conservatively allocate the storage.
  if (Ctx.getLangOpts().trackLocalOwningModule() || !Parent) {
    // Ensure required alignment of the resulting object by adding extra
    // padding at the start if required.
    size_t ExtraAlign =
        llvm::offsetToAlignment(sizeof(Module *), llvm::Align(alignof(Decl)));
    auto *Buffer = reinterpret_cast<char *>(
        ::operator new(ExtraAlign + sizeof(Module *) + Size + Extra, Ctx));
    Buffer += ExtraAlign;
    auto *ParentModule =
        Parent ? cast<Decl>(Parent)->getOwningModule() : nullptr;
    return new (Buffer) Module*(ParentModule) + 1;
  }
  return ::operator new(Size + Extra, Ctx);
}

GlobalDeclID Decl::getGlobalID() const {
  if (!isFromASTFile())
    return GlobalDeclID();
  // See the comments in `Decl::operator new` for details.
  uint64_t ID = *((const uint64_t *)this - 1);
  return GlobalDeclID(ID & llvm::maskTrailingOnes<uint64_t>(48));
}

unsigned Decl::getOwningModuleID() const {
  if (!isFromASTFile())
    return 0;

  uint64_t ID = *((const uint64_t *)this - 1);
  return ID >> 48;
}

void Decl::setOwningModuleID(unsigned ID) {
  assert(isFromASTFile() && "Only works on a deserialized declaration");
  uint64_t *IDAddress = (uint64_t *)this - 1;
  *IDAddress &= llvm::maskTrailingOnes<uint64_t>(48);
  *IDAddress |= (uint64_t)ID << 48;
}

Module *Decl::getOwningModuleSlow() const {
  assert(isFromASTFile() && "Not from AST file?");
  return getASTContext().getExternalSource()->getModule(getOwningModuleID());
}

bool Decl::hasLocalOwningModuleStorage() const {
  return getASTContext().getLangOpts().trackLocalOwningModule();
}

const char *Decl::getDeclKindName() const {
  switch (DeclKind) {
  default: llvm_unreachable("Declaration not in DeclNodes.inc!");
#define DECL(DERIVED, BASE) case DERIVED: return #DERIVED;
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
  }
}

void Decl::setInvalidDecl(bool Invalid) {
  InvalidDecl = Invalid;
  assert(!isa<TagDecl>(this) || !cast<TagDecl>(this)->isCompleteDefinition());
  if (!Invalid) {
    return;
  }

  if (!isa<ParmVarDecl>(this)) {
    // Defensive maneuver for ill-formed code: we're likely not to make it to
    // a point where we set the access specifier, so default it to "public"
    // to avoid triggering asserts elsewhere in the front end.
    setAccess(AS_public);
  }

  // Marking a DecompositionDecl as invalid implies all the child BindingDecl's
  // are invalid too.
  if (auto *DD = dyn_cast<DecompositionDecl>(this)) {
    for (auto *Binding : DD->bindings()) {
      Binding->setInvalidDecl();
    }
  }
}

bool DeclContext::hasValidDeclKind() const {
  switch (getDeclKind()) {
#define DECL(DERIVED, BASE) case Decl::DERIVED: return true;
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
  }
  return false;
}

const char *DeclContext::getDeclKindName() const {
  switch (getDeclKind()) {
#define DECL(DERIVED, BASE) case Decl::DERIVED: return #DERIVED;
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
  }
  llvm_unreachable("Declaration context not in DeclNodes.inc!");
}

bool Decl::StatisticsEnabled = false;
void Decl::EnableStatistics() {
  StatisticsEnabled = true;
}

void Decl::PrintStats() {
  llvm::errs() << "\n*** Decl Stats:\n";

  int totalDecls = 0;
#define DECL(DERIVED, BASE) totalDecls += n##DERIVED##s;
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
  llvm::errs() << "  " << totalDecls << " decls total.\n";

  int totalBytes = 0;
#define DECL(DERIVED, BASE)                                             \
  if (n##DERIVED##s > 0) {                                              \
    totalBytes += (int)(n##DERIVED##s * sizeof(DERIVED##Decl));         \
    llvm::errs() << "    " << n##DERIVED##s << " " #DERIVED " decls, "  \
                 << sizeof(DERIVED##Decl) << " each ("                  \
                 << n##DERIVED##s * sizeof(DERIVED##Decl)               \
                 << " bytes)\n";                                        \
  }
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"

  llvm::errs() << "Total bytes = " << totalBytes << "\n";
}

void Decl::add(Kind k) {
  switch (k) {
#define DECL(DERIVED, BASE) case DERIVED: ++n##DERIVED##s; break;
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
  }
}

bool Decl::isTemplateParameterPack() const {
  if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(this))
    return TTP->isParameterPack();
  if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(this))
    return NTTP->isParameterPack();
  if (const auto *TTP = dyn_cast<TemplateTemplateParmDecl>(this))
    return TTP->isParameterPack();
  return false;
}

bool Decl::isParameterPack() const {
  if (const auto *Var = dyn_cast<VarDecl>(this))
    return Var->isParameterPack();

  return isTemplateParameterPack();
}

FunctionDecl *Decl::getAsFunction() {
  if (auto *FD = dyn_cast<FunctionDecl>(this))
    return FD;
  if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(this))
    return FTD->getTemplatedDecl();
  return nullptr;
}

bool Decl::isTemplateDecl() const {
  return isa<TemplateDecl>(this);
}

TemplateDecl *Decl::getDescribedTemplate() const {
  if (auto *FD = dyn_cast<FunctionDecl>(this))
    return FD->getDescribedFunctionTemplate();
  if (auto *RD = dyn_cast<CXXRecordDecl>(this))
    return RD->getDescribedClassTemplate();
  if (auto *VD = dyn_cast<VarDecl>(this))
    return VD->getDescribedVarTemplate();
  if (auto *AD = dyn_cast<TypeAliasDecl>(this))
    return AD->getDescribedAliasTemplate();

  return nullptr;
}

const TemplateParameterList *Decl::getDescribedTemplateParams() const {
  if (auto *TD = getDescribedTemplate())
    return TD->getTemplateParameters();
  if (auto *CTPSD = dyn_cast<ClassTemplatePartialSpecializationDecl>(this))
    return CTPSD->getTemplateParameters();
  if (auto *VTPSD = dyn_cast<VarTemplatePartialSpecializationDecl>(this))
    return VTPSD->getTemplateParameters();
  return nullptr;
}

bool Decl::isTemplated() const {
  // A declaration is templated if it is a template or a template pattern, or
  // is within (lexcially for a friend or local function declaration,
  // semantically otherwise) a dependent context.
  if (auto *AsDC = dyn_cast<DeclContext>(this))
    return AsDC->isDependentContext();
  auto *DC = getFriendObjectKind() || isLocalExternDecl()
      ? getLexicalDeclContext() : getDeclContext();
  return DC->isDependentContext() || isTemplateDecl() ||
         getDescribedTemplateParams();
}

unsigned Decl::getTemplateDepth() const {
  if (auto *DC = dyn_cast<DeclContext>(this))
    if (DC->isFileContext())
      return 0;

  if (auto *TPL = getDescribedTemplateParams())
    return TPL->getDepth() + 1;

  // If this is a dependent lambda, there might be an enclosing variable
  // template. In this case, the next step is not the parent DeclContext (or
  // even a DeclContext at all).
  auto *RD = dyn_cast<CXXRecordDecl>(this);
  if (RD && RD->isDependentLambda())
    if (Decl *Context = RD->getLambdaContextDecl())
      return Context->getTemplateDepth();

  const DeclContext *DC =
      getFriendObjectKind() ? getLexicalDeclContext() : getDeclContext();
  return cast<Decl>(DC)->getTemplateDepth();
}

const DeclContext *Decl::getParentFunctionOrMethod(bool LexicalParent) const {
  for (const DeclContext *DC = LexicalParent ? getLexicalDeclContext()
                                             : getDeclContext();
       DC && !DC->isFileContext(); DC = DC->getParent())
    if (DC->isFunctionOrMethod())
      return DC;

  return nullptr;
}

//===----------------------------------------------------------------------===//
// PrettyStackTraceDecl Implementation
//===----------------------------------------------------------------------===//

void PrettyStackTraceDecl::print(raw_ostream &OS) const {
  SourceLocation TheLoc = Loc;
  if (TheLoc.isInvalid() && TheDecl)
    TheLoc = TheDecl->getLocation();

  if (TheLoc.isValid()) {
    TheLoc.print(OS, SM);
    OS << ": ";
  }

  OS << Message;

  if (const auto *DN = dyn_cast_or_null<NamedDecl>(TheDecl)) {
    OS << " '";
    DN->printQualifiedName(OS);
    OS << '\'';
  }
  OS << '\n';
}

//===----------------------------------------------------------------------===//
// Decl Implementation
//===----------------------------------------------------------------------===//

// Out-of-line virtual method providing a home for Decl.
Decl::~Decl() = default;

void Decl::setDeclContext(DeclContext *DC) {
  DeclCtx = DC;
}

void Decl::setLexicalDeclContext(DeclContext *DC) {
  if (DC == getLexicalDeclContext())
    return;

  if (isInSemaDC()) {
    setDeclContextsImpl(getDeclContext(), DC, getASTContext());
  } else {
    getMultipleDC()->LexicalDC = DC;
  }

  // FIXME: We shouldn't be changing the lexical context of declarations
  // imported from AST files.
  if (!isFromASTFile()) {
    setModuleOwnershipKind(getModuleOwnershipKindForChildOf(DC));
    if (hasOwningModule())
      setLocalOwningModule(cast<Decl>(DC)->getOwningModule());
  }

  assert(
      (getModuleOwnershipKind() != ModuleOwnershipKind::VisibleWhenImported ||
       getOwningModule()) &&
      "hidden declaration has no owning module");
}

void Decl::setDeclContextsImpl(DeclContext *SemaDC, DeclContext *LexicalDC,
                               ASTContext &Ctx) {
  if (SemaDC == LexicalDC) {
    DeclCtx = SemaDC;
  } else {
    auto *MDC = new (Ctx) Decl::MultipleDC();
    MDC->SemanticDC = SemaDC;
    MDC->LexicalDC = LexicalDC;
    DeclCtx = MDC;
  }
}

bool Decl::isInLocalScopeForInstantiation() const {
  const DeclContext *LDC = getLexicalDeclContext();
  if (!LDC->isDependentContext())
    return false;
  while (true) {
    if (LDC->isFunctionOrMethod())
      return true;
    if (!isa<TagDecl>(LDC))
      return false;
    if (const auto *CRD = dyn_cast<CXXRecordDecl>(LDC))
      if (CRD->isLambda())
        return true;
    LDC = LDC->getLexicalParent();
  }
  return false;
}

bool Decl::isInAnonymousNamespace() const {
  for (const DeclContext *DC = getDeclContext(); DC; DC = DC->getParent()) {
    if (const auto *ND = dyn_cast<NamespaceDecl>(DC))
      if (ND->isAnonymousNamespace())
        return true;
  }

  return false;
}

bool Decl::isInStdNamespace() const {
  const DeclContext *DC = getDeclContext();
  return DC && DC->getNonTransparentContext()->isStdNamespace();
}

bool Decl::isFileContextDecl() const {
  const auto *DC = dyn_cast<DeclContext>(this);
  return DC && DC->isFileContext();
}

bool Decl::isFlexibleArrayMemberLike(
    ASTContext &Ctx, const Decl *D, QualType Ty,
    LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel,
    bool IgnoreTemplateOrMacroSubstitution) {
  // For compatibility with existing code, we treat arrays of length 0 or
  // 1 as flexible array members.
  const auto *CAT = Ctx.getAsConstantArrayType(Ty);
  if (CAT) {
    using FAMKind = LangOptions::StrictFlexArraysLevelKind;

    llvm::APInt Size = CAT->getSize();
    if (StrictFlexArraysLevel == FAMKind::IncompleteOnly)
      return false;

    // GCC extension, only allowed to represent a FAM.
    if (Size.isZero())
      return true;

    if (StrictFlexArraysLevel == FAMKind::ZeroOrIncomplete && Size.uge(1))
      return false;

    if (StrictFlexArraysLevel == FAMKind::OneZeroOrIncomplete && Size.uge(2))
      return false;
  } else if (!Ctx.getAsIncompleteArrayType(Ty)) {
    return false;
  }

  if (const auto *OID = dyn_cast_if_present<ObjCIvarDecl>(D))
    return OID->getNextIvar() == nullptr;

  const auto *FD = dyn_cast_if_present<FieldDecl>(D);
  if (!FD)
    return false;

  if (CAT) {
    // GCC treats an array memeber of a union as an FAM if the size is one or
    // zero.
    llvm::APInt Size = CAT->getSize();
    if (FD->getParent()->isUnion() && (Size.isZero() || Size.isOne()))
      return true;
  }

  // Don't consider sizes resulting from macro expansions or template argument
  // substitution to form C89 tail-padded arrays.
  if (IgnoreTemplateOrMacroSubstitution) {
    TypeSourceInfo *TInfo = FD->getTypeSourceInfo();
    while (TInfo) {
      TypeLoc TL = TInfo->getTypeLoc();

      // Look through typedefs.
      if (TypedefTypeLoc TTL = TL.getAsAdjusted<TypedefTypeLoc>()) {
        const TypedefNameDecl *TDL = TTL.getTypedefNameDecl();
        TInfo = TDL->getTypeSourceInfo();
        continue;
      }

      if (auto CTL = TL.getAs<ConstantArrayTypeLoc>()) {
        if (const Expr *SizeExpr =
                dyn_cast_if_present<IntegerLiteral>(CTL.getSizeExpr());
            !SizeExpr || SizeExpr->getExprLoc().isMacroID())
          return false;
      }

      break;
    }
  }

  // Test that the field is the last in the structure.
  RecordDecl::field_iterator FI(
      DeclContext::decl_iterator(const_cast<FieldDecl *>(FD)));
  return ++FI == FD->getParent()->field_end();
}

TranslationUnitDecl *Decl::getTranslationUnitDecl() {
  if (auto *TUD = dyn_cast<TranslationUnitDecl>(this))
    return TUD;

  DeclContext *DC = getDeclContext();
  assert(DC && "This decl is not contained in a translation unit!");

  while (!DC->isTranslationUnit()) {
    DC = DC->getParent();
    assert(DC && "This decl is not contained in a translation unit!");
  }

  return cast<TranslationUnitDecl>(DC);
}

ASTContext &Decl::getASTContext() const {
  return getTranslationUnitDecl()->getASTContext();
}

/// Helper to get the language options from the ASTContext.
/// Defined out of line to avoid depending on ASTContext.h.
const LangOptions &Decl::getLangOpts() const {
  return getASTContext().getLangOpts();
}

ASTMutationListener *Decl::getASTMutationListener() const {
  return getASTContext().getASTMutationListener();
}

unsigned Decl::getMaxAlignment() const {
  if (!hasAttrs())
    return 0;

  unsigned Align = 0;
  const AttrVec &V = getAttrs();
  ASTContext &Ctx = getASTContext();
  specific_attr_iterator<AlignedAttr> I(V.begin()), E(V.end());
  for (; I != E; ++I) {
    if (!I->isAlignmentErrorDependent())
      Align = std::max(Align, I->getAlignment(Ctx));
  }
  return Align;
}

bool Decl::isUsed(bool CheckUsedAttr) const {
  const Decl *CanonD = getCanonicalDecl();
  if (CanonD->Used)
    return true;

  // Check for used attribute.
  // Ask the most recent decl, since attributes accumulate in the redecl chain.
  if (CheckUsedAttr && getMostRecentDecl()->hasAttr<UsedAttr>())
    return true;

  // The information may have not been deserialized yet. Force deserialization
  // to complete the needed information.
  return getMostRecentDecl()->getCanonicalDecl()->Used;
}

void Decl::markUsed(ASTContext &C) {
  if (isUsed(false))
    return;

  if (C.getASTMutationListener())
    C.getASTMutationListener()->DeclarationMarkedUsed(this);

  setIsUsed();
}

bool Decl::isReferenced() const {
  if (Referenced)
    return true;

  // Check redeclarations.
  for (const auto *I : redecls())
    if (I->Referenced)
      return true;

  return false;
}

ExternalSourceSymbolAttr *Decl::getExternalSourceSymbolAttr() const {
  const Decl *Definition = nullptr;
  if (auto *ID = dyn_cast<ObjCInterfaceDecl>(this)) {
    Definition = ID->getDefinition();
  } else if (auto *PD = dyn_cast<ObjCProtocolDecl>(this)) {
    Definition = PD->getDefinition();
  } else if (auto *TD = dyn_cast<TagDecl>(this)) {
    Definition = TD->getDefinition();
  }
  if (!Definition)
    Definition = this;

  if (auto *attr = Definition->getAttr<ExternalSourceSymbolAttr>())
    return attr;
  if (auto *dcd = dyn_cast<Decl>(getDeclContext())) {
    return dcd->getAttr<ExternalSourceSymbolAttr>();
  }

  return nullptr;
}

bool Decl::hasDefiningAttr() const {
  return hasAttr<AliasAttr>() || hasAttr<IFuncAttr>() ||
         hasAttr<LoaderUninitializedAttr>();
}

const Attr *Decl::getDefiningAttr() const {
  if (auto *AA = getAttr<AliasAttr>())
    return AA;
  if (auto *IFA = getAttr<IFuncAttr>())
    return IFA;
  if (auto *NZA = getAttr<LoaderUninitializedAttr>())
    return NZA;
  return nullptr;
}

static StringRef getRealizedPlatform(const AvailabilityAttr *A,
                                     const ASTContext &Context) {
  // Check if this is an App Extension "platform", and if so chop off
  // the suffix for matching with the actual platform.
  StringRef RealizedPlatform = A->getPlatform()->getName();
  if (!Context.getLangOpts().AppExt)
    return RealizedPlatform;
  size_t suffix = RealizedPlatform.rfind("_app_extension");
  if (suffix != StringRef::npos)
    return RealizedPlatform.slice(0, suffix);
  return RealizedPlatform;
}

/// Determine the availability of the given declaration based on
/// the target platform.
///
/// When it returns an availability result other than \c AR_Available,
/// if the \p Message parameter is non-NULL, it will be set to a
/// string describing why the entity is unavailable.
///
/// FIXME: Make these strings localizable, since they end up in
/// diagnostics.
static AvailabilityResult CheckAvailability(ASTContext &Context,
                                            const AvailabilityAttr *A,
                                            std::string *Message,
                                            VersionTuple EnclosingVersion) {
  if (EnclosingVersion.empty())
    EnclosingVersion = Context.getTargetInfo().getPlatformMinVersion();

  if (EnclosingVersion.empty())
    return AR_Available;

  StringRef ActualPlatform = A->getPlatform()->getName();
  StringRef TargetPlatform = Context.getTargetInfo().getPlatformName();

  // Match the platform name.
  if (getRealizedPlatform(A, Context) != TargetPlatform)
    return AR_Available;

  StringRef PrettyPlatformName
    = AvailabilityAttr::getPrettyPlatformName(ActualPlatform);

  if (PrettyPlatformName.empty())
    PrettyPlatformName = ActualPlatform;

  std::string HintMessage;
  if (!A->getMessage().empty()) {
    HintMessage = " - ";
    HintMessage += A->getMessage();
  }

  // Make sure that this declaration has not been marked 'unavailable'.
  if (A->getUnavailable()) {
    if (Message) {
      Message->clear();
      llvm::raw_string_ostream Out(*Message);
      Out << "not available on " << PrettyPlatformName
          << HintMessage;
    }

    return AR_Unavailable;
  }

  // Make sure that this declaration has already been introduced.
  if (!A->getIntroduced().empty() &&
      EnclosingVersion < A->getIntroduced()) {
    IdentifierInfo *IIEnv = A->getEnvironment();
    StringRef TargetEnv =
        Context.getTargetInfo().getTriple().getEnvironmentName();
    StringRef EnvName = llvm::Triple::getEnvironmentTypeName(
        Context.getTargetInfo().getTriple().getEnvironment());
    // Matching environment or no environment on attribute
    if (!IIEnv || (!TargetEnv.empty() && IIEnv->getName() == TargetEnv)) {
      if (Message) {
        Message->clear();
        llvm::raw_string_ostream Out(*Message);
        VersionTuple VTI(A->getIntroduced());
        Out << "introduced in " << PrettyPlatformName << " " << VTI << " "
            << EnvName << HintMessage;
      }
    }
    // Non-matching environment or no environment on target
    else {
      if (Message) {
        Message->clear();
        llvm::raw_string_ostream Out(*Message);
        Out << "not available on " << PrettyPlatformName << " " << EnvName
            << HintMessage;
      }
    }

    return A->getStrict() ? AR_Unavailable : AR_NotYetIntroduced;
  }

  // Make sure that this declaration hasn't been obsoleted.
  if (!A->getObsoleted().empty() && EnclosingVersion >= A->getObsoleted()) {
    if (Message) {
      Message->clear();
      llvm::raw_string_ostream Out(*Message);
      VersionTuple VTO(A->getObsoleted());
      Out << "obsoleted in " << PrettyPlatformName << ' '
          << VTO << HintMessage;
    }

    return AR_Unavailable;
  }

  // Make sure that this declaration hasn't been deprecated.
  if (!A->getDeprecated().empty() && EnclosingVersion >= A->getDeprecated()) {
    if (Message) {
      Message->clear();
      llvm::raw_string_ostream Out(*Message);
      VersionTuple VTD(A->getDeprecated());
      Out << "first deprecated in " << PrettyPlatformName << ' '
          << VTD << HintMessage;
    }

    return AR_Deprecated;
  }

  return AR_Available;
}

AvailabilityResult Decl::getAvailability(std::string *Message,
                                         VersionTuple EnclosingVersion,
                                         StringRef *RealizedPlatform) const {
  if (auto *FTD = dyn_cast<FunctionTemplateDecl>(this))
    return FTD->getTemplatedDecl()->getAvailability(Message, EnclosingVersion,
                                                    RealizedPlatform);

  AvailabilityResult Result = AR_Available;
  std::string ResultMessage;

  for (const auto *A : attrs()) {
    if (const auto *Deprecated = dyn_cast<DeprecatedAttr>(A)) {
      if (Result >= AR_Deprecated)
        continue;

      if (Message)
        ResultMessage = std::string(Deprecated->getMessage());

      Result = AR_Deprecated;
      continue;
    }

    if (const auto *Unavailable = dyn_cast<UnavailableAttr>(A)) {
      if (Message)
        *Message = std::string(Unavailable->getMessage());
      return AR_Unavailable;
    }

    if (const auto *Availability = dyn_cast<AvailabilityAttr>(A)) {
      AvailabilityResult AR = CheckAvailability(getASTContext(), Availability,
                                                Message, EnclosingVersion);

      if (AR == AR_Unavailable) {
        if (RealizedPlatform)
          *RealizedPlatform = Availability->getPlatform()->getName();
        return AR_Unavailable;
      }

      if (AR > Result) {
        Result = AR;
        if (Message)
          ResultMessage.swap(*Message);
      }
      continue;
    }
  }

  if (Message)
    Message->swap(ResultMessage);
  return Result;
}

VersionTuple Decl::getVersionIntroduced() const {
  const ASTContext &Context = getASTContext();
  StringRef TargetPlatform = Context.getTargetInfo().getPlatformName();
  for (const auto *A : attrs()) {
    if (const auto *Availability = dyn_cast<AvailabilityAttr>(A)) {
      if (getRealizedPlatform(Availability, Context) != TargetPlatform)
        continue;
      if (!Availability->getIntroduced().empty())
        return Availability->getIntroduced();
    }
  }
  return {};
}

bool Decl::canBeWeakImported(bool &IsDefinition) const {
  IsDefinition = false;

  // Variables, if they aren't definitions.
  if (const auto *Var = dyn_cast<VarDecl>(this)) {
    if (Var->isThisDeclarationADefinition()) {
      IsDefinition = true;
      return false;
    }
    return true;
  }
  // Functions, if they aren't definitions.
  if (const auto *FD = dyn_cast<FunctionDecl>(this)) {
    if (FD->hasBody()) {
      IsDefinition = true;
      return false;
    }
    return true;

  }
  // Objective-C classes, if this is the non-fragile runtime.
  if (isa<ObjCInterfaceDecl>(this) &&
             getASTContext().getLangOpts().ObjCRuntime.hasWeakClassImport()) {
    return true;
  }
  // Nothing else.
  return false;
}

bool Decl::isWeakImported() const {
  bool IsDefinition;
  if (!canBeWeakImported(IsDefinition))
    return false;

  for (const auto *A : getMostRecentDecl()->attrs()) {
    if (isa<WeakImportAttr>(A))
      return true;

    if (const auto *Availability = dyn_cast<AvailabilityAttr>(A)) {
      if (CheckAvailability(getASTContext(), Availability, nullptr,
                            VersionTuple()) == AR_NotYetIntroduced)
        return true;
    }
  }

  return false;
}

unsigned Decl::getIdentifierNamespaceForKind(Kind DeclKind) {
  switch (DeclKind) {
    case Function:
    case CXXDeductionGuide:
    case CXXMethod:
    case CXXConstructor:
    case ConstructorUsingShadow:
    case CXXDestructor:
    case CXXConversion:
    case EnumConstant:
    case Var:
    case ImplicitParam:
    case ParmVar:
    case ObjCMethod:
    case ObjCProperty:
    case MSProperty:
    case HLSLBuffer:
      return IDNS_Ordinary;
    case Label:
      return IDNS_Label;
    case IndirectField:
      return IDNS_Ordinary | IDNS_Member;

    case Binding:
    case NonTypeTemplateParm:
    case VarTemplate:
    case Concept:
      // These (C++-only) declarations are found by redeclaration lookup for
      // tag types, so we include them in the tag namespace.
      return IDNS_Ordinary | IDNS_Tag;

    case ObjCCompatibleAlias:
    case ObjCInterface:
      return IDNS_Ordinary | IDNS_Type;

    case Typedef:
    case TypeAlias:
    case TemplateTypeParm:
    case ObjCTypeParam:
      return IDNS_Ordinary | IDNS_Type;

    case UnresolvedUsingTypename:
      return IDNS_Ordinary | IDNS_Type | IDNS_Using;

    case UsingShadow:
      return 0; // we'll actually overwrite this later

    case UnresolvedUsingValue:
      return IDNS_Ordinary | IDNS_Using;

    case Using:
    case UsingPack:
    case UsingEnum:
      return IDNS_Using;

    case ObjCProtocol:
      return IDNS_ObjCProtocol;

    case Field:
    case ObjCAtDefsField:
    case ObjCIvar:
      return IDNS_Member;

    case Record:
    case CXXRecord:
    case Enum:
      return IDNS_Tag | IDNS_Type;

    case Namespace:
    case NamespaceAlias:
      return IDNS_Namespace;

    case FunctionTemplate:
      return IDNS_Ordinary;

    case ClassTemplate:
    case TemplateTemplateParm:
    case TypeAliasTemplate:
      return IDNS_Ordinary | IDNS_Tag | IDNS_Type;

    case UnresolvedUsingIfExists:
      return IDNS_Type | IDNS_Ordinary;

    case OMPDeclareReduction:
      return IDNS_OMPReduction;

    case OMPDeclareMapper:
      return IDNS_OMPMapper;

    // Never have names.
    case Friend:
    case FriendTemplate:
    case AccessSpec:
    case LinkageSpec:
    case Export:
    case FileScopeAsm:
    case TopLevelStmt:
    case StaticAssert:
    case ObjCPropertyImpl:
    case PragmaComment:
    case PragmaDetectMismatch:
    case Block:
    case Captured:
    case TranslationUnit:
    case ExternCContext:
    case Decomposition:
    case MSGuid:
    case UnnamedGlobalConstant:
    case TemplateParamObject:

    case UsingDirective:
    case BuiltinTemplate:
    case ClassTemplateSpecialization:
    case ClassTemplatePartialSpecialization:
    case VarTemplateSpecialization:
    case VarTemplatePartialSpecialization:
    case ObjCImplementation:
    case ObjCCategory:
    case ObjCCategoryImpl:
    case Import:
    case OMPThreadPrivate:
    case OMPAllocate:
    case OMPRequires:
    case OMPCapturedExpr:
    case Empty:
    case LifetimeExtendedTemporary:
    case RequiresExprBody:
    case ImplicitConceptSpecialization:
      // Never looked up by name.
      return 0;
  }

  llvm_unreachable("Invalid DeclKind!");
}

void Decl::setAttrsImpl(const AttrVec &attrs, ASTContext &Ctx) {
  assert(!HasAttrs && "Decl already contains attrs.");

  AttrVec &AttrBlank = Ctx.getDeclAttrs(this);
  assert(AttrBlank.empty() && "HasAttrs was wrong?");

  AttrBlank = attrs;
  HasAttrs = true;
}

void Decl::dropAttrs() {
  if (!HasAttrs) return;

  HasAttrs = false;
  getASTContext().eraseDeclAttrs(this);
}

void Decl::addAttr(Attr *A) {
  if (!hasAttrs()) {
    setAttrs(AttrVec(1, A));
    return;
  }

  AttrVec &Attrs = getAttrs();
  if (!A->isInherited()) {
    Attrs.push_back(A);
    return;
  }

  // Attribute inheritance is processed after attribute parsing. To keep the
  // order as in the source code, add inherited attributes before non-inherited
  // ones.
  auto I = Attrs.begin(), E = Attrs.end();
  for (; I != E; ++I) {
    if (!(*I)->isInherited())
      break;
  }
  Attrs.insert(I, A);
}

const AttrVec &Decl::getAttrs() const {
  assert(HasAttrs && "No attrs to get!");
  return getASTContext().getDeclAttrs(this);
}

Decl *Decl::castFromDeclContext (const DeclContext *D) {
  Decl::Kind DK = D->getDeclKind();
  switch (DK) {
#define DECL(NAME, BASE)
#define DECL_CONTEXT(NAME)                                                     \
  case Decl::NAME:                                                             \
    return static_cast<NAME##Decl *>(const_cast<DeclContext *>(D));
#include "clang/AST/DeclNodes.inc"
  default:
    llvm_unreachable("a decl that inherits DeclContext isn't handled");
  }
}

DeclContext *Decl::castToDeclContext(const Decl *D) {
  Decl::Kind DK = D->getKind();
  switch(DK) {
#define DECL(NAME, BASE)
#define DECL_CONTEXT(NAME)                                                     \
  case Decl::NAME:                                                             \
    return static_cast<NAME##Decl *>(const_cast<Decl *>(D));
#include "clang/AST/DeclNodes.inc"
  default:
    llvm_unreachable("a decl that inherits DeclContext isn't handled");
  }
}

SourceLocation Decl::getBodyRBrace() const {
  // Special handling of FunctionDecl to avoid de-serializing the body from PCH.
  // FunctionDecl stores EndRangeLoc for this purpose.
  if (const auto *FD = dyn_cast<FunctionDecl>(this)) {
    const FunctionDecl *Definition;
    if (FD->hasBody(Definition))
      return Definition->getSourceRange().getEnd();
    return {};
  }

  if (Stmt *Body = getBody())
    return Body->getSourceRange().getEnd();

  return {};
}

bool Decl::AccessDeclContextCheck() const {
#ifndef NDEBUG
  // Suppress this check if any of the following hold:
  // 1. this is the translation unit (and thus has no parent)
  // 2. this is a template parameter (and thus doesn't belong to its context)
  // 3. this is a non-type template parameter
  // 4. the context is not a record
  // 5. it's invalid
  // 6. it's a C++0x static_assert.
  // 7. it's a block literal declaration
  // 8. it's a temporary with lifetime extended due to being default value.
  if (isa<TranslationUnitDecl>(this) || isa<TemplateTypeParmDecl>(this) ||
      isa<NonTypeTemplateParmDecl>(this) || !getDeclContext() ||
      !isa<CXXRecordDecl>(getDeclContext()) || isInvalidDecl() ||
      isa<StaticAssertDecl>(this) || isa<BlockDecl>(this) ||
      // FIXME: a ParmVarDecl can have ClassTemplateSpecialization
      // as DeclContext (?).
      isa<ParmVarDecl>(this) ||
      // FIXME: a ClassTemplateSpecialization or CXXRecordDecl can have
      // AS_none as access specifier.
      isa<CXXRecordDecl>(this) || isa<LifetimeExtendedTemporaryDecl>(this))
    return true;

  assert(Access != AS_none &&
         "Access specifier is AS_none inside a record decl");
#endif
  return true;
}

bool Decl::isInExportDeclContext() const {
  const DeclContext *DC = getLexicalDeclContext();

  while (DC && !isa<ExportDecl>(DC))
    DC = DC->getLexicalParent();

  return isa_and_nonnull<ExportDecl>(DC);
}

bool Decl::isInAnotherModuleUnit() const {
  auto *M = getOwningModule();

  if (!M)
    return false;

  // FIXME or NOTE: maybe we need to be clear about the semantics
  // of clang header modules. e.g., if this lives in a clang header
  // module included by the current unit, should we return false
  // here?
  //
  // This is clear for header units as the specification says the
  // header units live in a synthesised translation unit. So we
  // can return false here.
  M = M->getTopLevelModule();
  if (!M->isNamedModule())
    return false;

  return M != getASTContext().getCurrentNamedModule();
}

bool Decl::isInCurrentModuleUnit() const {
  auto *M = getOwningModule();

  if (!M || !M->isNamedModule())
    return false;

  return M == getASTContext().getCurrentNamedModule();
}

bool Decl::shouldEmitInExternalSource() const {
  ExternalASTSource *Source = getASTContext().getExternalSource();
  if (!Source)
    return false;

  return Source->hasExternalDefinitions(this) == ExternalASTSource::EK_Always;
}

bool Decl::isFromExplicitGlobalModule() const {
  return getOwningModule() && getOwningModule()->isExplicitGlobalModule();
}

bool Decl::isFromGlobalModule() const {
  return getOwningModule() && getOwningModule()->isGlobalModule();
}

bool Decl::isInNamedModule() const {
  return getOwningModule() && getOwningModule()->isNamedModule();
}

static Decl::Kind getKind(const Decl *D) { return D->getKind(); }
static Decl::Kind getKind(const DeclContext *DC) { return DC->getDeclKind(); }

int64_t Decl::getID() const {
  return getASTContext().getAllocator().identifyKnownAlignedObject<Decl>(this);
}

const FunctionType *Decl::getFunctionType(bool BlocksToo) const {
  QualType Ty;
  if (isa<BindingDecl>(this))
    return nullptr;
  else if (const auto *D = dyn_cast<ValueDecl>(this))
    Ty = D->getType();
  else if (const auto *D = dyn_cast<TypedefNameDecl>(this))
    Ty = D->getUnderlyingType();
  else
    return nullptr;

  if (Ty->isFunctionPointerType())
    Ty = Ty->castAs<PointerType>()->getPointeeType();
  else if (Ty->isFunctionReferenceType())
    Ty = Ty->castAs<ReferenceType>()->getPointeeType();
  else if (BlocksToo && Ty->isBlockPointerType())
    Ty = Ty->castAs<BlockPointerType>()->getPointeeType();

  return Ty->getAs<FunctionType>();
}

bool Decl::isFunctionPointerType() const {
  QualType Ty;
  if (const auto *D = dyn_cast<ValueDecl>(this))
    Ty = D->getType();
  else if (const auto *D = dyn_cast<TypedefNameDecl>(this))
    Ty = D->getUnderlyingType();
  else
    return false;

  return Ty.getCanonicalType()->isFunctionPointerType();
}

DeclContext *Decl::getNonTransparentDeclContext() {
  assert(getDeclContext());
  return getDeclContext()->getNonTransparentContext();
}

/// Starting at a given context (a Decl or DeclContext), look for a
/// code context that is not a closure (a lambda, block, etc.).
template <class T> static Decl *getNonClosureContext(T *D) {
  if (getKind(D) == Decl::CXXMethod) {
    auto *MD = cast<CXXMethodDecl>(D);
    if (MD->getOverloadedOperator() == OO_Call &&
        MD->getParent()->isLambda())
      return getNonClosureContext(MD->getParent()->getParent());
    return MD;
  }
  if (auto *FD = dyn_cast<FunctionDecl>(D))
    return FD;
  if (auto *MD = dyn_cast<ObjCMethodDecl>(D))
    return MD;
  if (auto *BD = dyn_cast<BlockDecl>(D))
    return getNonClosureContext(BD->getParent());
  if (auto *CD = dyn_cast<CapturedDecl>(D))
    return getNonClosureContext(CD->getParent());
  return nullptr;
}

Decl *Decl::getNonClosureContext() {
  return ::getNonClosureContext(this);
}

Decl *DeclContext::getNonClosureAncestor() {
  return ::getNonClosureContext(this);
}

//===----------------------------------------------------------------------===//
// DeclContext Implementation
//===----------------------------------------------------------------------===//

DeclContext::DeclContext(Decl::Kind K) {
  DeclContextBits.DeclKind = K;
  setHasExternalLexicalStorage(false);
  setHasExternalVisibleStorage(false);
  setNeedToReconcileExternalVisibleStorage(false);
  setHasLazyLocalLexicalLookups(false);
  setHasLazyExternalLexicalLookups(false);
  setUseQualifiedLookup(false);
}

bool DeclContext::classof(const Decl *D) {
  Decl::Kind DK = D->getKind();
  switch (DK) {
#define DECL(NAME, BASE)
#define DECL_CONTEXT(NAME) case Decl::NAME:
#include "clang/AST/DeclNodes.inc"
    return true;
  default:
    return false;
  }
}

DeclContext::~DeclContext() = default;

/// Find the parent context of this context that will be
/// used for unqualified name lookup.
///
/// Generally, the parent lookup context is the semantic context. However, for
/// a friend function the parent lookup context is the lexical context, which
/// is the class in which the friend is declared.
DeclContext *DeclContext::getLookupParent() {
  // FIXME: Find a better way to identify friends.
  if (isa<FunctionDecl>(this))
    if (getParent()->getRedeclContext()->isFileContext() &&
        getLexicalParent()->getRedeclContext()->isRecord())
      return getLexicalParent();

  // A lookup within the call operator of a lambda never looks in the lambda
  // class; instead, skip to the context in which that closure type is
  // declared.
  if (isLambdaCallOperator(this))
    return getParent()->getParent();

  return getParent();
}

const BlockDecl *DeclContext::getInnermostBlockDecl() const {
  const DeclContext *Ctx = this;

  do {
    if (Ctx->isClosure())
      return cast<BlockDecl>(Ctx);
    Ctx = Ctx->getParent();
  } while (Ctx);

  return nullptr;
}

bool DeclContext::isInlineNamespace() const {
  return isNamespace() &&
         cast<NamespaceDecl>(this)->isInline();
}

bool DeclContext::isStdNamespace() const {
  if (!isNamespace())
    return false;

  const auto *ND = cast<NamespaceDecl>(this);
  if (ND->isInline()) {
    return ND->getParent()->isStdNamespace();
  }

  if (!getParent()->getRedeclContext()->isTranslationUnit())
    return false;

  const IdentifierInfo *II = ND->getIdentifier();
  return II && II->isStr("std");
}

bool DeclContext::isDependentContext() const {
  if (isFileContext())
    return false;

  if (isa<ClassTemplatePartialSpecializationDecl>(this))
    return true;

  if (const auto *Record = dyn_cast<CXXRecordDecl>(this)) {
    if (Record->getDescribedClassTemplate())
      return true;

    if (Record->isDependentLambda())
      return true;
    if (Record->isNeverDependentLambda())
      return false;
  }

  if (const auto *Function = dyn_cast<FunctionDecl>(this)) {
    if (Function->getDescribedFunctionTemplate())
      return true;

    // Friend function declarations are dependent if their *lexical*
    // context is dependent.
    if (cast<Decl>(this)->getFriendObjectKind())
      return getLexicalParent()->isDependentContext();
  }

  // FIXME: A variable template is a dependent context, but is not a
  // DeclContext. A context within it (such as a lambda-expression)
  // should be considered dependent.

  return getParent() && getParent()->isDependentContext();
}

bool DeclContext::isTransparentContext() const {
  if (getDeclKind() == Decl::Enum)
    return !cast<EnumDecl>(this)->isScoped();

  return isa<LinkageSpecDecl, ExportDecl, HLSLBufferDecl>(this);
}

static bool isLinkageSpecContext(const DeclContext *DC,
                                 LinkageSpecLanguageIDs ID) {
  while (DC->getDeclKind() != Decl::TranslationUnit) {
    if (DC->getDeclKind() == Decl::LinkageSpec)
      return cast<LinkageSpecDecl>(DC)->getLanguage() == ID;
    DC = DC->getLexicalParent();
  }
  return false;
}

bool DeclContext::isExternCContext() const {
  return isLinkageSpecContext(this, LinkageSpecLanguageIDs::C);
}

const LinkageSpecDecl *DeclContext::getExternCContext() const {
  const DeclContext *DC = this;
  while (DC->getDeclKind() != Decl::TranslationUnit) {
    if (DC->getDeclKind() == Decl::LinkageSpec &&
        cast<LinkageSpecDecl>(DC)->getLanguage() == LinkageSpecLanguageIDs::C)
      return cast<LinkageSpecDecl>(DC);
    DC = DC->getLexicalParent();
  }
  return nullptr;
}

bool DeclContext::isExternCXXContext() const {
  return isLinkageSpecContext(this, LinkageSpecLanguageIDs::CXX);
}

bool DeclContext::Encloses(const DeclContext *DC) const {
  if (getPrimaryContext() != this)
    return getPrimaryContext()->Encloses(DC);

  for (; DC; DC = DC->getParent())
    if (!isa<LinkageSpecDecl>(DC) && !isa<ExportDecl>(DC) &&
        DC->getPrimaryContext() == this)
      return true;
  return false;
}

DeclContext *DeclContext::getNonTransparentContext() {
  DeclContext *DC = this;
  while (DC->isTransparentContext()) {
    DC = DC->getParent();
    assert(DC && "All transparent contexts should have a parent!");
  }
  return DC;
}

DeclContext *DeclContext::getPrimaryContext() {
  switch (getDeclKind()) {
  case Decl::ExternCContext:
  case Decl::LinkageSpec:
  case Decl::Export:
  case Decl::TopLevelStmt:
  case Decl::Block:
  case Decl::Captured:
  case Decl::OMPDeclareReduction:
  case Decl::OMPDeclareMapper:
  case Decl::RequiresExprBody:
    // There is only one DeclContext for these entities.
    return this;

  case Decl::HLSLBuffer:
    // Each buffer, even with the same name, is a distinct construct.
    // Multiple buffers with the same name are allowed for backward
    // compatibility.
    // As long as buffers have unique resource bindings the names don't matter.
    // The names get exposed via the CPU-side reflection API which
    // supports querying bindings, so we cannot remove them.
    return this;

  case Decl::TranslationUnit:
    return static_cast<TranslationUnitDecl *>(this)->getFirstDecl();
  case Decl::Namespace:
    return static_cast<NamespaceDecl *>(this)->getFirstDecl();

  case Decl::ObjCMethod:
    return this;

  case Decl::ObjCInterface:
    if (auto *OID = dyn_cast<ObjCInterfaceDecl>(this))
      if (auto *Def = OID->getDefinition())
        return Def;
    return this;

  case Decl::ObjCProtocol:
    if (auto *OPD = dyn_cast<ObjCProtocolDecl>(this))
      if (auto *Def = OPD->getDefinition())
        return Def;
    return this;

  case Decl::ObjCCategory:
    return this;

  case Decl::ObjCImplementation:
  case Decl::ObjCCategoryImpl:
    return this;

  default:
    if (getDeclKind() >= Decl::firstTag && getDeclKind() <= Decl::lastTag) {
      // If this is a tag type that has a definition or is currently
      // being defined, that definition is our primary context.
      auto *Tag = cast<TagDecl>(this);

      if (TagDecl *Def = Tag->getDefinition())
        return Def;

      if (const auto *TagTy = dyn_cast<TagType>(Tag->getTypeForDecl())) {
        // Note, TagType::getDecl returns the (partial) definition one exists.
        TagDecl *PossiblePartialDef = TagTy->getDecl();
        if (PossiblePartialDef->isBeingDefined())
          return PossiblePartialDef;
      } else {
        assert(isa<InjectedClassNameType>(Tag->getTypeForDecl()));
      }

      return Tag;
    }

    assert(getDeclKind() >= Decl::firstFunction &&
           getDeclKind() <= Decl::lastFunction &&
          "Unknown DeclContext kind");
    return this;
  }
}

template <typename T>
void collectAllContextsImpl(T *Self, SmallVectorImpl<DeclContext *> &Contexts) {
  for (T *D = Self->getMostRecentDecl(); D; D = D->getPreviousDecl())
    Contexts.push_back(D);

  std::reverse(Contexts.begin(), Contexts.end());
}

void DeclContext::collectAllContexts(SmallVectorImpl<DeclContext *> &Contexts) {
  Contexts.clear();

  Decl::Kind Kind = getDeclKind();

  if (Kind == Decl::TranslationUnit)
    collectAllContextsImpl(static_cast<TranslationUnitDecl *>(this), Contexts);
  else if (Kind == Decl::Namespace)
    collectAllContextsImpl(static_cast<NamespaceDecl *>(this), Contexts);
  else
    Contexts.push_back(this);
}

std::pair<Decl *, Decl *>
DeclContext::BuildDeclChain(ArrayRef<Decl *> Decls,
                            bool FieldsAlreadyLoaded) {
  // Build up a chain of declarations via the Decl::NextInContextAndBits field.
  Decl *FirstNewDecl = nullptr;
  Decl *PrevDecl = nullptr;
  for (auto *D : Decls) {
    if (FieldsAlreadyLoaded && isa<FieldDecl>(D))
      continue;

    if (PrevDecl)
      PrevDecl->NextInContextAndBits.setPointer(D);
    else
      FirstNewDecl = D;

    PrevDecl = D;
  }

  return std::make_pair(FirstNewDecl, PrevDecl);
}

/// We have just acquired external visible storage, and we already have
/// built a lookup map. For every name in the map, pull in the new names from
/// the external storage.
void DeclContext::reconcileExternalVisibleStorage() const {
  assert(hasNeedToReconcileExternalVisibleStorage() && LookupPtr);
  setNeedToReconcileExternalVisibleStorage(false);

  for (auto &Lookup : *LookupPtr)
    Lookup.second.setHasExternalDecls();
}

/// Load the declarations within this lexical storage from an
/// external source.
/// \return \c true if any declarations were added.
bool
DeclContext::LoadLexicalDeclsFromExternalStorage() const {
  ExternalASTSource *Source = getParentASTContext().getExternalSource();
  assert(hasExternalLexicalStorage() && Source && "No external storage?");

  // Notify that we have a DeclContext that is initializing.
  ExternalASTSource::Deserializing ADeclContext(Source);

  // Load the external declarations, if any.
  SmallVector<Decl*, 64> Decls;
  setHasExternalLexicalStorage(false);
  Source->FindExternalLexicalDecls(this, Decls);

  if (Decls.empty())
    return false;

  // We may have already loaded just the fields of this record, in which case
  // we need to ignore them.
  bool FieldsAlreadyLoaded = false;
  if (const auto *RD = dyn_cast<RecordDecl>(this))
    FieldsAlreadyLoaded = RD->hasLoadedFieldsFromExternalStorage();

  // Splice the newly-read declarations into the beginning of the list
  // of declarations.
  Decl *ExternalFirst, *ExternalLast;
  std::tie(ExternalFirst, ExternalLast) =
      BuildDeclChain(Decls, FieldsAlreadyLoaded);
  ExternalLast->NextInContextAndBits.setPointer(FirstDecl);
  FirstDecl = ExternalFirst;
  if (!LastDecl)
    LastDecl = ExternalLast;
  return true;
}

DeclContext::lookup_result
ExternalASTSource::SetNoExternalVisibleDeclsForName(const DeclContext *DC,
                                                    DeclarationName Name) {
  ASTContext &Context = DC->getParentASTContext();
  StoredDeclsMap *Map;
  if (!(Map = DC->LookupPtr))
    Map = DC->CreateStoredDeclsMap(Context);
  if (DC->hasNeedToReconcileExternalVisibleStorage())
    DC->reconcileExternalVisibleStorage();

  (*Map)[Name].removeExternalDecls();

  return DeclContext::lookup_result();
}

DeclContext::lookup_result
ExternalASTSource::SetExternalVisibleDeclsForName(const DeclContext *DC,
                                                  DeclarationName Name,
                                                  ArrayRef<NamedDecl*> Decls) {
  ASTContext &Context = DC->getParentASTContext();
  StoredDeclsMap *Map;
  if (!(Map = DC->LookupPtr))
    Map = DC->CreateStoredDeclsMap(Context);
  if (DC->hasNeedToReconcileExternalVisibleStorage())
    DC->reconcileExternalVisibleStorage();

  StoredDeclsList &List = (*Map)[Name];
  List.replaceExternalDecls(Decls);
  return List.getLookupResult();
}

DeclContext::decl_iterator DeclContext::decls_begin() const {
  if (hasExternalLexicalStorage())
    LoadLexicalDeclsFromExternalStorage();
  return decl_iterator(FirstDecl);
}

bool DeclContext::decls_empty() const {
  if (hasExternalLexicalStorage())
    LoadLexicalDeclsFromExternalStorage();

  return !FirstDecl;
}

bool DeclContext::containsDecl(Decl *D) const {
  return (D->getLexicalDeclContext() == this &&
          (D->NextInContextAndBits.getPointer() || D == LastDecl));
}

bool DeclContext::containsDeclAndLoad(Decl *D) const {
  if (hasExternalLexicalStorage())
    LoadLexicalDeclsFromExternalStorage();
  return containsDecl(D);
}

/// shouldBeHidden - Determine whether a declaration which was declared
/// within its semantic context should be invisible to qualified name lookup.
static bool shouldBeHidden(NamedDecl *D) {
  // Skip unnamed declarations.
  if (!D->getDeclName())
    return true;

  // Skip entities that can't be found by name lookup into a particular
  // context.
  if ((D->getIdentifierNamespace() == 0 && !isa<UsingDirectiveDecl>(D)) ||
      D->isTemplateParameter())
    return true;

  // Skip friends and local extern declarations unless they're the first
  // declaration of the entity.
  if ((D->isLocalExternDecl() || D->getFriendObjectKind()) &&
      D != D->getCanonicalDecl())
    return true;

  // Skip template specializations.
  // FIXME: This feels like a hack. Should DeclarationName support
  // template-ids, or is there a better way to keep specializations
  // from being visible?
  if (isa<ClassTemplateSpecializationDecl>(D))
    return true;
  if (auto *FD = dyn_cast<FunctionDecl>(D))
    if (FD->isFunctionTemplateSpecialization())
      return true;

  // Hide destructors that are invalid. There should always be one destructor,
  // but if it is an invalid decl, another one is created. We need to hide the
  // invalid one from places that expect exactly one destructor, like the
  // serialization code.
  if (isa<CXXDestructorDecl>(D) && D->isInvalidDecl())
    return true;

  return false;
}

void DeclContext::removeDecl(Decl *D) {
  assert(D->getLexicalDeclContext() == this &&
         "decl being removed from non-lexical context");
  assert((D->NextInContextAndBits.getPointer() || D == LastDecl) &&
         "decl is not in decls list");

  // Remove D from the decl chain.  This is O(n) but hopefully rare.
  if (D == FirstDecl) {
    if (D == LastDecl)
      FirstDecl = LastDecl = nullptr;
    else
      FirstDecl = D->NextInContextAndBits.getPointer();
  } else {
    for (Decl *I = FirstDecl; true; I = I->NextInContextAndBits.getPointer()) {
      assert(I && "decl not found in linked list");
      if (I->NextInContextAndBits.getPointer() == D) {
        I->NextInContextAndBits.setPointer(D->NextInContextAndBits.getPointer());
        if (D == LastDecl) LastDecl = I;
        break;
      }
    }
  }

  // Mark that D is no longer in the decl chain.
  D->NextInContextAndBits.setPointer(nullptr);

  // Remove D from the lookup table if necessary.
  if (isa<NamedDecl>(D)) {
    auto *ND = cast<NamedDecl>(D);

    // Do not try to remove the declaration if that is invisible to qualified
    // lookup.  E.g. template specializations are skipped.
    if (shouldBeHidden(ND))
      return;

    // Remove only decls that have a name
    if (!ND->getDeclName())
      return;

    auto *DC = D->getDeclContext();
    do {
      StoredDeclsMap *Map = DC->getPrimaryContext()->LookupPtr;
      if (Map) {
        StoredDeclsMap::iterator Pos = Map->find(ND->getDeclName());
        assert(Pos != Map->end() && "no lookup entry for decl");
        StoredDeclsList &List = Pos->second;
        List.remove(ND);
        // Clean up the entry if there are no more decls.
        if (List.isNull())
          Map->erase(Pos);
      }
    } while (DC->isTransparentContext() && (DC = DC->getParent()));
  }
}

void DeclContext::addHiddenDecl(Decl *D) {
  assert(D->getLexicalDeclContext() == this &&
         "Decl inserted into wrong lexical context");
  assert(!D->getNextDeclInContext() && D != LastDecl &&
         "Decl already inserted into a DeclContext");

  if (FirstDecl) {
    LastDecl->NextInContextAndBits.setPointer(D);
    LastDecl = D;
  } else {
    FirstDecl = LastDecl = D;
  }

  // Notify a C++ record declaration that we've added a member, so it can
  // update its class-specific state.
  if (auto *Record = dyn_cast<CXXRecordDecl>(this))
    Record->addedMember(D);

  // If this is a newly-created (not de-serialized) import declaration, wire
  // it in to the list of local import declarations.
  if (!D->isFromASTFile()) {
    if (auto *Import = dyn_cast<ImportDecl>(D))
      D->getASTContext().addedLocalImportDecl(Import);
  }
}

void DeclContext::addDecl(Decl *D) {
  addHiddenDecl(D);

  if (auto *ND = dyn_cast<NamedDecl>(D))
    ND->getDeclContext()->getPrimaryContext()->
        makeDeclVisibleInContextWithFlags(ND, false, true);
}

void DeclContext::addDeclInternal(Decl *D) {
  addHiddenDecl(D);

  if (auto *ND = dyn_cast<NamedDecl>(D))
    ND->getDeclContext()->getPrimaryContext()->
        makeDeclVisibleInContextWithFlags(ND, true, true);
}

/// buildLookup - Build the lookup data structure with all of the
/// declarations in this DeclContext (and any other contexts linked
/// to it or transparent contexts nested within it) and return it.
///
/// Note that the produced map may miss out declarations from an
/// external source. If it does, those entries will be marked with
/// the 'hasExternalDecls' flag.
StoredDeclsMap *DeclContext::buildLookup() {
  assert(this == getPrimaryContext() && "buildLookup called on non-primary DC");

  if (!hasLazyLocalLexicalLookups() &&
      !hasLazyExternalLexicalLookups())
    return LookupPtr;

  SmallVector<DeclContext *, 2> Contexts;
  collectAllContexts(Contexts);

  if (hasLazyExternalLexicalLookups()) {
    setHasLazyExternalLexicalLookups(false);
    for (auto *DC : Contexts) {
      if (DC->hasExternalLexicalStorage()) {
        bool LoadedDecls = DC->LoadLexicalDeclsFromExternalStorage();
        setHasLazyLocalLexicalLookups(
            hasLazyLocalLexicalLookups() | LoadedDecls );
      }
    }

    if (!hasLazyLocalLexicalLookups())
      return LookupPtr;
  }

  for (auto *DC : Contexts)
    buildLookupImpl(DC, hasExternalVisibleStorage());

  // We no longer have any lazy decls.
  setHasLazyLocalLexicalLookups(false);
  return LookupPtr;
}

/// buildLookupImpl - Build part of the lookup data structure for the
/// declarations contained within DCtx, which will either be this
/// DeclContext, a DeclContext linked to it, or a transparent context
/// nested within it.
void DeclContext::buildLookupImpl(DeclContext *DCtx, bool Internal) {
  for (auto *D : DCtx->noload_decls()) {
    // Insert this declaration into the lookup structure, but only if
    // it's semantically within its decl context. Any other decls which
    // should be found in this context are added eagerly.
    //
    // If it's from an AST file, don't add it now. It'll get handled by
    // FindExternalVisibleDeclsByName if needed. Exception: if we're not
    // in C++, we do not track external visible decls for the TU, so in
    // that case we need to collect them all here.
    if (auto *ND = dyn_cast<NamedDecl>(D))
      if (ND->getDeclContext() == DCtx && !shouldBeHidden(ND) &&
          (!ND->isFromASTFile() ||
           (isTranslationUnit() &&
            !getParentASTContext().getLangOpts().CPlusPlus)))
        makeDeclVisibleInContextImpl(ND, Internal);

    // If this declaration is itself a transparent declaration context
    // or inline namespace, add the members of this declaration of that
    // context (recursively).
    if (auto *InnerCtx = dyn_cast<DeclContext>(D))
      if (InnerCtx->isTransparentContext() || InnerCtx->isInlineNamespace())
        buildLookupImpl(InnerCtx, Internal);
  }
}

DeclContext::lookup_result
DeclContext::lookup(DeclarationName Name) const {
  // For transparent DeclContext, we should lookup in their enclosing context.
  if (getDeclKind() == Decl::LinkageSpec || getDeclKind() == Decl::Export)
    return getParent()->lookup(Name);

  const DeclContext *PrimaryContext = getPrimaryContext();
  if (PrimaryContext != this)
    return PrimaryContext->lookup(Name);

  // If we have an external source, ensure that any later redeclarations of this
  // context have been loaded, since they may add names to the result of this
  // lookup (or add external visible storage).
  ExternalASTSource *Source = getParentASTContext().getExternalSource();
  if (Source)
    (void)cast<Decl>(this)->getMostRecentDecl();

  if (hasExternalVisibleStorage()) {
    assert(Source && "external visible storage but no external source?");

    if (hasNeedToReconcileExternalVisibleStorage())
      reconcileExternalVisibleStorage();

    StoredDeclsMap *Map = LookupPtr;

    if (hasLazyLocalLexicalLookups() ||
        hasLazyExternalLexicalLookups())
      // FIXME: Make buildLookup const?
      Map = const_cast<DeclContext*>(this)->buildLookup();

    if (!Map)
      Map = CreateStoredDeclsMap(getParentASTContext());

    // If we have a lookup result with no external decls, we are done.
    std::pair<StoredDeclsMap::iterator, bool> R =
        Map->insert(std::make_pair(Name, StoredDeclsList()));
    if (!R.second && !R.first->second.hasExternalDecls())
      return R.first->second.getLookupResult();

    if (Source->FindExternalVisibleDeclsByName(this, Name) || !R.second) {
      if (StoredDeclsMap *Map = LookupPtr) {
        StoredDeclsMap::iterator I = Map->find(Name);
        if (I != Map->end())
          return I->second.getLookupResult();
      }
    }

    return {};
  }

  StoredDeclsMap *Map = LookupPtr;
  if (hasLazyLocalLexicalLookups() ||
      hasLazyExternalLexicalLookups())
    Map = const_cast<DeclContext*>(this)->buildLookup();

  if (!Map)
    return {};

  StoredDeclsMap::iterator I = Map->find(Name);
  if (I == Map->end())
    return {};

  return I->second.getLookupResult();
}

DeclContext::lookup_result
DeclContext::noload_lookup(DeclarationName Name) {
  // For transparent DeclContext, we should lookup in their enclosing context.
  if (getDeclKind() == Decl::LinkageSpec || getDeclKind() == Decl::Export)
    return getParent()->noload_lookup(Name);

  DeclContext *PrimaryContext = getPrimaryContext();
  if (PrimaryContext != this)
    return PrimaryContext->noload_lookup(Name);

  loadLazyLocalLexicalLookups();
  StoredDeclsMap *Map = LookupPtr;
  if (!Map)
    return {};

  StoredDeclsMap::iterator I = Map->find(Name);
  return I != Map->end() ? I->second.getLookupResult()
                         : lookup_result();
}

// If we have any lazy lexical declarations not in our lookup map, add them
// now. Don't import any external declarations, not even if we know we have
// some missing from the external visible lookups.
void DeclContext::loadLazyLocalLexicalLookups() {
  if (hasLazyLocalLexicalLookups()) {
    SmallVector<DeclContext *, 2> Contexts;
    collectAllContexts(Contexts);
    for (auto *Context : Contexts)
      buildLookupImpl(Context, hasExternalVisibleStorage());
    setHasLazyLocalLexicalLookups(false);
  }
}

void DeclContext::localUncachedLookup(DeclarationName Name,
                                      SmallVectorImpl<NamedDecl *> &Results) {
  Results.clear();

  // If there's no external storage, just perform a normal lookup and copy
  // the results.
  if (!hasExternalVisibleStorage() && !hasExternalLexicalStorage() && Name) {
    lookup_result LookupResults = lookup(Name);
    Results.insert(Results.end(), LookupResults.begin(), LookupResults.end());
    if (!Results.empty())
      return;
  }

  // If we have a lookup table, check there first. Maybe we'll get lucky.
  // FIXME: Should we be checking these flags on the primary context?
  if (Name && !hasLazyLocalLexicalLookups() &&
      !hasLazyExternalLexicalLookups()) {
    if (StoredDeclsMap *Map = LookupPtr) {
      StoredDeclsMap::iterator Pos = Map->find(Name);
      if (Pos != Map->end()) {
        Results.insert(Results.end(),
                       Pos->second.getLookupResult().begin(),
                       Pos->second.getLookupResult().end());
        return;
      }
    }
  }

  // Slow case: grovel through the declarations in our chain looking for
  // matches.
  // FIXME: If we have lazy external declarations, this will not find them!
  // FIXME: Should we CollectAllContexts and walk them all here?
  for (Decl *D = FirstDecl; D; D = D->getNextDeclInContext()) {
    if (auto *ND = dyn_cast<NamedDecl>(D))
      if (ND->getDeclName() == Name)
        Results.push_back(ND);
  }
}

DeclContext *DeclContext::getRedeclContext() {
  DeclContext *Ctx = this;

  // In C, a record type is the redeclaration context for its fields only. If
  // we arrive at a record context after skipping anything else, we should skip
  // the record as well. Currently, this means skipping enumerations because
  // they're the only transparent context that can exist within a struct or
  // union.
  bool SkipRecords = getDeclKind() == Decl::Kind::Enum &&
                     !getParentASTContext().getLangOpts().CPlusPlus;

  // Skip through contexts to get to the redeclaration context. Transparent
  // contexts are always skipped.
  while ((SkipRecords && Ctx->isRecord()) || Ctx->isTransparentContext())
    Ctx = Ctx->getParent();
  return Ctx;
}

DeclContext *DeclContext::getEnclosingNamespaceContext() {
  DeclContext *Ctx = this;
  // Skip through non-namespace, non-translation-unit contexts.
  while (!Ctx->isFileContext())
    Ctx = Ctx->getParent();
  return Ctx->getPrimaryContext();
}

RecordDecl *DeclContext::getOuterLexicalRecordContext() {
  // Loop until we find a non-record context.
  RecordDecl *OutermostRD = nullptr;
  DeclContext *DC = this;
  while (DC->isRecord()) {
    OutermostRD = cast<RecordDecl>(DC);
    DC = DC->getLexicalParent();
  }
  return OutermostRD;
}

bool DeclContext::InEnclosingNamespaceSetOf(const DeclContext *O) const {
  // For non-file contexts, this is equivalent to Equals.
  if (!isFileContext())
    return O->Equals(this);

  do {
    if (O->Equals(this))
      return true;

    const auto *NS = dyn_cast<NamespaceDecl>(O);
    if (!NS || !NS->isInline())
      break;
    O = NS->getParent();
  } while (O);

  return false;
}

void DeclContext::makeDeclVisibleInContext(NamedDecl *D) {
  DeclContext *PrimaryDC = this->getPrimaryContext();
  DeclContext *DeclDC = D->getDeclContext()->getPrimaryContext();
  // If the decl is being added outside of its semantic decl context, we
  // need to ensure that we eagerly build the lookup information for it.
  PrimaryDC->makeDeclVisibleInContextWithFlags(D, false, PrimaryDC == DeclDC);
}

void DeclContext::makeDeclVisibleInContextWithFlags(NamedDecl *D, bool Internal,
                                                    bool Recoverable) {
  assert(this == getPrimaryContext() && "expected a primary DC");

  if (!isLookupContext()) {
    if (isTransparentContext())
      getParent()->getPrimaryContext()
        ->makeDeclVisibleInContextWithFlags(D, Internal, Recoverable);
    return;
  }

  // Skip declarations which should be invisible to name lookup.
  if (shouldBeHidden(D))
    return;

  // If we already have a lookup data structure, perform the insertion into
  // it. If we might have externally-stored decls with this name, look them
  // up and perform the insertion. If this decl was declared outside its
  // semantic context, buildLookup won't add it, so add it now.
  //
  // FIXME: As a performance hack, don't add such decls into the translation
  // unit unless we're in C++, since qualified lookup into the TU is never
  // performed.
  if (LookupPtr || hasExternalVisibleStorage() ||
      ((!Recoverable || D->getDeclContext() != D->getLexicalDeclContext()) &&
       (getParentASTContext().getLangOpts().CPlusPlus ||
        !isTranslationUnit()))) {
    // If we have lazily omitted any decls, they might have the same name as
    // the decl which we are adding, so build a full lookup table before adding
    // this decl.
    buildLookup();
    makeDeclVisibleInContextImpl(D, Internal);
  } else {
    setHasLazyLocalLexicalLookups(true);
  }

  // If we are a transparent context or inline namespace, insert into our
  // parent context, too. This operation is recursive.
  if (isTransparentContext() || isInlineNamespace())
    getParent()->getPrimaryContext()->
        makeDeclVisibleInContextWithFlags(D, Internal, Recoverable);

  auto *DCAsDecl = cast<Decl>(this);
  // Notify that a decl was made visible unless we are a Tag being defined.
  if (!(isa<TagDecl>(DCAsDecl) && cast<TagDecl>(DCAsDecl)->isBeingDefined()))
    if (ASTMutationListener *L = DCAsDecl->getASTMutationListener())
      L->AddedVisibleDecl(this, D);
}

void DeclContext::makeDeclVisibleInContextImpl(NamedDecl *D, bool Internal) {
  // Find or create the stored declaration map.
  StoredDeclsMap *Map = LookupPtr;
  if (!Map) {
    ASTContext *C = &getParentASTContext();
    Map = CreateStoredDeclsMap(*C);
  }

  // If there is an external AST source, load any declarations it knows about
  // with this declaration's name.
  // If the lookup table contains an entry about this name it means that we
  // have already checked the external source.
  if (!Internal)
    if (ExternalASTSource *Source = getParentASTContext().getExternalSource())
      if (hasExternalVisibleStorage() &&
          Map->find(D->getDeclName()) == Map->end())
        Source->FindExternalVisibleDeclsByName(this, D->getDeclName());

  // Insert this declaration into the map.
  StoredDeclsList &DeclNameEntries = (*Map)[D->getDeclName()];

  if (Internal) {
    // If this is being added as part of loading an external declaration,
    // this may not be the only external declaration with this name.
    // In this case, we never try to replace an existing declaration; we'll
    // handle that when we finalize the list of declarations for this name.
    DeclNameEntries.setHasExternalDecls();
    DeclNameEntries.prependDeclNoReplace(D);
    return;
  }

  DeclNameEntries.addOrReplaceDecl(D);
}

UsingDirectiveDecl *DeclContext::udir_iterator::operator*() const {
  return cast<UsingDirectiveDecl>(*I);
}

/// Returns iterator range [First, Last) of UsingDirectiveDecls stored within
/// this context.
DeclContext::udir_range DeclContext::using_directives() const {
  // FIXME: Use something more efficient than normal lookup for using
  // directives. In C++, using directives are looked up more than anything else.
  lookup_result Result = lookup(UsingDirectiveDecl::getName());
  return udir_range(Result.begin(), Result.end());
}

//===----------------------------------------------------------------------===//
// Creation and Destruction of StoredDeclsMaps.                               //
//===----------------------------------------------------------------------===//

StoredDeclsMap *DeclContext::CreateStoredDeclsMap(ASTContext &C) const {
  assert(!LookupPtr && "context already has a decls map");
  assert(getPrimaryContext() == this &&
         "creating decls map on non-primary context");

  StoredDeclsMap *M;
  bool Dependent = isDependentContext();
  if (Dependent)
    M = new DependentStoredDeclsMap();
  else
    M = new StoredDeclsMap();
  M->Previous = C.LastSDM;
  C.LastSDM = llvm::PointerIntPair<StoredDeclsMap*,1>(M, Dependent);
  LookupPtr = M;
  return M;
}

void ASTContext::ReleaseDeclContextMaps() {
  // It's okay to delete DependentStoredDeclsMaps via a StoredDeclsMap
  // pointer because the subclass doesn't add anything that needs to
  // be deleted.
  StoredDeclsMap::DestroyAll(LastSDM.getPointer(), LastSDM.getInt());
  LastSDM.setPointer(nullptr);
}

void StoredDeclsMap::DestroyAll(StoredDeclsMap *Map, bool Dependent) {
  while (Map) {
    // Advance the iteration before we invalidate memory.
    llvm::PointerIntPair<StoredDeclsMap*,1> Next = Map->Previous;

    if (Dependent)
      delete static_cast<DependentStoredDeclsMap*>(Map);
    else
      delete Map;

    Map = Next.getPointer();
    Dependent = Next.getInt();
  }
}

DependentDiagnostic *DependentDiagnostic::Create(ASTContext &C,
                                                 DeclContext *Parent,
                                           const PartialDiagnostic &PDiag) {
  assert(Parent->isDependentContext()
         && "cannot iterate dependent diagnostics of non-dependent context");
  Parent = Parent->getPrimaryContext();
  if (!Parent->LookupPtr)
    Parent->CreateStoredDeclsMap(C);

  auto *Map = static_cast<DependentStoredDeclsMap *>(Parent->LookupPtr);

  // Allocate the copy of the PartialDiagnostic via the ASTContext's
  // BumpPtrAllocator, rather than the ASTContext itself.
  DiagnosticStorage *DiagStorage = nullptr;
  if (PDiag.hasStorage())
    DiagStorage = new (C) DiagnosticStorage;

  auto *DD = new (C) DependentDiagnostic(PDiag, DiagStorage);

  // TODO: Maybe we shouldn't reverse the order during insertion.
  DD->NextDiagnostic = Map->FirstDiagnostic;
  Map->FirstDiagnostic = DD;

  return DD;
}

unsigned DeclIDBase::getLocalDeclIndex() const {
  return ID & llvm::maskTrailingOnes<DeclID>(32);
}
