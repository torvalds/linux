//== RetainSummaryManager.cpp - Summaries for reference counting --*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines summaries implementation for retain counting, which
//  implements a reference count checker for Core Foundation, Cocoa
//  and OSObject (on Mac OS X).
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/DomainSpecific/CocoaConventions.h"
#include "clang/Analysis/RetainSummaryManager.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ParentMap.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <optional>

using namespace clang;
using namespace ento;

template <class T>
constexpr static bool isOneOf() {
  return false;
}

/// Helper function to check whether the class is one of the
/// rest of varargs.
template <class T, class P, class... ToCompare>
constexpr static bool isOneOf() {
  return std::is_same_v<T, P> || isOneOf<T, ToCompare...>();
}

namespace {

/// Fake attribute class for RC* attributes.
struct GeneralizedReturnsRetainedAttr {
  static bool classof(const Attr *A) {
    if (auto AA = dyn_cast<AnnotateAttr>(A))
      return AA->getAnnotation() == "rc_ownership_returns_retained";
    return false;
  }
};

struct GeneralizedReturnsNotRetainedAttr {
  static bool classof(const Attr *A) {
    if (auto AA = dyn_cast<AnnotateAttr>(A))
      return AA->getAnnotation() == "rc_ownership_returns_not_retained";
    return false;
  }
};

struct GeneralizedConsumedAttr {
  static bool classof(const Attr *A) {
    if (auto AA = dyn_cast<AnnotateAttr>(A))
      return AA->getAnnotation() == "rc_ownership_consumed";
    return false;
  }
};

}

template <class T>
std::optional<ObjKind> RetainSummaryManager::hasAnyEnabledAttrOf(const Decl *D,
                                                                 QualType QT) {
  ObjKind K;
  if (isOneOf<T, CFConsumedAttr, CFReturnsRetainedAttr,
              CFReturnsNotRetainedAttr>()) {
    if (!TrackObjCAndCFObjects)
      return std::nullopt;

    K = ObjKind::CF;
  } else if (isOneOf<T, NSConsumedAttr, NSConsumesSelfAttr,
                     NSReturnsAutoreleasedAttr, NSReturnsRetainedAttr,
                     NSReturnsNotRetainedAttr, NSConsumesSelfAttr>()) {

    if (!TrackObjCAndCFObjects)
      return std::nullopt;

    if (isOneOf<T, NSReturnsRetainedAttr, NSReturnsAutoreleasedAttr,
                NSReturnsNotRetainedAttr>() &&
        !cocoa::isCocoaObjectRef(QT))
      return std::nullopt;
    K = ObjKind::ObjC;
  } else if (isOneOf<T, OSConsumedAttr, OSConsumesThisAttr,
                     OSReturnsNotRetainedAttr, OSReturnsRetainedAttr,
                     OSReturnsRetainedOnZeroAttr,
                     OSReturnsRetainedOnNonZeroAttr>()) {
    if (!TrackOSObjects)
      return std::nullopt;
    K = ObjKind::OS;
  } else if (isOneOf<T, GeneralizedReturnsNotRetainedAttr,
                     GeneralizedReturnsRetainedAttr,
                     GeneralizedConsumedAttr>()) {
    K = ObjKind::Generalized;
  } else {
    llvm_unreachable("Unexpected attribute");
  }
  if (D->hasAttr<T>())
    return K;
  return std::nullopt;
}

template <class T1, class T2, class... Others>
std::optional<ObjKind> RetainSummaryManager::hasAnyEnabledAttrOf(const Decl *D,
                                                                 QualType QT) {
  if (auto Out = hasAnyEnabledAttrOf<T1>(D, QT))
    return Out;
  return hasAnyEnabledAttrOf<T2, Others...>(D, QT);
}

const RetainSummary *
RetainSummaryManager::getPersistentSummary(const RetainSummary &OldSumm) {
  // Unique "simple" summaries -- those without ArgEffects.
  if (OldSumm.isSimple()) {
    ::llvm::FoldingSetNodeID ID;
    OldSumm.Profile(ID);

    void *Pos;
    CachedSummaryNode *N = SimpleSummaries.FindNodeOrInsertPos(ID, Pos);

    if (!N) {
      N = (CachedSummaryNode *) BPAlloc.Allocate<CachedSummaryNode>();
      new (N) CachedSummaryNode(OldSumm);
      SimpleSummaries.InsertNode(N, Pos);
    }

    return &N->getValue();
  }

  RetainSummary *Summ = (RetainSummary *) BPAlloc.Allocate<RetainSummary>();
  new (Summ) RetainSummary(OldSumm);
  return Summ;
}

static bool isSubclass(const Decl *D,
                       StringRef ClassName) {
  using namespace ast_matchers;
  DeclarationMatcher SubclassM =
      cxxRecordDecl(isSameOrDerivedFrom(std::string(ClassName)));
  return !(match(SubclassM, *D, D->getASTContext()).empty());
}

static bool isExactClass(const Decl *D, StringRef ClassName) {
  using namespace ast_matchers;
  DeclarationMatcher sameClassM =
      cxxRecordDecl(hasName(std::string(ClassName)));
  return !(match(sameClassM, *D, D->getASTContext()).empty());
}

static bool isOSObjectSubclass(const Decl *D) {
  return D && isSubclass(D, "OSMetaClassBase") &&
         !isExactClass(D, "OSMetaClass");
}

static bool isOSObjectDynamicCast(StringRef S) { return S == "safeMetaCast"; }

static bool isOSObjectRequiredCast(StringRef S) {
  return S == "requiredMetaCast";
}

static bool isOSObjectThisCast(StringRef S) {
  return S == "metaCast";
}


static bool isOSObjectPtr(QualType QT) {
  return isOSObjectSubclass(QT->getPointeeCXXRecordDecl());
}

static bool isISLObjectRef(QualType Ty) {
  return StringRef(Ty.getAsString()).starts_with("isl_");
}

static bool isOSIteratorSubclass(const Decl *D) {
  return isSubclass(D, "OSIterator");
}

static bool hasRCAnnotation(const Decl *D, StringRef rcAnnotation) {
  for (const auto *Ann : D->specific_attrs<AnnotateAttr>()) {
    if (Ann->getAnnotation() == rcAnnotation)
      return true;
  }
  return false;
}

static bool isRetain(const FunctionDecl *FD, StringRef FName) {
  return FName.starts_with_insensitive("retain") ||
         FName.ends_with_insensitive("retain");
}

