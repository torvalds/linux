//===---- SemaAccess.cpp - C++ Access Control -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Sema routines for C++ access control semantics.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DependentDiagnostic.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/STLForwardCompat.h"

using namespace clang;
using namespace sema;

/// A copy of Sema's enum without AR_delayed.
enum AccessResult {
  AR_accessible,
  AR_inaccessible,
  AR_dependent
};

bool Sema::SetMemberAccessSpecifier(NamedDecl *MemberDecl,
                                    NamedDecl *PrevMemberDecl,
                                    AccessSpecifier LexicalAS) {
  if (!PrevMemberDecl) {
    // Use the lexical access specifier.
    MemberDecl->setAccess(LexicalAS);
    return false;
  }

  // C++ [class.access.spec]p3: When a member is redeclared its access
  // specifier must be same as its initial declaration.
  if (LexicalAS != AS_none && LexicalAS != PrevMemberDecl->getAccess()) {
    Diag(MemberDecl->getLocation(),
         diag::err_class_redeclared_with_different_access)
      << MemberDecl << LexicalAS;
    Diag(PrevMemberDecl->getLocation(), diag::note_previous_access_declaration)
      << PrevMemberDecl << PrevMemberDecl->getAccess();

    MemberDecl->setAccess(LexicalAS);
    return true;
  }

  MemberDecl->setAccess(PrevMemberDecl->getAccess());
  return false;
}

static CXXRecordDecl *FindDeclaringClass(NamedDecl *D) {
  DeclContext *DC = D->getDeclContext();

  // This can only happen at top: enum decls only "publish" their
  // immediate members.
  if (isa<EnumDecl>(DC))
    DC = cast<EnumDecl>(DC)->getDeclContext();

  CXXRecordDecl *DeclaringClass = cast<CXXRecordDecl>(DC);
  while (DeclaringClass->isAnonymousStructOrUnion())
    DeclaringClass = cast<CXXRecordDecl>(DeclaringClass->getDeclContext());
  return DeclaringClass;
}

namespace {
struct EffectiveContext {
  EffectiveContext() : Inner(nullptr), Dependent(false) {}

  explicit EffectiveContext(DeclContext *DC)
    : Inner(DC),
      Dependent(DC->isDependentContext()) {

    // An implicit deduction guide is semantically in the context enclosing the
    // class template, but for access purposes behaves like the constructor
    // from which it was produced.
    if (auto *DGD = dyn_cast<CXXDeductionGuideDecl>(DC)) {
      if (DGD->isImplicit()) {
        DC = DGD->getCorrespondingConstructor();
        if (!DC) {
          // The copy deduction candidate doesn't have a corresponding
          // constructor.
          DC = cast<DeclContext>(DGD->getDeducedTemplate()->getTemplatedDecl());
        }
      }
    }

    // C++11 [class.access.nest]p1:
    //   A nested class is a member and as such has the same access
    //   rights as any other member.
    // C++11 [class.access]p2:
    //   A member of a class can also access all the names to which
    //   the class has access.  A local class of a member function
    //   may access the same names that the member function itself
    //   may access.
    // This almost implies that the privileges of nesting are transitive.
    // Technically it says nothing about the local classes of non-member
    // functions (which can gain privileges through friendship), but we
    // take that as an oversight.
    while (true) {
      // We want to add canonical declarations to the EC lists for
      // simplicity of checking, but we need to walk up through the
      // actual current DC chain.  Otherwise, something like a local
      // extern or friend which happens to be the canonical
      // declaration will really mess us up.

      if (isa<CXXRecordDecl>(DC)) {
        CXXRecordDecl *Record = cast<CXXRecordDecl>(DC);
        Records.push_back(Record->getCanonicalDecl());
        DC = Record->getDeclContext();
      } else if (isa<FunctionDecl>(DC)) {
        FunctionDecl *Function = cast<FunctionDecl>(DC);
        Functions.push_back(Function->getCanonicalDecl());
        if (Function->getFriendObjectKind())
          DC = Function->getLexicalDeclContext();
        else
          DC = Function->getDeclContext();
      } else if (DC->isFileContext()) {
        break;
      } else {
        DC = DC->getParent();
      }
    }
  }

  bool isDependent() const { return Dependent; }

  bool includesClass(const CXXRecordDecl *R) const {
    R = R->getCanonicalDecl();
    return llvm::is_contained(Records, R);
  }

  /// Retrieves the innermost "useful" context.  Can be null if we're
  /// doing access-control without privileges.
  DeclContext *getInnerContext() const {
    return Inner;
  }

  typedef SmallVectorImpl<CXXRecordDecl*>::const_iterator record_iterator;

  DeclContext *Inner;
  SmallVector<FunctionDecl*, 4> Functions;
  SmallVector<CXXRecordDecl*, 4> Records;
  bool Dependent;
};

/// Like sema::AccessedEntity, but kindly lets us scribble all over
/// it.
struct AccessTarget : public AccessedEntity {
  AccessTarget(const AccessedEntity &Entity)
    : AccessedEntity(Entity) {
    initialize();
  }

  AccessTarget(ASTContext &Context,
               MemberNonce _,
               CXXRecordDecl *NamingClass,
               DeclAccessPair FoundDecl,
               QualType BaseObjectType)
    : AccessedEntity(Context.getDiagAllocator(), Member, NamingClass,
                     FoundDecl, BaseObjectType) {
    initialize();
  }

  AccessTarget(ASTContext &Context,
               BaseNonce _,
               CXXRecordDecl *BaseClass,
               CXXRecordDecl *DerivedClass,
               AccessSpecifier Access)
    : AccessedEntity(Context.getDiagAllocator(), Base, BaseClass, DerivedClass,
                     Access) {
    initialize();
  }

  bool isInstanceMember() const {
    return (isMemberAccess() && getTargetDecl()->isCXXInstanceMember());
  }

  bool hasInstanceContext() const {
    return HasInstanceContext;
  }

  class SavedInstanceContext {
  public:
    SavedInstanceContext(SavedInstanceContext &&S)
        : Target(S.Target), Has(S.Has) {
      S.Target = nullptr;
    }

    // The move assignment operator is defined as deleted pending further
    // motivation.
    SavedInstanceContext &operator=(SavedInstanceContext &&) = delete;

    // The copy constrcutor and copy assignment operator is defined as deleted
    // pending further motivation.
    SavedInstanceContext(const SavedInstanceContext &) = delete;
    SavedInstanceContext &operator=(const SavedInstanceContext &) = delete;

    ~SavedInstanceContext() {
      if (Target)
        Target->HasInstanceContext = Has;
    }

  private:
    friend struct AccessTarget;
    explicit SavedInstanceContext(AccessTarget &Target)
        : Target(&Target), Has(Target.HasInstanceContext) {}
    AccessTarget *Target;
    bool Has;
  };

  SavedInstanceContext saveInstanceContext() {
    return SavedInstanceContext(*this);
  }

  void suppressInstanceContext() {
    HasInstanceContext = false;
  }

  const CXXRecordDecl *resolveInstanceContext(Sema &S) const {
    assert(HasInstanceContext);
    if (CalculatedInstanceContext)
      return InstanceContext;

    CalculatedInstanceContext = true;
    DeclContext *IC = S.computeDeclContext(getBaseObjectType());
    InstanceContext = (IC ? cast<CXXRecordDecl>(IC)->getCanonicalDecl()
                          : nullptr);
    return InstanceContext;
  }

  const CXXRecordDecl *getDeclaringClass() const {
    return DeclaringClass;
  }

  /// The "effective" naming class is the canonical non-anonymous
  /// class containing the actual naming class.
  const CXXRecordDecl *getEffectiveNamingClass() const {
    const CXXRecordDecl *namingClass = getNamingClass();
    while (namingClass->isAnonymousStructOrUnion())
      namingClass = cast<CXXRecordDecl>(namingClass->getParent());
    return namingClass->getCanonicalDecl();
  }

private:
  void initialize() {
    HasInstanceContext = (isMemberAccess() &&
                          !getBaseObjectType().isNull() &&
                          getTargetDecl()->isCXXInstanceMember());
    CalculatedInstanceContext = false;
    InstanceContext = nullptr;

    if (isMemberAccess())
      DeclaringClass = FindDeclaringClass(getTargetDecl());
    else
      DeclaringClass = getBaseClass();
    DeclaringClass = DeclaringClass->getCanonicalDecl();
  }

  bool HasInstanceContext : 1;
  mutable bool CalculatedInstanceContext : 1;
  mutable const CXXRecordDecl *InstanceContext;
  const CXXRecordDecl *DeclaringClass;
};

}

