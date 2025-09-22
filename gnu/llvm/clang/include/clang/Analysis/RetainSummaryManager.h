//=== RetainSummaryManager.h - Summaries for reference counting ---*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines summaries implementation for retain counting, which
//  implements a reference count checker for Core Foundation and Cocoa
//  on (Mac OS X).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_RETAINSUMMARYMANAGER_H
#define LLVM_CLANG_ANALYSIS_RETAINSUMMARYMANAGER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableMap.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ParentMap.h"
#include "clang/Analysis/AnyCall.h"
#include "clang/Analysis/SelectorExtras.h"
#include "llvm/ADT/STLExtras.h"
#include <optional>

using namespace clang;

namespace clang {
namespace ento {

/// Determines the object kind of a tracked object.
enum class ObjKind {
  /// Indicates that the tracked object is a CF object.
  CF,

  /// Indicates that the tracked object is an Objective-C object.
  ObjC,

  /// Indicates that the tracked object could be a CF or Objective-C object.
  AnyObj,

  /// Indicates that the tracked object is a generalized object.
  Generalized,

  /// Indicates that the tracking object is a descendant of a
  /// referenced-counted OSObject, used in the Darwin kernel.
  OS
};

enum ArgEffectKind {
  /// There is no effect.
  DoNothing,

  /// The argument is treated as if an -autorelease message had been sent to
  /// the referenced object.
  Autorelease,

  /// The argument is treated as if the referenced object was deallocated.
  Dealloc,

  /// The argument has its reference count decreased by 1.
  DecRef,

  /// The argument has its reference count decreased by 1 to model
  /// a transferred bridge cast under ARC.
  DecRefBridgedTransferred,

  /// The argument has its reference count increased by 1.
  IncRef,

  /// The argument is a pointer to a retain-counted object; on exit, the new
  /// value of the pointer is a +0 value.
  UnretainedOutParameter,

  /// The argument is a pointer to a retain-counted object; on exit, the new
  /// value of the pointer is a +1 value.
  RetainedOutParameter,

  /// The argument is a pointer to a retain-counted object; on exit, the new
  /// value of the pointer is a +1 value iff the return code is zero.
  RetainedOutParameterOnZero,

  /// The argument is a pointer to a retain-counted object; on exit, the new
  /// value of the pointer is a +1 value iff the return code is non-zero.
  RetainedOutParameterOnNonZero,

  /// The argument is treated as potentially escaping, meaning that
  /// even when its reference count hits 0 it should be treated as still
  /// possibly being alive as someone else *may* be holding onto the object.
  MayEscape,

  /// All typestate tracking of the object ceases.  This is usually employed
  /// when the effect of the call is completely unknown.
  StopTracking,

  /// All typestate tracking of the object ceases.  Unlike StopTracking,
  /// this is also enforced when the method body is inlined.
  ///
  /// In some cases, we obtain a better summary for this checker
  /// by looking at the call site than by inlining the function.
  /// Signifies that we should stop tracking the symbol even if
  /// the function is inlined.
  StopTrackingHard,

  /// Performs the combined functionality of DecRef and StopTrackingHard.
  ///
  /// The models the effect that the called function decrements the reference
  /// count of the argument and all typestate tracking on that argument
  /// should cease.
  DecRefAndStopTrackingHard,
};

/// An ArgEffect summarizes the retain count behavior on an argument or receiver
/// to a function or method.
class ArgEffect {
  ArgEffectKind K;
  ObjKind O;
public:
  explicit ArgEffect(ArgEffectKind K = DoNothing, ObjKind O = ObjKind::AnyObj)
      : K(K), O(O) {}

  ArgEffectKind getKind() const { return K; }
  ObjKind getObjKind() const { return O; }

  ArgEffect withKind(ArgEffectKind NewK) {
    return ArgEffect(NewK, O);
  }

