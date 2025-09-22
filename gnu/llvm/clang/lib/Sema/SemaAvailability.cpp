//===--- SemaAvailability.cpp - Availability attribute handling -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file processes the availability attribute.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaObjC.h"
#include "llvm/ADT/StringRef.h"
#include <optional>

using namespace clang;
using namespace sema;

static bool hasMatchingEnvironmentOrNone(const ASTContext &Context,
                                         const AvailabilityAttr *AA) {
  IdentifierInfo *IIEnvironment = AA->getEnvironment();
  auto Environment = Context.getTargetInfo().getTriple().getEnvironment();
  if (!IIEnvironment || Environment == llvm::Triple::UnknownEnvironment)
    return true;

  llvm::Triple::EnvironmentType ET =
      AvailabilityAttr::getEnvironmentType(IIEnvironment->getName());
  return Environment == ET;
}

static const AvailabilityAttr *getAttrForPlatform(ASTContext &Context,
                                                  const Decl *D) {
  AvailabilityAttr const *PartialMatch = nullptr;
  // Check each AvailabilityAttr to find the one for this platform.
  // For multiple attributes with the same platform try to find one for this
  // environment.
  // The attribute is always on the FunctionDecl, not on the
  // FunctionTemplateDecl.
  if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D))
    D = FTD->getTemplatedDecl();
  for (const auto *A : D->attrs()) {
    if (const auto *Avail = dyn_cast<AvailabilityAttr>(A)) {
      // FIXME: this is copied from CheckAvailability. We should try to
      // de-duplicate.

      // Check if this is an App Extension "platform", and if so chop off
      // the suffix for matching with the actual platform.
      StringRef ActualPlatform = Avail->getPlatform()->getName();
      StringRef RealizedPlatform = ActualPlatform;
      if (Context.getLangOpts().AppExt) {
        size_t suffix = RealizedPlatform.rfind("_app_extension");
        if (suffix != StringRef::npos)
          RealizedPlatform = RealizedPlatform.slice(0, suffix);
      }

      StringRef TargetPlatform = Context.getTargetInfo().getPlatformName();

      // Match the platform name.
      if (RealizedPlatform == TargetPlatform) {
        // Find the best matching attribute for this environment
        if (hasMatchingEnvironmentOrNone(Context, Avail))
          return Avail;
        PartialMatch = Avail;
      }
    }
  }
  return PartialMatch;
}

/// The diagnostic we should emit for \c D, and the declaration that
/// originated it, or \c AR_Available.
///
/// \param D The declaration to check.
/// \param Message If non-null, this will be populated with the message from
/// the availability attribute that is selected.
/// \param ClassReceiver If we're checking the method of a class message
/// send, the class. Otherwise nullptr.
static std::pair<AvailabilityResult, const NamedDecl *>
ShouldDiagnoseAvailabilityOfDecl(Sema &S, const NamedDecl *D,
                                 std::string *Message,
                                 ObjCInterfaceDecl *ClassReceiver) {
  AvailabilityResult Result = D->getAvailability(Message);

  // For typedefs, if the typedef declaration appears available look
  // to the underlying type to see if it is more restrictive.
  while (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    if (Result == AR_Available) {
      if (const auto *TT = TD->getUnderlyingType()->getAs<TagType>()) {
        D = TT->getDecl();
        Result = D->getAvailability(Message);
        continue;
      }
    }
    break;
  }

  // For alias templates, get the underlying declaration.
  if (const auto *ADecl = dyn_cast<TypeAliasTemplateDecl>(D)) {
    D = ADecl->getTemplatedDecl();
    Result = D->getAvailability(Message);
  }

  // Forward class declarations get their attributes from their definition.
  if (const auto *IDecl = dyn_cast<ObjCInterfaceDecl>(D)) {
    if (IDecl->getDefinition()) {
      D = IDecl->getDefinition();
      Result = D->getAvailability(Message);
    }
  }

  if (const auto *ECD = dyn_cast<EnumConstantDecl>(D))
    if (Result == AR_Available) {
      const DeclContext *DC = ECD->getDeclContext();
      if (const auto *TheEnumDecl = dyn_cast<EnumDecl>(DC)) {
        Result = TheEnumDecl->getAvailability(Message);
        D = TheEnumDecl;
      }
    }

  // For +new, infer availability from -init.
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    if (S.ObjC().NSAPIObj && ClassReceiver) {
      ObjCMethodDecl *Init = ClassReceiver->lookupInstanceMethod(
          S.ObjC().NSAPIObj->getInitSelector());
      if (Init && Result == AR_Available && MD->isClassMethod() &&
          MD->getSelector() == S.ObjC().NSAPIObj->getNewSelector() &&
          MD->definedInNSObject(S.getASTContext())) {
        Result = Init->getAvailability(Message);
        D = Init;
      }
    }
  }

  return {Result, D};
}