/// Checks whether one class might instantiate to the other.
static bool MightInstantiateTo(const CXXRecordDecl *From,
                               const CXXRecordDecl *To) {
  // Declaration names are always preserved by instantiation.
  if (From->getDeclName() != To->getDeclName())
    return false;

  const DeclContext *FromDC = From->getDeclContext()->getPrimaryContext();
  const DeclContext *ToDC = To->getDeclContext()->getPrimaryContext();
  if (FromDC == ToDC) return true;
  if (FromDC->isFileContext() || ToDC->isFileContext()) return false;

  // Be conservative.
  return true;
}

/// Checks whether one class is derived from another, inclusively.
/// Properly indicates when it couldn't be determined due to
/// dependence.
///
/// This should probably be donated to AST or at least Sema.
static AccessResult IsDerivedFromInclusive(const CXXRecordDecl *Derived,
                                           const CXXRecordDecl *Target) {
  assert(Derived->getCanonicalDecl() == Derived);
  assert(Target->getCanonicalDecl() == Target);

  if (Derived == Target) return AR_accessible;

  bool CheckDependent = Derived->isDependentContext();
  if (CheckDependent && MightInstantiateTo(Derived, Target))
    return AR_dependent;

  AccessResult OnFailure = AR_inaccessible;
  SmallVector<const CXXRecordDecl*, 8> Queue; // actually a stack

  while (true) {
    if (Derived->isDependentContext() && !Derived->hasDefinition() &&
        !Derived->isLambda())
      return AR_dependent;

    for (const auto &I : Derived->bases()) {
      const CXXRecordDecl *RD;

      QualType T = I.getType();
      if (const RecordType *RT = T->getAs<RecordType>()) {
        RD = cast<CXXRecordDecl>(RT->getDecl());
      } else if (const InjectedClassNameType *IT
                   = T->getAs<InjectedClassNameType>()) {
        RD = IT->getDecl();
      } else {
        assert(T->isDependentType() && "non-dependent base wasn't a record?");
        OnFailure = AR_dependent;
        continue;
      }

      RD = RD->getCanonicalDecl();
      if (RD == Target) return AR_accessible;
      if (CheckDependent && MightInstantiateTo(RD, Target))
        OnFailure = AR_dependent;

      Queue.push_back(RD);
    }

    if (Queue.empty()) break;

    Derived = Queue.pop_back_val();
  }

  return OnFailure;
}


static bool MightInstantiateTo(Sema &S, DeclContext *Context,
                               DeclContext *Friend) {
  if (Friend == Context)
    return true;

  assert(!Friend->isDependentContext() &&
         "can't handle friends with dependent contexts here");

  if (!Context->isDependentContext())
    return false;

  if (Friend->isFileContext())
    return false;

  // TODO: this is very conservative
  return true;
}

// Asks whether the type in 'context' can ever instantiate to the type
// in 'friend'.
static bool MightInstantiateTo(Sema &S, CanQualType Context, CanQualType Friend) {
  if (Friend == Context)
    return true;

  if (!Friend->isDependentType() && !Context->isDependentType())
    return false;

  // TODO: this is very conservative.
  return true;
}

static bool MightInstantiateTo(Sema &S,
                               FunctionDecl *Context,
                               FunctionDecl *Friend) {
  if (Context->getDeclName() != Friend->getDeclName())
    return false;

  if (!MightInstantiateTo(S,
                          Context->getDeclContext(),
                          Friend->getDeclContext()))
    return false;

  CanQual<FunctionProtoType> FriendTy
    = S.Context.getCanonicalType(Friend->getType())
         ->getAs<FunctionProtoType>();
  CanQual<FunctionProtoType> ContextTy
    = S.Context.getCanonicalType(Context->getType())
         ->getAs<FunctionProtoType>();

  // There isn't any way that I know of to add qualifiers
  // during instantiation.
  if (FriendTy.getQualifiers() != ContextTy.getQualifiers())
    return false;

  if (FriendTy->getNumParams() != ContextTy->getNumParams())
    return false;

  if (!MightInstantiateTo(S, ContextTy->getReturnType(),
                          FriendTy->getReturnType()))
    return false;

  for (unsigned I = 0, E = FriendTy->getNumParams(); I != E; ++I)
    if (!MightInstantiateTo(S, ContextTy->getParamType(I),
                            FriendTy->getParamType(I)))
      return false;

  return true;
}

static bool MightInstantiateTo(Sema &S,
                               FunctionTemplateDecl *Context,
                               FunctionTemplateDecl *Friend) {
  return MightInstantiateTo(S,
                            Context->getTemplatedDecl(),
                            Friend->getTemplatedDecl());
}

static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  const CXXRecordDecl *Friend) {
  if (EC.includesClass(Friend))
    return AR_accessible;

  if (EC.isDependent()) {
    for (const CXXRecordDecl *Context : EC.Records) {
      if (MightInstantiateTo(Context, Friend))
        return AR_dependent;
    }
  }

  return AR_inaccessible;
}

static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  CanQualType Friend) {
  if (const RecordType *RT = Friend->getAs<RecordType>())
    return MatchesFriend(S, EC, cast<CXXRecordDecl>(RT->getDecl()));

  // TODO: we can do better than this
  if (Friend->isDependentType())
    return AR_dependent;

  return AR_inaccessible;
}

/// Determines whether the given friend class template matches
/// anything in the effective context.
static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  ClassTemplateDecl *Friend) {
  AccessResult OnFailure = AR_inaccessible;

  // Check whether the friend is the template of a class in the
  // context chain.
  for (SmallVectorImpl<CXXRecordDecl*>::const_iterator
         I = EC.Records.begin(), E = EC.Records.end(); I != E; ++I) {
    CXXRecordDecl *Record = *I;

    // Figure out whether the current class has a template:
    ClassTemplateDecl *CTD;

    // A specialization of the template...
    if (isa<ClassTemplateSpecializationDecl>(Record)) {
      CTD = cast<ClassTemplateSpecializationDecl>(Record)
        ->getSpecializedTemplate();

    // ... or the template pattern itself.
    } else {
      CTD = Record->getDescribedClassTemplate();
      if (!CTD) continue;
    }

    // It's a match.
    if (Friend == CTD->getCanonicalDecl())
      return AR_accessible;

    // If the context isn't dependent, it can't be a dependent match.
    if (!EC.isDependent())
      continue;

    // If the template names don't match, it can't be a dependent
    // match.
    if (CTD->getDeclName() != Friend->getDeclName())
      continue;

    // If the class's context can't instantiate to the friend's
    // context, it can't be a dependent match.
    if (!MightInstantiateTo(S, CTD->getDeclContext(),
                            Friend->getDeclContext()))
      continue;

    // Otherwise, it's a dependent match.
    OnFailure = AR_dependent;
  }

  return OnFailure;
}

/// Determines whether the given friend function matches anything in
/// the effective context.
static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  FunctionDecl *Friend) {
  AccessResult OnFailure = AR_inaccessible;

  for (SmallVectorImpl<FunctionDecl*>::const_iterator
         I = EC.Functions.begin(), E = EC.Functions.end(); I != E; ++I) {
    if (Friend == *I)
      return AR_accessible;

    if (EC.isDependent() && MightInstantiateTo(S, *I, Friend))
      OnFailure = AR_dependent;
  }

  return OnFailure;
}

/// Determines whether the given friend function template matches
/// anything in the effective context.
static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  FunctionTemplateDecl *Friend) {
  if (EC.Functions.empty()) return AR_inaccessible;

  AccessResult OnFailure = AR_inaccessible;

  for (SmallVectorImpl<FunctionDecl*>::const_iterator
         I = EC.Functions.begin(), E = EC.Functions.end(); I != E; ++I) {

    FunctionTemplateDecl *FTD = (*I)->getPrimaryTemplate();
    if (!FTD)
      FTD = (*I)->getDescribedFunctionTemplate();
    if (!FTD)
      continue;

    FTD = FTD->getCanonicalDecl();

    if (Friend == FTD)
      return AR_accessible;

    if (EC.isDependent() && MightInstantiateTo(S, FTD, Friend))
      OnFailure = AR_dependent;
  }

  return OnFailure;
}

/// Determines whether the given friend declaration matches anything
/// in the effective context.
static AccessResult MatchesFriend(Sema &S,
                                  const EffectiveContext &EC,
                                  FriendDecl *FriendD) {
  // Whitelist accesses if there's an invalid or unsupported friend
  // declaration.
  if (FriendD->isInvalidDecl() || FriendD->isUnsupportedFriend())
    return AR_accessible;

  if (TypeSourceInfo *T = FriendD->getFriendType())
    return MatchesFriend(S, EC, T->getType()->getCanonicalTypeUnqualified());

  NamedDecl *Friend
    = cast<NamedDecl>(FriendD->getFriendDecl()->getCanonicalDecl());

  // FIXME: declarations with dependent or templated scope.

  if (isa<ClassTemplateDecl>(Friend))
    return MatchesFriend(S, EC, cast<ClassTemplateDecl>(Friend));

  if (isa<FunctionTemplateDecl>(Friend))
    return MatchesFriend(S, EC, cast<FunctionTemplateDecl>(Friend));

  if (isa<CXXRecordDecl>(Friend))
    return MatchesFriend(S, EC, cast<CXXRecordDecl>(Friend));

  assert(isa<FunctionDecl>(Friend) && "unknown friend decl kind");
  return MatchesFriend(S, EC, cast<FunctionDecl>(Friend));
}