static bool isRelease(const FunctionDecl *FD, StringRef FName) {
  return FName.starts_with_insensitive("release") ||
         FName.ends_with_insensitive("release");
}

static bool isAutorelease(const FunctionDecl *FD, StringRef FName) {
  return FName.starts_with_insensitive("autorelease") ||
         FName.ends_with_insensitive("autorelease");
}

static bool isMakeCollectable(StringRef FName) {
  return FName.contains_insensitive("MakeCollectable");
}

/// A function is OSObject related if it is declared on a subclass
/// of OSObject, or any of the parameters is a subclass of an OSObject.
static bool isOSObjectRelated(const CXXMethodDecl *MD) {
  if (isOSObjectSubclass(MD->getParent()))
    return true;

  for (ParmVarDecl *Param : MD->parameters()) {
    QualType PT = Param->getType()->getPointeeType();
    if (!PT.isNull())
      if (CXXRecordDecl *RD = PT->getAsCXXRecordDecl())
        if (isOSObjectSubclass(RD))
          return true;
  }

  return false;
}

bool
RetainSummaryManager::isKnownSmartPointer(QualType QT) {
  QT = QT.getCanonicalType();
  const auto *RD = QT->getAsCXXRecordDecl();
  if (!RD)
    return false;
  const IdentifierInfo *II = RD->getIdentifier();
  if (II && II->getName() == "smart_ptr")
    if (const auto *ND = dyn_cast<NamespaceDecl>(RD->getDeclContext()))
      if (ND->getNameAsString() == "os")
        return true;
  return false;
}

const RetainSummary *
RetainSummaryManager::getSummaryForOSObject(const FunctionDecl *FD,
                                            StringRef FName, QualType RetTy) {
  assert(TrackOSObjects &&
         "Requesting a summary for an OSObject but OSObjects are not tracked");

  if (RetTy->isPointerType()) {
    const CXXRecordDecl *PD = RetTy->getPointeeType()->getAsCXXRecordDecl();
    if (PD && isOSObjectSubclass(PD)) {
      if (isOSObjectDynamicCast(FName) || isOSObjectRequiredCast(FName) ||
          isOSObjectThisCast(FName))
        return getDefaultSummary();

      // TODO: Add support for the slightly common *Matching(table) idiom.
      // Cf. IOService::nameMatching() etc. - these function have an unusual
      // contract of returning at +0 or +1 depending on their last argument.
      if (FName.ends_with("Matching")) {
        return getPersistentStopSummary();
      }

      // All objects returned with functions *not* starting with 'get',
      // or iterators, are returned at +1.
      if ((!FName.starts_with("get") && !FName.starts_with("Get")) ||
          isOSIteratorSubclass(PD)) {
        return getOSSummaryCreateRule(FD);
      } else {
        return getOSSummaryGetRule(FD);
      }
    }
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
    const CXXRecordDecl *Parent = MD->getParent();
    if (Parent && isOSObjectSubclass(Parent)) {
      if (FName == "release" || FName == "taggedRelease")
        return getOSSummaryReleaseRule(FD);

      if (FName == "retain" || FName == "taggedRetain")
        return getOSSummaryRetainRule(FD);

      if (FName == "free")
        return getOSSummaryFreeRule(FD);

      if (MD->getOverloadedOperator() == OO_New)
        return getOSSummaryCreateRule(MD);
    }
  }

  return nullptr;
}