/// whether we should emit a diagnostic for \c K and \c DeclVersion in
/// the context of \c Ctx. For example, we should emit an unavailable diagnostic
/// in a deprecated context, but not the other way around.
static bool ShouldDiagnoseAvailabilityInContext(
    Sema &S, AvailabilityResult K, VersionTuple DeclVersion,
    const IdentifierInfo *DeclEnv, Decl *Ctx, const NamedDecl *OffendingDecl) {
  assert(K != AR_Available && "Expected an unavailable declaration here!");

  // If this was defined using CF_OPTIONS, etc. then ignore the diagnostic.
  auto DeclLoc = Ctx->getBeginLoc();
  // This is only a problem in Foundation's C++ implementation for CF_OPTIONS.
  if (DeclLoc.isMacroID() && S.getLangOpts().CPlusPlus &&
      isa<TypedefDecl>(OffendingDecl)) {
    StringRef MacroName = S.getPreprocessor().getImmediateMacroName(DeclLoc);
    if (MacroName == "CF_OPTIONS" || MacroName == "OBJC_OPTIONS" ||
        MacroName == "SWIFT_OPTIONS" || MacroName == "NS_OPTIONS") {
      return false;
    }
  }

  // In HLSL, skip emitting diagnostic if the diagnostic mode is not set to
  // strict (-fhlsl-strict-availability), or if the target is library and the
  // availability is restricted to a specific environment/shader stage.
  // For libraries the availability will be checked later in
  // DiagnoseHLSLAvailability class once where the specific environment/shader
  // stage of the caller is known.
  if (S.getLangOpts().HLSL) {
    if (!S.getLangOpts().HLSLStrictAvailability ||
        (DeclEnv != nullptr &&
         S.getASTContext().getTargetInfo().getTriple().getEnvironment() ==
             llvm::Triple::EnvironmentType::Library))
      return false;
  }

  // Checks if we should emit the availability diagnostic in the context of C.
  auto CheckContext = [&](const Decl *C) {
    if (K == AR_NotYetIntroduced) {
      if (const AvailabilityAttr *AA = getAttrForPlatform(S.Context, C))
        if (AA->getIntroduced() >= DeclVersion &&
            AA->getEnvironment() == DeclEnv)
          return true;
    } else if (K == AR_Deprecated) {
      if (C->isDeprecated())
        return true;
    } else if (K == AR_Unavailable) {
      // It is perfectly fine to refer to an 'unavailable' Objective-C method
      // when it is referenced from within the @implementation itself. In this
      // context, we interpret unavailable as a form of access control.
      if (const auto *MD = dyn_cast<ObjCMethodDecl>(OffendingDecl)) {
        if (const auto *Impl = dyn_cast<ObjCImplDecl>(C)) {
          if (MD->getClassInterface() == Impl->getClassInterface())
            return true;
        }
      }
    }

    if (C->isUnavailable())
      return true;
    return false;
  };

  do {
    if (CheckContext(Ctx))
      return false;

    // An implementation implicitly has the availability of the interface.
    // Unless it is "+load" method.
    if (const auto *MethodD = dyn_cast<ObjCMethodDecl>(Ctx))
      if (MethodD->isClassMethod() &&
          MethodD->getSelector().getAsString() == "load")
        return true;

    if (const auto *CatOrImpl = dyn_cast<ObjCImplDecl>(Ctx)) {
      if (const ObjCInterfaceDecl *Interface = CatOrImpl->getClassInterface())
        if (CheckContext(Interface))
          return false;
    }
    // A category implicitly has the availability of the interface.
    else if (const auto *CatD = dyn_cast<ObjCCategoryDecl>(Ctx))
      if (const ObjCInterfaceDecl *Interface = CatD->getClassInterface())
        if (CheckContext(Interface))
          return false;
  } while ((Ctx = cast_or_null<Decl>(Ctx->getDeclContext())));

  return true;
}

static unsigned getAvailabilityDiagnosticKind(
    const ASTContext &Context, const VersionTuple &DeploymentVersion,
    const VersionTuple &DeclVersion, bool HasMatchingEnv) {
  const auto &Triple = Context.getTargetInfo().getTriple();
  VersionTuple ForceAvailabilityFromVersion;
  switch (Triple.getOS()) {
  // For iOS, emit the diagnostic even if -Wunguarded-availability is
  // not specified for deployment targets >= to iOS 11 or equivalent or
  // for declarations that were introduced in iOS 11 (macOS 10.13, ...) or
  // later.
  case llvm::Triple::IOS:
  case llvm::Triple::TvOS:
    ForceAvailabilityFromVersion = VersionTuple(/*Major=*/11);
    break;
  case llvm::Triple::WatchOS:
    ForceAvailabilityFromVersion = VersionTuple(/*Major=*/4);
    break;
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    ForceAvailabilityFromVersion = VersionTuple(/*Major=*/10, /*Minor=*/13);
    break;
  // For HLSL, use diagnostic from HLSLAvailability group which
  // are reported as errors by default and in strict diagnostic mode
  // (-fhlsl-strict-availability) and as warnings in relaxed diagnostic
  // mode (-Wno-error=hlsl-availability)
  case llvm::Triple::ShaderModel:
    return HasMatchingEnv ? diag::warn_hlsl_availability
                          : diag::warn_hlsl_availability_unavailable;
  default:
    // New Apple targets should always warn about availability.
    ForceAvailabilityFromVersion =
        (Triple.getVendor() == llvm::Triple::Apple)
            ? VersionTuple(/*Major=*/0, 0)
            : VersionTuple(/*Major=*/(unsigned)-1, (unsigned)-1);
  }
  if (DeploymentVersion >= ForceAvailabilityFromVersion ||
      DeclVersion >= ForceAvailabilityFromVersion)
    return HasMatchingEnv ? diag::warn_unguarded_availability_new
                          : diag::warn_unguarded_availability_unavailable_new;
  return HasMatchingEnv ? diag::warn_unguarded_availability
                        : diag::warn_unguarded_availability_unavailable;
}