static AccessResult GetFriendKind(Sema &S,
                                  const EffectiveContext &EC,
                                  const CXXRecordDecl *Class) {
  AccessResult OnFailure = AR_inaccessible;

  // Okay, check friends.
  for (auto *Friend : Class->friends()) {
    switch (MatchesFriend(S, EC, Friend)) {
    case AR_accessible:
      return AR_accessible;

    case AR_inaccessible:
      continue;

    case AR_dependent:
      OnFailure = AR_dependent;
      break;
    }
  }

  // That's it, give up.
  return OnFailure;
}

namespace {

/// A helper class for checking for a friend which will grant access
/// to a protected instance member.
struct ProtectedFriendContext {
  Sema &S;
  const EffectiveContext &EC;
  const CXXRecordDecl *NamingClass;
  bool CheckDependent;
  bool EverDependent;

  /// The path down to the current base class.
  SmallVector<const CXXRecordDecl*, 20> CurPath;

  ProtectedFriendContext(Sema &S, const EffectiveContext &EC,
                         const CXXRecordDecl *InstanceContext,
                         const CXXRecordDecl *NamingClass)
    : S(S), EC(EC), NamingClass(NamingClass),
      CheckDependent(InstanceContext->isDependentContext() ||
                     NamingClass->isDependentContext()),
      EverDependent(false) {}

  /// Check classes in the current path for friendship, starting at
  /// the given index.
  bool checkFriendshipAlongPath(unsigned I) {
    assert(I < CurPath.size());
    for (unsigned E = CurPath.size(); I != E; ++I) {
      switch (GetFriendKind(S, EC, CurPath[I])) {
      case AR_accessible:   return true;
      case AR_inaccessible: continue;
      case AR_dependent:    EverDependent = true; continue;
      }
    }
    return false;
  }

  /// Perform a search starting at the given class.
  ///
  /// PrivateDepth is the index of the last (least derived) class
  /// along the current path such that a notional public member of
  /// the final class in the path would have access in that class.
  bool findFriendship(const CXXRecordDecl *Cur, unsigned PrivateDepth) {
    // If we ever reach the naming class, check the current path for
    // friendship.  We can also stop recursing because we obviously
    // won't find the naming class there again.
    if (Cur == NamingClass)
      return checkFriendshipAlongPath(PrivateDepth);

    if (CheckDependent && MightInstantiateTo(Cur, NamingClass))
      EverDependent = true;

    // Recurse into the base classes.
    for (const auto &I : Cur->bases()) {
      // If this is private inheritance, then a public member of the
      // base will not have any access in classes derived from Cur.
      unsigned BasePrivateDepth = PrivateDepth;
      if (I.getAccessSpecifier() == AS_private)
        BasePrivateDepth = CurPath.size() - 1;

      const CXXRecordDecl *RD;

      QualType T = I.getType();
      if (const RecordType *RT = T->getAs<RecordType>()) {
        RD = cast<CXXRecordDecl>(RT->getDecl());
      } else if (const InjectedClassNameType *IT
                   = T->getAs<InjectedClassNameType>()) {
        RD = IT->getDecl();
      } else {
        assert(T->isDependentType() && "non-dependent base wasn't a record?");
        EverDependent = true;
        continue;
      }

      // Recurse.  We don't need to clean up if this returns true.
      CurPath.push_back(RD);
      if (findFriendship(RD->getCanonicalDecl(), BasePrivateDepth))
        return true;
      CurPath.pop_back();
    }

    return false;
  }

  bool findFriendship(const CXXRecordDecl *Cur) {
    assert(CurPath.empty());
    CurPath.push_back(Cur);
    return findFriendship(Cur, 0);
  }
};
}

/// Search for a class P that EC is a friend of, under the constraint
///   InstanceContext <= P
/// if InstanceContext exists, or else
///   NamingClass <= P
/// and with the additional restriction that a protected member of
/// NamingClass would have some natural access in P, which implicitly
/// imposes the constraint that P <= NamingClass.
///
/// This isn't quite the condition laid out in the standard.
/// Instead of saying that a notional protected member of NamingClass
/// would have to have some natural access in P, it says the actual
/// target has to have some natural access in P, which opens up the
/// possibility that the target (which is not necessarily a member
/// of NamingClass) might be more accessible along some path not
/// passing through it.  That's really a bad idea, though, because it
/// introduces two problems:
///   - Most importantly, it breaks encapsulation because you can
///     access a forbidden base class's members by directly subclassing
///     it elsewhere.
///   - It also makes access substantially harder to compute because it
///     breaks the hill-climbing algorithm: knowing that the target is
///     accessible in some base class would no longer let you change
///     the question solely to whether the base class is accessible,
///     because the original target might have been more accessible
///     because of crazy subclassing.
/// So we don't implement that.
static AccessResult GetProtectedFriendKind(Sema &S, const EffectiveContext &EC,
                                           const CXXRecordDecl *InstanceContext,
                                           const CXXRecordDecl *NamingClass) {
  assert(InstanceContext == nullptr ||
         InstanceContext->getCanonicalDecl() == InstanceContext);
  assert(NamingClass->getCanonicalDecl() == NamingClass);

  // If we don't have an instance context, our constraints give us
  // that NamingClass <= P <= NamingClass, i.e. P == NamingClass.
  // This is just the usual friendship check.
  if (!InstanceContext) return GetFriendKind(S, EC, NamingClass);

  ProtectedFriendContext PRC(S, EC, InstanceContext, NamingClass);
  if (PRC.findFriendship(InstanceContext)) return AR_accessible;
  if (PRC.EverDependent) return AR_dependent;
  return AR_inaccessible;
}

static AccessResult HasAccess(Sema &S,
                              const EffectiveContext &EC,
                              const CXXRecordDecl *NamingClass,
                              AccessSpecifier Access,
                              const AccessTarget &Target) {
  assert(NamingClass->getCanonicalDecl() == NamingClass &&
         "declaration should be canonicalized before being passed here");

  if (Access == AS_public) return AR_accessible;
  assert(Access == AS_private || Access == AS_protected);

  AccessResult OnFailure = AR_inaccessible;

  for (EffectiveContext::record_iterator
         I = EC.Records.begin(), E = EC.Records.end(); I != E; ++I) {
    // All the declarations in EC have been canonicalized, so pointer
    // equality from this point on will work fine.
    const CXXRecordDecl *ECRecord = *I;

    // [B2] and [M2]
    if (Access == AS_private) {
      if (ECRecord == NamingClass)
        return AR_accessible;

      if (EC.isDependent() && MightInstantiateTo(ECRecord, NamingClass))
        OnFailure = AR_dependent;

    // [B3] and [M3]
    } else {
      assert(Access == AS_protected);
      switch (IsDerivedFromInclusive(ECRecord, NamingClass)) {
      case AR_accessible: break;
      case AR_inaccessible: continue;
      case AR_dependent: OnFailure = AR_dependent; continue;
      }

      // C++ [class.protected]p1:
      //   An additional access check beyond those described earlier in
      //   [class.access] is applied when a non-static data member or
      //   non-static member function is a protected member of its naming
      //   class.  As described earlier, access to a protected member is
      //   granted because the reference occurs in a friend or member of
      //   some class C.  If the access is to form a pointer to member,
      //   the nested-name-specifier shall name C or a class derived from
      //   C. All other accesses involve a (possibly implicit) object
      //   expression. In this case, the class of the object expression
      //   shall be C or a class derived from C.
      //
      // We interpret this as a restriction on [M3].

      // In this part of the code, 'C' is just our context class ECRecord.

      // These rules are different if we don't have an instance context.
      if (!Target.hasInstanceContext()) {
        // If it's not an instance member, these restrictions don't apply.
        if (!Target.isInstanceMember()) return AR_accessible;

        // If it's an instance member, use the pointer-to-member rule
        // that the naming class has to be derived from the effective
        // context.

        // Emulate a MSVC bug where the creation of pointer-to-member
        // to protected member of base class is allowed but only from
        // static member functions.
        if (S.getLangOpts().MSVCCompat && !EC.Functions.empty())
          if (CXXMethodDecl* MD = dyn_cast<CXXMethodDecl>(EC.Functions.front()))
            if (MD->isStatic()) return AR_accessible;

        // Despite the standard's confident wording, there is a case
        // where you can have an instance member that's neither in a
        // pointer-to-member expression nor in a member access:  when
        // it names a field in an unevaluated context that can't be an
        // implicit member.  Pending clarification, we just apply the
        // same naming-class restriction here.
        //   FIXME: we're probably not correctly adding the
        //   protected-member restriction when we retroactively convert
        //   an expression to being evaluated.

        // We know that ECRecord derives from NamingClass.  The
        // restriction says to check whether NamingClass derives from
        // ECRecord, but that's not really necessary: two distinct
        // classes can't be recursively derived from each other.  So
        // along this path, we just need to check whether the classes
        // are equal.
        if (NamingClass == ECRecord) return AR_accessible;

        // Otherwise, this context class tells us nothing;  on to the next.
        continue;
      }

      assert(Target.isInstanceMember());

      const CXXRecordDecl *InstanceContext = Target.resolveInstanceContext(S);
      if (!InstanceContext) {
        OnFailure = AR_dependent;
        continue;
      }

      switch (IsDerivedFromInclusive(InstanceContext, ECRecord)) {
      case AR_accessible: return AR_accessible;
      case AR_inaccessible: continue;
      case AR_dependent: OnFailure = AR_dependent; continue;
      }
    }
  }

  // [M3] and [B3] say that, if the target is protected in N, we grant
  // access if the access occurs in a friend or member of some class P
  // that's a subclass of N and where the target has some natural
  // access in P.  The 'member' aspect is easy to handle because P
  // would necessarily be one of the effective-context records, and we
  // address that above.  The 'friend' aspect is completely ridiculous
  // to implement because there are no restrictions at all on P
  // *unless* the [class.protected] restriction applies.  If it does,
  // however, we should ignore whether the naming class is a friend,
  // and instead rely on whether any potential P is a friend.
  if (Access == AS_protected && Target.isInstanceMember()) {
    // Compute the instance context if possible.
    const CXXRecordDecl *InstanceContext = nullptr;
    if (Target.hasInstanceContext()) {
      InstanceContext = Target.resolveInstanceContext(S);
      if (!InstanceContext) return AR_dependent;
    }

    switch (GetProtectedFriendKind(S, EC, InstanceContext, NamingClass)) {
    case AR_accessible: return AR_accessible;
    case AR_inaccessible: return OnFailure;
    case AR_dependent: return AR_dependent;
    }
    llvm_unreachable("impossible friendship kind");
  }

  switch (GetFriendKind(S, EC, NamingClass)) {
  case AR_accessible: return AR_accessible;
  case AR_inaccessible: return OnFailure;
  case AR_dependent: return AR_dependent;
  }

  // Silence bogus warnings
  llvm_unreachable("impossible friendship kind");
}