const RetainSummary *RetainSummaryManager::getSummaryForObjCOrCFObject(
    const FunctionDecl *FD,
    StringRef FName,
    QualType RetTy,
    const FunctionType *FT,
    bool &AllowAnnotations) {

  ArgEffects ScratchArgs(AF.getEmptyMap());

  std::string RetTyName = RetTy.getAsString();
  if (FName == "pthread_create" || FName == "pthread_setspecific") {
    // It's not uncommon to pass a tracked object into the thread
    // as 'void *arg', and then release it inside the thread.
    // FIXME: We could build a much more precise model for these functions.
    return getPersistentStopSummary();
  } else if(FName == "NSMakeCollectable") {
    // Handle: id NSMakeCollectable(CFTypeRef)
    AllowAnnotations = false;
    return RetTy->isObjCIdType() ? getUnarySummary(FT, DoNothing)
                                 : getPersistentStopSummary();
  } else if (FName == "CMBufferQueueDequeueAndRetain" ||
             FName == "CMBufferQueueDequeueIfDataReadyAndRetain") {
    // These API functions are known to NOT act as a CFRetain wrapper.
    // They simply make a new object owned by the caller.
    return getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF),
                                ScratchArgs,
                                ArgEffect(DoNothing),
                                ArgEffect(DoNothing));
  } else if (FName == "CFPlugInInstanceCreate") {
    return getPersistentSummary(RetEffect::MakeNoRet(), ScratchArgs);
  } else if (FName == "IORegistryEntrySearchCFProperty" ||
             (RetTyName == "CFMutableDictionaryRef" &&
              (FName == "IOBSDNameMatching" || FName == "IOServiceMatching" ||
               FName == "IOServiceNameMatching" ||
               FName == "IORegistryEntryIDMatching" ||
               FName == "IOOpenFirmwarePathMatching"))) {
    // Yes, these IOKit functions return CF objects.
    // They also violate the CF naming convention.
    return getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF), ScratchArgs,
                                ArgEffect(DoNothing), ArgEffect(DoNothing));
  } else if (FName == "IOServiceGetMatchingService" ||
             FName == "IOServiceGetMatchingServices") {
    // These IOKit functions accept CF objects as arguments.
    // They also consume them without an appropriate annotation.
    ScratchArgs = AF.add(ScratchArgs, 1, ArgEffect(DecRef, ObjKind::CF));
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                ArgEffect(DoNothing), ArgEffect(DoNothing));
  } else if (FName == "IOServiceAddNotification" ||
             FName == "IOServiceAddMatchingNotification") {
    // More IOKit functions suddenly accepting (and even more suddenly,
    // consuming) CF objects.
    ScratchArgs = AF.add(ScratchArgs, 2, ArgEffect(DecRef, ObjKind::CF));
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                ArgEffect(DoNothing), ArgEffect(DoNothing));
  } else if (FName == "CVPixelBufferCreateWithBytes") {
    // Eventually this can be improved by recognizing that the pixel
    // buffer passed to CVPixelBufferCreateWithBytes is released via
    // a callback and doing full IPA to make sure this is done correctly.
    // Note that it's passed as a 'void *', so it's hard to annotate.
    // FIXME: This function also has an out parameter that returns an
    // allocated object.
    ScratchArgs = AF.add(ScratchArgs, 7, ArgEffect(StopTracking));
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                ArgEffect(DoNothing), ArgEffect(DoNothing));
  } else if (FName == "CGBitmapContextCreateWithData") {
    // This is similar to the CVPixelBufferCreateWithBytes situation above.
    // Eventually this can be improved by recognizing that 'releaseInfo'
    // passed to CGBitmapContextCreateWithData is released via
    // a callback and doing full IPA to make sure this is done correctly.
    ScratchArgs = AF.add(ScratchArgs, 8, ArgEffect(ArgEffect(StopTracking)));
    return getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF), ScratchArgs,
                                ArgEffect(DoNothing), ArgEffect(DoNothing));
  } else if (FName == "CVPixelBufferCreateWithPlanarBytes") {
    // Same as CVPixelBufferCreateWithBytes, just more arguments.
    ScratchArgs = AF.add(ScratchArgs, 12, ArgEffect(StopTracking));
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                ArgEffect(DoNothing), ArgEffect(DoNothing));
  } else if (FName == "VTCompressionSessionEncodeFrame" ||
             FName == "VTCompressionSessionEncodeMultiImageFrame") {
    // The context argument passed to VTCompressionSessionEncodeFrame() et.al.
    // is passed to the callback specified when creating the session
    // (e.g. with VTCompressionSessionCreate()) which can release it.
    // To account for this possibility, conservatively stop tracking
    // the context.
    ScratchArgs = AF.add(ScratchArgs, 5, ArgEffect(StopTracking));
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                ArgEffect(DoNothing), ArgEffect(DoNothing));
  } else if (FName == "dispatch_set_context" ||
             FName == "xpc_connection_set_context") {
    // The analyzer currently doesn't have a good way to reason about
    // dispatch_set_finalizer_f() which typically cleans up the context.
    // If we pass a context object that is memory managed, stop tracking it.
    // Same with xpc_connection_set_finalizer_f().
    ScratchArgs = AF.add(ScratchArgs, 1, ArgEffect(StopTracking));
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                ArgEffect(DoNothing), ArgEffect(DoNothing));
  } else if (FName.starts_with("NSLog")) {
    return getDoNothingSummary();
  } else if (FName.starts_with("NS") && FName.contains("Insert")) {
    // Allowlist NSXXInsertXX, for example NSMapInsertIfAbsent, since they can
    // be deallocated by NSMapRemove.
    ScratchArgs = AF.add(ScratchArgs, 1, ArgEffect(StopTracking));
    ScratchArgs = AF.add(ScratchArgs, 2, ArgEffect(StopTracking));
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs, ArgEffect(DoNothing),
                                ArgEffect(DoNothing));
  }

  if (RetTy->isPointerType()) {

    // For CoreFoundation ('CF') types.
    if (cocoa::isRefType(RetTy, "CF", FName)) {
      if (isRetain(FD, FName)) {
        // CFRetain isn't supposed to be annotated. However, this may as
        // well be a user-made "safe" CFRetain function that is incorrectly
        // annotated as cf_returns_retained due to lack of better options.
        // We want to ignore such annotation.
        AllowAnnotations = false;

        return getUnarySummary(FT, IncRef);
      } else if (isAutorelease(FD, FName)) {
        // The headers use cf_consumed, but we can fully model CFAutorelease
        // ourselves.
        AllowAnnotations = false;

        return getUnarySummary(FT, Autorelease);
      } else if (isMakeCollectable(FName)) {
        AllowAnnotations = false;
        return getUnarySummary(FT, DoNothing);
      } else {
        return getCFCreateGetRuleSummary(FD);
      }
    }

    // For CoreGraphics ('CG') and CoreVideo ('CV') types.
    if (cocoa::isRefType(RetTy, "CG", FName) ||
        cocoa::isRefType(RetTy, "CV", FName)) {
      if (isRetain(FD, FName))
        return getUnarySummary(FT, IncRef);
      else
        return getCFCreateGetRuleSummary(FD);
    }

    // For all other CF-style types, use the Create/Get
    // rule for summaries but don't support Retain functions
    // with framework-specific prefixes.
    if (coreFoundation::isCFObjectRef(RetTy)) {
      return getCFCreateGetRuleSummary(FD);
    }

    if (FD->hasAttr<CFAuditedTransferAttr>()) {
      return getCFCreateGetRuleSummary(FD);
    }
  }

  // Check for release functions, the only kind of functions that we care
  // about that don't return a pointer type.
  if (FName.starts_with("CG") || FName.starts_with("CF")) {
    // Test for 'CGCF'.
    FName = FName.substr(FName.starts_with("CGCF") ? 4 : 2);

    if (isRelease(FD, FName))
      return getUnarySummary(FT, DecRef);
    else {
      assert(ScratchArgs.isEmpty());
      // Remaining CoreFoundation and CoreGraphics functions.
      // We use to assume that they all strictly followed the ownership idiom
      // and that ownership cannot be transferred.  While this is technically
      // correct, many methods allow a tracked object to escape.  For example:
      //
      //   CFMutableDictionaryRef x = CFDictionaryCreateMutable(...);
      //   CFDictionaryAddValue(y, key, x);
      //   CFRelease(x);
      //   ... it is okay to use 'x' since 'y' has a reference to it
      //
      // We handle this and similar cases with the follow heuristic.  If the
      // function name contains "InsertValue", "SetValue", "AddValue",
      // "AppendValue", or "SetAttribute", then we assume that arguments may
      // "escape."  This means that something else holds on to the object,
      // allowing it be used even after its local retain count drops to 0.
      ArgEffectKind E =
          (StrInStrNoCase(FName, "InsertValue") != StringRef::npos ||
           StrInStrNoCase(FName, "AddValue") != StringRef::npos ||
           StrInStrNoCase(FName, "SetValue") != StringRef::npos ||
           StrInStrNoCase(FName, "AppendValue") != StringRef::npos ||
           StrInStrNoCase(FName, "SetAttribute") != StringRef::npos)
              ? MayEscape
              : DoNothing;

      return getPersistentSummary(RetEffect::MakeNoRet(), ScratchArgs,
                                  ArgEffect(DoNothing), ArgEffect(E, ObjKind::CF));
    }
  }

  return nullptr;
}