  bool operator==(const ArgEffect &Other) const {
    return K == Other.K && O == Other.O;
  }
};

/// RetEffect summarizes a call's retain/release behavior with respect
/// to its return value.
class RetEffect {
public:
  enum Kind {
    /// Indicates that no retain count information is tracked for
    /// the return value.
    NoRet,

    /// Indicates that the returned value is an owned (+1) symbol.
    OwnedSymbol,

    /// Indicates that the returned value is an object with retain count
    /// semantics but that it is not owned (+0).  This is the default
    /// for getters, etc.
    NotOwnedSymbol,

    /// Indicates that the return value is an owned object when the
    /// receiver is also a tracked object.
    OwnedWhenTrackedReceiver,

    // Treat this function as returning a non-tracked symbol even if
    // the function has been inlined. This is used where the call
    // site summary is more precise than the summary indirectly produced
    // by inlining the function
    NoRetHard
  };

private:
  Kind K;
  ObjKind O;

  RetEffect(Kind k, ObjKind o = ObjKind::AnyObj) : K(k), O(o) {}

public:
  Kind getKind() const { return K; }

  ObjKind getObjKind() const { return O; }

  bool isOwned() const {
    return K == OwnedSymbol || K == OwnedWhenTrackedReceiver;
  }

  bool notOwned() const {
    return K == NotOwnedSymbol;
  }

  bool operator==(const RetEffect &Other) const {
    return K == Other.K && O == Other.O;
  }

  static RetEffect MakeOwnedWhenTrackedReceiver() {
    return RetEffect(OwnedWhenTrackedReceiver, ObjKind::ObjC);
  }

  static RetEffect MakeOwned(ObjKind o) {
    return RetEffect(OwnedSymbol, o);
  }
  static RetEffect MakeNotOwned(ObjKind o) {
    return RetEffect(NotOwnedSymbol, o);
  }
  static RetEffect MakeNoRet() {
    return RetEffect(NoRet);
  }
  static RetEffect MakeNoRetHard() {
    return RetEffect(NoRetHard);
  }
};

/// A key identifying a summary.
class ObjCSummaryKey {
  IdentifierInfo* II;
  Selector S;
public:
  ObjCSummaryKey(IdentifierInfo* ii, Selector s)
    : II(ii), S(s) {}

  ObjCSummaryKey(const ObjCInterfaceDecl *d, Selector s)
    : II(d ? d->getIdentifier() : nullptr), S(s) {}

  ObjCSummaryKey(Selector s)
    : II(nullptr), S(s) {}

  IdentifierInfo *getIdentifier() const { return II; }
  Selector getSelector() const { return S; }
};

} // end namespace ento
} // end namespace clang

using namespace ento;

namespace llvm {

//===----------------------------------------------------------------------===//
// Adapters for FoldingSet.
//===----------------------------------------------------------------------===//
template <> struct FoldingSetTrait<ArgEffect> {
static inline void Profile(const ArgEffect X, FoldingSetNodeID &ID) {
  ID.AddInteger((unsigned) X.getKind());
  ID.AddInteger((unsigned) X.getObjKind());
}
};
template <> struct FoldingSetTrait<RetEffect> {
  static inline void Profile(const RetEffect &X, FoldingSetNodeID &ID) {
    ID.AddInteger((unsigned) X.getKind());
    ID.AddInteger((unsigned) X.getObjKind());
}
};

template <> struct DenseMapInfo<ObjCSummaryKey> {
  static inline ObjCSummaryKey getEmptyKey() {
    return ObjCSummaryKey(DenseMapInfo<IdentifierInfo*>::getEmptyKey(),
                          DenseMapInfo<Selector>::getEmptyKey());
  }

  static inline ObjCSummaryKey getTombstoneKey() {
    return ObjCSummaryKey(DenseMapInfo<IdentifierInfo*>::getTombstoneKey(),
                          DenseMapInfo<Selector>::getTombstoneKey());
  }

  static unsigned getHashValue(const ObjCSummaryKey &V) {
    typedef std::pair<IdentifierInfo*, Selector> PairTy;
    return DenseMapInfo<PairTy>::getHashValue(PairTy(V.getIdentifier(),
                                                     V.getSelector()));
  }

  static bool isEqual(const ObjCSummaryKey& LHS, const ObjCSummaryKey& RHS) {
    return LHS.getIdentifier() == RHS.getIdentifier() &&
           LHS.getSelector() == RHS.getSelector();
  }

};

} // end llvm namespace