/// Finds the best path from the naming class to the declaring class,
/// taking friend declarations into account.
///
/// C++0x [class.access.base]p5:
///   A member m is accessible at the point R when named in class N if
///   [M1] m as a member of N is public, or
///   [M2] m as a member of N is private, and R occurs in a member or
///        friend of class N, or
///   [M3] m as a member of N is protected, and R occurs in a member or
///        friend of class N, or in a member or friend of a class P
///        derived from N, where m as a member of P is public, private,
///        or protected, or
///   [M4] there exists a base class B of N that is accessible at R, and
///        m is accessible at R when named in class B.
///
/// C++0x [class.access.base]p4:
///   A base class B of N is accessible at R, if
///   [B1] an invented public member of B would be a public member of N, or
///   [B2] R occurs in a member or friend of class N, and an invented public
///        member of B would be a private or protected member of N, or
///   [B3] R occurs in a member or friend of a class P derived from N, and an
///        invented public member of B would be a private or protected member
///        of P, or
///   [B4] there exists a class S such that B is a base class of S accessible
///        at R and S is a base class of N accessible at R.
///
/// Along a single inheritance path we can restate both of these
/// iteratively:
///
/// First, we note that M1-4 are equivalent to B1-4 if the member is
/// treated as a notional base of its declaring class with inheritance
/// access equivalent to the member's access.  Therefore we need only
/// ask whether a class B is accessible from a class N in context R.
///
/// Let B_1 .. B_n be the inheritance path in question (i.e. where
/// B_1 = N, B_n = B, and for all i, B_{i+1} is a direct base class of
/// B_i).  For i in 1..n, we will calculate ACAB(i), the access to the
/// closest accessible base in the path:
///   Access(a, b) = (* access on the base specifier from a to b *)
///   Merge(a, forbidden) = forbidden
///   Merge(a, private) = forbidden
///   Merge(a, b) = min(a,b)
///   Accessible(c, forbidden) = false
///   Accessible(c, private) = (R is c) || IsFriend(c, R)
///   Accessible(c, protected) = (R derived from c) || IsFriend(c, R)
///   Accessible(c, public) = true
///   ACAB(n) = public
///   ACAB(i) =
///     let AccessToBase = Merge(Access(B_i, B_{i+1}), ACAB(i+1)) in
///     if Accessible(B_i, AccessToBase) then public else AccessToBase
///
/// B is an accessible base of N at R iff ACAB(1) = public.
///
/// \param FinalAccess the access of the "final step", or AS_public if
///   there is no final step.
/// \return null if friendship is dependent
static CXXBasePath *FindBestPath(Sema &S,
                                 const EffectiveContext &EC,
                                 AccessTarget &Target,
                                 AccessSpecifier FinalAccess,
                                 CXXBasePaths &Paths) {
  // Derive the paths to the desired base.
  const CXXRecordDecl *Derived = Target.getNamingClass();
  const CXXRecordDecl *Base = Target.getDeclaringClass();

  // FIXME: fail correctly when there are dependent paths.
  bool isDerived = Derived->isDerivedFrom(const_cast<CXXRecordDecl*>(Base),
                                          Paths);
  assert(isDerived && "derived class not actually derived from base");
  (void) isDerived;

  CXXBasePath *BestPath = nullptr;

  assert(FinalAccess != AS_none && "forbidden access after declaring class");

  bool AnyDependent = false;

  // Derive the friend-modified access along each path.
  for (CXXBasePaths::paths_iterator PI = Paths.begin(), PE = Paths.end();
         PI != PE; ++PI) {
    AccessTarget::SavedInstanceContext _ = Target.saveInstanceContext();

    // Walk through the path backwards.
    AccessSpecifier PathAccess = FinalAccess;
    CXXBasePath::iterator I = PI->end(), E = PI->begin();
    while (I != E) {
      --I;

      assert(PathAccess != AS_none);

      // If the declaration is a private member of a base class, there
      // is no level of friendship in derived classes that can make it
      // accessible.
      if (PathAccess == AS_private) {
        PathAccess = AS_none;
        break;
      }

      const CXXRecordDecl *NC = I->Class->getCanonicalDecl();

      AccessSpecifier BaseAccess = I->Base->getAccessSpecifier();
      PathAccess = std::max(PathAccess, BaseAccess);

      switch (HasAccess(S, EC, NC, PathAccess, Target)) {
      case AR_inaccessible: break;
      case AR_accessible:
        PathAccess = AS_public;

        // Future tests are not against members and so do not have
        // instance context.
        Target.suppressInstanceContext();
        break;
      case AR_dependent:
        AnyDependent = true;
        goto Next;
      }
    }

    // Note that we modify the path's Access field to the
    // friend-modified access.
    if (BestPath == nullptr || PathAccess < BestPath->Access) {
      BestPath = &*PI;
      BestPath->Access = PathAccess;

      // Short-circuit if we found a public path.
      if (BestPath->Access == AS_public)
        return BestPath;
    }

  Next: ;
  }

  assert((!BestPath || BestPath->Access != AS_public) &&
         "fell out of loop with public path");

  // We didn't find a public path, but at least one path was subject
  // to dependent friendship, so delay the check.
  if (AnyDependent)
    return nullptr;

  return BestPath;
}