const RetainSummary *
RetainSummaryManager::generateSummary(const FunctionDecl *FD,
                                      bool &AllowAnnotations) {
  // We generate "stop" summaries for implicitly defined functions.
  if (FD->isImplicit())
    return getPersistentStopSummary();

  const IdentifierInfo *II = FD->getIdentifier();

  StringRef FName = II ? II->getName() : "";

  // Strip away preceding '_'.  Doing this here will effect all the checks
  // down below.
  FName = FName.substr(FName.find_first_not_of('_'));

  // Inspect the result type. Strip away any typedefs.
  const auto *FT = FD->getType()->castAs<FunctionType>();
  QualType RetTy = FT->getReturnType();

  if (TrackOSObjects)
    if (const RetainSummary *S = getSummaryForOSObject(FD, FName, RetTy))
      return S;

  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD))
    if (!isOSObjectRelated(MD))
      return getPersistentSummary(RetEffect::MakeNoRet(),
                                  ArgEffects(AF.getEmptyMap()),
                                  ArgEffect(DoNothing),
                                  ArgEffect(StopTracking),
                                  ArgEffect(DoNothing));

  if (TrackObjCAndCFObjects)
    if (const RetainSummary *S =
            getSummaryForObjCOrCFObject(FD, FName, RetTy, FT, AllowAnnotations))
      return S;

  return getDefaultSummary();
}

const RetainSummary *
RetainSummaryManager::getFunctionSummary(const FunctionDecl *FD) {
  // If we don't know what function we're calling, use our default summary.
  if (!FD)
    return getDefaultSummary();

  // Look up a summary in our cache of FunctionDecls -> Summaries.
  FuncSummariesTy::iterator I = FuncSummaries.find(FD);
  if (I != FuncSummaries.end())
    return I->second;

  // No summary?  Generate one.
  bool AllowAnnotations = true;
  const RetainSummary *S = generateSummary(FD, AllowAnnotations);

  // Annotations override defaults.
  if (AllowAnnotations)
    updateSummaryFromAnnotations(S, FD);

  FuncSummaries[FD] = S;
  return S;
}

//===----------------------------------------------------------------------===//
// Summary creation for functions (largely uses of Core Foundation).
//===----------------------------------------------------------------------===//

static ArgEffect getStopTrackingHardEquivalent(ArgEffect E) {
  switch (E.getKind()) {
  case DoNothing:
  case Autorelease:
  case DecRefBridgedTransferred:
  case IncRef:
  case UnretainedOutParameter:
  case RetainedOutParameter:
  case RetainedOutParameterOnZero:
  case RetainedOutParameterOnNonZero:
  case MayEscape:
  case StopTracking:
  case StopTrackingHard:
    return E.withKind(StopTrackingHard);
  case DecRef:
  case DecRefAndStopTrackingHard:
    return E.withKind(DecRefAndStopTrackingHard);
  case Dealloc:
    return E.withKind(Dealloc);
  }

  llvm_unreachable("Unknown ArgEffect kind");
}

const RetainSummary *
RetainSummaryManager::updateSummaryForNonZeroCallbackArg(const RetainSummary *S,
                                                         AnyCall &C) {
  ArgEffect RecEffect = getStopTrackingHardEquivalent(S->getReceiverEffect());
  ArgEffect DefEffect = getStopTrackingHardEquivalent(S->getDefaultArgEffect());

  ArgEffects ScratchArgs(AF.getEmptyMap());
  ArgEffects CustomArgEffects = S->getArgEffects();
  for (ArgEffects::iterator I = CustomArgEffects.begin(),
                            E = CustomArgEffects.end();
       I != E; ++I) {
    ArgEffect Translated = getStopTrackingHardEquivalent(I->second);
    if (Translated.getKind() != DefEffect.getKind())
      ScratchArgs = AF.add(ScratchArgs, I->first, Translated);
  }

  RetEffect RE = RetEffect::MakeNoRetHard();

  // Special cases where the callback argument CANNOT free the return value.
  // This can generally only happen if we know that the callback will only be
  // called when the return value is already being deallocated.
  if (const IdentifierInfo *Name = C.getIdentifier()) {
    // When the CGBitmapContext is deallocated, the callback here will free
    // the associated data buffer.
    // The callback in dispatch_data_create frees the buffer, but not
    // the data object.
    if (Name->isStr("CGBitmapContextCreateWithData") ||
        Name->isStr("dispatch_data_create"))
      RE = S->getRetEffect();
  }

  return getPersistentSummary(RE, ScratchArgs, RecEffect, DefEffect);
}

void RetainSummaryManager::updateSummaryForReceiverUnconsumedSelf(
    const RetainSummary *&S) {

  RetainSummaryTemplate Template(S, *this);

  Template->setReceiverEffect(ArgEffect(DoNothing));
  Template->setRetEffect(RetEffect::MakeNoRet());
}


void RetainSummaryManager::updateSummaryForArgumentTypes(
  const AnyCall &C, const RetainSummary *&RS) {
  RetainSummaryTemplate Template(RS, *this);

  unsigned parm_idx = 0;
  for (auto pi = C.param_begin(), pe = C.param_end(); pi != pe;
       ++pi, ++parm_idx) {
    QualType QT = (*pi)->getType();

    // Skip already created values.
    if (RS->getArgEffects().contains(parm_idx))
      continue;

    ObjKind K = ObjKind::AnyObj;

    if (isISLObjectRef(QT)) {
      K = ObjKind::Generalized;
    } else if (isOSObjectPtr(QT)) {
      K = ObjKind::OS;
    } else if (cocoa::isCocoaObjectRef(QT)) {
      K = ObjKind::ObjC;
    } else if (coreFoundation::isCFObjectRef(QT)) {
      K = ObjKind::CF;
    }

    if (K != ObjKind::AnyObj)
      Template->addArg(AF, parm_idx,
                       ArgEffect(RS->getDefaultArgEffect().getKind(), K));
  }
}