namespace clang {
namespace ento {

/// ArgEffects summarizes the effects of a function/method call on all of
/// its arguments.
typedef llvm::ImmutableMap<unsigned, ArgEffect> ArgEffects;

/// Summary for a function with respect to ownership changes.
class RetainSummary {
  /// Args - a map of (index, ArgEffect) pairs, where index
  ///  specifies the argument (starting from 0).  This can be sparsely
  ///  populated; arguments with no entry in Args use 'DefaultArgEffect'.
  ArgEffects Args;

  /// DefaultArgEffect - The default ArgEffect to apply to arguments that
  ///  do not have an entry in Args.
  ArgEffect DefaultArgEffect;

  /// Receiver - If this summary applies to an Objective-C message expression,
  ///  this is the effect applied to the state of the receiver.
  ArgEffect Receiver;

  /// Effect on "this" pointer - applicable only to C++ method calls.
  ArgEffect This;

  /// Ret - The effect on the return value.  Used to indicate if the
  ///  function/method call returns a new tracked symbol.
  RetEffect Ret;

public:
  RetainSummary(ArgEffects A,
                RetEffect R,
                ArgEffect defaultEff,
                ArgEffect ReceiverEff,
                ArgEffect ThisEff)
    : Args(A), DefaultArgEffect(defaultEff), Receiver(ReceiverEff),
      This(ThisEff), Ret(R) {}

  /// getArg - Return the argument effect on the argument specified by
  ///  idx (starting from 0).
  ArgEffect getArg(unsigned idx) const {
    if (const ArgEffect *AE = Args.lookup(idx))
      return *AE;

    return DefaultArgEffect;
  }

  void addArg(ArgEffects::Factory &af, unsigned idx, ArgEffect e) {
    Args = af.add(Args, idx, e);
  }

  /// setDefaultArgEffect - Set the default argument effect.
  void setDefaultArgEffect(ArgEffect E) {
    DefaultArgEffect = E;
  }

  /// getRetEffect - Returns the effect on the return value of the call.
  RetEffect getRetEffect() const { return Ret; }

  /// setRetEffect - Set the effect of the return value of the call.
  void setRetEffect(RetEffect E) { Ret = E; }


  /// Sets the effect on the receiver of the message.
  void setReceiverEffect(ArgEffect e) { Receiver = e; }

  /// getReceiverEffect - Returns the effect on the receiver of the call.
  ///  This is only meaningful if the summary applies to an ObjCMessageExpr*.
  ArgEffect getReceiverEffect() const { return Receiver; }

  /// \return the effect on the "this" receiver of the method call.
  /// This is only meaningful if the summary applies to CXXMethodDecl*.
  ArgEffect getThisEffect() const { return This; }

  ArgEffect getDefaultEffect() const { return DefaultArgEffect; }

  /// Set the effect of the method on "this".
  void setThisEffect(ArgEffect e) { This = e; }

  bool isNoop() const {
    return Ret == RetEffect::MakeNoRet() && Receiver.getKind() == DoNothing
      && DefaultArgEffect.getKind() == MayEscape && This.getKind() == DoNothing
      && Args.isEmpty();
  }

  /// Test if two retain summaries are identical. Note that merely equivalent
  /// summaries are not necessarily identical (for example, if an explicit
  /// argument effect matches the default effect).
  bool operator==(const RetainSummary &Other) const {
    return Args == Other.Args && DefaultArgEffect == Other.DefaultArgEffect &&
           Receiver == Other.Receiver && This == Other.This && Ret == Other.Ret;
  }