/// Given that an entity has protected natural access, check whether
/// access might be denied because of the protected member access
/// restriction.
///
/// \return true if a note was emitted
static bool TryDiagnoseProtectedAccess(Sema &S, const EffectiveContext &EC,
                                       AccessTarget &Target) {
  // Only applies to instance accesses.
  if (!Target.isInstanceMember())
    return false;

  assert(Target.isMemberAccess());

  const CXXRecordDecl *NamingClass = Target.getEffectiveNamingClass();

  for (EffectiveContext::record_iterator
         I = EC.Records.begin(), E = EC.Records.end(); I != E; ++I) {
    const CXXRecordDecl *ECRecord = *I;
    switch (IsDerivedFromInclusive(ECRecord, NamingClass)) {
    case AR_accessible: break;
    case AR_inaccessible: continue;
    case AR_dependent: continue;
    }

    // The effective context is a subclass of the declaring class.
    // Check whether the [class.protected] restriction is limiting
    // access.

    // To get this exactly right, this might need to be checked more
    // holistically;  it's not necessarily the case that gaining
    // access here would grant us access overall.

    NamedDecl *D = Target.getTargetDecl();

    // If we don't have an instance context, [class.protected] says the
    // naming class has to equal the context class.
    if (!Target.hasInstanceContext()) {
      // If it does, the restriction doesn't apply.
      if (NamingClass == ECRecord) continue;

      // TODO: it would be great to have a fixit here, since this is
      // such an obvious error.
      S.Diag(D->getLocation(), diag::note_access_protected_restricted_noobject)
        << S.Context.getTypeDeclType(ECRecord);
      return true;
    }

    const CXXRecordDecl *InstanceContext = Target.resolveInstanceContext(S);
    assert(InstanceContext && "diagnosing dependent access");

    switch (IsDerivedFromInclusive(InstanceContext, ECRecord)) {
    case AR_accessible: continue;
    case AR_dependent: continue;
    case AR_inaccessible:
      break;
    }

    // Okay, the restriction seems to be what's limiting us.

    // Use a special diagnostic for constructors and destructors.
    if (isa<CXXConstructorDecl>(D) || isa<CXXDestructorDecl>(D) ||
        (isa<FunctionTemplateDecl>(D) &&
         isa<CXXConstructorDecl>(
                cast<FunctionTemplateDecl>(D)->getTemplatedDecl()))) {
      return S.Diag(D->getLocation(),
                    diag::note_access_protected_restricted_ctordtor)
             << isa<CXXDestructorDecl>(D->getAsFunction());
    }

    // Otherwise, use the generic diagnostic.
    return S.Diag(D->getLocation(),
                  diag::note_access_protected_restricted_object)
           << S.Context.getTypeDeclType(ECRecord);
  }

  return false;
}

/// We are unable to access a given declaration due to its direct
/// access control;  diagnose that.
static void diagnoseBadDirectAccess(Sema &S,
                                    const EffectiveContext &EC,
                                    AccessTarget &entity) {
  assert(entity.isMemberAccess());
  NamedDecl *D = entity.getTargetDecl();

  if (D->getAccess() == AS_protected &&
      TryDiagnoseProtectedAccess(S, EC, entity))
    return;

  // Find an original declaration.
  while (D->isOutOfLine()) {
    NamedDecl *PrevDecl = nullptr;
    if (VarDecl *VD = dyn_cast<VarDecl>(D))
      PrevDecl = VD->getPreviousDecl();
    else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
      PrevDecl = FD->getPreviousDecl();
    else if (TypedefNameDecl *TND = dyn_cast<TypedefNameDecl>(D))
      PrevDecl = TND->getPreviousDecl();
    else if (TagDecl *TD = dyn_cast<TagDecl>(D)) {
      if (isa<RecordDecl>(D) && cast<RecordDecl>(D)->isInjectedClassName())
        break;
      PrevDecl = TD->getPreviousDecl();
    }
    if (!PrevDecl) break;
    D = PrevDecl;
  }

  CXXRecordDecl *DeclaringClass = FindDeclaringClass(D);
  Decl *ImmediateChild;
  if (D->getDeclContext() == DeclaringClass)
    ImmediateChild = D;
  else {
    DeclContext *DC = D->getDeclContext();
    while (DC->getParent() != DeclaringClass)
      DC = DC->getParent();
    ImmediateChild = cast<Decl>(DC);
  }

  // Check whether there's an AccessSpecDecl preceding this in the
  // chain of the DeclContext.
  bool isImplicit = true;
  for (const auto *I : DeclaringClass->decls()) {
    if (I == ImmediateChild) break;
    if (isa<AccessSpecDecl>(I)) {
      isImplicit = false;
      break;
    }
  }

  S.Diag(D->getLocation(), diag::note_access_natural)
    << (unsigned) (D->getAccess() == AS_protected)
    << isImplicit;
}

/// Diagnose the path which caused the given declaration or base class
/// to become inaccessible.
static void DiagnoseAccessPath(Sema &S,
                               const EffectiveContext &EC,
                               AccessTarget &entity) {
  // Save the instance context to preserve invariants.
  AccessTarget::SavedInstanceContext _ = entity.saveInstanceContext();

  // This basically repeats the main algorithm but keeps some more
  // information.

  // The natural access so far.
  AccessSpecifier accessSoFar = AS_public;

  // Check whether we have special rights to the declaring class.
  if (entity.isMemberAccess()) {
    NamedDecl *D = entity.getTargetDecl();
    accessSoFar = D->getAccess();
    const CXXRecordDecl *declaringClass = entity.getDeclaringClass();

    switch (HasAccess(S, EC, declaringClass, accessSoFar, entity)) {
    // If the declaration is accessible when named in its declaring
    // class, then we must be constrained by the path.
    case AR_accessible:
      accessSoFar = AS_public;
      entity.suppressInstanceContext();
      break;

    case AR_inaccessible:
      if (accessSoFar == AS_private ||
          declaringClass == entity.getEffectiveNamingClass())
        return diagnoseBadDirectAccess(S, EC, entity);
      break;

    case AR_dependent:
      llvm_unreachable("cannot diagnose dependent access");
    }
  }

  CXXBasePaths paths;
  CXXBasePath &path = *FindBestPath(S, EC, entity, accessSoFar, paths);
  assert(path.Access != AS_public);

  CXXBasePath::iterator i = path.end(), e = path.begin();
  CXXBasePath::iterator constrainingBase = i;
  while (i != e) {
    --i;

    assert(accessSoFar != AS_none && accessSoFar != AS_private);

    // Is the entity accessible when named in the deriving class, as
    // modified by the base specifier?
    const CXXRecordDecl *derivingClass = i->Class->getCanonicalDecl();
    const CXXBaseSpecifier *base = i->Base;

    // If the access to this base is worse than the access we have to
    // the declaration, remember it.
    AccessSpecifier baseAccess = base->getAccessSpecifier();
    if (baseAccess > accessSoFar) {
      constrainingBase = i;
      accessSoFar = baseAccess;
    }

    switch (HasAccess(S, EC, derivingClass, accessSoFar, entity)) {
    case AR_inaccessible: break;
    case AR_accessible:
      accessSoFar = AS_public;
      entity.suppressInstanceContext();
      constrainingBase = nullptr;
      break;
    case AR_dependent:
      llvm_unreachable("cannot diagnose dependent access");
    }

    // If this was private inheritance, but we don't have access to
    // the deriving class, we're done.
    if (accessSoFar == AS_private) {
      assert(baseAccess == AS_private);
      assert(constrainingBase == i);
      break;
    }
  }

  // If we don't have a constraining base, the access failure must be
  // due to the original declaration.
  if (constrainingBase == path.end())
    return diagnoseBadDirectAccess(S, EC, entity);

  // We're constrained by inheritance, but we want to say
  // "declared private here" if we're diagnosing a hierarchy
  // conversion and this is the final step.
  unsigned diagnostic;
  if (entity.isMemberAccess() ||
      constrainingBase + 1 != path.end()) {
    diagnostic = diag::note_access_constrained_by_path;
  } else {
    diagnostic = diag::note_access_natural;
  }

  const CXXBaseSpecifier *base = constrainingBase->Base;

  S.Diag(base->getSourceRange().getBegin(), diagnostic)
    << base->getSourceRange()
    << (base->getAccessSpecifier() == AS_protected)
    << (base->getAccessSpecifierAsWritten() == AS_none);

  if (entity.isMemberAccess())
    S.Diag(entity.getTargetDecl()->getLocation(),
           diag::note_member_declared_at);
}

static void DiagnoseBadAccess(Sema &S, SourceLocation Loc,
                              const EffectiveContext &EC,
                              AccessTarget &Entity) {
  const CXXRecordDecl *NamingClass = Entity.getNamingClass();
  const CXXRecordDecl *DeclaringClass = Entity.getDeclaringClass();
  NamedDecl *D = (Entity.isMemberAccess() ? Entity.getTargetDecl() : nullptr);

  S.Diag(Loc, Entity.getDiag())
    << (Entity.getAccess() == AS_protected)
    << (D ? D->getDeclName() : DeclarationName())
    << S.Context.getTypeDeclType(NamingClass)
    << S.Context.getTypeDeclType(DeclaringClass);
  DiagnoseAccessPath(S, EC, Entity);
}