static NamedDecl *findEnclosingDeclToAnnotate(Decl *OrigCtx) {
  for (Decl *Ctx = OrigCtx; Ctx;
       Ctx = cast_or_null<Decl>(Ctx->getDeclContext())) {
    if (isa<TagDecl>(Ctx) || isa<FunctionDecl>(Ctx) || isa<ObjCMethodDecl>(Ctx))
      return cast<NamedDecl>(Ctx);
    if (auto *CD = dyn_cast<ObjCContainerDecl>(Ctx)) {
      if (auto *Imp = dyn_cast<ObjCImplDecl>(Ctx))
        return Imp->getClassInterface();
      return CD;
    }
  }

  return dyn_cast<NamedDecl>(OrigCtx);
}

namespace {

struct AttributeInsertion {
  StringRef Prefix;
  SourceLocation Loc;
  StringRef Suffix;

  static AttributeInsertion createInsertionAfter(const NamedDecl *D) {
    return {" ", D->getEndLoc(), ""};
  }
  static AttributeInsertion createInsertionAfter(SourceLocation Loc) {
    return {" ", Loc, ""};
  }
  static AttributeInsertion createInsertionBefore(const NamedDecl *D) {
    return {"", D->getBeginLoc(), "\n"};
  }
};

} // end anonymous namespace

/// Tries to parse a string as ObjC method name.
///
/// \param Name The string to parse. Expected to originate from availability
/// attribute argument.
/// \param SlotNames The vector that will be populated with slot names. In case
/// of unsuccessful parsing can contain invalid data.
/// \returns A number of method parameters if parsing was successful,
/// std::nullopt otherwise.
static std::optional<unsigned>
tryParseObjCMethodName(StringRef Name, SmallVectorImpl<StringRef> &SlotNames,
                       const LangOptions &LangOpts) {
  // Accept replacements starting with - or + as valid ObjC method names.
  if (!Name.empty() && (Name.front() == '-' || Name.front() == '+'))
    Name = Name.drop_front(1);
  if (Name.empty())
    return std::nullopt;
  Name.split(SlotNames, ':');
  unsigned NumParams;
  if (Name.back() == ':') {
    // Remove an empty string at the end that doesn't represent any slot.
    SlotNames.pop_back();
    NumParams = SlotNames.size();
  } else {
    if (SlotNames.size() != 1)
      // Not a valid method name, just a colon-separated string.
      return std::nullopt;
    NumParams = 0;
  }
  // Verify all slot names are valid.
  bool AllowDollar = LangOpts.DollarIdents;
  for (StringRef S : SlotNames) {
    if (S.empty())
      continue;
    if (!isValidAsciiIdentifier(S, AllowDollar))
      return std::nullopt;
  }
  return NumParams;
}

/// Returns a source location in which it's appropriate to insert a new
/// attribute for the given declaration \D.
static std::optional<AttributeInsertion>
createAttributeInsertion(const NamedDecl *D, const SourceManager &SM,
                         const LangOptions &LangOpts) {
  if (isa<ObjCPropertyDecl>(D))
    return AttributeInsertion::createInsertionAfter(D);
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    if (MD->hasBody())
      return std::nullopt;
    return AttributeInsertion::createInsertionAfter(D);
  }
  if (const auto *TD = dyn_cast<TagDecl>(D)) {
    SourceLocation Loc =
        Lexer::getLocForEndOfToken(TD->getInnerLocStart(), 0, SM, LangOpts);
    if (Loc.isInvalid())
      return std::nullopt;
    // Insert after the 'struct'/whatever keyword.
    return AttributeInsertion::createInsertionAfter(Loc);
  }
  return AttributeInsertion::createInsertionBefore(D);
}