const RetainSummary *
RetainSummaryManager::getSummary(AnyCall C,
                                 bool HasNonZeroCallbackArg,
                                 bool IsReceiverUnconsumedSelf,
                                 QualType ReceiverType) {
  const RetainSummary *Summ;
  switch (C.getKind()) {
  case AnyCall::Function:
  case AnyCall::Constructor:
  case AnyCall::InheritedConstructor:
  case AnyCall::Allocator:
  case AnyCall::Deallocator:
    Summ = getFunctionSummary(cast_or_null<FunctionDecl>(C.getDecl()));
    break;
  case AnyCall::Block:
  case AnyCall::Destructor:
    // FIXME: These calls are currently unsupported.
    return getPersistentStopSummary();
  case AnyCall::ObjCMethod: {
    const auto *ME = cast_or_null<ObjCMessageExpr>(C.getExpr());
    if (!ME) {
      Summ = getMethodSummary(cast<ObjCMethodDecl>(C.getDecl()));
    } else if (ME->isInstanceMessage()) {
      Summ = getInstanceMethodSummary(ME, ReceiverType);
    } else {
      Summ = getClassMethodSummary(ME);
    }
    break;
  }
  }

  if (HasNonZeroCallbackArg)
    Summ = updateSummaryForNonZeroCallbackArg(Summ, C);

  if (IsReceiverUnconsumedSelf)
    updateSummaryForReceiverUnconsumedSelf(Summ);

  updateSummaryForArgumentTypes(C, Summ);

  assert(Summ && "Unknown call type?");
  return Summ;
}


const RetainSummary *
RetainSummaryManager::getCFCreateGetRuleSummary(const FunctionDecl *FD) {
  if (coreFoundation::followsCreateRule(FD))
    return getCFSummaryCreateRule(FD);

  return getCFSummaryGetRule(FD);
}

bool RetainSummaryManager::isTrustedReferenceCountImplementation(
    const Decl *FD) {
  return hasRCAnnotation(FD, "rc_ownership_trusted_implementation");
}

std::optional<RetainSummaryManager::BehaviorSummary>
RetainSummaryManager::canEval(const CallExpr *CE, const FunctionDecl *FD,
                              bool &hasTrustedImplementationAnnotation) {

  IdentifierInfo *II = FD->getIdentifier();
  if (!II)
    return std::nullopt;

  StringRef FName = II->getName();
  FName = FName.substr(FName.find_first_not_of('_'));

  QualType ResultTy = CE->getCallReturnType(Ctx);
  if (ResultTy->isObjCIdType()) {
    if (II->isStr("NSMakeCollectable"))
      return BehaviorSummary::Identity;
  } else if (ResultTy->isPointerType()) {
    // Handle: (CF|CG|CV)Retain
    //         CFAutorelease
    // It's okay to be a little sloppy here.
    if (FName == "CMBufferQueueDequeueAndRetain" ||
        FName == "CMBufferQueueDequeueIfDataReadyAndRetain") {
      // These API functions are known to NOT act as a CFRetain wrapper.
      // They simply make a new object owned by the caller.
      return std::nullopt;
    }
    if (CE->getNumArgs() == 1 &&
        (cocoa::isRefType(ResultTy, "CF", FName) ||
         cocoa::isRefType(ResultTy, "CG", FName) ||
         cocoa::isRefType(ResultTy, "CV", FName)) &&
        (isRetain(FD, FName) || isAutorelease(FD, FName) ||
         isMakeCollectable(FName)))
      return BehaviorSummary::Identity;

    // safeMetaCast is called by OSDynamicCast.
    // We assume that OSDynamicCast is either an identity (cast is OK,
    // the input was non-zero),
    // or that it returns zero (when the cast failed, or the input
    // was zero).
    if (TrackOSObjects) {
      if (isOSObjectDynamicCast(FName) && FD->param_size() >= 1) {
        return BehaviorSummary::IdentityOrZero;
      } else if (isOSObjectRequiredCast(FName) && FD->param_size() >= 1) {
        return BehaviorSummary::Identity;
      } else if (isOSObjectThisCast(FName) && isa<CXXMethodDecl>(FD) &&
                 !cast<CXXMethodDecl>(FD)->isStatic()) {
        return BehaviorSummary::IdentityThis;
      }
    }

    const FunctionDecl* FDD = FD->getDefinition();
    if (FDD && isTrustedReferenceCountImplementation(FDD)) {
      hasTrustedImplementationAnnotation = true;
      return BehaviorSummary::Identity;
    }
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
    const CXXRecordDecl *Parent = MD->getParent();
    if (TrackOSObjects && Parent && isOSObjectSubclass(Parent))
      if (FName == "release" || FName == "retain")
        return BehaviorSummary::NoOp;
  }

  return std::nullopt;
}

const RetainSummary *
RetainSummaryManager::getUnarySummary(const FunctionType* FT,
                                      ArgEffectKind AE) {

  // Unary functions have no arg effects by definition.
  ArgEffects ScratchArgs(AF.getEmptyMap());

  // Verify that this is *really* a unary function.  This can
  // happen if people do weird things.
  const FunctionProtoType* FTP = dyn_cast<FunctionProtoType>(FT);
  if (!FTP || FTP->getNumParams() != 1)
    return getPersistentStopSummary();

  ArgEffect Effect(AE, ObjKind::CF);

  ScratchArgs = AF.add(ScratchArgs, 0, Effect);
  return getPersistentSummary(RetEffect::MakeNoRet(),
                              ScratchArgs,
                              ArgEffect(DoNothing), ArgEffect(DoNothing));
}

const RetainSummary *
RetainSummaryManager::getOSSummaryRetainRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNoRet(),
                              AF.getEmptyMap(),
                              /*ReceiverEff=*/ArgEffect(DoNothing),
                              /*DefaultEff=*/ArgEffect(DoNothing),
                              /*ThisEff=*/ArgEffect(IncRef, ObjKind::OS));
}

const RetainSummary *
RetainSummaryManager::getOSSummaryReleaseRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNoRet(),
                              AF.getEmptyMap(),
                              /*ReceiverEff=*/ArgEffect(DoNothing),
                              /*DefaultEff=*/ArgEffect(DoNothing),
                              /*ThisEff=*/ArgEffect(DecRef, ObjKind::OS));
}

const RetainSummary *
RetainSummaryManager::getOSSummaryFreeRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNoRet(),
                              AF.getEmptyMap(),
                              /*ReceiverEff=*/ArgEffect(DoNothing),
                              /*DefaultEff=*/ArgEffect(DoNothing),
                              /*ThisEff=*/ArgEffect(Dealloc, ObjKind::OS));
}

const RetainSummary *
RetainSummaryManager::getOSSummaryCreateRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeOwned(ObjKind::OS),
                              AF.getEmptyMap());
}