/// MSVC has a bug where if during an using declaration name lookup,
/// the declaration found is unaccessible (private) and that declaration
/// was bring into scope via another using declaration whose target
/// declaration is accessible (public) then no error is generated.
/// Example:
///   class A {
///   public:
///     int f();
///   };
///   class B : public A {
///   private:
///     using A::f;
///   };
///   class C : public B {
///   private:
///     using B::f;
///   };
///
/// Here, B::f is private so this should fail in Standard C++, but
/// because B::f refers to A::f which is public MSVC accepts it.
static bool IsMicrosoftUsingDeclarationAccessBug(Sema& S,
                                                 SourceLocation AccessLoc,
                                                 AccessTarget &Entity) {
  if (UsingShadowDecl *Shadow =
          dyn_cast<UsingShadowDecl>(Entity.getTargetDecl()))
    if (UsingDecl *UD = dyn_cast<UsingDecl>(Shadow->getIntroducer())) {
      const NamedDecl *OrigDecl = Entity.getTargetDecl()->getUnderlyingDecl();
      if (Entity.getTargetDecl()->getAccess() == AS_private &&
          (OrigDecl->getAccess() == AS_public ||
           OrigDecl->getAccess() == AS_protected)) {
        S.Diag(AccessLoc, diag::ext_ms_using_declaration_inaccessible)
            << UD->getQualifiedNameAsString()
            << OrigDecl->getQualifiedNameAsString();
        return true;
      }
    }
  return false;
}

/// Determines whether the accessed entity is accessible.  Public members
/// have been weeded out by this point.
static AccessResult IsAccessible(Sema &S,
                                 const EffectiveContext &EC,
                                 AccessTarget &Entity) {
  // Determine the actual naming class.
  const CXXRecordDecl *NamingClass = Entity.getEffectiveNamingClass();

  AccessSpecifier UnprivilegedAccess = Entity.getAccess();
  assert(UnprivilegedAccess != AS_public && "public access not weeded out");

  // Before we try to recalculate access paths, try to white-list
  // accesses which just trade in on the final step, i.e. accesses
  // which don't require [M4] or [B4]. These are by far the most
  // common forms of privileged access.
  if (UnprivilegedAccess != AS_none) {
    switch (HasAccess(S, EC, NamingClass, UnprivilegedAccess, Entity)) {
    case AR_dependent:
      // This is actually an interesting policy decision.  We don't
      // *have* to delay immediately here: we can do the full access
      // calculation in the hope that friendship on some intermediate
      // class will make the declaration accessible non-dependently.
      // But that's not cheap, and odds are very good (note: assertion
      // made without data) that the friend declaration will determine
      // access.
      return AR_dependent;

    case AR_accessible: return AR_accessible;
    case AR_inaccessible: break;
    }
  }

  AccessTarget::SavedInstanceContext _ = Entity.saveInstanceContext();

  // We lower member accesses to base accesses by pretending that the
  // member is a base class of its declaring class.
  AccessSpecifier FinalAccess;

  if (Entity.isMemberAccess()) {
    // Determine if the declaration is accessible from EC when named
    // in its declaring class.
    NamedDecl *Target = Entity.getTargetDecl();
    const CXXRecordDecl *DeclaringClass = Entity.getDeclaringClass();

    FinalAccess = Target->getAccess();
    switch (HasAccess(S, EC, DeclaringClass, FinalAccess, Entity)) {
    case AR_accessible:
      // Target is accessible at EC when named in its declaring class.
      // We can now hill-climb and simply check whether the declaring
      // class is accessible as a base of the naming class.  This is
      // equivalent to checking the access of a notional public
      // member with no instance context.
      FinalAccess = AS_public;
      Entity.suppressInstanceContext();
      break;
    case AR_inaccessible: break;
    case AR_dependent: return AR_dependent; // see above
    }

    if (DeclaringClass == NamingClass)
      return (FinalAccess == AS_public ? AR_accessible : AR_inaccessible);
  } else {
    FinalAccess = AS_public;
  }

  assert(Entity.getDeclaringClass() != NamingClass);

  // Append the declaration's access if applicable.
  CXXBasePaths Paths;
  CXXBasePath *Path = FindBestPath(S, EC, Entity, FinalAccess, Paths);
  if (!Path)
    return AR_dependent;

  assert(Path->Access <= UnprivilegedAccess &&
         "access along best path worse than direct?");
  if (Path->Access == AS_public)
    return AR_accessible;
  return AR_inaccessible;
}

static void DelayDependentAccess(Sema &S,
                                 const EffectiveContext &EC,
                                 SourceLocation Loc,
                                 const AccessTarget &Entity) {
  assert(EC.isDependent() && "delaying non-dependent access");
  DeclContext *DC = EC.getInnerContext();
  assert(DC->isDependentContext() && "delaying non-dependent access");
  DependentDiagnostic::Create(S.Context, DC, DependentDiagnostic::Access,
                              Loc,
                              Entity.isMemberAccess(),
                              Entity.getAccess(),
                              Entity.getTargetDecl(),
                              Entity.getNamingClass(),
                              Entity.getBaseObjectType(),
                              Entity.getDiag());
}

/// Checks access to an entity from the given effective context.
static AccessResult CheckEffectiveAccess(Sema &S,
                                         const EffectiveContext &EC,
                                         SourceLocation Loc,
                                         AccessTarget &Entity) {
  assert(Entity.getAccess() != AS_public && "called for public access!");

  switch (IsAccessible(S, EC, Entity)) {
  case AR_dependent:
    DelayDependentAccess(S, EC, Loc, Entity);
    return AR_dependent;

  case AR_inaccessible:
    if (S.getLangOpts().MSVCCompat &&
        IsMicrosoftUsingDeclarationAccessBug(S, Loc, Entity))
      return AR_accessible;
    if (!Entity.isQuiet())
      DiagnoseBadAccess(S, Loc, EC, Entity);
    return AR_inaccessible;

  case AR_accessible:
    return AR_accessible;
  }

  // silence unnecessary warning
  llvm_unreachable("invalid access result");
}

static Sema::AccessResult CheckAccess(Sema &S, SourceLocation Loc,
                                      AccessTarget &Entity) {
  // If the access path is public, it's accessible everywhere.
  if (Entity.getAccess() == AS_public)
    return Sema::AR_accessible;

  // If we're currently parsing a declaration, we may need to delay
  // access control checking, because our effective context might be
  // different based on what the declaration comes out as.
  //
  // For example, we might be parsing a declaration with a scope
  // specifier, like this:
  //   A::private_type A::foo() { ... }
  //
  // friend declaration should not be delayed because it may lead to incorrect
  // redeclaration chain, such as:
  //   class D {
  //    class E{
  //     class F{};
  //     friend  void foo(D::E::F& q);
  //    };
  //    friend  void foo(D::E::F& q);
  //   };
  if (S.DelayedDiagnostics.shouldDelayDiagnostics()) {
    // [class.friend]p9:
    // A member nominated by a friend declaration shall be accessible in the
    // class containing the friend declaration. The meaning of the friend
    // declaration is the same whether the friend declaration appears in the
    // private, protected, or public ([class.mem]) portion of the class
    // member-specification.
    Scope *TS = S.getCurScope();
    bool IsFriendDeclaration = false;
    while (TS && !IsFriendDeclaration) {
      IsFriendDeclaration = TS->isFriendScope();
      TS = TS->getParent();
    }
    if (!IsFriendDeclaration) {
      S.DelayedDiagnostics.add(DelayedDiagnostic::makeAccess(Loc, Entity));
      return Sema::AR_delayed;
    }
  }

  EffectiveContext EC(S.CurContext);
  switch (CheckEffectiveAccess(S, EC, Loc, Entity)) {
  case AR_accessible: return Sema::AR_accessible;
  case AR_inaccessible: return Sema::AR_inaccessible;
  case AR_dependent: return Sema::AR_dependent;
  }
  llvm_unreachable("invalid access result");
}

void Sema::HandleDelayedAccessCheck(DelayedDiagnostic &DD, Decl *D) {
  // Access control for names used in the declarations of functions
  // and function templates should normally be evaluated in the context
  // of the declaration, just in case it's a friend of something.
  // However, this does not apply to local extern declarations.

  DeclContext *DC = D->getDeclContext();
  if (D->isLocalExternDecl()) {
    DC = D->getLexicalDeclContext();
  } else if (FunctionDecl *FN = dyn_cast<FunctionDecl>(D)) {
    DC = FN;
  } else if (TemplateDecl *TD = dyn_cast<TemplateDecl>(D)) {
    if (isa<DeclContext>(TD->getTemplatedDecl()))
      DC = cast<DeclContext>(TD->getTemplatedDecl());
  } else if (auto *RD = dyn_cast<RequiresExprBodyDecl>(D)) {
    DC = RD;
  }

  EffectiveContext EC(DC);

  AccessTarget Target(DD.getAccessData());

  if (CheckEffectiveAccess(*this, EC, DD.Loc, Target) == ::AR_inaccessible)
    DD.Triggered = true;
}