/// Actually emit an availability diagnostic for a reference to an unavailable
/// decl.
///
/// \param Ctx The context that the reference occurred in
/// \param ReferringDecl The exact declaration that was referenced.
/// \param OffendingDecl A related decl to \c ReferringDecl that has an
/// availability attribute corresponding to \c K attached to it. Note that this
/// may not be the same as ReferringDecl, i.e. if an EnumDecl is annotated and
/// we refer to a member EnumConstantDecl, ReferringDecl is the EnumConstantDecl
/// and OffendingDecl is the EnumDecl.
static void DoEmitAvailabilityWarning(Sema &S, AvailabilityResult K,
                                      Decl *Ctx, const NamedDecl *ReferringDecl,
                                      const NamedDecl *OffendingDecl,
                                      StringRef Message,
                                      ArrayRef<SourceLocation> Locs,
                                      const ObjCInterfaceDecl *UnknownObjCClass,
                                      const ObjCPropertyDecl *ObjCProperty,
                                      bool ObjCPropertyAccess) {
  // Diagnostics for deprecated or unavailable.
  unsigned diag, diag_message, diag_fwdclass_message;
  unsigned diag_available_here = diag::note_availability_specified_here;
  SourceLocation NoteLocation = OffendingDecl->getLocation();

  // Matches 'diag::note_property_attribute' options.
  unsigned property_note_select;

  // Matches diag::note_availability_specified_here.
  unsigned available_here_select_kind;

  VersionTuple DeclVersion;
  const AvailabilityAttr *AA = getAttrForPlatform(S.Context, OffendingDecl);
  const IdentifierInfo *IIEnv = nullptr;
  if (AA) {
    DeclVersion = AA->getIntroduced();
    IIEnv = AA->getEnvironment();
  }

  if (!ShouldDiagnoseAvailabilityInContext(S, K, DeclVersion, IIEnv, Ctx,
                                           OffendingDecl))
    return;

  SourceLocation Loc = Locs.front();

  // The declaration can have multiple availability attributes, we are looking
  // at one of them.
  if (AA && AA->isInherited()) {
    for (const Decl *Redecl = OffendingDecl->getMostRecentDecl(); Redecl;
         Redecl = Redecl->getPreviousDecl()) {
      const AvailabilityAttr *AForRedecl =
          getAttrForPlatform(S.Context, Redecl);
      if (AForRedecl && !AForRedecl->isInherited()) {
        // If D is a declaration with inherited attributes, the note should
        // point to the declaration with actual attributes.
        NoteLocation = Redecl->getLocation();
        break;
      }
    }
  }

  switch (K) {
  case AR_NotYetIntroduced: {
    // We would like to emit the diagnostic even if -Wunguarded-availability is
    // not specified for deployment targets >= to iOS 11 or equivalent or
    // for declarations that were introduced in iOS 11 (macOS 10.13, ...) or
    // later.
    assert(AA != nullptr && "expecting valid availability attribute");
    VersionTuple Introduced = AA->getIntroduced();
    bool EnvironmentMatchesOrNone =
        hasMatchingEnvironmentOrNone(S.getASTContext(), AA);

    const TargetInfo &TI = S.getASTContext().getTargetInfo();
    std::string PlatformName(
        AvailabilityAttr::getPrettyPlatformName(TI.getPlatformName()));
    llvm::StringRef TargetEnvironment(
        llvm::Triple::getEnvironmentTypeName(TI.getTriple().getEnvironment()));
    llvm::StringRef AttrEnvironment =
        AA->getEnvironment() ? AA->getEnvironment()->getName() : "";
    bool UseEnvironment =
        (!AttrEnvironment.empty() && !TargetEnvironment.empty());

    unsigned DiagKind = getAvailabilityDiagnosticKind(
        S.Context, S.Context.getTargetInfo().getPlatformMinVersion(),
        Introduced, EnvironmentMatchesOrNone);

    S.Diag(Loc, DiagKind) << OffendingDecl << PlatformName
                          << Introduced.getAsString() << UseEnvironment
                          << TargetEnvironment;

    S.Diag(OffendingDecl->getLocation(),
           diag::note_partial_availability_specified_here)
        << OffendingDecl << PlatformName << Introduced.getAsString()
        << S.Context.getTargetInfo().getPlatformMinVersion().getAsString()
        << UseEnvironment << AttrEnvironment << TargetEnvironment;

    // Do not offer to silence the warning or fixits for HLSL
    if (S.getLangOpts().HLSL)
      return;

    if (const auto *Enclosing = findEnclosingDeclToAnnotate(Ctx)) {
      if (const auto *TD = dyn_cast<TagDecl>(Enclosing))
        if (TD->getDeclName().isEmpty()) {
          S.Diag(TD->getLocation(),
                 diag::note_decl_unguarded_availability_silence)
              << /*Anonymous*/ 1 << TD->getKindName();
          return;
        }
      auto FixitNoteDiag =
          S.Diag(Enclosing->getLocation(),
                 diag::note_decl_unguarded_availability_silence)
          << /*Named*/ 0 << Enclosing;
      // Don't offer a fixit for declarations with availability attributes.
      if (Enclosing->hasAttr<AvailabilityAttr>())
        return;
      if (!S.getPreprocessor().isMacroDefined("API_AVAILABLE"))
        return;
      std::optional<AttributeInsertion> Insertion = createAttributeInsertion(
          Enclosing, S.getSourceManager(), S.getLangOpts());
      if (!Insertion)
        return;
      std::string PlatformName =
          AvailabilityAttr::getPlatformNameSourceSpelling(
              S.getASTContext().getTargetInfo().getPlatformName())
              .lower();
      std::string Introduced =
          OffendingDecl->getVersionIntroduced().getAsString();
      FixitNoteDiag << FixItHint::CreateInsertion(
          Insertion->Loc,
          (llvm::Twine(Insertion->Prefix) + "API_AVAILABLE(" + PlatformName +
           "(" + Introduced + "))" + Insertion->Suffix)
              .str());
    }
    return;
  }
  case AR_Deprecated:
    diag = !ObjCPropertyAccess ? diag::warn_deprecated
                               : diag::warn_property_method_deprecated;
    diag_message = diag::warn_deprecated_message;
    diag_fwdclass_message = diag::warn_deprecated_fwdclass_message;
    property_note_select = /* deprecated */ 0;
    available_here_select_kind = /* deprecated */ 2;
    if (const auto *AL = OffendingDecl->getAttr<DeprecatedAttr>())
      NoteLocation = AL->getLocation();
    break;

  case AR_Unavailable:
    diag = !ObjCPropertyAccess ? diag::err_unavailable
                               : diag::err_property_method_unavailable;
    diag_message = diag::err_unavailable_message;
    diag_fwdclass_message = diag::warn_unavailable_fwdclass_message;
    property_note_select = /* unavailable */ 1;
    available_here_select_kind = /* unavailable */ 0;

    if (auto AL = OffendingDecl->getAttr<UnavailableAttr>()) {
      if (AL->isImplicit() && AL->getImplicitReason()) {
        // Most of these failures are due to extra restrictions in ARC;
        // reflect that in the primary diagnostic when applicable.
        auto flagARCError = [&] {
          if (S.getLangOpts().ObjCAutoRefCount &&
              S.getSourceManager().isInSystemHeader(
                  OffendingDecl->getLocation()))
            diag = diag::err_unavailable_in_arc;
        };

        switch (AL->getImplicitReason()) {
        case UnavailableAttr::IR_None: break;

        case UnavailableAttr::IR_ARCForbiddenType:
          flagARCError();
          diag_available_here = diag::note_arc_forbidden_type;
          break;

        case UnavailableAttr::IR_ForbiddenWeak:
          if (S.getLangOpts().ObjCWeakRuntime)
            diag_available_here = diag::note_arc_weak_disabled;
          else
            diag_available_here = diag::note_arc_weak_no_runtime;
          break;

        case UnavailableAttr::IR_ARCForbiddenConversion:
          flagARCError();
          diag_available_here = diag::note_performs_forbidden_arc_conversion;
          break;

        case UnavailableAttr::IR_ARCInitReturnsUnrelated:
          flagARCError();
          diag_available_here = diag::note_arc_init_returns_unrelated;
          break;

        case UnavailableAttr::IR_ARCFieldWithOwnership:
          flagARCError();
          diag_available_here = diag::note_arc_field_with_ownership;
          break;
        }
      }
    }
    break;

  case AR_Available:
    llvm_unreachable("Warning for availability of available declaration?");
  }

  SmallVector<FixItHint, 12> FixIts;
  if (K == AR_Deprecated) {
    StringRef Replacement;
    if (auto AL = OffendingDecl->getAttr<DeprecatedAttr>())
      Replacement = AL->getReplacement();
    if (auto AL = getAttrForPlatform(S.Context, OffendingDecl))
      Replacement = AL->getReplacement();

    CharSourceRange UseRange;
    if (!Replacement.empty())
      UseRange =
          CharSourceRange::getCharRange(Loc, S.getLocForEndOfToken(Loc));
    if (UseRange.isValid()) {
      if (const auto *MethodDecl = dyn_cast<ObjCMethodDecl>(ReferringDecl)) {
        Selector Sel = MethodDecl->getSelector();
        SmallVector<StringRef, 12> SelectorSlotNames;
        std::optional<unsigned> NumParams = tryParseObjCMethodName(
            Replacement, SelectorSlotNames, S.getLangOpts());
        if (NumParams && *NumParams == Sel.getNumArgs()) {
          assert(SelectorSlotNames.size() == Locs.size());
          for (unsigned I = 0; I < Locs.size(); ++I) {
            if (!Sel.getNameForSlot(I).empty()) {
              CharSourceRange NameRange = CharSourceRange::getCharRange(
                  Locs[I], S.getLocForEndOfToken(Locs[I]));
              FixIts.push_back(FixItHint::CreateReplacement(
                  NameRange, SelectorSlotNames[I]));
            } else
              FixIts.push_back(
                  FixItHint::CreateInsertion(Locs[I], SelectorSlotNames[I]));
          }
        } else
          FixIts.push_back(FixItHint::CreateReplacement(UseRange, Replacement));
      } else
        FixIts.push_back(FixItHint::CreateReplacement(UseRange, Replacement));
    }
  }

  // We emit deprecation warning for deprecated specializations
  // when their instantiation stacks originate outside
  // of a system header, even if the diagnostics is suppresed at the
  // point of definition.
  SourceLocation InstantiationLoc =
      S.getTopMostPointOfInstantiation(ReferringDecl);
  bool ShouldAllowWarningInSystemHeader =
      InstantiationLoc != Loc &&
      !S.getSourceManager().isInSystemHeader(InstantiationLoc);
  struct AllowWarningInSystemHeaders {
    AllowWarningInSystemHeaders(DiagnosticsEngine &E,
                                bool AllowWarningInSystemHeaders)
        : Engine(E), Prev(E.getSuppressSystemWarnings()) {
      E.setSuppressSystemWarnings(!AllowWarningInSystemHeaders);
    }
    ~AllowWarningInSystemHeaders() { Engine.setSuppressSystemWarnings(Prev); }

  private:
    DiagnosticsEngine &Engine;
    bool Prev;
  } SystemWarningOverrideRAII(S.getDiagnostics(),
                              ShouldAllowWarningInSystemHeader);

  if (!Message.empty()) {
    S.Diag(Loc, diag_message) << ReferringDecl << Message << FixIts;
    if (ObjCProperty)
      S.Diag(ObjCProperty->getLocation(), diag::note_property_attribute)
          << ObjCProperty->getDeclName() << property_note_select;
  } else if (!UnknownObjCClass) {
    S.Diag(Loc, diag) << ReferringDecl << FixIts;
    if (ObjCProperty)
      S.Diag(ObjCProperty->getLocation(), diag::note_property_attribute)
          << ObjCProperty->getDeclName() << property_note_select;
  } else {
    S.Diag(Loc, diag_fwdclass_message) << ReferringDecl << FixIts;
    S.Diag(UnknownObjCClass->getLocation(), diag::note_forward_class);
  }

  S.Diag(NoteLocation, diag_available_here)
    << OffendingDecl << available_here_select_kind;
}