  /// Profile this summary for inclusion in a FoldingSet.
  void Profile(llvm::FoldingSetNodeID& ID) const {
    ID.Add(Args);
    ID.Add(DefaultArgEffect);
    ID.Add(Receiver);
    ID.Add(This);
    ID.Add(Ret);
  }

  /// A retain summary is simple if it has no ArgEffects other than the default.
  bool isSimple() const {
    return Args.isEmpty();
  }

  ArgEffects getArgEffects() const { return Args; }

private:
  ArgEffect getDefaultArgEffect() const { return DefaultArgEffect; }

  friend class RetainSummaryManager;
};

class ObjCSummaryCache {
  typedef llvm::DenseMap<ObjCSummaryKey, const RetainSummary *> MapTy;
  MapTy M;
public:
  ObjCSummaryCache() {}

  const RetainSummary * find(const ObjCInterfaceDecl *D, Selector S) {
    // Do a lookup with the (D,S) pair.  If we find a match return
    // the iterator.
    ObjCSummaryKey K(D, S);
    MapTy::iterator I = M.find(K);

    if (I != M.end())
      return I->second;
    if (!D)
      return nullptr;

    // Walk the super chain.  If we find a hit with a parent, we'll end
    // up returning that summary.  We actually allow that key (null,S), as
    // we cache summaries for the null ObjCInterfaceDecl* to allow us to
    // generate initial summaries without having to worry about NSObject
    // being declared.
    // FIXME: We may change this at some point.
    for (ObjCInterfaceDecl *C=D->getSuperClass() ;; C=C->getSuperClass()) {
      if ((I = M.find(ObjCSummaryKey(C, S))) != M.end())
        break;

      if (!C)
        return nullptr;
    }

    // Cache the summary with original key to make the next lookup faster
    // and return the iterator.
    const RetainSummary *Summ = I->second;
    M[K] = Summ;
    return Summ;
  }

  const RetainSummary *find(IdentifierInfo* II, Selector S) {
    // FIXME: Class method lookup.  Right now we don't have a good way
    // of going between IdentifierInfo* and the class hierarchy.
    MapTy::iterator I = M.find(ObjCSummaryKey(II, S));

    if (I == M.end())
      I = M.find(ObjCSummaryKey(S));

    return I == M.end() ? nullptr : I->second;
  }

  const RetainSummary *& operator[](ObjCSummaryKey K) {
    return M[K];
  }

  const RetainSummary *& operator[](Selector S) {
    return M[ ObjCSummaryKey(S) ];
  }
};

class RetainSummaryTemplate;

class RetainSummaryManager {
  typedef llvm::DenseMap<const FunctionDecl*, const RetainSummary *>
          FuncSummariesTy;

  typedef ObjCSummaryCache ObjCMethodSummariesTy;

  typedef llvm::FoldingSetNodeWrapper<RetainSummary> CachedSummaryNode;

  /// Ctx - The ASTContext object for the analyzed ASTs.
  ASTContext &Ctx;

  /// Records whether or not the analyzed code runs in ARC mode.
  const bool ARCEnabled;

  /// Track Objective-C and CoreFoundation objects.
  const bool TrackObjCAndCFObjects;

  /// Track sublcasses of OSObject.
  const bool TrackOSObjects;

  /// FuncSummaries - A map from FunctionDecls to summaries.
  FuncSummariesTy FuncSummaries;

  /// ObjCClassMethodSummaries - A map from selectors (for instance methods)
  ///  to summaries.
  ObjCMethodSummariesTy ObjCClassMethodSummaries;

  /// ObjCMethodSummaries - A map from selectors to summaries.
  ObjCMethodSummariesTy ObjCMethodSummaries;

  /// BPAlloc - A BumpPtrAllocator used for allocating summaries, ArgEffects,
  ///  and all other data used by the checker.
  llvm::BumpPtrAllocator BPAlloc;