void Sema::HandleDependentAccessCheck(const DependentDiagnostic &DD,
                        const MultiLevelTemplateArgumentList &TemplateArgs) {
  SourceLocation Loc = DD.getAccessLoc();
  AccessSpecifier Access = DD.getAccess();

  Decl *NamingD = FindInstantiatedDecl(Loc, DD.getAccessNamingClass(),
                                       TemplateArgs);
  if (!NamingD) return;
  Decl *TargetD = FindInstantiatedDecl(Loc, DD.getAccessTarget(),
                                       TemplateArgs);
  if (!TargetD) return;

  if (DD.isAccessToMember()) {
    CXXRecordDecl *NamingClass = cast<CXXRecordDecl>(NamingD);
    NamedDecl *TargetDecl = cast<NamedDecl>(TargetD);
    QualType BaseObjectType = DD.getAccessBaseObjectType();
    if (!BaseObjectType.isNull()) {
      BaseObjectType = SubstType(BaseObjectType, TemplateArgs, Loc,
                                 DeclarationName());
      if (BaseObjectType.isNull()) return;
    }

    AccessTarget Entity(Context,
                        AccessTarget::Member,
                        NamingClass,
                        DeclAccessPair::make(TargetDecl, Access),
                        BaseObjectType);
    Entity.setDiag(DD.getDiagnostic());
    CheckAccess(*this, Loc, Entity);
  } else {
    AccessTarget Entity(Context,
                        AccessTarget::Base,
                        cast<CXXRecordDecl>(TargetD),
                        cast<CXXRecordDecl>(NamingD),
                        Access);
    Entity.setDiag(DD.getDiagnostic());
    CheckAccess(*this, Loc, Entity);
  }
}