const RetainSummary *
RetainSummaryManager::getOSSummaryGetRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNotOwned(ObjKind::OS),
                              AF.getEmptyMap());
}

const RetainSummary *
RetainSummaryManager::getCFSummaryCreateRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF),
                              ArgEffects(AF.getEmptyMap()));
}

const RetainSummary *
RetainSummaryManager::getCFSummaryGetRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNotOwned(ObjKind::CF),
                              ArgEffects(AF.getEmptyMap()),
                              ArgEffect(DoNothing), ArgEffect(DoNothing));
}




//===----------------------------------------------------------------------===//
// Summary creation for Selectors.
//===----------------------------------------------------------------------===//

std::optional<RetEffect>
RetainSummaryManager::getRetEffectFromAnnotations(QualType RetTy,
                                                  const Decl *D) {
  if (hasAnyEnabledAttrOf<NSReturnsRetainedAttr>(D, RetTy))
    return ObjCAllocRetE;

  if (auto K = hasAnyEnabledAttrOf<CFReturnsRetainedAttr, OSReturnsRetainedAttr,
                                   GeneralizedReturnsRetainedAttr>(D, RetTy))
    return RetEffect::MakeOwned(*K);

  if (auto K = hasAnyEnabledAttrOf<
          CFReturnsNotRetainedAttr, OSReturnsNotRetainedAttr,
          GeneralizedReturnsNotRetainedAttr, NSReturnsNotRetainedAttr,
          NSReturnsAutoreleasedAttr>(D, RetTy))
    return RetEffect::MakeNotOwned(*K);

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D))
    for (const auto *PD : MD->overridden_methods())
      if (auto RE = getRetEffectFromAnnotations(RetTy, PD))
        return RE;

  return std::nullopt;
}

/// \return Whether the chain of typedefs starting from @c QT
/// has a typedef with a given name @c Name.
static bool hasTypedefNamed(QualType QT,
                            StringRef Name) {
  while (auto *T = QT->getAs<TypedefType>()) {
    const auto &Context = T->getDecl()->getASTContext();
    if (T->getDecl()->getIdentifier() == &Context.Idents.get(Name))
      return true;
    QT = T->getDecl()->getUnderlyingType();
  }
  return false;
}

static QualType getCallableReturnType(const NamedDecl *ND) {
  if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
    return FD->getReturnType();
  } else if (const auto *MD = dyn_cast<ObjCMethodDecl>(ND)) {
    return MD->getReturnType();
  } else {
    llvm_unreachable("Unexpected decl");
  }
}

bool RetainSummaryManager::applyParamAnnotationEffect(
    const ParmVarDecl *pd, unsigned parm_idx, const NamedDecl *FD,
    RetainSummaryTemplate &Template) {
  QualType QT = pd->getType();
  if (auto K =
          hasAnyEnabledAttrOf<NSConsumedAttr, CFConsumedAttr, OSConsumedAttr,
                              GeneralizedConsumedAttr>(pd, QT)) {
    Template->addArg(AF, parm_idx, ArgEffect(DecRef, *K));
    return true;
  } else if (auto K = hasAnyEnabledAttrOf<
                 CFReturnsRetainedAttr, OSReturnsRetainedAttr,
                 OSReturnsRetainedOnNonZeroAttr, OSReturnsRetainedOnZeroAttr,
                 GeneralizedReturnsRetainedAttr>(pd, QT)) {

    // For OSObjects, we try to guess whether the object is created based
    // on the return value.
    if (K == ObjKind::OS) {
      QualType QT = getCallableReturnType(FD);

      bool HasRetainedOnZero = pd->hasAttr<OSReturnsRetainedOnZeroAttr>();
      bool HasRetainedOnNonZero = pd->hasAttr<OSReturnsRetainedOnNonZeroAttr>();

      // The usual convention is to create an object on non-zero return, but
      // it's reverted if the typedef chain has a typedef kern_return_t,
      // because kReturnSuccess constant is defined as zero.
      // The convention can be overwritten by custom attributes.
      bool SuccessOnZero =
          HasRetainedOnZero ||
          (hasTypedefNamed(QT, "kern_return_t") && !HasRetainedOnNonZero);
      bool ShouldSplit = !QT.isNull() && !QT->isVoidType();
      ArgEffectKind AK = RetainedOutParameter;
      if (ShouldSplit && SuccessOnZero) {
        AK = RetainedOutParameterOnZero;
      } else if (ShouldSplit && (!SuccessOnZero || HasRetainedOnNonZero)) {
        AK = RetainedOutParameterOnNonZero;
      }
      Template->addArg(AF, parm_idx, ArgEffect(AK, ObjKind::OS));
    }

    // For others:
    // Do nothing. Retained out parameters will either point to a +1 reference
    // or NULL, but the way you check for failure differs depending on the
    // API. Consequently, we don't have a good way to track them yet.
    return true;
  } else if (auto K = hasAnyEnabledAttrOf<CFReturnsNotRetainedAttr,
                                          OSReturnsNotRetainedAttr,
                                          GeneralizedReturnsNotRetainedAttr>(
                 pd, QT)) {
    Template->addArg(AF, parm_idx, ArgEffect(UnretainedOutParameter, *K));
    return true;
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
    for (const auto *OD : MD->overridden_methods()) {
      const ParmVarDecl *OP = OD->parameters()[parm_idx];
      if (applyParamAnnotationEffect(OP, parm_idx, OD, Template))
        return true;
    }
  }

  return false;
}

void
RetainSummaryManager::updateSummaryFromAnnotations(const RetainSummary *&Summ,
                                                   const FunctionDecl *FD) {
  if (!FD)
    return;

  assert(Summ && "Must have a summary to add annotations to.");
  RetainSummaryTemplate Template(Summ, *this);

  // Effects on the parameters.
  unsigned parm_idx = 0;
  for (auto pi = FD->param_begin(),
         pe = FD->param_end(); pi != pe; ++pi, ++parm_idx)
    applyParamAnnotationEffect(*pi, parm_idx, FD, Template);

  QualType RetTy = FD->getReturnType();
  if (std::optional<RetEffect> RetE = getRetEffectFromAnnotations(RetTy, FD))
    Template->setRetEffect(*RetE);

  if (hasAnyEnabledAttrOf<OSConsumesThisAttr>(FD, RetTy))
    Template->setThisEffect(ArgEffect(DecRef, ObjKind::OS));
}