  /// AF - A factory for ArgEffects objects.
  ArgEffects::Factory AF;

  /// ObjCAllocRetE - Default return effect for methods returning Objective-C
  ///  objects.
  RetEffect ObjCAllocRetE;

  /// ObjCInitRetE - Default return effect for init methods returning
  ///   Objective-C objects.
  RetEffect ObjCInitRetE;

  /// SimpleSummaries - Used for uniquing summaries that don't have special
  /// effects.
  llvm::FoldingSet<CachedSummaryNode> SimpleSummaries;

  /// Create an OS object at +1.
  const RetainSummary *getOSSummaryCreateRule(const FunctionDecl *FD);

  /// Get an OS object at +0.
  const RetainSummary *getOSSummaryGetRule(const FunctionDecl *FD);

  /// Increment the reference count on OS object.
  const RetainSummary *getOSSummaryRetainRule(const FunctionDecl *FD);

  /// Decrement the reference count on OS object.
  const RetainSummary *getOSSummaryReleaseRule(const FunctionDecl *FD);

  /// Free the OS object.
  const RetainSummary *getOSSummaryFreeRule(const FunctionDecl *FD);

  const RetainSummary *getUnarySummary(const FunctionType* FT,
                                       ArgEffectKind AE);

  const RetainSummary *getCFSummaryCreateRule(const FunctionDecl *FD);
  const RetainSummary *getCFSummaryGetRule(const FunctionDecl *FD);
  const RetainSummary *getCFCreateGetRuleSummary(const FunctionDecl *FD);

  const RetainSummary *getPersistentSummary(const RetainSummary &OldSumm);

  const RetainSummary *
  getPersistentSummary(RetEffect RetEff, ArgEffects ScratchArgs,
                       ArgEffect ReceiverEff = ArgEffect(DoNothing),
                       ArgEffect DefaultEff = ArgEffect(MayEscape),
                       ArgEffect ThisEff = ArgEffect(DoNothing)) {
    RetainSummary Summ(ScratchArgs, RetEff, DefaultEff, ReceiverEff, ThisEff);
    return getPersistentSummary(Summ);
  }

  const RetainSummary *getDoNothingSummary() {
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ArgEffects(AF.getEmptyMap()),
                                ArgEffect(DoNothing), ArgEffect(DoNothing));
  }

  const RetainSummary *getDefaultSummary() {
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ArgEffects(AF.getEmptyMap()),
                                ArgEffect(DoNothing), ArgEffect(MayEscape));
  }

  const RetainSummary *getPersistentStopSummary() {
    return getPersistentSummary(
        RetEffect::MakeNoRet(), ArgEffects(AF.getEmptyMap()),
        ArgEffect(StopTracking), ArgEffect(StopTracking));
  }

  void InitializeClassMethodSummaries();
  void InitializeMethodSummaries();

  void addNSObjectClsMethSummary(Selector S, const RetainSummary *Summ) {
    ObjCClassMethodSummaries[S] = Summ;
  }

  void addNSObjectMethSummary(Selector S, const RetainSummary *Summ) {
    ObjCMethodSummaries[S] = Summ;
  }

  void addClassMethSummary(const char* Cls, const char* name,
                           const RetainSummary *Summ, bool isNullary = true) {
    IdentifierInfo* ClsII = &Ctx.Idents.get(Cls);
    Selector S = isNullary ? GetNullarySelector(name, Ctx)
                           : GetUnarySelector(name, Ctx);
    ObjCClassMethodSummaries[ObjCSummaryKey(ClsII, S)]  = Summ;
  }

  void addInstMethSummary(const char* Cls, const char* nullaryName,
                          const RetainSummary *Summ) {
    IdentifierInfo* ClsII = &Ctx.Idents.get(Cls);
    Selector S = GetNullarySelector(nullaryName, Ctx);
    ObjCMethodSummaries[ObjCSummaryKey(ClsII, S)]  = Summ;
  }