void Sema::handleDelayedAvailabilityCheck(DelayedDiagnostic &DD, Decl *Ctx) {
  assert(DD.Kind == DelayedDiagnostic::Availability &&
         "Expected an availability diagnostic here");

  DD.Triggered = true;
  DoEmitAvailabilityWarning(
      *this, DD.getAvailabilityResult(), Ctx, DD.getAvailabilityReferringDecl(),
      DD.getAvailabilityOffendingDecl(), DD.getAvailabilityMessage(),
      DD.getAvailabilitySelectorLocs(), DD.getUnknownObjCClass(),
      DD.getObjCProperty(), false);
}

static void EmitAvailabilityWarning(Sema &S, AvailabilityResult AR,
                                    const NamedDecl *ReferringDecl,
                                    const NamedDecl *OffendingDecl,
                                    StringRef Message,
                                    ArrayRef<SourceLocation> Locs,
                                    const ObjCInterfaceDecl *UnknownObjCClass,
                                    const ObjCPropertyDecl *ObjCProperty,
                                    bool ObjCPropertyAccess) {
  // Delay if we're currently parsing a declaration.
  if (S.DelayedDiagnostics.shouldDelayDiagnostics()) {
    S.DelayedDiagnostics.add(
        DelayedDiagnostic::makeAvailability(
            AR, Locs, ReferringDecl, OffendingDecl, UnknownObjCClass,
            ObjCProperty, Message, ObjCPropertyAccess));
    return;
  }

  Decl *Ctx = cast<Decl>(S.getCurLexicalContext());
  DoEmitAvailabilityWarning(S, AR, Ctx, ReferringDecl, OffendingDecl,
                            Message, Locs, UnknownObjCClass, ObjCProperty,
                            ObjCPropertyAccess);
}