void
RetainSummaryManager::updateSummaryFromAnnotations(const RetainSummary *&Summ,
                                                   const ObjCMethodDecl *MD) {
  if (!MD)
    return;

  assert(Summ && "Must have a valid summary to add annotations to");
  RetainSummaryTemplate Template(Summ, *this);

  // Effects on the receiver.
  if (hasAnyEnabledAttrOf<NSConsumesSelfAttr>(MD, MD->getReturnType()))
    Template->setReceiverEffect(ArgEffect(DecRef, ObjKind::ObjC));

  // Effects on the parameters.
  unsigned parm_idx = 0;
  for (auto pi = MD->param_begin(), pe = MD->param_end(); pi != pe;
       ++pi, ++parm_idx)
    applyParamAnnotationEffect(*pi, parm_idx, MD, Template);

  QualType RetTy = MD->getReturnType();
  if (std::optional<RetEffect> RetE = getRetEffectFromAnnotations(RetTy, MD))
    Template->setRetEffect(*RetE);
}

const RetainSummary *
RetainSummaryManager::getStandardMethodSummary(const ObjCMethodDecl *MD,
                                               Selector S, QualType RetTy) {
  // Any special effects?
  ArgEffect ReceiverEff = ArgEffect(DoNothing, ObjKind::ObjC);
  RetEffect ResultEff = RetEffect::MakeNoRet();

  // Check the method family, and apply any default annotations.
  switch (MD ? MD->getMethodFamily() : S.getMethodFamily()) {
    case OMF_None:
    case OMF_initialize:
    case OMF_performSelector:
      // Assume all Objective-C methods follow Cocoa Memory Management rules.
      // FIXME: Does the non-threaded performSelector family really belong here?
      // The selector could be, say, @selector(copy).
      if (cocoa::isCocoaObjectRef(RetTy))
        ResultEff = RetEffect::MakeNotOwned(ObjKind::ObjC);
      else if (coreFoundation::isCFObjectRef(RetTy)) {
        // ObjCMethodDecl currently doesn't consider CF objects as valid return
        // values for alloc, new, copy, or mutableCopy, so we have to
        // double-check with the selector. This is ugly, but there aren't that
        // many Objective-C methods that return CF objects, right?
        if (MD) {
          switch (S.getMethodFamily()) {
          case OMF_alloc:
          case OMF_new:
          case OMF_copy:
          case OMF_mutableCopy:
            ResultEff = RetEffect::MakeOwned(ObjKind::CF);
            break;
          default:
            ResultEff = RetEffect::MakeNotOwned(ObjKind::CF);
            break;
          }
        } else {
          ResultEff = RetEffect::MakeNotOwned(ObjKind::CF);
        }
      }
      break;
    case OMF_init:
      ResultEff = ObjCInitRetE;
      ReceiverEff = ArgEffect(DecRef, ObjKind::ObjC);
      break;
    case OMF_alloc:
    case OMF_new:
    case OMF_copy:
    case OMF_mutableCopy:
      if (cocoa::isCocoaObjectRef(RetTy))
        ResultEff = ObjCAllocRetE;
      else if (coreFoundation::isCFObjectRef(RetTy))
        ResultEff = RetEffect::MakeOwned(ObjKind::CF);
      break;
    case OMF_autorelease:
      ReceiverEff = ArgEffect(Autorelease, ObjKind::ObjC);
      break;
    case OMF_retain:
      ReceiverEff = ArgEffect(IncRef, ObjKind::ObjC);
      break;
    case OMF_release:
      ReceiverEff = ArgEffect(DecRef, ObjKind::ObjC);
      break;
    case OMF_dealloc:
      ReceiverEff = ArgEffect(Dealloc, ObjKind::ObjC);
      break;
    case OMF_self:
      // -self is handled specially by the ExprEngine to propagate the receiver.
      break;
    case OMF_retainCount:
    case OMF_finalize:
      // These methods don't return objects.
      break;
  }

  // If one of the arguments in the selector has the keyword 'delegate' we
  // should stop tracking the reference count for the receiver.  This is
  // because the reference count is quite possibly handled by a delegate
  // method.
  if (S.isKeywordSelector()) {
    for (unsigned i = 0, e = S.getNumArgs(); i != e; ++i) {
      StringRef Slot = S.getNameForSlot(i);
      if (Slot.ends_with_insensitive("delegate")) {
        if (ResultEff == ObjCInitRetE)
          ResultEff = RetEffect::MakeNoRetHard();
        else
          ReceiverEff = ArgEffect(StopTrackingHard, ObjKind::ObjC);
      }
    }
  }

  if (ReceiverEff.getKind() == DoNothing &&
      ResultEff.getKind() == RetEffect::NoRet)
    return getDefaultSummary();

  return getPersistentSummary(ResultEff, ArgEffects(AF.getEmptyMap()),
                              ArgEffect(ReceiverEff), ArgEffect(MayEscape));
}

const RetainSummary *
RetainSummaryManager::getClassMethodSummary(const ObjCMessageExpr *ME) {
  assert(!ME->isInstanceMessage());
  const ObjCInterfaceDecl *Class = ME->getReceiverInterface();

  return getMethodSummary(ME->getSelector(), Class, ME->getMethodDecl(),
                          ME->getType(), ObjCClassMethodSummaries);
}

const RetainSummary *RetainSummaryManager::getInstanceMethodSummary(
    const ObjCMessageExpr *ME,
    QualType ReceiverType) {
  const ObjCInterfaceDecl *ReceiverClass = nullptr;

  // We do better tracking of the type of the object than the core ExprEngine.
  // See if we have its type in our private state.
  if (!ReceiverType.isNull())
    if (const auto *PT = ReceiverType->getAs<ObjCObjectPointerType>())
      ReceiverClass = PT->getInterfaceDecl();

  // If we don't know what kind of object this is, fall back to its static type.
  if (!ReceiverClass)
    ReceiverClass = ME->getReceiverInterface();

  // FIXME: The receiver could be a reference to a class, meaning that
  //  we should use the class method.
  // id x = [NSObject class];
  // [x performSelector:... withObject:... afterDelay:...];
  Selector S = ME->getSelector();
  const ObjCMethodDecl *Method = ME->getMethodDecl();
  if (!Method && ReceiverClass)
    Method = ReceiverClass->getInstanceMethod(S);

  return getMethodSummary(S, ReceiverClass, Method, ME->getType(),
                          ObjCMethodSummaries);
}