  template <typename... Keywords>
  void addMethodSummary(IdentifierInfo *ClsII, ObjCMethodSummariesTy &Summaries,
                        const RetainSummary *Summ, Keywords *... Kws) {
    Selector S = getKeywordSelector(Ctx, Kws...);
    Summaries[ObjCSummaryKey(ClsII, S)] = Summ;
  }

  template <typename... Keywords>
  void addInstMethSummary(const char *Cls, const RetainSummary *Summ,
                          Keywords *... Kws) {
    addMethodSummary(&Ctx.Idents.get(Cls), ObjCMethodSummaries, Summ, Kws...);
  }

  template <typename... Keywords>
  void addClsMethSummary(const char *Cls, const RetainSummary *Summ,
                         Keywords *... Kws) {
    addMethodSummary(&Ctx.Idents.get(Cls), ObjCClassMethodSummaries, Summ,
                     Kws...);
  }

  template <typename... Keywords>
  void addClsMethSummary(IdentifierInfo *II, const RetainSummary *Summ,
                         Keywords *... Kws) {
    addMethodSummary(II, ObjCClassMethodSummaries, Summ, Kws...);
  }

  const RetainSummary * generateSummary(const FunctionDecl *FD,
                                        bool &AllowAnnotations);

  /// Return a summary for OSObject, or nullptr if not found.
  const RetainSummary *getSummaryForOSObject(const FunctionDecl *FD,
                                             StringRef FName, QualType RetTy);

  /// Return a summary for Objective-C or CF object, or nullptr if not found.
  const RetainSummary *getSummaryForObjCOrCFObject(
    const FunctionDecl *FD,
    StringRef FName,
    QualType RetTy,
    const FunctionType *FT,
    bool &AllowAnnotations);

  /// Apply the annotation of @c pd in function @c FD
  /// to the resulting summary stored in out-parameter @c Template.
  /// \return whether an annotation was applied.
  bool applyParamAnnotationEffect(const ParmVarDecl *pd, unsigned parm_idx,
                                  const NamedDecl *FD,
                                  RetainSummaryTemplate &Template);

public:
  RetainSummaryManager(ASTContext &ctx, bool trackObjCAndCFObjects,
                       bool trackOSObjects)
      : Ctx(ctx), ARCEnabled((bool)Ctx.getLangOpts().ObjCAutoRefCount),
        TrackObjCAndCFObjects(trackObjCAndCFObjects),
        TrackOSObjects(trackOSObjects), AF(BPAlloc),
        ObjCAllocRetE(ARCEnabled ? RetEffect::MakeNotOwned(ObjKind::ObjC)
                                 : RetEffect::MakeOwned(ObjKind::ObjC)),
        ObjCInitRetE(ARCEnabled ? RetEffect::MakeNotOwned(ObjKind::ObjC)
                                : RetEffect::MakeOwnedWhenTrackedReceiver()) {
    InitializeClassMethodSummaries();
    InitializeMethodSummaries();
  }

  enum class BehaviorSummary {
    // Function does not return.
    NoOp,

    // Function returns the first argument.
    Identity,

    // Function returns "this" argument.
    IdentityThis,

    // Function either returns zero, or the input parameter.
    IdentityOrZero
  };

  std::optional<BehaviorSummary>
  canEval(const CallExpr *CE, const FunctionDecl *FD,
          bool &hasTrustedImplementationAnnotation);

  /// \return Whether the type corresponds to a known smart pointer
  /// implementation (that is, everything about it is inlineable).
  static bool isKnownSmartPointer(QualType QT);

  bool isTrustedReferenceCountImplementation(const Decl *FD);

  const RetainSummary *getSummary(AnyCall C,
                                  bool HasNonZeroCallbackArg=false,
                                  bool IsReceiverUnconsumedSelf=false,
                                  QualType ReceiverType={});

  RetEffect getObjAllocRetEffect() const { return ObjCAllocRetE; }

private:

  /// getMethodSummary - This version of getMethodSummary is used to query
  ///  the summary for the current method being analyzed.
  const RetainSummary *getMethodSummary(const ObjCMethodDecl *MD);

  const RetainSummary *getFunctionSummary(const FunctionDecl *FD);

  const RetainSummary *getMethodSummary(Selector S, const ObjCInterfaceDecl *ID,
                                        const ObjCMethodDecl *MD,
                                        QualType RetTy,
                                        ObjCMethodSummariesTy &CachedSummaries);

  const RetainSummary *
  getInstanceMethodSummary(const ObjCMessageExpr *ME, QualType ReceiverType);

  const RetainSummary *getClassMethodSummary(const ObjCMessageExpr *ME);

  const RetainSummary *getStandardMethodSummary(const ObjCMethodDecl *MD,
                                                Selector S, QualType RetTy);

  /// Determine if there is a special return effect for this function or method.
  std::optional<RetEffect> getRetEffectFromAnnotations(QualType RetTy,
                                                       const Decl *D);

  void updateSummaryFromAnnotations(const RetainSummary *&Summ,
                                    const ObjCMethodDecl *MD);

  void updateSummaryFromAnnotations(const RetainSummary *&Summ,
                                    const FunctionDecl *FD);

  const RetainSummary *updateSummaryForNonZeroCallbackArg(const RetainSummary *S,
                                                          AnyCall &C);

  /// Special case '[super init];' and '[self init];'
  ///
  /// Even though calling '[super init]' without assigning the result to self
  /// and checking if the parent returns 'nil' is a bad pattern, it is common.
  /// Additionally, our Self Init checker already warns about it. To avoid
  /// overwhelming the user with messages from both checkers, we model the case
  /// of '[super init]' in cases when it is not consumed by another expression
  /// as if the call preserves the value of 'self'; essentially, assuming it can
  /// never fail and return 'nil'.
  /// Note, we don't want to just stop tracking the value since we want the
  /// RetainCount checker to report leaks and use-after-free if SelfInit checker
  /// is turned off.
  void updateSummaryForReceiverUnconsumedSelf(const RetainSummary *&S);

  /// Set argument types for arguments which are not doing anything.
  void updateSummaryForArgumentTypes(const AnyCall &C, const RetainSummary *&RS);

  /// Determine whether a declaration @c D of correspondent type (return
  /// type for functions/methods) @c QT has any of the given attributes,
  /// provided they pass necessary validation checks AND tracking the given
  /// attribute is enabled.
  /// Returns the object kind corresponding to the present attribute, or
  /// std::nullopt, if none of the specified attributes are present.
  /// Crashes if passed an attribute which is not explicitly handled.
  template <class T>
  std::optional<ObjKind> hasAnyEnabledAttrOf(const Decl *D, QualType QT);

  template <class T1, class T2, class... Others>
  std::optional<ObjKind> hasAnyEnabledAttrOf(const Decl *D, QualType QT);

  friend class RetainSummaryTemplate;
};


// Used to avoid allocating long-term (BPAlloc'd) memory for default retain
// summaries. If a function or method looks like it has a default summary, but
// it has annotations, the annotations are added to the stack-based template
// and then copied into managed memory.
class RetainSummaryTemplate {
  RetainSummaryManager &Manager;
  const RetainSummary *&RealSummary;
  RetainSummary ScratchSummary;
  bool Accessed;
public:
  RetainSummaryTemplate(const RetainSummary *&real, RetainSummaryManager &mgr)
    : Manager(mgr), RealSummary(real), ScratchSummary(*real), Accessed(false) {}

  ~RetainSummaryTemplate() {
    if (Accessed)
      RealSummary = Manager.getPersistentSummary(ScratchSummary);
  }

  RetainSummary &operator*() {
    Accessed = true;
    return ScratchSummary;
  }

  RetainSummary *operator->() {
    Accessed = true;
    return &ScratchSummary;
  }
};

} // end namespace ento
} // end namespace clang

#endif