namespace {

/// Returns true if the given statement can be a body-like child of \p Parent.
bool isBodyLikeChildStmt(const Stmt *S, const Stmt *Parent) {
  switch (Parent->getStmtClass()) {
  case Stmt::IfStmtClass:
    return cast<IfStmt>(Parent)->getThen() == S ||
           cast<IfStmt>(Parent)->getElse() == S;
  case Stmt::WhileStmtClass:
    return cast<WhileStmt>(Parent)->getBody() == S;
  case Stmt::DoStmtClass:
    return cast<DoStmt>(Parent)->getBody() == S;
  case Stmt::ForStmtClass:
    return cast<ForStmt>(Parent)->getBody() == S;
  case Stmt::CXXForRangeStmtClass:
    return cast<CXXForRangeStmt>(Parent)->getBody() == S;
  case Stmt::ObjCForCollectionStmtClass:
    return cast<ObjCForCollectionStmt>(Parent)->getBody() == S;
  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
    return cast<SwitchCase>(Parent)->getSubStmt() == S;
  default:
    return false;
  }
}

class StmtUSEFinder : public RecursiveASTVisitor<StmtUSEFinder> {
  const Stmt *Target;

public:
  bool VisitStmt(Stmt *S) { return S != Target; }

  /// Returns true if the given statement is present in the given declaration.
  static bool isContained(const Stmt *Target, const Decl *D) {
    StmtUSEFinder Visitor;
    Visitor.Target = Target;
    return !Visitor.TraverseDecl(const_cast<Decl *>(D));
  }
};

/// Traverses the AST and finds the last statement that used a given
/// declaration.
class LastDeclUSEFinder : public RecursiveASTVisitor<LastDeclUSEFinder> {
  const Decl *D;

public:
  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (DRE->getDecl() == D)
      return false;
    return true;
  }

  static const Stmt *findLastStmtThatUsesDecl(const Decl *D,
                                              const CompoundStmt *Scope) {
    LastDeclUSEFinder Visitor;
    Visitor.D = D;
    for (const Stmt *S : llvm::reverse(Scope->body())) {
      if (!Visitor.TraverseStmt(const_cast<Stmt *>(S)))
        return S;
    }
    return nullptr;
  }
};

/// This class implements -Wunguarded-availability.
///
/// This is done with a traversal of the AST of a function that makes reference
/// to a partially available declaration. Whenever we encounter an \c if of the
/// form: \c if(@available(...)), we use the version from the condition to visit
/// the then statement.
class DiagnoseUnguardedAvailability
    : public RecursiveASTVisitor<DiagnoseUnguardedAvailability> {
  typedef RecursiveASTVisitor<DiagnoseUnguardedAvailability> Base;

  Sema &SemaRef;
  Decl *Ctx;

  /// Stack of potentially nested 'if (@available(...))'s.
  SmallVector<VersionTuple, 8> AvailabilityStack;
  SmallVector<const Stmt *, 16> StmtStack;

  void DiagnoseDeclAvailability(NamedDecl *D, SourceRange Range,
                                ObjCInterfaceDecl *ClassReceiver = nullptr);

public:
  DiagnoseUnguardedAvailability(Sema &SemaRef, Decl *Ctx)
      : SemaRef(SemaRef), Ctx(Ctx) {
    AvailabilityStack.push_back(
        SemaRef.Context.getTargetInfo().getPlatformMinVersion());
  }

  bool TraverseStmt(Stmt *S) {
    if (!S)
      return true;
    StmtStack.push_back(S);
    bool Result = Base::TraverseStmt(S);
    StmtStack.pop_back();
    return Result;
  }

  void IssueDiagnostics(Stmt *S) { TraverseStmt(S); }

  bool TraverseIfStmt(IfStmt *If);

  // for 'case X:' statements, don't bother looking at the 'X'; it can't lead
  // to any useful diagnostics.
  bool TraverseCaseStmt(CaseStmt *CS) { return TraverseStmt(CS->getSubStmt()); }

  bool VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *PRE) { return true; }

  bool VisitObjCMessageExpr(ObjCMessageExpr *Msg) {
    if (ObjCMethodDecl *D = Msg->getMethodDecl()) {
      ObjCInterfaceDecl *ID = nullptr;
      QualType ReceiverTy = Msg->getClassReceiver();
      if (!ReceiverTy.isNull() && ReceiverTy->getAsObjCInterfaceType())
        ID = ReceiverTy->getAsObjCInterfaceType()->getInterface();

      DiagnoseDeclAvailability(
          D, SourceRange(Msg->getSelectorStartLoc(), Msg->getEndLoc()), ID);
    }
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    DiagnoseDeclAvailability(DRE->getDecl(),
                             SourceRange(DRE->getBeginLoc(), DRE->getEndLoc()));
    return true;
  }

  bool VisitMemberExpr(MemberExpr *ME) {
    DiagnoseDeclAvailability(ME->getMemberDecl(),
                             SourceRange(ME->getBeginLoc(), ME->getEndLoc()));
    return true;
  }

  bool VisitObjCAvailabilityCheckExpr(ObjCAvailabilityCheckExpr *E) {
    SemaRef.Diag(E->getBeginLoc(), diag::warn_at_available_unchecked_use)
        << (!SemaRef.getLangOpts().ObjC);
    return true;
  }

  bool VisitTypeLoc(TypeLoc Ty);
};