Sema::AccessResult Sema::CheckUnresolvedLookupAccess(UnresolvedLookupExpr *E,
                                                     DeclAccessPair Found) {
  if (!getLangOpts().AccessControl ||
      !E->getNamingClass() ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  AccessTarget Entity(Context, AccessTarget::Member, E->getNamingClass(),
                      Found, QualType());
  Entity.setDiag(diag::err_access) << E->getSourceRange();

  return CheckAccess(*this, E->getNameLoc(), Entity);
}

Sema::AccessResult Sema::CheckUnresolvedMemberAccess(UnresolvedMemberExpr *E,
                                                     DeclAccessPair Found) {
  if (!getLangOpts().AccessControl ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  QualType BaseType = E->getBaseType();
  if (E->isArrow())
    BaseType = BaseType->castAs<PointerType>()->getPointeeType();

  AccessTarget Entity(Context, AccessTarget::Member, E->getNamingClass(),
                      Found, BaseType);
  Entity.setDiag(diag::err_access) << E->getSourceRange();

  return CheckAccess(*this, E->getMemberLoc(), Entity);
}

bool Sema::isMemberAccessibleForDeletion(CXXRecordDecl *NamingClass,
                                         DeclAccessPair Found,
                                         QualType ObjectType,
                                         SourceLocation Loc,
                                         const PartialDiagnostic &Diag) {
  // Fast path.
  if (Found.getAccess() == AS_public || !getLangOpts().AccessControl)
    return true;

  AccessTarget Entity(Context, AccessTarget::Member, NamingClass, Found,
                      ObjectType);

  // Suppress diagnostics.
  Entity.setDiag(Diag);

  switch (CheckAccess(*this, Loc, Entity)) {
  case AR_accessible: return true;
  case AR_inaccessible: return false;
  case AR_dependent: llvm_unreachable("dependent for =delete computation");
  case AR_delayed: llvm_unreachable("cannot delay =delete computation");
  }
  llvm_unreachable("bad access result");
}

Sema::AccessResult Sema::CheckDestructorAccess(SourceLocation Loc,
                                               CXXDestructorDecl *Dtor,
                                               const PartialDiagnostic &PDiag,
                                               QualType ObjectTy) {
  if (!getLangOpts().AccessControl)
    return AR_accessible;

  // There's never a path involved when checking implicit destructor access.
  AccessSpecifier Access = Dtor->getAccess();
  if (Access == AS_public)
    return AR_accessible;

  CXXRecordDecl *NamingClass = Dtor->getParent();
  if (ObjectTy.isNull()) ObjectTy = Context.getTypeDeclType(NamingClass);

  AccessTarget Entity(Context, AccessTarget::Member, NamingClass,
                      DeclAccessPair::make(Dtor, Access),
                      ObjectTy);
  Entity.setDiag(PDiag); // TODO: avoid copy

  return CheckAccess(*this, Loc, Entity);
}

Sema::AccessResult Sema::CheckConstructorAccess(SourceLocation UseLoc,
                                                CXXConstructorDecl *Constructor,
                                                DeclAccessPair Found,
                                                const InitializedEntity &Entity,
                                                bool IsCopyBindingRefToTemp) {
  if (!getLangOpts().AccessControl || Found.getAccess() == AS_public)
    return AR_accessible;

  PartialDiagnostic PD(PDiag());
  switch (Entity.getKind()) {
  default:
    PD = PDiag(IsCopyBindingRefToTemp
                 ? diag::ext_rvalue_to_reference_access_ctor
                 : diag::err_access_ctor);

    break;

  case InitializedEntity::EK_Base:
    PD = PDiag(diag::err_access_base_ctor);
    PD << Entity.isInheritedVirtualBase()
       << Entity.getBaseSpecifier()->getType()
       << llvm::to_underlying(getSpecialMember(Constructor));
    break;

  case InitializedEntity::EK_Member:
  case InitializedEntity::EK_ParenAggInitMember: {
    const FieldDecl *Field = cast<FieldDecl>(Entity.getDecl());
    PD = PDiag(diag::err_access_field_ctor);
    PD << Field->getType()
       << llvm::to_underlying(getSpecialMember(Constructor));
    break;
  }

  case InitializedEntity::EK_LambdaCapture: {
    StringRef VarName = Entity.getCapturedVarName();
    PD = PDiag(diag::err_access_lambda_capture);
    PD << VarName << Entity.getType()
       << llvm::to_underlying(getSpecialMember(Constructor));
    break;
  }

  }

  return CheckConstructorAccess(UseLoc, Constructor, Found, Entity, PD);
}

Sema::AccessResult Sema::CheckConstructorAccess(SourceLocation UseLoc,
                                                CXXConstructorDecl *Constructor,
                                                DeclAccessPair Found,
                                                const InitializedEntity &Entity,
                                                const PartialDiagnostic &PD) {
  if (!getLangOpts().AccessControl ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  CXXRecordDecl *NamingClass = Constructor->getParent();

  // Initializing a base sub-object is an instance method call on an
  // object of the derived class.  Otherwise, we have an instance method
  // call on an object of the constructed type.
  //
  // FIXME: If we have a parent, we're initializing the base class subobject
  // in aggregate initialization. It's not clear whether the object class
  // should be the base class or the derived class in that case.
  CXXRecordDecl *ObjectClass;
  if ((Entity.getKind() == InitializedEntity::EK_Base ||
       Entity.getKind() == InitializedEntity::EK_Delegating) &&
      !Entity.getParent()) {
    ObjectClass = cast<CXXConstructorDecl>(CurContext)->getParent();
  } else if (auto *Shadow =
                 dyn_cast<ConstructorUsingShadowDecl>(Found.getDecl())) {
    // If we're using an inheriting constructor to construct an object,
    // the object class is the derived class, not the base class.
    ObjectClass = Shadow->getParent();
  } else {
    ObjectClass = NamingClass;
  }

  AccessTarget AccessEntity(
      Context, AccessTarget::Member, NamingClass,
      DeclAccessPair::make(Constructor, Found.getAccess()),
      Context.getTypeDeclType(ObjectClass));
  AccessEntity.setDiag(PD);

  return CheckAccess(*this, UseLoc, AccessEntity);
}

Sema::AccessResult Sema::CheckAllocationAccess(SourceLocation OpLoc,
                                               SourceRange PlacementRange,
                                               CXXRecordDecl *NamingClass,
                                               DeclAccessPair Found,
                                               bool Diagnose) {
  if (!getLangOpts().AccessControl ||
      !NamingClass ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  AccessTarget Entity(Context, AccessTarget::Member, NamingClass, Found,
                      QualType());
  if (Diagnose)
    Entity.setDiag(diag::err_access)
      << PlacementRange;

  return CheckAccess(*this, OpLoc, Entity);
}

Sema::AccessResult Sema::CheckMemberAccess(SourceLocation UseLoc,
                                           CXXRecordDecl *NamingClass,
                                           DeclAccessPair Found) {
  if (!getLangOpts().AccessControl ||
      !NamingClass ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  AccessTarget Entity(Context, AccessTarget::Member, NamingClass,
                      Found, QualType());

  return CheckAccess(*this, UseLoc, Entity);
}

Sema::AccessResult
Sema::CheckStructuredBindingMemberAccess(SourceLocation UseLoc,
                                         CXXRecordDecl *DecomposedClass,
                                         DeclAccessPair Field) {
  if (!getLangOpts().AccessControl ||
      Field.getAccess() == AS_public)
    return AR_accessible;

  AccessTarget Entity(Context, AccessTarget::Member, DecomposedClass, Field,
                      Context.getRecordType(DecomposedClass));
  Entity.setDiag(diag::err_decomp_decl_inaccessible_field);

  return CheckAccess(*this, UseLoc, Entity);
}

Sema::AccessResult Sema::CheckMemberOperatorAccess(SourceLocation OpLoc,
                                                   Expr *ObjectExpr,
                                                   const SourceRange &Range,
                                                   DeclAccessPair Found) {
  if (!getLangOpts().AccessControl || Found.getAccess() == AS_public)
    return AR_accessible;

  const RecordType *RT = ObjectExpr->getType()->castAs<RecordType>();
  CXXRecordDecl *NamingClass = cast<CXXRecordDecl>(RT->getDecl());

  AccessTarget Entity(Context, AccessTarget::Member, NamingClass, Found,
                      ObjectExpr->getType());
  Entity.setDiag(diag::err_access) << ObjectExpr->getSourceRange() << Range;

  return CheckAccess(*this, OpLoc, Entity);
}

Sema::AccessResult Sema::CheckMemberOperatorAccess(SourceLocation OpLoc,
                                                   Expr *ObjectExpr,
                                                   Expr *ArgExpr,
                                                   DeclAccessPair Found) {
  return CheckMemberOperatorAccess(
      OpLoc, ObjectExpr, ArgExpr ? ArgExpr->getSourceRange() : SourceRange(),
      Found);
}

Sema::AccessResult Sema::CheckMemberOperatorAccess(SourceLocation OpLoc,
                                                   Expr *ObjectExpr,
                                                   ArrayRef<Expr *> ArgExprs,
                                                   DeclAccessPair FoundDecl) {
  SourceRange R;
  if (!ArgExprs.empty()) {
    R = SourceRange(ArgExprs.front()->getBeginLoc(),
                    ArgExprs.back()->getEndLoc());
  }

  return CheckMemberOperatorAccess(OpLoc, ObjectExpr, R, FoundDecl);
}

Sema::AccessResult Sema::CheckFriendAccess(NamedDecl *target) {
  assert(isa<CXXMethodDecl>(target->getAsFunction()));

  // Friendship lookup is a redeclaration lookup, so there's never an
  // inheritance path modifying access.
  AccessSpecifier access = target->getAccess();

  if (!getLangOpts().AccessControl || access == AS_public)
    return AR_accessible;

  CXXMethodDecl *method = cast<CXXMethodDecl>(target->getAsFunction());

  AccessTarget entity(Context, AccessTarget::Member,
                      cast<CXXRecordDecl>(target->getDeclContext()),
                      DeclAccessPair::make(target, access),
                      /*no instance context*/ QualType());
  entity.setDiag(diag::err_access_friend_function)
      << (method->getQualifier() ? method->getQualifierLoc().getSourceRange()
                                 : method->getNameInfo().getSourceRange());

  // We need to bypass delayed-diagnostics because we might be called
  // while the ParsingDeclarator is active.
  EffectiveContext EC(CurContext);
  switch (CheckEffectiveAccess(*this, EC, target->getLocation(), entity)) {
  case ::AR_accessible: return Sema::AR_accessible;
  case ::AR_inaccessible: return Sema::AR_inaccessible;
  case ::AR_dependent: return Sema::AR_dependent;
  }
  llvm_unreachable("invalid access result");
}

Sema::AccessResult Sema::CheckAddressOfMemberAccess(Expr *OvlExpr,
                                                    DeclAccessPair Found) {
  if (!getLangOpts().AccessControl ||
      Found.getAccess() == AS_none ||
      Found.getAccess() == AS_public)
    return AR_accessible;

  OverloadExpr *Ovl = OverloadExpr::find(OvlExpr).Expression;
  CXXRecordDecl *NamingClass = Ovl->getNamingClass();

  AccessTarget Entity(Context, AccessTarget::Member, NamingClass, Found,
                      /*no instance context*/ QualType());
  Entity.setDiag(diag::err_access)
    << Ovl->getSourceRange();

  return CheckAccess(*this, Ovl->getNameLoc(), Entity);
}

Sema::AccessResult Sema::CheckBaseClassAccess(SourceLocation AccessLoc,
                                              QualType Base,
                                              QualType Derived,
                                              const CXXBasePath &Path,
                                              unsigned DiagID,
                                              bool ForceCheck,
                                              bool ForceUnprivileged) {
  if (!ForceCheck && !getLangOpts().AccessControl)
    return AR_accessible;

  if (Path.Access == AS_public)
    return AR_accessible;

  CXXRecordDecl *BaseD, *DerivedD;
  BaseD = cast<CXXRecordDecl>(Base->castAs<RecordType>()->getDecl());
  DerivedD = cast<CXXRecordDecl>(Derived->castAs<RecordType>()->getDecl());

  AccessTarget Entity(Context, AccessTarget::Base, BaseD, DerivedD,
                      Path.Access);
  if (DiagID)
    Entity.setDiag(DiagID) << Derived << Base;

  if (ForceUnprivileged) {
    switch (CheckEffectiveAccess(*this, EffectiveContext(),
                                 AccessLoc, Entity)) {
    case ::AR_accessible: return Sema::AR_accessible;
    case ::AR_inaccessible: return Sema::AR_inaccessible;
    case ::AR_dependent: return Sema::AR_dependent;
    }
    llvm_unreachable("unexpected result from CheckEffectiveAccess");
  }
  return CheckAccess(*this, AccessLoc, Entity);
}

void Sema::CheckLookupAccess(const LookupResult &R) {
  assert(getLangOpts().AccessControl
         && "performing access check without access control");
  assert(R.getNamingClass() && "performing access check without naming class");

  for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I) {
    if (I.getAccess() != AS_public) {
      AccessTarget Entity(Context, AccessedEntity::Member,
                          R.getNamingClass(), I.getPair(),
                          R.getBaseObjectType());
      Entity.setDiag(diag::err_access);
      CheckAccess(*this, R.getNameLoc(), Entity);
    }
  }
}

bool Sema::IsSimplyAccessible(NamedDecl *Target, CXXRecordDecl *NamingClass,
                              QualType BaseType) {
  // Perform the C++ accessibility checks first.
  if (Target->isCXXClassMember() && NamingClass) {
    if (!getLangOpts().CPlusPlus)
      return false;
    // The unprivileged access is AS_none as we don't know how the member was
    // accessed, which is described by the access in DeclAccessPair.
    // `IsAccessible` will examine the actual access of Target (i.e.
    // Decl->getAccess()) when calculating the access.
    AccessTarget Entity(Context, AccessedEntity::Member, NamingClass,
                        DeclAccessPair::make(Target, AS_none), BaseType);
    EffectiveContext EC(CurContext);
    return ::IsAccessible(*this, EC, Entity) != ::AR_inaccessible;
  }

  if (ObjCIvarDecl *Ivar = dyn_cast<ObjCIvarDecl>(Target)) {
    // @public and @package ivars are always accessible.
    if (Ivar->getCanonicalAccessControl() == ObjCIvarDecl::Public ||
        Ivar->getCanonicalAccessControl() == ObjCIvarDecl::Package)
      return true;

    // If we are inside a class or category implementation, determine the
    // interface we're in.
    ObjCInterfaceDecl *ClassOfMethodDecl = nullptr;
    if (ObjCMethodDecl *MD = getCurMethodDecl())
      ClassOfMethodDecl =  MD->getClassInterface();
    else if (FunctionDecl *FD = getCurFunctionDecl()) {
      if (ObjCImplDecl *Impl
            = dyn_cast<ObjCImplDecl>(FD->getLexicalDeclContext())) {
        if (ObjCImplementationDecl *IMPD
              = dyn_cast<ObjCImplementationDecl>(Impl))
          ClassOfMethodDecl = IMPD->getClassInterface();
        else if (ObjCCategoryImplDecl* CatImplClass
                   = dyn_cast<ObjCCategoryImplDecl>(Impl))
          ClassOfMethodDecl = CatImplClass->getClassInterface();
      }
    }

    // If we're not in an interface, this ivar is inaccessible.
    if (!ClassOfMethodDecl)
      return false;

    // If we're inside the same interface that owns the ivar, we're fine.
    if (declaresSameEntity(ClassOfMethodDecl, Ivar->getContainingInterface()))
      return true;

    // If the ivar is private, it's inaccessible.
    if (Ivar->getCanonicalAccessControl() == ObjCIvarDecl::Private)
      return false;

    return Ivar->getContainingInterface()->isSuperClassOf(ClassOfMethodDecl);
  }

  return true;
}