const RetainSummary *
RetainSummaryManager::getMethodSummary(Selector S,
                                       const ObjCInterfaceDecl *ID,
                                       const ObjCMethodDecl *MD, QualType RetTy,
                                       ObjCMethodSummariesTy &CachedSummaries) {

  // Objective-C method summaries are only applicable to ObjC and CF objects.
  if (!TrackObjCAndCFObjects)
    return getDefaultSummary();

  // Look up a summary in our summary cache.
  const RetainSummary *Summ = CachedSummaries.find(ID, S);

  if (!Summ) {
    Summ = getStandardMethodSummary(MD, S, RetTy);

    // Annotations override defaults.
    updateSummaryFromAnnotations(Summ, MD);

    // Memoize the summary.
    CachedSummaries[ObjCSummaryKey(ID, S)] = Summ;
  }

  return Summ;
}

void RetainSummaryManager::InitializeClassMethodSummaries() {
  ArgEffects ScratchArgs = AF.getEmptyMap();

  // Create the [NSAssertionHandler currentHander] summary.
  addClassMethSummary("NSAssertionHandler", "currentHandler",
                getPersistentSummary(RetEffect::MakeNotOwned(ObjKind::ObjC),
                                     ScratchArgs));

  // Create the [NSAutoreleasePool addObject:] summary.
  ScratchArgs = AF.add(ScratchArgs, 0, ArgEffect(Autorelease));
  addClassMethSummary("NSAutoreleasePool", "addObject",
                      getPersistentSummary(RetEffect::MakeNoRet(), ScratchArgs,
                                           ArgEffect(DoNothing),
                                           ArgEffect(Autorelease)));
}

void RetainSummaryManager::InitializeMethodSummaries() {

  ArgEffects ScratchArgs = AF.getEmptyMap();
  // Create the "init" selector.  It just acts as a pass-through for the
  // receiver.
  const RetainSummary *InitSumm = getPersistentSummary(
      ObjCInitRetE, ScratchArgs, ArgEffect(DecRef, ObjKind::ObjC));
  addNSObjectMethSummary(GetNullarySelector("init", Ctx), InitSumm);

  // awakeAfterUsingCoder: behaves basically like an 'init' method.  It
  // claims the receiver and returns a retained object.
  addNSObjectMethSummary(GetUnarySelector("awakeAfterUsingCoder", Ctx),
                         InitSumm);

  // The next methods are allocators.
  const RetainSummary *AllocSumm = getPersistentSummary(ObjCAllocRetE,
                                                        ScratchArgs);
  const RetainSummary *CFAllocSumm =
    getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF), ScratchArgs);

  // Create the "retain" selector.
  RetEffect NoRet = RetEffect::MakeNoRet();
  const RetainSummary *Summ = getPersistentSummary(
      NoRet, ScratchArgs, ArgEffect(IncRef, ObjKind::ObjC));
  addNSObjectMethSummary(GetNullarySelector("retain", Ctx), Summ);

  // Create the "release" selector.
  Summ = getPersistentSummary(NoRet, ScratchArgs,
                              ArgEffect(DecRef, ObjKind::ObjC));
  addNSObjectMethSummary(GetNullarySelector("release", Ctx), Summ);

  // Create the -dealloc summary.
  Summ = getPersistentSummary(NoRet, ScratchArgs, ArgEffect(Dealloc,
                                                            ObjKind::ObjC));
  addNSObjectMethSummary(GetNullarySelector("dealloc", Ctx), Summ);

  // Create the "autorelease" selector.
  Summ = getPersistentSummary(NoRet, ScratchArgs, ArgEffect(Autorelease,
                                                            ObjKind::ObjC));
  addNSObjectMethSummary(GetNullarySelector("autorelease", Ctx), Summ);

  // For NSWindow, allocated objects are (initially) self-owned.
  // FIXME: For now we opt for false negatives with NSWindow, as these objects
  //  self-own themselves.  However, they only do this once they are displayed.
  //  Thus, we need to track an NSWindow's display status.
  const RetainSummary *NoTrackYet =
      getPersistentSummary(RetEffect::MakeNoRet(), ScratchArgs,
                           ArgEffect(StopTracking), ArgEffect(StopTracking));

  addClassMethSummary("NSWindow", "alloc", NoTrackYet);

  // For NSPanel (which subclasses NSWindow), allocated objects are not
  //  self-owned.
  // FIXME: For now we don't track NSPanels. object for the same reason
  //   as for NSWindow objects.
  addClassMethSummary("NSPanel", "alloc", NoTrackYet);

  // For NSNull, objects returned by +null are singletons that ignore
  // retain/release semantics.  Just don't track them.
  addClassMethSummary("NSNull", "null", NoTrackYet);

  // Don't track allocated autorelease pools, as it is okay to prematurely
  // exit a method.
  addClassMethSummary("NSAutoreleasePool", "alloc", NoTrackYet);
  addClassMethSummary("NSAutoreleasePool", "allocWithZone", NoTrackYet, false);
  addClassMethSummary("NSAutoreleasePool", "new", NoTrackYet);

  // Create summaries QCRenderer/QCView -createSnapShotImageOfType:
  addInstMethSummary("QCRenderer", AllocSumm, "createSnapshotImageOfType");
  addInstMethSummary("QCView", AllocSumm, "createSnapshotImageOfType");

  // Create summaries for CIContext, 'createCGImage' and
  // 'createCGLayerWithSize'.  These objects are CF objects, and are not
  // automatically garbage collected.
  addInstMethSummary("CIContext", CFAllocSumm, "createCGImage", "fromRect");
  addInstMethSummary("CIContext", CFAllocSumm, "createCGImage", "fromRect",
                     "format", "colorSpace");
  addInstMethSummary("CIContext", CFAllocSumm, "createCGLayerWithSize", "info");
}

const RetainSummary *
RetainSummaryManager::getMethodSummary(const ObjCMethodDecl *MD) {
  const ObjCInterfaceDecl *ID = MD->getClassInterface();
  Selector S = MD->getSelector();
  QualType ResultTy = MD->getReturnType();

  ObjCMethodSummariesTy *CachedSummaries;
  if (MD->isInstanceMethod())
    CachedSummaries = &ObjCMethodSummaries;
  else
    CachedSummaries = &ObjCClassMethodSummaries;

  return getMethodSummary(S, ID, MD, ResultTy, *CachedSummaries);
}