void DiagnoseUnguardedAvailability::DiagnoseDeclAvailability(
    NamedDecl *D, SourceRange Range, ObjCInterfaceDecl *ReceiverClass) {
  AvailabilityResult Result;
  const NamedDecl *OffendingDecl;
  std::tie(Result, OffendingDecl) =
      ShouldDiagnoseAvailabilityOfDecl(SemaRef, D, nullptr, ReceiverClass);
  if (Result != AR_Available) {
    // All other diagnostic kinds have already been handled in
    // DiagnoseAvailabilityOfDecl.
    if (Result != AR_NotYetIntroduced)
      return;

    const AvailabilityAttr *AA =
      getAttrForPlatform(SemaRef.getASTContext(), OffendingDecl);
    assert(AA != nullptr && "expecting valid availability attribute");
    bool EnvironmentMatchesOrNone =
        hasMatchingEnvironmentOrNone(SemaRef.getASTContext(), AA);
    VersionTuple Introduced = AA->getIntroduced();

    if (EnvironmentMatchesOrNone && AvailabilityStack.back() >= Introduced)
      return;

    // If the context of this function is less available than D, we should not
    // emit a diagnostic.
    if (!ShouldDiagnoseAvailabilityInContext(SemaRef, Result, Introduced,
                                             AA->getEnvironment(), Ctx,
                                             OffendingDecl))
      return;

    const TargetInfo &TI = SemaRef.getASTContext().getTargetInfo();
    std::string PlatformName(
        AvailabilityAttr::getPrettyPlatformName(TI.getPlatformName()));
    llvm::StringRef TargetEnvironment(TI.getTriple().getEnvironmentName());
    llvm::StringRef AttrEnvironment =
        AA->getEnvironment() ? AA->getEnvironment()->getName() : "";
    bool UseEnvironment =
        (!AttrEnvironment.empty() && !TargetEnvironment.empty());

    unsigned DiagKind = getAvailabilityDiagnosticKind(
        SemaRef.Context,
        SemaRef.Context.getTargetInfo().getPlatformMinVersion(), Introduced,
        EnvironmentMatchesOrNone);

    SemaRef.Diag(Range.getBegin(), DiagKind)
        << Range << D << PlatformName << Introduced.getAsString()
        << UseEnvironment << TargetEnvironment;

    SemaRef.Diag(OffendingDecl->getLocation(),
                 diag::note_partial_availability_specified_here)
        << OffendingDecl << PlatformName << Introduced.getAsString()
        << SemaRef.Context.getTargetInfo().getPlatformMinVersion().getAsString()
        << UseEnvironment << AttrEnvironment << TargetEnvironment;

    // Do not offer to silence the warning or fixits for HLSL
    if (SemaRef.getLangOpts().HLSL)
      return;

    auto FixitDiag =
        SemaRef.Diag(Range.getBegin(), diag::note_unguarded_available_silence)
        << Range << D
        << (SemaRef.getLangOpts().ObjC ? /*@available*/ 0
                                       : /*__builtin_available*/ 1);

    // Find the statement which should be enclosed in the if @available check.
    if (StmtStack.empty())
      return;
    const Stmt *StmtOfUse = StmtStack.back();
    const CompoundStmt *Scope = nullptr;
    for (const Stmt *S : llvm::reverse(StmtStack)) {
      if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
        Scope = CS;
        break;
      }
      if (isBodyLikeChildStmt(StmtOfUse, S)) {
        // The declaration won't be seen outside of the statement, so we don't
        // have to wrap the uses of any declared variables in if (@available).
        // Therefore we can avoid setting Scope here.
        break;
      }
      StmtOfUse = S;
    }
    const Stmt *LastStmtOfUse = nullptr;
    if (isa<DeclStmt>(StmtOfUse) && Scope) {
      for (const Decl *D : cast<DeclStmt>(StmtOfUse)->decls()) {
        if (StmtUSEFinder::isContained(StmtStack.back(), D)) {
          LastStmtOfUse = LastDeclUSEFinder::findLastStmtThatUsesDecl(D, Scope);
          break;
        }
      }
    }

    const SourceManager &SM = SemaRef.getSourceManager();
    SourceLocation IfInsertionLoc =
        SM.getExpansionLoc(StmtOfUse->getBeginLoc());
    SourceLocation StmtEndLoc =
        SM.getExpansionRange(
              (LastStmtOfUse ? LastStmtOfUse : StmtOfUse)->getEndLoc())
            .getEnd();
    if (SM.getFileID(IfInsertionLoc) != SM.getFileID(StmtEndLoc))
      return;

    StringRef Indentation = Lexer::getIndentationForLine(IfInsertionLoc, SM);
    const char *ExtraIndentation = "    ";
    std::string FixItString;
    llvm::raw_string_ostream FixItOS(FixItString);
    FixItOS << "if (" << (SemaRef.getLangOpts().ObjC ? "@available"
                                                     : "__builtin_available")
            << "("
            << AvailabilityAttr::getPlatformNameSourceSpelling(
                   SemaRef.getASTContext().getTargetInfo().getPlatformName())
            << " " << Introduced.getAsString() << ", *)) {\n"
            << Indentation << ExtraIndentation;
    FixitDiag << FixItHint::CreateInsertion(IfInsertionLoc, FixItOS.str());
    SourceLocation ElseInsertionLoc = Lexer::findLocationAfterToken(
        StmtEndLoc, tok::semi, SM, SemaRef.getLangOpts(),
        /*SkipTrailingWhitespaceAndNewLine=*/false);
    if (ElseInsertionLoc.isInvalid())
      ElseInsertionLoc =
          Lexer::getLocForEndOfToken(StmtEndLoc, 0, SM, SemaRef.getLangOpts());
    FixItOS.str().clear();
    FixItOS << "\n"
            << Indentation << "} else {\n"
            << Indentation << ExtraIndentation
            << "// Fallback on earlier versions\n"
            << Indentation << "}";
    FixitDiag << FixItHint::CreateInsertion(ElseInsertionLoc, FixItOS.str());
  }
}

bool DiagnoseUnguardedAvailability::VisitTypeLoc(TypeLoc Ty) {
  const Type *TyPtr = Ty.getTypePtr();
  SourceRange Range{Ty.getBeginLoc(), Ty.getEndLoc()};

  if (Range.isInvalid())
    return true;

  if (const auto *TT = dyn_cast<TagType>(TyPtr)) {
    TagDecl *TD = TT->getDecl();
    DiagnoseDeclAvailability(TD, Range);

  } else if (const auto *TD = dyn_cast<TypedefType>(TyPtr)) {
    TypedefNameDecl *D = TD->getDecl();
    DiagnoseDeclAvailability(D, Range);

  } else if (const auto *ObjCO = dyn_cast<ObjCObjectType>(TyPtr)) {
    if (NamedDecl *D = ObjCO->getInterface())
      DiagnoseDeclAvailability(D, Range);
  }

  return true;
}

bool DiagnoseUnguardedAvailability::TraverseIfStmt(IfStmt *If) {
  VersionTuple CondVersion;
  if (auto *E = dyn_cast<ObjCAvailabilityCheckExpr>(If->getCond())) {
    CondVersion = E->getVersion();

    // If we're using the '*' case here or if this check is redundant, then we
    // use the enclosing version to check both branches.
    if (CondVersion.empty() || CondVersion <= AvailabilityStack.back())
      return TraverseStmt(If->getThen()) && TraverseStmt(If->getElse());
  } else {
    // This isn't an availability checking 'if', we can just continue.
    return Base::TraverseIfStmt(If);
  }

  AvailabilityStack.push_back(CondVersion);
  bool ShouldContinue = TraverseStmt(If->getThen());
  AvailabilityStack.pop_back();

  return ShouldContinue && TraverseStmt(If->getElse());
}

} // end anonymous namespace

void Sema::DiagnoseUnguardedAvailabilityViolations(Decl *D) {
  Stmt *Body = nullptr;

  if (auto *FD = D->getAsFunction()) {
    Body = FD->getBody();

    if (auto *CD = dyn_cast<CXXConstructorDecl>(FD))
      for (const CXXCtorInitializer *CI : CD->inits())
        DiagnoseUnguardedAvailability(*this, D).IssueDiagnostics(CI->getInit());

  } else if (auto *MD = dyn_cast<ObjCMethodDecl>(D))
    Body = MD->getBody();
  else if (auto *BD = dyn_cast<BlockDecl>(D))
    Body = BD->getBody();

  assert(Body && "Need a body here!");

  DiagnoseUnguardedAvailability(*this, D).IssueDiagnostics(Body);
}

FunctionScopeInfo *Sema::getCurFunctionAvailabilityContext() {
  if (FunctionScopes.empty())
    return nullptr;

  // Conservatively search the entire current function scope context for
  // availability violations. This ensures we always correctly analyze nested
  // classes, blocks, lambdas, etc. that may or may not be inside if(@available)
  // checks themselves.
  return FunctionScopes.front();
}

void Sema::DiagnoseAvailabilityOfDecl(NamedDecl *D,
                                      ArrayRef<SourceLocation> Locs,
                                      const ObjCInterfaceDecl *UnknownObjCClass,
                                      bool ObjCPropertyAccess,
                                      bool AvoidPartialAvailabilityChecks,
                                      ObjCInterfaceDecl *ClassReceiver) {
  std::string Message;
  AvailabilityResult Result;
  const NamedDecl* OffendingDecl;
  // See if this declaration is unavailable, deprecated, or partial.
  std::tie(Result, OffendingDecl) =
      ShouldDiagnoseAvailabilityOfDecl(*this, D, &Message, ClassReceiver);
  if (Result == AR_Available)
    return;

  if (Result == AR_NotYetIntroduced) {
    if (AvoidPartialAvailabilityChecks)
      return;

    // We need to know the @available context in the current function to
    // diagnose this use, let DiagnoseUnguardedAvailabilityViolations do that
    // when we're done parsing the current function.
    if (FunctionScopeInfo *Context = getCurFunctionAvailabilityContext()) {
      Context->HasPotentialAvailabilityViolations = true;
      return;
    }
  }

  const ObjCPropertyDecl *ObjCPDecl = nullptr;
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    if (const ObjCPropertyDecl *PD = MD->findPropertyDecl()) {
      AvailabilityResult PDeclResult = PD->getAvailability(nullptr);
      if (PDeclResult == Result)
        ObjCPDecl = PD;
    }
  }

  EmitAvailabilityWarning(*this, Result, D, OffendingDecl, Message, Locs,
                          UnknownObjCClass, ObjCPDecl, ObjCPropertyAccess);
}
