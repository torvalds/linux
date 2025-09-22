//===--------------------- SemaLookup.cpp - Name Lookup  ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements name lookup for C, C++, Objective-C, and
//  Objective-C++.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclLookups.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/RISCVIntrinsicManager.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaRISCV.h"
#include "clang/Sema/TemplateDeduction.h"
#include "clang/Sema/TypoCorrection.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/edit_distance.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <iterator>
#include <list>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "OpenCLBuiltins.inc"

using namespace clang;
using namespace sema;

namespace {
  class UnqualUsingEntry {
    const DeclContext *Nominated;
    const DeclContext *CommonAncestor;

  public:
    UnqualUsingEntry(const DeclContext *Nominated,
                     const DeclContext *CommonAncestor)
      : Nominated(Nominated), CommonAncestor(CommonAncestor) {
    }

    const DeclContext *getCommonAncestor() const {
      return CommonAncestor;
    }

    const DeclContext *getNominatedNamespace() const {
      return Nominated;
    }

    // Sort by the pointer value of the common ancestor.
    struct Comparator {
      bool operator()(const UnqualUsingEntry &L, const UnqualUsingEntry &R) {
        return L.getCommonAncestor() < R.getCommonAncestor();
      }

      bool operator()(const UnqualUsingEntry &E, const DeclContext *DC) {
        return E.getCommonAncestor() < DC;
      }

      bool operator()(const DeclContext *DC, const UnqualUsingEntry &E) {
        return DC < E.getCommonAncestor();
      }
    };
  };

  /// A collection of using directives, as used by C++ unqualified
  /// lookup.
  class UnqualUsingDirectiveSet {
    Sema &SemaRef;

    typedef SmallVector<UnqualUsingEntry, 8> ListTy;

    ListTy list;
    llvm::SmallPtrSet<DeclContext*, 8> visited;

  public:
    UnqualUsingDirectiveSet(Sema &SemaRef) : SemaRef(SemaRef) {}

    void visitScopeChain(Scope *S, Scope *InnermostFileScope) {
      // C++ [namespace.udir]p1:
      //   During unqualified name lookup, the names appear as if they
      //   were declared in the nearest enclosing namespace which contains
      //   both the using-directive and the nominated namespace.
      DeclContext *InnermostFileDC = InnermostFileScope->getEntity();
      assert(InnermostFileDC && InnermostFileDC->isFileContext());

      for (; S; S = S->getParent()) {
        // C++ [namespace.udir]p1:
        //   A using-directive shall not appear in class scope, but may
        //   appear in namespace scope or in block scope.
        DeclContext *Ctx = S->getEntity();
        if (Ctx && Ctx->isFileContext()) {
          visit(Ctx, Ctx);
        } else if (!Ctx || Ctx->isFunctionOrMethod()) {
          for (auto *I : S->using_directives())
            if (SemaRef.isVisible(I))
              visit(I, InnermostFileDC);
        }
      }
    }

    // Visits a context and collect all of its using directives
    // recursively.  Treats all using directives as if they were
    // declared in the context.
    //
    // A given context is only every visited once, so it is important
    // that contexts be visited from the inside out in order to get
    // the effective DCs right.
    void visit(DeclContext *DC, DeclContext *EffectiveDC) {
      if (!visited.insert(DC).second)
        return;

      addUsingDirectives(DC, EffectiveDC);
    }

    // Visits a using directive and collects all of its using
    // directives recursively.  Treats all using directives as if they
    // were declared in the effective DC.
    void visit(UsingDirectiveDecl *UD, DeclContext *EffectiveDC) {
      DeclContext *NS = UD->getNominatedNamespace();
      if (!visited.insert(NS).second)
        return;

      addUsingDirective(UD, EffectiveDC);
      addUsingDirectives(NS, EffectiveDC);
    }

    // Adds all the using directives in a context (and those nominated
    // by its using directives, transitively) as if they appeared in
    // the given effective context.
    void addUsingDirectives(DeclContext *DC, DeclContext *EffectiveDC) {
      SmallVector<DeclContext*, 4> queue;
      while (true) {
        for (auto *UD : DC->using_directives()) {
          DeclContext *NS = UD->getNominatedNamespace();
          if (SemaRef.isVisible(UD) && visited.insert(NS).second) {
            addUsingDirective(UD, EffectiveDC);
            queue.push_back(NS);
          }
        }

        if (queue.empty())
          return;

        DC = queue.pop_back_val();
      }
    }

    // Add a using directive as if it had been declared in the given
    // context.  This helps implement C++ [namespace.udir]p3:
    //   The using-directive is transitive: if a scope contains a
    //   using-directive that nominates a second namespace that itself
    //   contains using-directives, the effect is as if the
    //   using-directives from the second namespace also appeared in
    //   the first.
    void addUsingDirective(UsingDirectiveDecl *UD, DeclContext *EffectiveDC) {
      // Find the common ancestor between the effective context and
      // the nominated namespace.
      DeclContext *Common = UD->getNominatedNamespace();
      while (!Common->Encloses(EffectiveDC))
        Common = Common->getParent();
      Common = Common->getPrimaryContext();

      list.push_back(UnqualUsingEntry(UD->getNominatedNamespace(), Common));
    }

    void done() { llvm::sort(list, UnqualUsingEntry::Comparator()); }

    typedef ListTy::const_iterator const_iterator;

    const_iterator begin() const { return list.begin(); }
    const_iterator end() const { return list.end(); }

    llvm::iterator_range<const_iterator>
    getNamespacesFor(const DeclContext *DC) const {
      return llvm::make_range(std::equal_range(begin(), end(),
                                               DC->getPrimaryContext(),
                                               UnqualUsingEntry::Comparator()));
    }
  };
} // end anonymous namespace

// Retrieve the set of identifier namespaces that correspond to a
// specific kind of name lookup.
static inline unsigned getIDNS(Sema::LookupNameKind NameKind,
                               bool CPlusPlus,
                               bool Redeclaration) {
  unsigned IDNS = 0;
  switch (NameKind) {
  case Sema::LookupObjCImplicitSelfParam:
  case Sema::LookupOrdinaryName:
  case Sema::LookupRedeclarationWithLinkage:
  case Sema::LookupLocalFriendName:
  case Sema::LookupDestructorName:
    IDNS = Decl::IDNS_Ordinary;
    if (CPlusPlus) {
      IDNS |= Decl::IDNS_Tag | Decl::IDNS_Member | Decl::IDNS_Namespace;
      if (Redeclaration)
        IDNS |= Decl::IDNS_TagFriend | Decl::IDNS_OrdinaryFriend;
    }
    if (Redeclaration)
      IDNS |= Decl::IDNS_LocalExtern;
    break;

  case Sema::LookupOperatorName:
    // Operator lookup is its own crazy thing;  it is not the same
    // as (e.g.) looking up an operator name for redeclaration.
    assert(!Redeclaration && "cannot do redeclaration operator lookup");
    IDNS = Decl::IDNS_NonMemberOperator;
    break;

  case Sema::LookupTagName:
    if (CPlusPlus) {
      IDNS = Decl::IDNS_Type;

      // When looking for a redeclaration of a tag name, we add:
      // 1) TagFriend to find undeclared friend decls
      // 2) Namespace because they can't "overload" with tag decls.
      // 3) Tag because it includes class templates, which can't
      //    "overload" with tag decls.
      if (Redeclaration)
        IDNS |= Decl::IDNS_Tag | Decl::IDNS_TagFriend | Decl::IDNS_Namespace;
    } else {
      IDNS = Decl::IDNS_Tag;
    }
    break;

  case Sema::LookupLabel:
    IDNS = Decl::IDNS_Label;
    break;

  case Sema::LookupMemberName:
    IDNS = Decl::IDNS_Member;
    if (CPlusPlus)
      IDNS |= Decl::IDNS_Tag | Decl::IDNS_Ordinary;
    break;

  case Sema::LookupNestedNameSpecifierName:
    IDNS = Decl::IDNS_Type | Decl::IDNS_Namespace;
    break;

  case Sema::LookupNamespaceName:
    IDNS = Decl::IDNS_Namespace;
    break;

  case Sema::LookupUsingDeclName:
    assert(Redeclaration && "should only be used for redecl lookup");
    IDNS = Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Member |
           Decl::IDNS_Using | Decl::IDNS_TagFriend | Decl::IDNS_OrdinaryFriend |
           Decl::IDNS_LocalExtern;
    break;

  case Sema::LookupObjCProtocolName:
    IDNS = Decl::IDNS_ObjCProtocol;
    break;

  case Sema::LookupOMPReductionName:
    IDNS = Decl::IDNS_OMPReduction;
    break;

  case Sema::LookupOMPMapperName:
    IDNS = Decl::IDNS_OMPMapper;
    break;

  case Sema::LookupAnyName:
    IDNS = Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Member
      | Decl::IDNS_Using | Decl::IDNS_Namespace | Decl::IDNS_ObjCProtocol
      | Decl::IDNS_Type;
    break;
  }
  return IDNS;
}

void LookupResult::configure() {
  IDNS = getIDNS(LookupKind, getSema().getLangOpts().CPlusPlus,
                 isForRedeclaration());

  // If we're looking for one of the allocation or deallocation
  // operators, make sure that the implicitly-declared new and delete
  // operators can be found.
  switch (NameInfo.getName().getCXXOverloadedOperator()) {
  case OO_New:
  case OO_Delete:
  case OO_Array_New:
  case OO_Array_Delete:
    getSema().DeclareGlobalNewDelete();
    break;

  default:
    break;
  }

  // Compiler builtins are always visible, regardless of where they end
  // up being declared.
  if (IdentifierInfo *Id = NameInfo.getName().getAsIdentifierInfo()) {
    if (unsigned BuiltinID = Id->getBuiltinID()) {
      if (!getSema().Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))
        AllowHidden = true;
    }
  }
}

bool LookupResult::checkDebugAssumptions() const {
  // This function is never called by NDEBUG builds.
  assert(ResultKind != NotFound || Decls.size() == 0);
  assert(ResultKind != Found || Decls.size() == 1);
  assert(ResultKind != FoundOverloaded || Decls.size() > 1 ||
         (Decls.size() == 1 &&
          isa<FunctionTemplateDecl>((*begin())->getUnderlyingDecl())));
  assert(ResultKind != FoundUnresolvedValue || checkUnresolved());
  assert(ResultKind != Ambiguous || Decls.size() > 1 ||
         (Decls.size() == 1 && (Ambiguity == AmbiguousBaseSubobjects ||
                                Ambiguity == AmbiguousBaseSubobjectTypes)));
  assert((Paths != nullptr) == (ResultKind == Ambiguous &&
                                (Ambiguity == AmbiguousBaseSubobjectTypes ||
                                 Ambiguity == AmbiguousBaseSubobjects)));
  return true;
}

// Necessary because CXXBasePaths is not complete in Sema.h
void LookupResult::deletePaths(CXXBasePaths *Paths) {
  delete Paths;
}

/// Get a representative context for a declaration such that two declarations
/// will have the same context if they were found within the same scope.
static const DeclContext *getContextForScopeMatching(const Decl *D) {
  // For function-local declarations, use that function as the context. This
  // doesn't account for scopes within the function; the caller must deal with
  // those.
  if (const DeclContext *DC = D->getLexicalDeclContext();
      DC->isFunctionOrMethod())
    return DC;

  // Otherwise, look at the semantic context of the declaration. The
  // declaration must have been found there.
  return D->getDeclContext()->getRedeclContext();
}

/// Determine whether \p D is a better lookup result than \p Existing,
/// given that they declare the same entity.
static bool isPreferredLookupResult(Sema &S, Sema::LookupNameKind Kind,
                                    const NamedDecl *D,
                                    const NamedDecl *Existing) {
  // When looking up redeclarations of a using declaration, prefer a using
  // shadow declaration over any other declaration of the same entity.
  if (Kind == Sema::LookupUsingDeclName && isa<UsingShadowDecl>(D) &&
      !isa<UsingShadowDecl>(Existing))
    return true;

  const auto *DUnderlying = D->getUnderlyingDecl();
  const auto *EUnderlying = Existing->getUnderlyingDecl();

  // If they have different underlying declarations, prefer a typedef over the
  // original type (this happens when two type declarations denote the same
  // type), per a generous reading of C++ [dcl.typedef]p3 and p4. The typedef
  // might carry additional semantic information, such as an alignment override.
  // However, per C++ [dcl.typedef]p5, when looking up a tag name, prefer a tag
  // declaration over a typedef. Also prefer a tag over a typedef for
  // destructor name lookup because in some contexts we only accept a
  // class-name in a destructor declaration.
  if (DUnderlying->getCanonicalDecl() != EUnderlying->getCanonicalDecl()) {
    assert(isa<TypeDecl>(DUnderlying) && isa<TypeDecl>(EUnderlying));
    bool HaveTag = isa<TagDecl>(EUnderlying);
    bool WantTag =
        Kind == Sema::LookupTagName || Kind == Sema::LookupDestructorName;
    return HaveTag != WantTag;
  }

  // Pick the function with more default arguments.
  // FIXME: In the presence of ambiguous default arguments, we should keep both,
  //        so we can diagnose the ambiguity if the default argument is needed.
  //        See C++ [over.match.best]p3.
  if (const auto *DFD = dyn_cast<FunctionDecl>(DUnderlying)) {
    const auto *EFD = cast<FunctionDecl>(EUnderlying);
    unsigned DMin = DFD->getMinRequiredArguments();
    unsigned EMin = EFD->getMinRequiredArguments();
    // If D has more default arguments, it is preferred.
    if (DMin != EMin)
      return DMin < EMin;
    // FIXME: When we track visibility for default function arguments, check
    // that we pick the declaration with more visible default arguments.
  }

  // Pick the template with more default template arguments.
  if (const auto *DTD = dyn_cast<TemplateDecl>(DUnderlying)) {
    const auto *ETD = cast<TemplateDecl>(EUnderlying);
    unsigned DMin = DTD->getTemplateParameters()->getMinRequiredArguments();
    unsigned EMin = ETD->getTemplateParameters()->getMinRequiredArguments();
    // If D has more default arguments, it is preferred. Note that default
    // arguments (and their visibility) is monotonically increasing across the
    // redeclaration chain, so this is a quick proxy for "is more recent".
    if (DMin != EMin)
      return DMin < EMin;
    // If D has more *visible* default arguments, it is preferred. Note, an
    // earlier default argument being visible does not imply that a later
    // default argument is visible, so we can't just check the first one.
    for (unsigned I = DMin, N = DTD->getTemplateParameters()->size();
        I != N; ++I) {
      if (!S.hasVisibleDefaultArgument(
              ETD->getTemplateParameters()->getParam(I)) &&
          S.hasVisibleDefaultArgument(
              DTD->getTemplateParameters()->getParam(I)))
        return true;
    }
  }

  // VarDecl can have incomplete array types, prefer the one with more complete
  // array type.
  if (const auto *DVD = dyn_cast<VarDecl>(DUnderlying)) {
    const auto *EVD = cast<VarDecl>(EUnderlying);
    if (EVD->getType()->isIncompleteType() &&
        !DVD->getType()->isIncompleteType()) {
      // Prefer the decl with a more complete type if visible.
      return S.isVisible(DVD);
    }
    return false; // Avoid picking up a newer decl, just because it was newer.
  }

  // For most kinds of declaration, it doesn't really matter which one we pick.
  if (!isa<FunctionDecl>(DUnderlying) && !isa<VarDecl>(DUnderlying)) {
    // If the existing declaration is hidden, prefer the new one. Otherwise,
    // keep what we've got.
    return !S.isVisible(Existing);
  }

  // Pick the newer declaration; it might have a more precise type.
  for (const Decl *Prev = DUnderlying->getPreviousDecl(); Prev;
       Prev = Prev->getPreviousDecl())
    if (Prev == EUnderlying)
      return true;
  return false;
}

/// Determine whether \p D can hide a tag declaration.
static bool canHideTag(const NamedDecl *D) {
  // C++ [basic.scope.declarative]p4:
  //   Given a set of declarations in a single declarative region [...]
  //   exactly one declaration shall declare a class name or enumeration name
  //   that is not a typedef name and the other declarations shall all refer to
  //   the same variable, non-static data member, or enumerator, or all refer
  //   to functions and function templates; in this case the class name or
  //   enumeration name is hidden.
  // C++ [basic.scope.hiding]p2:
  //   A class name or enumeration name can be hidden by the name of a
  //   variable, data member, function, or enumerator declared in the same
  //   scope.
  // An UnresolvedUsingValueDecl always instantiates to one of these.
  D = D->getUnderlyingDecl();
  return isa<VarDecl>(D) || isa<EnumConstantDecl>(D) || isa<FunctionDecl>(D) ||
         isa<FunctionTemplateDecl>(D) || isa<FieldDecl>(D) ||
         isa<UnresolvedUsingValueDecl>(D);
}

/// Resolves the result kind of this lookup.
void LookupResult::resolveKind() {
  unsigned N = Decls.size();

  // Fast case: no possible ambiguity.
  if (N == 0) {
    assert(ResultKind == NotFound ||
           ResultKind == NotFoundInCurrentInstantiation);
    return;
  }

  // If there's a single decl, we need to examine it to decide what
  // kind of lookup this is.
  if (N == 1) {
    const NamedDecl *D = (*Decls.begin())->getUnderlyingDecl();
    if (isa<FunctionTemplateDecl>(D))
      ResultKind = FoundOverloaded;
    else if (isa<UnresolvedUsingValueDecl>(D))
      ResultKind = FoundUnresolvedValue;
    return;
  }

  // Don't do any extra resolution if we've already resolved as ambiguous.
  if (ResultKind == Ambiguous) return;

  llvm::SmallDenseMap<const NamedDecl *, unsigned, 16> Unique;
  llvm::SmallDenseMap<QualType, unsigned, 16> UniqueTypes;

  bool Ambiguous = false;
  bool ReferenceToPlaceHolderVariable = false;
  bool HasTag = false, HasFunction = false;
  bool HasFunctionTemplate = false, HasUnresolved = false;
  const NamedDecl *HasNonFunction = nullptr;

  llvm::SmallVector<const NamedDecl *, 4> EquivalentNonFunctions;
  llvm::BitVector RemovedDecls(N);

  for (unsigned I = 0; I < N; I++) {
    const NamedDecl *D = Decls[I]->getUnderlyingDecl();
    D = cast<NamedDecl>(D->getCanonicalDecl());

    // Ignore an invalid declaration unless it's the only one left.
    // Also ignore HLSLBufferDecl which not have name conflict with other Decls.
    if ((D->isInvalidDecl() || isa<HLSLBufferDecl>(D)) &&
        N - RemovedDecls.count() > 1) {
      RemovedDecls.set(I);
      continue;
    }

    // C++ [basic.scope.hiding]p2:
    //   A class name or enumeration name can be hidden by the name of
    //   an object, function, or enumerator declared in the same
    //   scope. If a class or enumeration name and an object, function,
    //   or enumerator are declared in the same scope (in any order)
    //   with the same name, the class or enumeration name is hidden
    //   wherever the object, function, or enumerator name is visible.
    if (HideTags && isa<TagDecl>(D)) {
      bool Hidden = false;
      for (auto *OtherDecl : Decls) {
        if (canHideTag(OtherDecl) && !OtherDecl->isInvalidDecl() &&
            getContextForScopeMatching(OtherDecl)->Equals(
                getContextForScopeMatching(Decls[I]))) {
          RemovedDecls.set(I);
          Hidden = true;
          break;
        }
      }
      if (Hidden)
        continue;
    }

    std::optional<unsigned> ExistingI;

    // Redeclarations of types via typedef can occur both within a scope
    // and, through using declarations and directives, across scopes. There is
    // no ambiguity if they all refer to the same type, so unique based on the
    // canonical type.
    if (const auto *TD = dyn_cast<TypeDecl>(D)) {
      QualType T = getSema().Context.getTypeDeclType(TD);
      auto UniqueResult = UniqueTypes.insert(
          std::make_pair(getSema().Context.getCanonicalType(T), I));
      if (!UniqueResult.second) {
        // The type is not unique.
        ExistingI = UniqueResult.first->second;
      }
    }

    // For non-type declarations, check for a prior lookup result naming this
    // canonical declaration.
    if (!ExistingI) {
      auto UniqueResult = Unique.insert(std::make_pair(D, I));
      if (!UniqueResult.second) {
        // We've seen this entity before.
        ExistingI = UniqueResult.first->second;
      }
    }

    if (ExistingI) {
      // This is not a unique lookup result. Pick one of the results and
      // discard the other.
      if (isPreferredLookupResult(getSema(), getLookupKind(), Decls[I],
                                  Decls[*ExistingI]))
        Decls[*ExistingI] = Decls[I];
      RemovedDecls.set(I);
      continue;
    }

    // Otherwise, do some decl type analysis and then continue.

    if (isa<UnresolvedUsingValueDecl>(D)) {
      HasUnresolved = true;
    } else if (isa<TagDecl>(D)) {
      if (HasTag)
        Ambiguous = true;
      HasTag = true;
    } else if (isa<FunctionTemplateDecl>(D)) {
      HasFunction = true;
      HasFunctionTemplate = true;
    } else if (isa<FunctionDecl>(D)) {
      HasFunction = true;
    } else {
      if (HasNonFunction) {
        // If we're about to create an ambiguity between two declarations that
        // are equivalent, but one is an internal linkage declaration from one
        // module and the other is an internal linkage declaration from another
        // module, just skip it.
        if (getSema().isEquivalentInternalLinkageDeclaration(HasNonFunction,
                                                             D)) {
          EquivalentNonFunctions.push_back(D);
          RemovedDecls.set(I);
          continue;
        }
        if (D->isPlaceholderVar(getSema().getLangOpts()) &&
            getContextForScopeMatching(D) ==
                getContextForScopeMatching(Decls[I])) {
          ReferenceToPlaceHolderVariable = true;
        }
        Ambiguous = true;
      }
      HasNonFunction = D;
    }
  }

  // FIXME: This diagnostic should really be delayed until we're done with
  // the lookup result, in case the ambiguity is resolved by the caller.
  if (!EquivalentNonFunctions.empty() && !Ambiguous)
    getSema().diagnoseEquivalentInternalLinkageDeclarations(
        getNameLoc(), HasNonFunction, EquivalentNonFunctions);

  // Remove decls by replacing them with decls from the end (which
  // means that we need to iterate from the end) and then truncating
  // to the new size.
  for (int I = RemovedDecls.find_last(); I >= 0; I = RemovedDecls.find_prev(I))
    Decls[I] = Decls[--N];
  Decls.truncate(N);

  if ((HasNonFunction && (HasFunction || HasUnresolved)) ||
      (HideTags && HasTag && (HasFunction || HasNonFunction || HasUnresolved)))
    Ambiguous = true;

  if (Ambiguous && ReferenceToPlaceHolderVariable)
    setAmbiguous(LookupResult::AmbiguousReferenceToPlaceholderVariable);
  else if (Ambiguous)
    setAmbiguous(LookupResult::AmbiguousReference);
  else if (HasUnresolved)
    ResultKind = LookupResult::FoundUnresolvedValue;
  else if (N > 1 || HasFunctionTemplate)
    ResultKind = LookupResult::FoundOverloaded;
  else
    ResultKind = LookupResult::Found;
}

void LookupResult::addDeclsFromBasePaths(const CXXBasePaths &P) {
  CXXBasePaths::const_paths_iterator I, E;
  for (I = P.begin(), E = P.end(); I != E; ++I)
    for (DeclContext::lookup_iterator DI = I->Decls, DE = DI.end(); DI != DE;
         ++DI)
      addDecl(*DI);
}

void LookupResult::setAmbiguousBaseSubobjects(CXXBasePaths &P) {
  Paths = new CXXBasePaths;
  Paths->swap(P);
  addDeclsFromBasePaths(*Paths);
  resolveKind();
  setAmbiguous(AmbiguousBaseSubobjects);
}

void LookupResult::setAmbiguousBaseSubobjectTypes(CXXBasePaths &P) {
  Paths = new CXXBasePaths;
  Paths->swap(P);
  addDeclsFromBasePaths(*Paths);
  resolveKind();
  setAmbiguous(AmbiguousBaseSubobjectTypes);
}

void LookupResult::print(raw_ostream &Out) {
  Out << Decls.size() << " result(s)";
  if (isAmbiguous()) Out << ", ambiguous";
  if (Paths) Out << ", base paths present";

  for (iterator I = begin(), E = end(); I != E; ++I) {
    Out << "\n";
    (*I)->print(Out, 2);
  }
}

LLVM_DUMP_METHOD void LookupResult::dump() {
  llvm::errs() << "lookup results for " << getLookupName().getAsString()
               << ":\n";
  for (NamedDecl *D : *this)
    D->dump();
}

/// Diagnose a missing builtin type.
static QualType diagOpenCLBuiltinTypeError(Sema &S, llvm::StringRef TypeClass,
                                           llvm::StringRef Name) {
  S.Diag(SourceLocation(), diag::err_opencl_type_not_found)
      << TypeClass << Name;
  return S.Context.VoidTy;
}

/// Lookup an OpenCL enum type.
static QualType getOpenCLEnumType(Sema &S, llvm::StringRef Name) {
  LookupResult Result(S, &S.Context.Idents.get(Name), SourceLocation(),
                      Sema::LookupTagName);
  S.LookupName(Result, S.TUScope);
  if (Result.empty())
    return diagOpenCLBuiltinTypeError(S, "enum", Name);
  EnumDecl *Decl = Result.getAsSingle<EnumDecl>();
  if (!Decl)
    return diagOpenCLBuiltinTypeError(S, "enum", Name);
  return S.Context.getEnumType(Decl);
}

/// Lookup an OpenCL typedef type.
static QualType getOpenCLTypedefType(Sema &S, llvm::StringRef Name) {
  LookupResult Result(S, &S.Context.Idents.get(Name), SourceLocation(),
                      Sema::LookupOrdinaryName);
  S.LookupName(Result, S.TUScope);
  if (Result.empty())
    return diagOpenCLBuiltinTypeError(S, "typedef", Name);
  TypedefNameDecl *Decl = Result.getAsSingle<TypedefNameDecl>();
  if (!Decl)
    return diagOpenCLBuiltinTypeError(S, "typedef", Name);
  return S.Context.getTypedefType(Decl);
}

/// Get the QualType instances of the return type and arguments for an OpenCL
/// builtin function signature.
/// \param S (in) The Sema instance.
/// \param OpenCLBuiltin (in) The signature currently handled.
/// \param GenTypeMaxCnt (out) Maximum number of types contained in a generic
///        type used as return type or as argument.
///        Only meaningful for generic types, otherwise equals 1.
/// \param RetTypes (out) List of the possible return types.
/// \param ArgTypes (out) List of the possible argument types.  For each
///        argument, ArgTypes contains QualTypes for the Cartesian product
///        of (vector sizes) x (types) .
static void GetQualTypesForOpenCLBuiltin(
    Sema &S, const OpenCLBuiltinStruct &OpenCLBuiltin, unsigned &GenTypeMaxCnt,
    SmallVector<QualType, 1> &RetTypes,
    SmallVector<SmallVector<QualType, 1>, 5> &ArgTypes) {
  // Get the QualType instances of the return types.
  unsigned Sig = SignatureTable[OpenCLBuiltin.SigTableIndex];
  OCL2Qual(S, TypeTable[Sig], RetTypes);
  GenTypeMaxCnt = RetTypes.size();

  // Get the QualType instances of the arguments.
  // First type is the return type, skip it.
  for (unsigned Index = 1; Index < OpenCLBuiltin.NumTypes; Index++) {
    SmallVector<QualType, 1> Ty;
    OCL2Qual(S, TypeTable[SignatureTable[OpenCLBuiltin.SigTableIndex + Index]],
             Ty);
    GenTypeMaxCnt = (Ty.size() > GenTypeMaxCnt) ? Ty.size() : GenTypeMaxCnt;
    ArgTypes.push_back(std::move(Ty));
  }
}

/// Create a list of the candidate function overloads for an OpenCL builtin
/// function.
/// \param Context (in) The ASTContext instance.
/// \param GenTypeMaxCnt (in) Maximum number of types contained in a generic
///        type used as return type or as argument.
///        Only meaningful for generic types, otherwise equals 1.
/// \param FunctionList (out) List of FunctionTypes.
/// \param RetTypes (in) List of the possible return types.
/// \param ArgTypes (in) List of the possible types for the arguments.
static void GetOpenCLBuiltinFctOverloads(
    ASTContext &Context, unsigned GenTypeMaxCnt,
    std::vector<QualType> &FunctionList, SmallVector<QualType, 1> &RetTypes,
    SmallVector<SmallVector<QualType, 1>, 5> &ArgTypes) {
  FunctionProtoType::ExtProtoInfo PI(
      Context.getDefaultCallingConvention(false, false, true));
  PI.Variadic = false;

  // Do not attempt to create any FunctionTypes if there are no return types,
  // which happens when a type belongs to a disabled extension.
  if (RetTypes.size() == 0)
    return;

  // Create FunctionTypes for each (gen)type.
  for (unsigned IGenType = 0; IGenType < GenTypeMaxCnt; IGenType++) {
    SmallVector<QualType, 5> ArgList;

    for (unsigned A = 0; A < ArgTypes.size(); A++) {
      // Bail out if there is an argument that has no available types.
      if (ArgTypes[A].size() == 0)
        return;

      // Builtins such as "max" have an "sgentype" argument that represents
      // the corresponding scalar type of a gentype.  The number of gentypes
      // must be a multiple of the number of sgentypes.
      assert(GenTypeMaxCnt % ArgTypes[A].size() == 0 &&
             "argument type count not compatible with gentype type count");
      unsigned Idx = IGenType % ArgTypes[A].size();
      ArgList.push_back(ArgTypes[A][Idx]);
    }

    FunctionList.push_back(Context.getFunctionType(
        RetTypes[(RetTypes.size() != 1) ? IGenType : 0], ArgList, PI));
  }
}

/// When trying to resolve a function name, if isOpenCLBuiltin() returns a
/// non-null <Index, Len> pair, then the name is referencing an OpenCL
/// builtin function.  Add all candidate signatures to the LookUpResult.
///
/// \param S (in) The Sema instance.
/// \param LR (inout) The LookupResult instance.
/// \param II (in) The identifier being resolved.
/// \param FctIndex (in) Starting index in the BuiltinTable.
/// \param Len (in) The signature list has Len elements.
static void InsertOCLBuiltinDeclarationsFromTable(Sema &S, LookupResult &LR,
                                                  IdentifierInfo *II,
                                                  const unsigned FctIndex,
                                                  const unsigned Len) {
  // The builtin function declaration uses generic types (gentype).
  bool HasGenType = false;

  // Maximum number of types contained in a generic type used as return type or
  // as argument.  Only meaningful for generic types, otherwise equals 1.
  unsigned GenTypeMaxCnt;

  ASTContext &Context = S.Context;

  for (unsigned SignatureIndex = 0; SignatureIndex < Len; SignatureIndex++) {
    const OpenCLBuiltinStruct &OpenCLBuiltin =
        BuiltinTable[FctIndex + SignatureIndex];

    // Ignore this builtin function if it is not available in the currently
    // selected language version.
    if (!isOpenCLVersionContainedInMask(Context.getLangOpts(),
                                        OpenCLBuiltin.Versions))
      continue;

    // Ignore this builtin function if it carries an extension macro that is
    // not defined. This indicates that the extension is not supported by the
    // target, so the builtin function should not be available.
    StringRef Extensions = FunctionExtensionTable[OpenCLBuiltin.Extension];
    if (!Extensions.empty()) {
      SmallVector<StringRef, 2> ExtVec;
      Extensions.split(ExtVec, " ");
      bool AllExtensionsDefined = true;
      for (StringRef Ext : ExtVec) {
        if (!S.getPreprocessor().isMacroDefined(Ext)) {
          AllExtensionsDefined = false;
          break;
        }
      }
      if (!AllExtensionsDefined)
        continue;
    }

    SmallVector<QualType, 1> RetTypes;
    SmallVector<SmallVector<QualType, 1>, 5> ArgTypes;

    // Obtain QualType lists for the function signature.
    GetQualTypesForOpenCLBuiltin(S, OpenCLBuiltin, GenTypeMaxCnt, RetTypes,
                                 ArgTypes);
    if (GenTypeMaxCnt > 1) {
      HasGenType = true;
    }

    // Create function overload for each type combination.
    std::vector<QualType> FunctionList;
    GetOpenCLBuiltinFctOverloads(Context, GenTypeMaxCnt, FunctionList, RetTypes,
                                 ArgTypes);

    SourceLocation Loc = LR.getNameLoc();
    DeclContext *Parent = Context.getTranslationUnitDecl();
    FunctionDecl *NewOpenCLBuiltin;

    for (const auto &FTy : FunctionList) {
      NewOpenCLBuiltin = FunctionDecl::Create(
          Context, Parent, Loc, Loc, II, FTy, /*TInfo=*/nullptr, SC_Extern,
          S.getCurFPFeatures().isFPConstrained(), false,
          FTy->isFunctionProtoType());
      NewOpenCLBuiltin->setImplicit();

      // Create Decl objects for each parameter, adding them to the
      // FunctionDecl.
      const auto *FP = cast<FunctionProtoType>(FTy);
      SmallVector<ParmVarDecl *, 4> ParmList;
      for (unsigned IParm = 0, e = FP->getNumParams(); IParm != e; ++IParm) {
        ParmVarDecl *Parm = ParmVarDecl::Create(
            Context, NewOpenCLBuiltin, SourceLocation(), SourceLocation(),
            nullptr, FP->getParamType(IParm), nullptr, SC_None, nullptr);
        Parm->setScopeInfo(0, IParm);
        ParmList.push_back(Parm);
      }
      NewOpenCLBuiltin->setParams(ParmList);

      // Add function attributes.
      if (OpenCLBuiltin.IsPure)
        NewOpenCLBuiltin->addAttr(PureAttr::CreateImplicit(Context));
      if (OpenCLBuiltin.IsConst)
        NewOpenCLBuiltin->addAttr(ConstAttr::CreateImplicit(Context));
      if (OpenCLBuiltin.IsConv)
        NewOpenCLBuiltin->addAttr(ConvergentAttr::CreateImplicit(Context));

      if (!S.getLangOpts().OpenCLCPlusPlus)
        NewOpenCLBuiltin->addAttr(OverloadableAttr::CreateImplicit(Context));

      LR.addDecl(NewOpenCLBuiltin);
    }
  }

  // If we added overloads, need to resolve the lookup result.
  if (Len > 1 || HasGenType)
    LR.resolveKind();
}

bool Sema::LookupBuiltin(LookupResult &R) {
  Sema::LookupNameKind NameKind = R.getLookupKind();

  // If we didn't find a use of this identifier, and if the identifier
  // corresponds to a compiler builtin, create the decl object for the builtin
  // now, injecting it into translation unit scope, and return it.
  if (NameKind == Sema::LookupOrdinaryName ||
      NameKind == Sema::LookupRedeclarationWithLinkage) {
    IdentifierInfo *II = R.getLookupName().getAsIdentifierInfo();
    if (II) {
      if (getLangOpts().CPlusPlus && NameKind == Sema::LookupOrdinaryName) {
        if (II == getASTContext().getMakeIntegerSeqName()) {
          R.addDecl(getASTContext().getMakeIntegerSeqDecl());
          return true;
        } else if (II == getASTContext().getTypePackElementName()) {
          R.addDecl(getASTContext().getTypePackElementDecl());
          return true;
        }
      }

      // Check if this is an OpenCL Builtin, and if so, insert its overloads.
      if (getLangOpts().OpenCL && getLangOpts().DeclareOpenCLBuiltins) {
        auto Index = isOpenCLBuiltin(II->getName());
        if (Index.first) {
          InsertOCLBuiltinDeclarationsFromTable(*this, R, II, Index.first - 1,
                                                Index.second);
          return true;
        }
      }

      if (RISCV().DeclareRVVBuiltins || RISCV().DeclareSiFiveVectorBuiltins) {
        if (!RISCV().IntrinsicManager)
          RISCV().IntrinsicManager = CreateRISCVIntrinsicManager(*this);

        RISCV().IntrinsicManager->InitIntrinsicList();

        if (RISCV().IntrinsicManager->CreateIntrinsicIfFound(R, II, PP))
          return true;
      }

      // If this is a builtin on this (or all) targets, create the decl.
      if (unsigned BuiltinID = II->getBuiltinID()) {
        // In C++ and OpenCL (spec v1.2 s6.9.f), we don't have any predefined
        // library functions like 'malloc'. Instead, we'll just error.
        if ((getLangOpts().CPlusPlus || getLangOpts().OpenCL) &&
            Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))
          return false;

        if (NamedDecl *D =
                LazilyCreateBuiltin(II, BuiltinID, TUScope,
                                    R.isForRedeclaration(), R.getNameLoc())) {
          R.addDecl(D);
          return true;
        }
      }
    }
  }

  return false;
}

/// Looks up the declaration of "struct objc_super" and
/// saves it for later use in building builtin declaration of
/// objc_msgSendSuper and objc_msgSendSuper_stret.
static void LookupPredefedObjCSuperType(Sema &Sema, Scope *S) {
  ASTContext &Context = Sema.Context;
  LookupResult Result(Sema, &Context.Idents.get("objc_super"), SourceLocation(),
                      Sema::LookupTagName);
  Sema.LookupName(Result, S);
  if (Result.getResultKind() == LookupResult::Found)
    if (const TagDecl *TD = Result.getAsSingle<TagDecl>())
      Context.setObjCSuperType(Context.getTagDeclType(TD));
}

void Sema::LookupNecessaryTypesForBuiltin(Scope *S, unsigned ID) {
  if (ID == Builtin::BIobjc_msgSendSuper)
    LookupPredefedObjCSuperType(*this, S);
}

/// Determine whether we can declare a special member function within
/// the class at this point.
static bool CanDeclareSpecialMemberFunction(const CXXRecordDecl *Class) {
  // We need to have a definition for the class.
  if (!Class->getDefinition() || Class->isDependentContext())
    return false;

  // We can't be in the middle of defining the class.
  return !Class->isBeingDefined();
}

void Sema::ForceDeclarationOfImplicitMembers(CXXRecordDecl *Class) {
  if (!CanDeclareSpecialMemberFunction(Class))
    return;

  // If the default constructor has not yet been declared, do so now.
  if (Class->needsImplicitDefaultConstructor())
    DeclareImplicitDefaultConstructor(Class);

  // If the copy constructor has not yet been declared, do so now.
  if (Class->needsImplicitCopyConstructor())
    DeclareImplicitCopyConstructor(Class);

  // If the copy assignment operator has not yet been declared, do so now.
  if (Class->needsImplicitCopyAssignment())
    DeclareImplicitCopyAssignment(Class);

  if (getLangOpts().CPlusPlus11) {
    // If the move constructor has not yet been declared, do so now.
    if (Class->needsImplicitMoveConstructor())
      DeclareImplicitMoveConstructor(Class);

    // If the move assignment operator has not yet been declared, do so now.
    if (Class->needsImplicitMoveAssignment())
      DeclareImplicitMoveAssignment(Class);
  }

  // If the destructor has not yet been declared, do so now.
  if (Class->needsImplicitDestructor())
    DeclareImplicitDestructor(Class);
}

/// Determine whether this is the name of an implicitly-declared
/// special member function.
static bool isImplicitlyDeclaredMemberFunctionName(DeclarationName Name) {
  switch (Name.getNameKind()) {
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
    return true;

  case DeclarationName::CXXOperatorName:
    return Name.getCXXOverloadedOperator() == OO_Equal;

  default:
    break;
  }

  return false;
}

/// If there are any implicit member functions with the given name
/// that need to be declared in the given declaration context, do so.
static void DeclareImplicitMemberFunctionsWithName(Sema &S,
                                                   DeclarationName Name,
                                                   SourceLocation Loc,
                                                   const DeclContext *DC) {
  if (!DC)
    return;

  switch (Name.getNameKind()) {
  case DeclarationName::CXXConstructorName:
    if (const CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(DC))
      if (Record->getDefinition() && CanDeclareSpecialMemberFunction(Record)) {
        CXXRecordDecl *Class = const_cast<CXXRecordDecl *>(Record);
        if (Record->needsImplicitDefaultConstructor())
          S.DeclareImplicitDefaultConstructor(Class);
        if (Record->needsImplicitCopyConstructor())
          S.DeclareImplicitCopyConstructor(Class);
        if (S.getLangOpts().CPlusPlus11 &&
            Record->needsImplicitMoveConstructor())
          S.DeclareImplicitMoveConstructor(Class);
      }
    break;

  case DeclarationName::CXXDestructorName:
    if (const CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(DC))
      if (Record->getDefinition() && Record->needsImplicitDestructor() &&
          CanDeclareSpecialMemberFunction(Record))
        S.DeclareImplicitDestructor(const_cast<CXXRecordDecl *>(Record));
    break;

  case DeclarationName::CXXOperatorName:
    if (Name.getCXXOverloadedOperator() != OO_Equal)
      break;

    if (const CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(DC)) {
      if (Record->getDefinition() && CanDeclareSpecialMemberFunction(Record)) {
        CXXRecordDecl *Class = const_cast<CXXRecordDecl *>(Record);
        if (Record->needsImplicitCopyAssignment())
          S.DeclareImplicitCopyAssignment(Class);
        if (S.getLangOpts().CPlusPlus11 &&
            Record->needsImplicitMoveAssignment())
          S.DeclareImplicitMoveAssignment(Class);
      }
    }
    break;

  case DeclarationName::CXXDeductionGuideName:
    S.DeclareImplicitDeductionGuides(Name.getCXXDeductionGuideTemplate(), Loc);
    break;

  default:
    break;
  }
}

// Adds all qualifying matches for a name within a decl context to the
// given lookup result.  Returns true if any matches were found.
static bool LookupDirect(Sema &S, LookupResult &R, const DeclContext *DC) {
  bool Found = false;

  // Lazily declare C++ special member functions.
  if (S.getLangOpts().CPlusPlus)
    DeclareImplicitMemberFunctionsWithName(S, R.getLookupName(), R.getNameLoc(),
                                           DC);

  // Perform lookup into this declaration context.
  DeclContext::lookup_result DR = DC->lookup(R.getLookupName());
  for (NamedDecl *D : DR) {
    if ((D = R.getAcceptableDecl(D))) {
      R.addDecl(D);
      Found = true;
    }
  }

  if (!Found && DC->isTranslationUnit() && S.LookupBuiltin(R))
    return true;

  if (R.getLookupName().getNameKind()
        != DeclarationName::CXXConversionFunctionName ||
      R.getLookupName().getCXXNameType()->isDependentType() ||
      !isa<CXXRecordDecl>(DC))
    return Found;

  // C++ [temp.mem]p6:
  //   A specialization of a conversion function template is not found by
  //   name lookup. Instead, any conversion function templates visible in the
  //   context of the use are considered. [...]
  const CXXRecordDecl *Record = cast<CXXRecordDecl>(DC);
  if (!Record->isCompleteDefinition())
    return Found;

  // For conversion operators, 'operator auto' should only match
  // 'operator auto'.  Since 'auto' is not a type, it shouldn't be considered
  // as a candidate for template substitution.
  auto *ContainedDeducedType =
      R.getLookupName().getCXXNameType()->getContainedDeducedType();
  if (R.getLookupName().getNameKind() ==
          DeclarationName::CXXConversionFunctionName &&
      ContainedDeducedType && ContainedDeducedType->isUndeducedType())
    return Found;

  for (CXXRecordDecl::conversion_iterator U = Record->conversion_begin(),
         UEnd = Record->conversion_end(); U != UEnd; ++U) {
    FunctionTemplateDecl *ConvTemplate = dyn_cast<FunctionTemplateDecl>(*U);
    if (!ConvTemplate)
      continue;

    // When we're performing lookup for the purposes of redeclaration, just
    // add the conversion function template. When we deduce template
    // arguments for specializations, we'll end up unifying the return
    // type of the new declaration with the type of the function template.
    if (R.isForRedeclaration()) {
      R.addDecl(ConvTemplate);
      Found = true;
      continue;
    }

    // C++ [temp.mem]p6:
    //   [...] For each such operator, if argument deduction succeeds
    //   (14.9.2.3), the resulting specialization is used as if found by
    //   name lookup.
    //
    // When referencing a conversion function for any purpose other than
    // a redeclaration (such that we'll be building an expression with the
    // result), perform template argument deduction and place the
    // specialization into the result set. We do this to avoid forcing all
    // callers to perform special deduction for conversion functions.
    TemplateDeductionInfo Info(R.getNameLoc());
    FunctionDecl *Specialization = nullptr;

    const FunctionProtoType *ConvProto
      = ConvTemplate->getTemplatedDecl()->getType()->getAs<FunctionProtoType>();
    assert(ConvProto && "Nonsensical conversion function template type");

    // Compute the type of the function that we would expect the conversion
    // function to have, if it were to match the name given.
    // FIXME: Calling convention!
    FunctionProtoType::ExtProtoInfo EPI = ConvProto->getExtProtoInfo();
    EPI.ExtInfo = EPI.ExtInfo.withCallingConv(CC_C);
    EPI.ExceptionSpec = EST_None;
    QualType ExpectedType = R.getSema().Context.getFunctionType(
        R.getLookupName().getCXXNameType(), std::nullopt, EPI);

    // Perform template argument deduction against the type that we would
    // expect the function to have.
    if (R.getSema().DeduceTemplateArguments(ConvTemplate, nullptr, ExpectedType,
                                            Specialization, Info) ==
        TemplateDeductionResult::Success) {
      R.addDecl(Specialization);
      Found = true;
    }
  }

  return Found;
}

// Performs C++ unqualified lookup into the given file context.
static bool CppNamespaceLookup(Sema &S, LookupResult &R, ASTContext &Context,
                               const DeclContext *NS,
                               UnqualUsingDirectiveSet &UDirs) {

  assert(NS && NS->isFileContext() && "CppNamespaceLookup() requires namespace!");

  // Perform direct name lookup into the LookupCtx.
  bool Found = LookupDirect(S, R, NS);

  // Perform direct name lookup into the namespaces nominated by the
  // using directives whose common ancestor is this namespace.
  for (const UnqualUsingEntry &UUE : UDirs.getNamespacesFor(NS))
    if (LookupDirect(S, R, UUE.getNominatedNamespace()))
      Found = true;

  R.resolveKind();

  return Found;
}

static bool isNamespaceOrTranslationUnitScope(Scope *S) {
  if (DeclContext *Ctx = S->getEntity())
    return Ctx->isFileContext();
  return false;
}

/// Find the outer declaration context from this scope. This indicates the
/// context that we should search up to (exclusive) before considering the
/// parent of the specified scope.
static DeclContext *findOuterContext(Scope *S) {
  for (Scope *OuterS = S->getParent(); OuterS; OuterS = OuterS->getParent())
    if (DeclContext *DC = OuterS->getLookupEntity())
      return DC;
  return nullptr;
}

namespace {
/// An RAII object to specify that we want to find block scope extern
/// declarations.
struct FindLocalExternScope {
  FindLocalExternScope(LookupResult &R)
      : R(R), OldFindLocalExtern(R.getIdentifierNamespace() &
                                 Decl::IDNS_LocalExtern) {
    R.setFindLocalExtern(R.getIdentifierNamespace() &
                         (Decl::IDNS_Ordinary | Decl::IDNS_NonMemberOperator));
  }
  void restore() {
    R.setFindLocalExtern(OldFindLocalExtern);
  }
  ~FindLocalExternScope() {
    restore();
  }
  LookupResult &R;
  bool OldFindLocalExtern;
};
} // end anonymous namespace

bool Sema::CppLookupName(LookupResult &R, Scope *S) {
  assert(getLangOpts().CPlusPlus && "Can perform only C++ lookup");

  DeclarationName Name = R.getLookupName();
  Sema::LookupNameKind NameKind = R.getLookupKind();

  // If this is the name of an implicitly-declared special member function,
  // go through the scope stack to implicitly declare
  if (isImplicitlyDeclaredMemberFunctionName(Name)) {
    for (Scope *PreS = S; PreS; PreS = PreS->getParent())
      if (DeclContext *DC = PreS->getEntity())
        DeclareImplicitMemberFunctionsWithName(*this, Name, R.getNameLoc(), DC);
  }

  // C++23 [temp.dep.general]p2:
  //   The component name of an unqualified-id is dependent if
  //   - it is a conversion-function-id whose conversion-type-id
  //     is dependent, or
  //   - it is operator= and the current class is a templated entity, or
  //   - the unqualified-id is the postfix-expression in a dependent call.
  if (Name.getNameKind() == DeclarationName::CXXConversionFunctionName &&
      Name.getCXXNameType()->isDependentType()) {
    R.setNotFoundInCurrentInstantiation();
    return false;
  }

  // Implicitly declare member functions with the name we're looking for, if in
  // fact we are in a scope where it matters.

  Scope *Initial = S;
  IdentifierResolver::iterator
    I = IdResolver.begin(Name),
    IEnd = IdResolver.end();

  // First we lookup local scope.
  // We don't consider using-directives, as per 7.3.4.p1 [namespace.udir]
  // ...During unqualified name lookup (3.4.1), the names appear as if
  // they were declared in the nearest enclosing namespace which contains
  // both the using-directive and the nominated namespace.
  // [Note: in this context, "contains" means "contains directly or
  // indirectly".
  //
  // For example:
  // namespace A { int i; }
  // void foo() {
  //   int i;
  //   {
  //     using namespace A;
  //     ++i; // finds local 'i', A::i appears at global scope
  //   }
  // }
  //
  UnqualUsingDirectiveSet UDirs(*this);
  bool VisitedUsingDirectives = false;
  bool LeftStartingScope = false;

  // When performing a scope lookup, we want to find local extern decls.
  FindLocalExternScope FindLocals(R);

  for (; S && !isNamespaceOrTranslationUnitScope(S); S = S->getParent()) {
    bool SearchNamespaceScope = true;
    // Check whether the IdResolver has anything in this scope.
    for (; I != IEnd && S->isDeclScope(*I); ++I) {
      if (NamedDecl *ND = R.getAcceptableDecl(*I)) {
        if (NameKind == LookupRedeclarationWithLinkage &&
            !(*I)->isTemplateParameter()) {
          // If it's a template parameter, we still find it, so we can diagnose
          // the invalid redeclaration.

          // Determine whether this (or a previous) declaration is
          // out-of-scope.
          if (!LeftStartingScope && !Initial->isDeclScope(*I))
            LeftStartingScope = true;

          // If we found something outside of our starting scope that
          // does not have linkage, skip it.
          if (LeftStartingScope && !((*I)->hasLinkage())) {
            R.setShadowed();
            continue;
          }
        } else {
          // We found something in this scope, we should not look at the
          // namespace scope
          SearchNamespaceScope = false;
        }
        R.addDecl(ND);
      }
    }
    if (!SearchNamespaceScope) {
      R.resolveKind();
      if (S->isClassScope())
        if (auto *Record = dyn_cast_if_present<CXXRecordDecl>(S->getEntity()))
          R.setNamingClass(Record);
      return true;
    }

    if (NameKind == LookupLocalFriendName && !S->isClassScope()) {
      // C++11 [class.friend]p11:
      //   If a friend declaration appears in a local class and the name
      //   specified is an unqualified name, a prior declaration is
      //   looked up without considering scopes that are outside the
      //   innermost enclosing non-class scope.
      return false;
    }

    if (DeclContext *Ctx = S->getLookupEntity()) {
      DeclContext *OuterCtx = findOuterContext(S);
      for (; Ctx && !Ctx->Equals(OuterCtx); Ctx = Ctx->getLookupParent()) {
        // We do not directly look into transparent contexts, since
        // those entities will be found in the nearest enclosing
        // non-transparent context.
        if (Ctx->isTransparentContext())
          continue;

        // We do not look directly into function or method contexts,
        // since all of the local variables and parameters of the
        // function/method are present within the Scope.
        if (Ctx->isFunctionOrMethod()) {
          // If we have an Objective-C instance method, look for ivars
          // in the corresponding interface.
          if (ObjCMethodDecl *Method = dyn_cast<ObjCMethodDecl>(Ctx)) {
            if (Method->isInstanceMethod() && Name.getAsIdentifierInfo())
              if (ObjCInterfaceDecl *Class = Method->getClassInterface()) {
                ObjCInterfaceDecl *ClassDeclared;
                if (ObjCIvarDecl *Ivar = Class->lookupInstanceVariable(
                                                 Name.getAsIdentifierInfo(),
                                                             ClassDeclared)) {
                  if (NamedDecl *ND = R.getAcceptableDecl(Ivar)) {
                    R.addDecl(ND);
                    R.resolveKind();
                    return true;
                  }
                }
              }
          }

          continue;
        }

        // If this is a file context, we need to perform unqualified name
        // lookup considering using directives.
        if (Ctx->isFileContext()) {
          // If we haven't handled using directives yet, do so now.
          if (!VisitedUsingDirectives) {
            // Add using directives from this context up to the top level.
            for (DeclContext *UCtx = Ctx; UCtx; UCtx = UCtx->getParent()) {
              if (UCtx->isTransparentContext())
                continue;

              UDirs.visit(UCtx, UCtx);
            }

            // Find the innermost file scope, so we can add using directives
            // from local scopes.
            Scope *InnermostFileScope = S;
            while (InnermostFileScope &&
                   !isNamespaceOrTranslationUnitScope(InnermostFileScope))
              InnermostFileScope = InnermostFileScope->getParent();
            UDirs.visitScopeChain(Initial, InnermostFileScope);

            UDirs.done();

            VisitedUsingDirectives = true;
          }

          if (CppNamespaceLookup(*this, R, Context, Ctx, UDirs)) {
            R.resolveKind();
            return true;
          }

          continue;
        }

        // Perform qualified name lookup into this context.
        // FIXME: In some cases, we know that every name that could be found by
        // this qualified name lookup will also be on the identifier chain. For
        // example, inside a class without any base classes, we never need to
        // perform qualified lookup because all of the members are on top of the
        // identifier chain.
        if (LookupQualifiedName(R, Ctx, /*InUnqualifiedLookup=*/true))
          return true;
      }
    }
  }

  // Stop if we ran out of scopes.
  // FIXME:  This really, really shouldn't be happening.
  if (!S) return false;

  // If we are looking for members, no need to look into global/namespace scope.
  if (NameKind == LookupMemberName)
    return false;

  // Collect UsingDirectiveDecls in all scopes, and recursively all
  // nominated namespaces by those using-directives.
  //
  // FIXME: Cache this sorted list in Scope structure, and DeclContext, so we
  // don't build it for each lookup!
  if (!VisitedUsingDirectives) {
    UDirs.visitScopeChain(Initial, S);
    UDirs.done();
  }

  // If we're not performing redeclaration lookup, do not look for local
  // extern declarations outside of a function scope.
  if (!R.isForRedeclaration())
    FindLocals.restore();

  // Lookup namespace scope, and global scope.
  // Unqualified name lookup in C++ requires looking into scopes
  // that aren't strictly lexical, and therefore we walk through the
  // context as well as walking through the scopes.
  for (; S; S = S->getParent()) {
    // Check whether the IdResolver has anything in this scope.
    bool Found = false;
    for (; I != IEnd && S->isDeclScope(*I); ++I) {
      if (NamedDecl *ND = R.getAcceptableDecl(*I)) {
        // We found something.  Look for anything else in our scope
        // with this same name and in an acceptable identifier
        // namespace, so that we can construct an overload set if we
        // need to.
        Found = true;
        R.addDecl(ND);
      }
    }

    if (Found && S->isTemplateParamScope()) {
      R.resolveKind();
      return true;
    }

    DeclContext *Ctx = S->getLookupEntity();
    if (Ctx) {
      DeclContext *OuterCtx = findOuterContext(S);
      for (; Ctx && !Ctx->Equals(OuterCtx); Ctx = Ctx->getLookupParent()) {
        // We do not directly look into transparent contexts, since
        // those entities will be found in the nearest enclosing
        // non-transparent context.
        if (Ctx->isTransparentContext())
          continue;

        // If we have a context, and it's not a context stashed in the
        // template parameter scope for an out-of-line definition, also
        // look into that context.
        if (!(Found && S->isTemplateParamScope())) {
          assert(Ctx->isFileContext() &&
              "We should have been looking only at file context here already.");

          // Look into context considering using-directives.
          if (CppNamespaceLookup(*this, R, Context, Ctx, UDirs))
            Found = true;
        }

        if (Found) {
          R.resolveKind();
          return true;
        }

        if (R.isForRedeclaration() && !Ctx->isTransparentContext())
          return false;
      }
    }

    if (R.isForRedeclaration() && Ctx && !Ctx->isTransparentContext())
      return false;
  }

  return !R.empty();
}

void Sema::makeMergedDefinitionVisible(NamedDecl *ND) {
  if (auto *M = getCurrentModule())
    Context.mergeDefinitionIntoModule(ND, M);
  else
    // We're not building a module; just make the definition visible.
    ND->setVisibleDespiteOwningModule();

  // If ND is a template declaration, make the template parameters
  // visible too. They're not (necessarily) within a mergeable DeclContext.
  if (auto *TD = dyn_cast<TemplateDecl>(ND))
    for (auto *Param : *TD->getTemplateParameters())
      makeMergedDefinitionVisible(Param);
}

/// Find the module in which the given declaration was defined.
static Module *getDefiningModule(Sema &S, Decl *Entity) {
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(Entity)) {
    // If this function was instantiated from a template, the defining module is
    // the module containing the pattern.
    if (FunctionDecl *Pattern = FD->getTemplateInstantiationPattern())
      Entity = Pattern;
  } else if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(Entity)) {
    if (CXXRecordDecl *Pattern = RD->getTemplateInstantiationPattern())
      Entity = Pattern;
  } else if (EnumDecl *ED = dyn_cast<EnumDecl>(Entity)) {
    if (auto *Pattern = ED->getTemplateInstantiationPattern())
      Entity = Pattern;
  } else if (VarDecl *VD = dyn_cast<VarDecl>(Entity)) {
    if (VarDecl *Pattern = VD->getTemplateInstantiationPattern())
      Entity = Pattern;
  }

  // Walk up to the containing context. That might also have been instantiated
  // from a template.
  DeclContext *Context = Entity->getLexicalDeclContext();
  if (Context->isFileContext())
    return S.getOwningModule(Entity);
  return getDefiningModule(S, cast<Decl>(Context));
}

llvm::DenseSet<Module*> &Sema::getLookupModules() {
  unsigned N = CodeSynthesisContexts.size();
  for (unsigned I = CodeSynthesisContextLookupModules.size();
       I != N; ++I) {
    Module *M = CodeSynthesisContexts[I].Entity ?
                getDefiningModule(*this, CodeSynthesisContexts[I].Entity) :
                nullptr;
    if (M && !LookupModulesCache.insert(M).second)
      M = nullptr;
    CodeSynthesisContextLookupModules.push_back(M);
  }
  return LookupModulesCache;
}

bool Sema::isUsableModule(const Module *M) {
  assert(M && "We shouldn't check nullness for module here");
  // Return quickly if we cached the result.
  if (UsableModuleUnitsCache.count(M))
    return true;

  // If M is the global module fragment of the current translation unit. So it
  // should be usable.
  // [module.global.frag]p1:
  //   The global module fragment can be used to provide declarations that are
  //   attached to the global module and usable within the module unit.
  if (M == TheGlobalModuleFragment || M == TheImplicitGlobalModuleFragment) {
    UsableModuleUnitsCache.insert(M);
    return true;
  }

  // Otherwise, the global module fragment from other translation unit is not
  // directly usable.
  if (M->isGlobalModule())
    return false;

  Module *Current = getCurrentModule();

  // If we're not parsing a module, we can't use all the declarations from
  // another module easily.
  if (!Current)
    return false;

  // If M is the module we're parsing or M and the current module unit lives in
  // the same module, M should be usable.
  //
  // Note: It should be fine to search the vector `ModuleScopes` linearly since
  // it should be generally small enough. There should be rare module fragments
  // in a named module unit.
  if (llvm::count_if(ModuleScopes,
                     [&M](const ModuleScope &MS) { return MS.Module == M; }) ||
      getASTContext().isInSameModule(M, Current)) {
    UsableModuleUnitsCache.insert(M);
    return true;
  }

  return false;
}

bool Sema::hasVisibleMergedDefinition(const NamedDecl *Def) {
  for (const Module *Merged : Context.getModulesWithMergedDefinition(Def))
    if (isModuleVisible(Merged))
      return true;
  return false;
}

bool Sema::hasMergedDefinitionInCurrentModule(const NamedDecl *Def) {
  for (const Module *Merged : Context.getModulesWithMergedDefinition(Def))
    if (isUsableModule(Merged))
      return true;
  return false;
}

template <typename ParmDecl>
static bool
hasAcceptableDefaultArgument(Sema &S, const ParmDecl *D,
                             llvm::SmallVectorImpl<Module *> *Modules,
                             Sema::AcceptableKind Kind) {
  if (!D->hasDefaultArgument())
    return false;

  llvm::SmallPtrSet<const ParmDecl *, 4> Visited;
  while (D && Visited.insert(D).second) {
    auto &DefaultArg = D->getDefaultArgStorage();
    if (!DefaultArg.isInherited() && S.isAcceptable(D, Kind))
      return true;

    if (!DefaultArg.isInherited() && Modules) {
      auto *NonConstD = const_cast<ParmDecl*>(D);
      Modules->push_back(S.getOwningModule(NonConstD));
    }

    // If there was a previous default argument, maybe its parameter is
    // acceptable.
    D = DefaultArg.getInheritedFrom();
  }
  return false;
}

bool Sema::hasAcceptableDefaultArgument(
    const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules,
    Sema::AcceptableKind Kind) {
  if (auto *P = dyn_cast<TemplateTypeParmDecl>(D))
    return ::hasAcceptableDefaultArgument(*this, P, Modules, Kind);

  if (auto *P = dyn_cast<NonTypeTemplateParmDecl>(D))
    return ::hasAcceptableDefaultArgument(*this, P, Modules, Kind);

  return ::hasAcceptableDefaultArgument(
      *this, cast<TemplateTemplateParmDecl>(D), Modules, Kind);
}

bool Sema::hasVisibleDefaultArgument(const NamedDecl *D,
                                     llvm::SmallVectorImpl<Module *> *Modules) {
  return hasAcceptableDefaultArgument(D, Modules,
                                      Sema::AcceptableKind::Visible);
}

bool Sema::hasReachableDefaultArgument(
    const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules) {
  return hasAcceptableDefaultArgument(D, Modules,
                                      Sema::AcceptableKind::Reachable);
}

template <typename Filter>
static bool
hasAcceptableDeclarationImpl(Sema &S, const NamedDecl *D,
                             llvm::SmallVectorImpl<Module *> *Modules, Filter F,
                             Sema::AcceptableKind Kind) {
  bool HasFilteredRedecls = false;

  for (auto *Redecl : D->redecls()) {
    auto *R = cast<NamedDecl>(Redecl);
    if (!F(R))
      continue;

    if (S.isAcceptable(R, Kind))
      return true;

    HasFilteredRedecls = true;

    if (Modules)
      Modules->push_back(R->getOwningModule());
  }

  // Only return false if there is at least one redecl that is not filtered out.
  if (HasFilteredRedecls)
    return false;

  return true;
}

static bool
hasAcceptableExplicitSpecialization(Sema &S, const NamedDecl *D,
                                    llvm::SmallVectorImpl<Module *> *Modules,
                                    Sema::AcceptableKind Kind) {
  return hasAcceptableDeclarationImpl(
      S, D, Modules,
      [](const NamedDecl *D) {
        if (auto *RD = dyn_cast<CXXRecordDecl>(D))
          return RD->getTemplateSpecializationKind() ==
                 TSK_ExplicitSpecialization;
        if (auto *FD = dyn_cast<FunctionDecl>(D))
          return FD->getTemplateSpecializationKind() ==
                 TSK_ExplicitSpecialization;
        if (auto *VD = dyn_cast<VarDecl>(D))
          return VD->getTemplateSpecializationKind() ==
                 TSK_ExplicitSpecialization;
        llvm_unreachable("unknown explicit specialization kind");
      },
      Kind);
}

bool Sema::hasVisibleExplicitSpecialization(
    const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules) {
  return ::hasAcceptableExplicitSpecialization(*this, D, Modules,
                                               Sema::AcceptableKind::Visible);
}

bool Sema::hasReachableExplicitSpecialization(
    const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules) {
  return ::hasAcceptableExplicitSpecialization(*this, D, Modules,
                                               Sema::AcceptableKind::Reachable);
}

static bool
hasAcceptableMemberSpecialization(Sema &S, const NamedDecl *D,
                                  llvm::SmallVectorImpl<Module *> *Modules,
                                  Sema::AcceptableKind Kind) {
  assert(isa<CXXRecordDecl>(D->getDeclContext()) &&
         "not a member specialization");
  return hasAcceptableDeclarationImpl(
      S, D, Modules,
      [](const NamedDecl *D) {
        // If the specialization is declared at namespace scope, then it's a
        // member specialization declaration. If it's lexically inside the class
        // definition then it was instantiated.
        //
        // FIXME: This is a hack. There should be a better way to determine
        // this.
        // FIXME: What about MS-style explicit specializations declared within a
        //        class definition?
        return D->getLexicalDeclContext()->isFileContext();
      },
      Kind);
}

bool Sema::hasVisibleMemberSpecialization(
    const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules) {
  return hasAcceptableMemberSpecialization(*this, D, Modules,
                                           Sema::AcceptableKind::Visible);
}

bool Sema::hasReachableMemberSpecialization(
    const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules) {
  return hasAcceptableMemberSpecialization(*this, D, Modules,
                                           Sema::AcceptableKind::Reachable);
}

/// Determine whether a declaration is acceptable to name lookup.
///
/// This routine determines whether the declaration D is acceptable in the
/// current lookup context, taking into account the current template
/// instantiation stack. During template instantiation, a declaration is
/// acceptable if it is acceptable from a module containing any entity on the
/// template instantiation path (by instantiating a template, you allow it to
/// see the declarations that your module can see, including those later on in
/// your module).
bool LookupResult::isAcceptableSlow(Sema &SemaRef, NamedDecl *D,
                                    Sema::AcceptableKind Kind) {
  assert(!D->isUnconditionallyVisible() &&
         "should not call this: not in slow case");

  Module *DeclModule = SemaRef.getOwningModule(D);
  assert(DeclModule && "hidden decl has no owning module");

  // If the owning module is visible, the decl is acceptable.
  if (SemaRef.isModuleVisible(DeclModule,
                              D->isInvisibleOutsideTheOwningModule()))
    return true;

  // Determine whether a decl context is a file context for the purpose of
  // visibility/reachability. This looks through some (export and linkage spec)
  // transparent contexts, but not others (enums).
  auto IsEffectivelyFileContext = [](const DeclContext *DC) {
    return DC->isFileContext() || isa<LinkageSpecDecl>(DC) ||
           isa<ExportDecl>(DC);
  };

  // If this declaration is not at namespace scope
  // then it is acceptable if its lexical parent has a acceptable definition.
  DeclContext *DC = D->getLexicalDeclContext();
  if (DC && !IsEffectivelyFileContext(DC)) {
    // For a parameter, check whether our current template declaration's
    // lexical context is acceptable, not whether there's some other acceptable
    // definition of it, because parameters aren't "within" the definition.
    //
    // In C++ we need to check for a acceptable definition due to ODR merging,
    // and in C we must not because each declaration of a function gets its own
    // set of declarations for tags in prototype scope.
    bool AcceptableWithinParent;
    if (D->isTemplateParameter()) {
      bool SearchDefinitions = true;
      if (const auto *DCD = dyn_cast<Decl>(DC)) {
        if (const auto *TD = DCD->getDescribedTemplate()) {
          TemplateParameterList *TPL = TD->getTemplateParameters();
          auto Index = getDepthAndIndex(D).second;
          SearchDefinitions = Index >= TPL->size() || TPL->getParam(Index) != D;
        }
      }
      if (SearchDefinitions)
        AcceptableWithinParent =
            SemaRef.hasAcceptableDefinition(cast<NamedDecl>(DC), Kind);
      else
        AcceptableWithinParent =
            isAcceptable(SemaRef, cast<NamedDecl>(DC), Kind);
    } else if (isa<ParmVarDecl>(D) ||
               (isa<FunctionDecl>(DC) && !SemaRef.getLangOpts().CPlusPlus))
      AcceptableWithinParent = isAcceptable(SemaRef, cast<NamedDecl>(DC), Kind);
    else if (D->isModulePrivate()) {
      // A module-private declaration is only acceptable if an enclosing lexical
      // parent was merged with another definition in the current module.
      AcceptableWithinParent = false;
      do {
        if (SemaRef.hasMergedDefinitionInCurrentModule(cast<NamedDecl>(DC))) {
          AcceptableWithinParent = true;
          break;
        }
        DC = DC->getLexicalParent();
      } while (!IsEffectivelyFileContext(DC));
    } else {
      AcceptableWithinParent =
          SemaRef.hasAcceptableDefinition(cast<NamedDecl>(DC), Kind);
    }

    if (AcceptableWithinParent && SemaRef.CodeSynthesisContexts.empty() &&
        Kind == Sema::AcceptableKind::Visible &&
        // FIXME: Do something better in this case.
        !SemaRef.getLangOpts().ModulesLocalVisibility) {
      // Cache the fact that this declaration is implicitly visible because
      // its parent has a visible definition.
      D->setVisibleDespiteOwningModule();
    }
    return AcceptableWithinParent;
  }

  if (Kind == Sema::AcceptableKind::Visible)
    return false;

  assert(Kind == Sema::AcceptableKind::Reachable &&
         "Additional Sema::AcceptableKind?");
  return isReachableSlow(SemaRef, D);
}

bool Sema::isModuleVisible(const Module *M, bool ModulePrivate) {
  // The module might be ordinarily visible. For a module-private query, that
  // means it is part of the current module.
  if (ModulePrivate && isUsableModule(M))
    return true;

  // For a query which is not module-private, that means it is in our visible
  // module set.
  if (!ModulePrivate && VisibleModules.isVisible(M))
    return true;

  // Otherwise, it might be visible by virtue of the query being within a
  // template instantiation or similar that is permitted to look inside M.

  // Find the extra places where we need to look.
  const auto &LookupModules = getLookupModules();
  if (LookupModules.empty())
    return false;

  // If our lookup set contains the module, it's visible.
  if (LookupModules.count(M))
    return true;

  // The global module fragments are visible to its corresponding module unit.
  // So the global module fragment should be visible if the its corresponding
  // module unit is visible.
  if (M->isGlobalModule() && LookupModules.count(M->getTopLevelModule()))
    return true;

  // For a module-private query, that's everywhere we get to look.
  if (ModulePrivate)
    return false;

  // Check whether M is transitively exported to an import of the lookup set.
  return llvm::any_of(LookupModules, [&](const Module *LookupM) {
    return LookupM->isModuleVisible(M);
  });
}

// FIXME: Return false directly if we don't have an interface dependency on the
// translation unit containing D.
bool LookupResult::isReachableSlow(Sema &SemaRef, NamedDecl *D) {
  assert(!isVisible(SemaRef, D) && "Shouldn't call the slow case.\n");

  Module *DeclModule = SemaRef.getOwningModule(D);
  assert(DeclModule && "hidden decl has no owning module");

  // Entities in header like modules are reachable only if they're visible.
  if (DeclModule->isHeaderLikeModule())
    return false;

  if (!D->isInAnotherModuleUnit())
    return true;

  // [module.reach]/p3:
  // A declaration D is reachable from a point P if:
  // ...
  // - D is not discarded ([module.global.frag]), appears in a translation unit
  //   that is reachable from P, and does not appear within a private module
  //   fragment.
  //
  // A declaration that's discarded in the GMF should be module-private.
  if (D->isModulePrivate())
    return false;

  // [module.reach]/p1
  //   A translation unit U is necessarily reachable from a point P if U is a
  //   module interface unit on which the translation unit containing P has an
  //   interface dependency, or the translation unit containing P imports U, in
  //   either case prior to P ([module.import]).
  //
  // [module.import]/p10
  //   A translation unit has an interface dependency on a translation unit U if
  //   it contains a declaration (possibly a module-declaration) that imports U
  //   or if it has an interface dependency on a translation unit that has an
  //   interface dependency on U.
  //
  // So we could conclude the module unit U is necessarily reachable if:
  // (1) The module unit U is module interface unit.
  // (2) The current unit has an interface dependency on the module unit U.
  //
  // Here we only check for the first condition. Since we couldn't see
  // DeclModule if it isn't (transitively) imported.
  if (DeclModule->getTopLevelModule()->isModuleInterfaceUnit())
    return true;

  // [module.reach]/p2
  //   Additional translation units on
  //   which the point within the program has an interface dependency may be
  //   considered reachable, but it is unspecified which are and under what
  //   circumstances.
  //
  // The decision here is to treat all additional tranditional units as
  // unreachable.
  return false;
}

bool Sema::isAcceptableSlow(const NamedDecl *D, Sema::AcceptableKind Kind) {
  return LookupResult::isAcceptable(*this, const_cast<NamedDecl *>(D), Kind);
}

bool Sema::shouldLinkPossiblyHiddenDecl(LookupResult &R, const NamedDecl *New) {
  // FIXME: If there are both visible and hidden declarations, we need to take
  // into account whether redeclaration is possible. Example:
  //
  // Non-imported module:
  //   int f(T);        // #1
  // Some TU:
  //   static int f(U); // #2, not a redeclaration of #1
  //   int f(T);        // #3, finds both, should link with #1 if T != U, but
  //                    // with #2 if T == U; neither should be ambiguous.
  for (auto *D : R) {
    if (isVisible(D))
      return true;
    assert(D->isExternallyDeclarable() &&
           "should not have hidden, non-externally-declarable result here");
  }

  // This function is called once "New" is essentially complete, but before a
  // previous declaration is attached. We can't query the linkage of "New" in
  // general, because attaching the previous declaration can change the
  // linkage of New to match the previous declaration.
  //
  // However, because we've just determined that there is no *visible* prior
  // declaration, we can compute the linkage here. There are two possibilities:
  //
  //  * This is not a redeclaration; it's safe to compute the linkage now.
  //
  //  * This is a redeclaration of a prior declaration that is externally
  //    redeclarable. In that case, the linkage of the declaration is not
  //    changed by attaching the prior declaration, because both are externally
  //    declarable (and thus ExternalLinkage or VisibleNoLinkage).
  //
  // FIXME: This is subtle and fragile.
  return New->isExternallyDeclarable();
}

/// Retrieve the visible declaration corresponding to D, if any.
///
/// This routine determines whether the declaration D is visible in the current
/// module, with the current imports. If not, it checks whether any
/// redeclaration of D is visible, and if so, returns that declaration.
///
/// \returns D, or a visible previous declaration of D, whichever is more recent
/// and visible. If no declaration of D is visible, returns null.
static NamedDecl *findAcceptableDecl(Sema &SemaRef, NamedDecl *D,
                                     unsigned IDNS) {
  assert(!LookupResult::isAvailableForLookup(SemaRef, D) && "not in slow case");

  for (auto *RD : D->redecls()) {
    // Don't bother with extra checks if we already know this one isn't visible.
    if (RD == D)
      continue;

    auto ND = cast<NamedDecl>(RD);
    // FIXME: This is wrong in the case where the previous declaration is not
    // visible in the same scope as D. This needs to be done much more
    // carefully.
    if (ND->isInIdentifierNamespace(IDNS) &&
        LookupResult::isAvailableForLookup(SemaRef, ND))
      return ND;
  }

  return nullptr;
}

bool Sema::hasVisibleDeclarationSlow(const NamedDecl *D,
                                     llvm::SmallVectorImpl<Module *> *Modules) {
  assert(!isVisible(D) && "not in slow case");
  return hasAcceptableDeclarationImpl(
      *this, D, Modules, [](const NamedDecl *) { return true; },
      Sema::AcceptableKind::Visible);
}

bool Sema::hasReachableDeclarationSlow(
    const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules) {
  assert(!isReachable(D) && "not in slow case");
  return hasAcceptableDeclarationImpl(
      *this, D, Modules, [](const NamedDecl *) { return true; },
      Sema::AcceptableKind::Reachable);
}

NamedDecl *LookupResult::getAcceptableDeclSlow(NamedDecl *D) const {
  if (auto *ND = dyn_cast<NamespaceDecl>(D)) {
    // Namespaces are a bit of a special case: we expect there to be a lot of
    // redeclarations of some namespaces, all declarations of a namespace are
    // essentially interchangeable, all declarations are found by name lookup
    // if any is, and namespaces are never looked up during template
    // instantiation. So we benefit from caching the check in this case, and
    // it is correct to do so.
    auto *Key = ND->getCanonicalDecl();
    if (auto *Acceptable = getSema().VisibleNamespaceCache.lookup(Key))
      return Acceptable;
    auto *Acceptable = isVisible(getSema(), Key)
                           ? Key
                           : findAcceptableDecl(getSema(), Key, IDNS);
    if (Acceptable)
      getSema().VisibleNamespaceCache.insert(std::make_pair(Key, Acceptable));
    return Acceptable;
  }

  return findAcceptableDecl(getSema(), D, IDNS);
}

bool LookupResult::isVisible(Sema &SemaRef, NamedDecl *D) {
  // If this declaration is already visible, return it directly.
  if (D->isUnconditionallyVisible())
    return true;

  // During template instantiation, we can refer to hidden declarations, if
  // they were visible in any module along the path of instantiation.
  return isAcceptableSlow(SemaRef, D, Sema::AcceptableKind::Visible);
}

bool LookupResult::isReachable(Sema &SemaRef, NamedDecl *D) {
  if (D->isUnconditionallyVisible())
    return true;

  return isAcceptableSlow(SemaRef, D, Sema::AcceptableKind::Reachable);
}

bool LookupResult::isAvailableForLookup(Sema &SemaRef, NamedDecl *ND) {
  // We should check the visibility at the callsite already.
  if (isVisible(SemaRef, ND))
    return true;

  // Deduction guide lives in namespace scope generally, but it is just a
  // hint to the compilers. What we actually lookup for is the generated member
  // of the corresponding template. So it is sufficient to check the
  // reachability of the template decl.
  if (auto *DeductionGuide = ND->getDeclName().getCXXDeductionGuideTemplate())
    return SemaRef.hasReachableDefinition(DeductionGuide);

  // FIXME: The lookup for allocation function is a standalone process.
  // (We can find the logics in Sema::FindAllocationFunctions)
  //
  // Such structure makes it a problem when we instantiate a template
  // declaration using placement allocation function if the placement
  // allocation function is invisible.
  // (See https://github.com/llvm/llvm-project/issues/59601)
  //
  // Here we workaround it by making the placement allocation functions
  // always acceptable. The downside is that we can't diagnose the direct
  // use of the invisible placement allocation functions. (Although such uses
  // should be rare).
  if (auto *FD = dyn_cast<FunctionDecl>(ND);
      FD && FD->isReservedGlobalPlacementOperator())
    return true;

  auto *DC = ND->getDeclContext();
  // If ND is not visible and it is at namespace scope, it shouldn't be found
  // by name lookup.
  if (DC->isFileContext())
    return false;

  // [module.interface]p7
  // Class and enumeration member names can be found by name lookup in any
  // context in which a definition of the type is reachable.
  //
  // FIXME: The current implementation didn't consider about scope. For example,
  // ```
  // // m.cppm
  // export module m;
  // enum E1 { e1 };
  // // Use.cpp
  // import m;
  // void test() {
  //   auto a = E1::e1; // Error as expected.
  //   auto b = e1; // Should be error. namespace-scope name e1 is not visible
  // }
  // ```
  // For the above example, the current implementation would emit error for `a`
  // correctly. However, the implementation wouldn't diagnose about `b` now.
  // Since we only check the reachability for the parent only.
  // See clang/test/CXX/module/module.interface/p7.cpp for example.
  if (auto *TD = dyn_cast<TagDecl>(DC))
    return SemaRef.hasReachableDefinition(TD);

  return false;
}

bool Sema::LookupName(LookupResult &R, Scope *S, bool AllowBuiltinCreation,
                      bool ForceNoCPlusPlus) {
  DeclarationName Name = R.getLookupName();
  if (!Name) return false;

  LookupNameKind NameKind = R.getLookupKind();

  if (!getLangOpts().CPlusPlus || ForceNoCPlusPlus) {
    // Unqualified name lookup in C/Objective-C is purely lexical, so
    // search in the declarations attached to the name.
    if (NameKind == Sema::LookupRedeclarationWithLinkage) {
      // Find the nearest non-transparent declaration scope.
      while (!(S->getFlags() & Scope::DeclScope) ||
             (S->getEntity() && S->getEntity()->isTransparentContext()))
        S = S->getParent();
    }

    // When performing a scope lookup, we want to find local extern decls.
    FindLocalExternScope FindLocals(R);

    // Scan up the scope chain looking for a decl that matches this
    // identifier that is in the appropriate namespace.  This search
    // should not take long, as shadowing of names is uncommon, and
    // deep shadowing is extremely uncommon.
    bool LeftStartingScope = false;

    for (IdentifierResolver::iterator I = IdResolver.begin(Name),
                                   IEnd = IdResolver.end();
         I != IEnd; ++I)
      if (NamedDecl *D = R.getAcceptableDecl(*I)) {
        if (NameKind == LookupRedeclarationWithLinkage) {
          // Determine whether this (or a previous) declaration is
          // out-of-scope.
          if (!LeftStartingScope && !S->isDeclScope(*I))
            LeftStartingScope = true;

          // If we found something outside of our starting scope that
          // does not have linkage, skip it.
          if (LeftStartingScope && !((*I)->hasLinkage())) {
            R.setShadowed();
            continue;
          }
        }
        else if (NameKind == LookupObjCImplicitSelfParam &&
                 !isa<ImplicitParamDecl>(*I))
          continue;

        R.addDecl(D);

        // Check whether there are any other declarations with the same name
        // and in the same scope.
        if (I != IEnd) {
          // Find the scope in which this declaration was declared (if it
          // actually exists in a Scope).
          while (S && !S->isDeclScope(D))
            S = S->getParent();

          // If the scope containing the declaration is the translation unit,
          // then we'll need to perform our checks based on the matching
          // DeclContexts rather than matching scopes.
          if (S && isNamespaceOrTranslationUnitScope(S))
            S = nullptr;

          // Compute the DeclContext, if we need it.
          DeclContext *DC = nullptr;
          if (!S)
            DC = (*I)->getDeclContext()->getRedeclContext();

          IdentifierResolver::iterator LastI = I;
          for (++LastI; LastI != IEnd; ++LastI) {
            if (S) {
              // Match based on scope.
              if (!S->isDeclScope(*LastI))
                break;
            } else {
              // Match based on DeclContext.
              DeclContext *LastDC
                = (*LastI)->getDeclContext()->getRedeclContext();
              if (!LastDC->Equals(DC))
                break;
            }

            // If the declaration is in the right namespace and visible, add it.
            if (NamedDecl *LastD = R.getAcceptableDecl(*LastI))
              R.addDecl(LastD);
          }

          R.resolveKind();
        }

        return true;
      }
  } else {
    // Perform C++ unqualified name lookup.
    if (CppLookupName(R, S))
      return true;
  }

  // If we didn't find a use of this identifier, and if the identifier
  // corresponds to a compiler builtin, create the decl object for the builtin
  // now, injecting it into translation unit scope, and return it.
  if (AllowBuiltinCreation && LookupBuiltin(R))
    return true;

  // If we didn't find a use of this identifier, the ExternalSource
  // may be able to handle the situation.
  // Note: some lookup failures are expected!
  // See e.g. R.isForRedeclaration().
  return (ExternalSource && ExternalSource->LookupUnqualified(R, S));
}

/// Perform qualified name lookup in the namespaces nominated by
/// using directives by the given context.
///
/// C++98 [namespace.qual]p2:
///   Given X::m (where X is a user-declared namespace), or given \::m
///   (where X is the global namespace), let S be the set of all
///   declarations of m in X and in the transitive closure of all
///   namespaces nominated by using-directives in X and its used
///   namespaces, except that using-directives are ignored in any
///   namespace, including X, directly containing one or more
///   declarations of m. No namespace is searched more than once in
///   the lookup of a name. If S is the empty set, the program is
///   ill-formed. Otherwise, if S has exactly one member, or if the
///   context of the reference is a using-declaration
///   (namespace.udecl), S is the required set of declarations of
///   m. Otherwise if the use of m is not one that allows a unique
///   declaration to be chosen from S, the program is ill-formed.
///
/// C++98 [namespace.qual]p5:
///   During the lookup of a qualified namespace member name, if the
///   lookup finds more than one declaration of the member, and if one
///   declaration introduces a class name or enumeration name and the
///   other declarations either introduce the same object, the same
///   enumerator or a set of functions, the non-type name hides the
///   class or enumeration name if and only if the declarations are
///   from the same namespace; otherwise (the declarations are from
///   different namespaces), the program is ill-formed.
static bool LookupQualifiedNameInUsingDirectives(Sema &S, LookupResult &R,
                                                 DeclContext *StartDC) {
  assert(StartDC->isFileContext() && "start context is not a file context");

  // We have not yet looked into these namespaces, much less added
  // their "using-children" to the queue.
  SmallVector<NamespaceDecl*, 8> Queue;

  // We have at least added all these contexts to the queue.
  llvm::SmallPtrSet<DeclContext*, 8> Visited;
  Visited.insert(StartDC);

  // We have already looked into the initial namespace; seed the queue
  // with its using-children.
  for (auto *I : StartDC->using_directives()) {
    NamespaceDecl *ND = I->getNominatedNamespace()->getFirstDecl();
    if (S.isVisible(I) && Visited.insert(ND).second)
      Queue.push_back(ND);
  }

  // The easiest way to implement the restriction in [namespace.qual]p5
  // is to check whether any of the individual results found a tag
  // and, if so, to declare an ambiguity if the final result is not
  // a tag.
  bool FoundTag = false;
  bool FoundNonTag = false;

  LookupResult LocalR(LookupResult::Temporary, R);

  bool Found = false;
  while (!Queue.empty()) {
    NamespaceDecl *ND = Queue.pop_back_val();

    // We go through some convolutions here to avoid copying results
    // between LookupResults.
    bool UseLocal = !R.empty();
    LookupResult &DirectR = UseLocal ? LocalR : R;
    bool FoundDirect = LookupDirect(S, DirectR, ND);

    if (FoundDirect) {
      // First do any local hiding.
      DirectR.resolveKind();

      // If the local result is a tag, remember that.
      if (DirectR.isSingleTagDecl())
        FoundTag = true;
      else
        FoundNonTag = true;

      // Append the local results to the total results if necessary.
      if (UseLocal) {
        R.addAllDecls(LocalR);
        LocalR.clear();
      }
    }

    // If we find names in this namespace, ignore its using directives.
    if (FoundDirect) {
      Found = true;
      continue;
    }

    for (auto *I : ND->using_directives()) {
      NamespaceDecl *Nom = I->getNominatedNamespace();
      if (S.isVisible(I) && Visited.insert(Nom).second)
        Queue.push_back(Nom);
    }
  }

  if (Found) {
    if (FoundTag && FoundNonTag)
      R.setAmbiguousQualifiedTagHiding();
    else
      R.resolveKind();
  }

  return Found;
}

bool Sema::LookupQualifiedName(LookupResult &R, DeclContext *LookupCtx,
                               bool InUnqualifiedLookup) {
  assert(LookupCtx && "Sema::LookupQualifiedName requires a lookup context");

  if (!R.getLookupName())
    return false;

  // Make sure that the declaration context is complete.
  assert((!isa<TagDecl>(LookupCtx) ||
          LookupCtx->isDependentContext() ||
          cast<TagDecl>(LookupCtx)->isCompleteDefinition() ||
          cast<TagDecl>(LookupCtx)->isBeingDefined()) &&
         "Declaration context must already be complete!");

  struct QualifiedLookupInScope {
    bool oldVal;
    DeclContext *Context;
    // Set flag in DeclContext informing debugger that we're looking for qualified name
    QualifiedLookupInScope(DeclContext *ctx)
        : oldVal(ctx->shouldUseQualifiedLookup()), Context(ctx) {
      ctx->setUseQualifiedLookup();
    }
    ~QualifiedLookupInScope() {
      Context->setUseQualifiedLookup(oldVal);
    }
  } QL(LookupCtx);

  CXXRecordDecl *LookupRec = dyn_cast<CXXRecordDecl>(LookupCtx);
  // FIXME: Per [temp.dep.general]p2, an unqualified name is also dependent
  // if it's a dependent conversion-function-id or operator= where the current
  // class is a templated entity. This should be handled in LookupName.
  if (!InUnqualifiedLookup && !R.isForRedeclaration()) {
    // C++23 [temp.dep.type]p5:
    //   A qualified name is dependent if
    //   - it is a conversion-function-id whose conversion-type-id
    //     is dependent, or
    //   - [...]
    //   - its lookup context is the current instantiation and it
    //     is operator=, or
    //   - [...]
    if (DeclarationName Name = R.getLookupName();
        Name.getNameKind() == DeclarationName::CXXConversionFunctionName &&
        Name.getCXXNameType()->isDependentType()) {
      R.setNotFoundInCurrentInstantiation();
      return false;
    }
  }

  if (LookupDirect(*this, R, LookupCtx)) {
    R.resolveKind();
    if (LookupRec)
      R.setNamingClass(LookupRec);
    return true;
  }

  // Don't descend into implied contexts for redeclarations.
  // C++98 [namespace.qual]p6:
  //   In a declaration for a namespace member in which the
  //   declarator-id is a qualified-id, given that the qualified-id
  //   for the namespace member has the form
  //     nested-name-specifier unqualified-id
  //   the unqualified-id shall name a member of the namespace
  //   designated by the nested-name-specifier.
  // See also [class.mfct]p5 and [class.static.data]p2.
  if (R.isForRedeclaration())
    return false;

  // If this is a namespace, look it up in the implied namespaces.
  if (LookupCtx->isFileContext())
    return LookupQualifiedNameInUsingDirectives(*this, R, LookupCtx);

  // If this isn't a C++ class, we aren't allowed to look into base
  // classes, we're done.
  if (!LookupRec || !LookupRec->getDefinition())
    return false;

  // We're done for lookups that can never succeed for C++ classes.
  if (R.getLookupKind() == LookupOperatorName ||
      R.getLookupKind() == LookupNamespaceName ||
      R.getLookupKind() == LookupObjCProtocolName ||
      R.getLookupKind() == LookupLabel)
    return false;

  // If we're performing qualified name lookup into a dependent class,
  // then we are actually looking into a current instantiation. If we have any
  // dependent base classes, then we either have to delay lookup until
  // template instantiation time (at which point all bases will be available)
  // or we have to fail.
  if (!InUnqualifiedLookup && LookupRec->isDependentContext() &&
      LookupRec->hasAnyDependentBases()) {
    R.setNotFoundInCurrentInstantiation();
    return false;
  }

  // Perform lookup into our base classes.

  DeclarationName Name = R.getLookupName();
  unsigned IDNS = R.getIdentifierNamespace();

  // Look for this member in our base classes.
  auto BaseCallback = [Name, IDNS](const CXXBaseSpecifier *Specifier,
                                   CXXBasePath &Path) -> bool {
    CXXRecordDecl *BaseRecord = Specifier->getType()->getAsCXXRecordDecl();
    // Drop leading non-matching lookup results from the declaration list so
    // we don't need to consider them again below.
    for (Path.Decls = BaseRecord->lookup(Name).begin();
         Path.Decls != Path.Decls.end(); ++Path.Decls) {
      if ((*Path.Decls)->isInIdentifierNamespace(IDNS))
        return true;
    }
    return false;
  };

  CXXBasePaths Paths;
  Paths.setOrigin(LookupRec);
  if (!LookupRec->lookupInBases(BaseCallback, Paths))
    return false;

  R.setNamingClass(LookupRec);

  // C++ [class.member.lookup]p2:
  //   [...] If the resulting set of declarations are not all from
  //   sub-objects of the same type, or the set has a nonstatic member
  //   and includes members from distinct sub-objects, there is an
  //   ambiguity and the program is ill-formed. Otherwise that set is
  //   the result of the lookup.
  QualType SubobjectType;
  int SubobjectNumber = 0;
  AccessSpecifier SubobjectAccess = AS_none;

  // Check whether the given lookup result contains only static members.
  auto HasOnlyStaticMembers = [&](DeclContext::lookup_iterator Result) {
    for (DeclContext::lookup_iterator I = Result, E = I.end(); I != E; ++I)
      if ((*I)->isInIdentifierNamespace(IDNS) && (*I)->isCXXInstanceMember())
        return false;
    return true;
  };

  bool TemplateNameLookup = R.isTemplateNameLookup();

  // Determine whether two sets of members contain the same members, as
  // required by C++ [class.member.lookup]p6.
  auto HasSameDeclarations = [&](DeclContext::lookup_iterator A,
                                 DeclContext::lookup_iterator B) {
    using Iterator = DeclContextLookupResult::iterator;
    using Result = const void *;

    auto Next = [&](Iterator &It, Iterator End) -> Result {
      while (It != End) {
        NamedDecl *ND = *It++;
        if (!ND->isInIdentifierNamespace(IDNS))
          continue;

        // C++ [temp.local]p3:
        //   A lookup that finds an injected-class-name (10.2) can result in
        //   an ambiguity in certain cases (for example, if it is found in
        //   more than one base class). If all of the injected-class-names
        //   that are found refer to specializations of the same class
        //   template, and if the name is used as a template-name, the
        //   reference refers to the class template itself and not a
        //   specialization thereof, and is not ambiguous.
        if (TemplateNameLookup)
          if (auto *TD = getAsTemplateNameDecl(ND))
            ND = TD;

        // C++ [class.member.lookup]p3:
        //   type declarations (including injected-class-names) are replaced by
        //   the types they designate
        if (const TypeDecl *TD = dyn_cast<TypeDecl>(ND->getUnderlyingDecl())) {
          QualType T = Context.getTypeDeclType(TD);
          return T.getCanonicalType().getAsOpaquePtr();
        }

        return ND->getUnderlyingDecl()->getCanonicalDecl();
      }
      return nullptr;
    };

    // We'll often find the declarations are in the same order. Handle this
    // case (and the special case of only one declaration) efficiently.
    Iterator AIt = A, BIt = B, AEnd, BEnd;
    while (true) {
      Result AResult = Next(AIt, AEnd);
      Result BResult = Next(BIt, BEnd);
      if (!AResult && !BResult)
        return true;
      if (!AResult || !BResult)
        return false;
      if (AResult != BResult) {
        // Found a mismatch; carefully check both lists, accounting for the
        // possibility of declarations appearing more than once.
        llvm::SmallDenseMap<Result, bool, 32> AResults;
        for (; AResult; AResult = Next(AIt, AEnd))
          AResults.insert({AResult, /*FoundInB*/false});
        unsigned Found = 0;
        for (; BResult; BResult = Next(BIt, BEnd)) {
          auto It = AResults.find(BResult);
          if (It == AResults.end())
            return false;
          if (!It->second) {
            It->second = true;
            ++Found;
          }
        }
        return AResults.size() == Found;
      }
    }
  };

  for (CXXBasePaths::paths_iterator Path = Paths.begin(), PathEnd = Paths.end();
       Path != PathEnd; ++Path) {
    const CXXBasePathElement &PathElement = Path->back();

    // Pick the best (i.e. most permissive i.e. numerically lowest) access
    // across all paths.
    SubobjectAccess = std::min(SubobjectAccess, Path->Access);

    // Determine whether we're looking at a distinct sub-object or not.
    if (SubobjectType.isNull()) {
      // This is the first subobject we've looked at. Record its type.
      SubobjectType = Context.getCanonicalType(PathElement.Base->getType());
      SubobjectNumber = PathElement.SubobjectNumber;
      continue;
    }

    if (SubobjectType !=
        Context.getCanonicalType(PathElement.Base->getType())) {
      // We found members of the given name in two subobjects of
      // different types. If the declaration sets aren't the same, this
      // lookup is ambiguous.
      //
      // FIXME: The language rule says that this applies irrespective of
      // whether the sets contain only static members.
      if (HasOnlyStaticMembers(Path->Decls) &&
          HasSameDeclarations(Paths.begin()->Decls, Path->Decls))
        continue;

      R.setAmbiguousBaseSubobjectTypes(Paths);
      return true;
    }

    // FIXME: This language rule no longer exists. Checking for ambiguous base
    // subobjects should be done as part of formation of a class member access
    // expression (when converting the object parameter to the member's type).
    if (SubobjectNumber != PathElement.SubobjectNumber) {
      // We have a different subobject of the same type.

      // C++ [class.member.lookup]p5:
      //   A static member, a nested type or an enumerator defined in
      //   a base class T can unambiguously be found even if an object
      //   has more than one base class subobject of type T.
      if (HasOnlyStaticMembers(Path->Decls))
        continue;

      // We have found a nonstatic member name in multiple, distinct
      // subobjects. Name lookup is ambiguous.
      R.setAmbiguousBaseSubobjects(Paths);
      return true;
    }
  }

  // Lookup in a base class succeeded; return these results.

  for (DeclContext::lookup_iterator I = Paths.front().Decls, E = I.end();
       I != E; ++I) {
    AccessSpecifier AS = CXXRecordDecl::MergeAccess(SubobjectAccess,
                                                    (*I)->getAccess());
    if (NamedDecl *ND = R.getAcceptableDecl(*I))
      R.addDecl(ND, AS);
  }
  R.resolveKind();
  return true;
}

bool Sema::LookupQualifiedName(LookupResult &R, DeclContext *LookupCtx,
                               CXXScopeSpec &SS) {
  auto *NNS = SS.getScopeRep();
  if (NNS && NNS->getKind() == NestedNameSpecifier::Super)
    return LookupInSuper(R, NNS->getAsRecordDecl());
  else

    return LookupQualifiedName(R, LookupCtx);
}

bool Sema::LookupParsedName(LookupResult &R, Scope *S, CXXScopeSpec *SS,
                            QualType ObjectType, bool AllowBuiltinCreation,
                            bool EnteringContext) {
  // When the scope specifier is invalid, don't even look for anything.
  if (SS && SS->isInvalid())
    return false;

  // Determine where to perform name lookup
  DeclContext *DC = nullptr;
  bool IsDependent = false;
  if (!ObjectType.isNull()) {
    // This nested-name-specifier occurs in a member access expression, e.g.,
    // x->B::f, and we are looking into the type of the object.
    assert((!SS || SS->isEmpty()) &&
           "ObjectType and scope specifier cannot coexist");
    DC = computeDeclContext(ObjectType);
    IsDependent = !DC && ObjectType->isDependentType();
    assert(((!DC && ObjectType->isDependentType()) ||
            !ObjectType->isIncompleteType() || !ObjectType->getAs<TagType>() ||
            ObjectType->castAs<TagType>()->isBeingDefined()) &&
           "Caller should have completed object type");
  } else if (SS && SS->isNotEmpty()) {
    // This nested-name-specifier occurs after another nested-name-specifier,
    // so long into the context associated with the prior nested-name-specifier.
    if ((DC = computeDeclContext(*SS, EnteringContext))) {
      // The declaration context must be complete.
      if (!DC->isDependentContext() && RequireCompleteDeclContext(*SS, DC))
        return false;
      R.setContextRange(SS->getRange());
      // FIXME: '__super' lookup semantics could be implemented by a
      // LookupResult::isSuperLookup flag which skips the initial search of
      // the lookup context in LookupQualified.
      if (NestedNameSpecifier *NNS = SS->getScopeRep();
          NNS->getKind() == NestedNameSpecifier::Super)
        return LookupInSuper(R, NNS->getAsRecordDecl());
    }
    IsDependent = !DC && isDependentScopeSpecifier(*SS);
  } else {
    // Perform unqualified name lookup starting in the given scope.
    return LookupName(R, S, AllowBuiltinCreation);
  }

  // If we were able to compute a declaration context, perform qualified name
  // lookup in that context.
  if (DC)
    return LookupQualifiedName(R, DC);
  else if (IsDependent)
    // We could not resolve the scope specified to a specific declaration
    // context, which means that SS refers to an unknown specialization.
    // Name lookup can't find anything in this case.
    R.setNotFoundInCurrentInstantiation();
  return false;
}

bool Sema::LookupInSuper(LookupResult &R, CXXRecordDecl *Class) {
  // The access-control rules we use here are essentially the rules for
  // doing a lookup in Class that just magically skipped the direct
  // members of Class itself.  That is, the naming class is Class, and the
  // access includes the access of the base.
  for (const auto &BaseSpec : Class->bases()) {
    CXXRecordDecl *RD = cast<CXXRecordDecl>(
        BaseSpec.getType()->castAs<RecordType>()->getDecl());
    LookupResult Result(*this, R.getLookupNameInfo(), R.getLookupKind());
    Result.setBaseObjectType(Context.getRecordType(Class));
    LookupQualifiedName(Result, RD);

    // Copy the lookup results into the target, merging the base's access into
    // the path access.
    for (auto I = Result.begin(), E = Result.end(); I != E; ++I) {
      R.addDecl(I.getDecl(),
                CXXRecordDecl::MergeAccess(BaseSpec.getAccessSpecifier(),
                                           I.getAccess()));
    }

    Result.suppressDiagnostics();
  }

  R.resolveKind();
  R.setNamingClass(Class);

  return !R.empty();
}

void Sema::DiagnoseAmbiguousLookup(LookupResult &Result) {
  assert(Result.isAmbiguous() && "Lookup result must be ambiguous");

  DeclarationName Name = Result.getLookupName();
  SourceLocation NameLoc = Result.getNameLoc();
  SourceRange LookupRange = Result.getContextRange();

  switch (Result.getAmbiguityKind()) {
  case LookupResult::AmbiguousBaseSubobjects: {
    CXXBasePaths *Paths = Result.getBasePaths();
    QualType SubobjectType = Paths->front().back().Base->getType();
    Diag(NameLoc, diag::err_ambiguous_member_multiple_subobjects)
      << Name << SubobjectType << getAmbiguousPathsDisplayString(*Paths)
      << LookupRange;

    DeclContext::lookup_iterator Found = Paths->front().Decls;
    while (isa<CXXMethodDecl>(*Found) &&
           cast<CXXMethodDecl>(*Found)->isStatic())
      ++Found;

    Diag((*Found)->getLocation(), diag::note_ambiguous_member_found);
    break;
  }

  case LookupResult::AmbiguousBaseSubobjectTypes: {
    Diag(NameLoc, diag::err_ambiguous_member_multiple_subobject_types)
      << Name << LookupRange;

    CXXBasePaths *Paths = Result.getBasePaths();
    std::set<const NamedDecl *> DeclsPrinted;
    for (CXXBasePaths::paths_iterator Path = Paths->begin(),
                                      PathEnd = Paths->end();
         Path != PathEnd; ++Path) {
      const NamedDecl *D = *Path->Decls;
      if (!D->isInIdentifierNamespace(Result.getIdentifierNamespace()))
        continue;
      if (DeclsPrinted.insert(D).second) {
        if (const auto *TD = dyn_cast<TypedefNameDecl>(D->getUnderlyingDecl()))
          Diag(D->getLocation(), diag::note_ambiguous_member_type_found)
              << TD->getUnderlyingType();
        else if (const auto *TD = dyn_cast<TypeDecl>(D->getUnderlyingDecl()))
          Diag(D->getLocation(), diag::note_ambiguous_member_type_found)
              << Context.getTypeDeclType(TD);
        else
          Diag(D->getLocation(), diag::note_ambiguous_member_found);
      }
    }
    break;
  }

  case LookupResult::AmbiguousTagHiding: {
    Diag(NameLoc, diag::err_ambiguous_tag_hiding) << Name << LookupRange;

    llvm::SmallPtrSet<NamedDecl*, 8> TagDecls;

    for (auto *D : Result)
      if (TagDecl *TD = dyn_cast<TagDecl>(D)) {
        TagDecls.insert(TD);
        Diag(TD->getLocation(), diag::note_hidden_tag);
      }

    for (auto *D : Result)
      if (!isa<TagDecl>(D))
        Diag(D->getLocation(), diag::note_hiding_object);

    // For recovery purposes, go ahead and implement the hiding.
    LookupResult::Filter F = Result.makeFilter();
    while (F.hasNext()) {
      if (TagDecls.count(F.next()))
        F.erase();
    }
    F.done();
    break;
  }

  case LookupResult::AmbiguousReferenceToPlaceholderVariable: {
    Diag(NameLoc, diag::err_using_placeholder_variable) << Name << LookupRange;
    DeclContext *DC = nullptr;
    for (auto *D : Result) {
      Diag(D->getLocation(), diag::note_reference_placeholder) << D;
      if (DC != nullptr && DC != D->getDeclContext())
        break;
      DC = D->getDeclContext();
    }
    break;
  }

  case LookupResult::AmbiguousReference: {
    Diag(NameLoc, diag::err_ambiguous_reference) << Name << LookupRange;

    for (auto *D : Result)
      Diag(D->getLocation(), diag::note_ambiguous_candidate) << D;
    break;
  }
  }
}

namespace {
  struct AssociatedLookup {
    AssociatedLookup(Sema &S, SourceLocation InstantiationLoc,
                     Sema::AssociatedNamespaceSet &Namespaces,
                     Sema::AssociatedClassSet &Classes)
      : S(S), Namespaces(Namespaces), Classes(Classes),
        InstantiationLoc(InstantiationLoc) {
    }

    bool addClassTransitive(CXXRecordDecl *RD) {
      Classes.insert(RD);
      return ClassesTransitive.insert(RD);
    }

    Sema &S;
    Sema::AssociatedNamespaceSet &Namespaces;
    Sema::AssociatedClassSet &Classes;
    SourceLocation InstantiationLoc;

  private:
    Sema::AssociatedClassSet ClassesTransitive;
  };
} // end anonymous namespace

static void
addAssociatedClassesAndNamespaces(AssociatedLookup &Result, QualType T);

// Given the declaration context \param Ctx of a class, class template or
// enumeration, add the associated namespaces to \param Namespaces as described
// in [basic.lookup.argdep]p2.
static void CollectEnclosingNamespace(Sema::AssociatedNamespaceSet &Namespaces,
                                      DeclContext *Ctx) {
  // The exact wording has been changed in C++14 as a result of
  // CWG 1691 (see also CWG 1690 and CWG 1692). We apply it unconditionally
  // to all language versions since it is possible to return a local type
  // from a lambda in C++11.
  //
  // C++14 [basic.lookup.argdep]p2:
  //   If T is a class type [...]. Its associated namespaces are the innermost
  //   enclosing namespaces of its associated classes. [...]
  //
  //   If T is an enumeration type, its associated namespace is the innermost
  //   enclosing namespace of its declaration. [...]

  // We additionally skip inline namespaces. The innermost non-inline namespace
  // contains all names of all its nested inline namespaces anyway, so we can
  // replace the entire inline namespace tree with its root.
  while (!Ctx->isFileContext() || Ctx->isInlineNamespace())
    Ctx = Ctx->getParent();

  Namespaces.insert(Ctx->getPrimaryContext());
}

// Add the associated classes and namespaces for argument-dependent
// lookup that involves a template argument (C++ [basic.lookup.argdep]p2).
static void
addAssociatedClassesAndNamespaces(AssociatedLookup &Result,
                                  const TemplateArgument &Arg) {
  // C++ [basic.lookup.argdep]p2, last bullet:
  //   -- [...] ;
  switch (Arg.getKind()) {
    case TemplateArgument::Null:
      break;

    case TemplateArgument::Type:
      // [...] the namespaces and classes associated with the types of the
      // template arguments provided for template type parameters (excluding
      // template template parameters)
      addAssociatedClassesAndNamespaces(Result, Arg.getAsType());
      break;

    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion: {
      // [...] the namespaces in which any template template arguments are
      // defined; and the classes in which any member templates used as
      // template template arguments are defined.
      TemplateName Template = Arg.getAsTemplateOrTemplatePattern();
      if (ClassTemplateDecl *ClassTemplate
                 = dyn_cast<ClassTemplateDecl>(Template.getAsTemplateDecl())) {
        DeclContext *Ctx = ClassTemplate->getDeclContext();
        if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
          Result.Classes.insert(EnclosingClass);
        // Add the associated namespace for this class.
        CollectEnclosingNamespace(Result.Namespaces, Ctx);
      }
      break;
    }

    case TemplateArgument::Declaration:
    case TemplateArgument::Integral:
    case TemplateArgument::Expression:
    case TemplateArgument::NullPtr:
    case TemplateArgument::StructuralValue:
      // [Note: non-type template arguments do not contribute to the set of
      //  associated namespaces. ]
      break;

    case TemplateArgument::Pack:
      for (const auto &P : Arg.pack_elements())
        addAssociatedClassesAndNamespaces(Result, P);
      break;
  }
}

// Add the associated classes and namespaces for argument-dependent lookup
// with an argument of class type (C++ [basic.lookup.argdep]p2).
static void
addAssociatedClassesAndNamespaces(AssociatedLookup &Result,
                                  CXXRecordDecl *Class) {

  // Just silently ignore anything whose name is __va_list_tag.
  if (Class->getDeclName() == Result.S.VAListTagName)
    return;

  // C++ [basic.lookup.argdep]p2:
  //   [...]
  //     -- If T is a class type (including unions), its associated
  //        classes are: the class itself; the class of which it is a
  //        member, if any; and its direct and indirect base classes.
  //        Its associated namespaces are the innermost enclosing
  //        namespaces of its associated classes.

  // Add the class of which it is a member, if any.
  DeclContext *Ctx = Class->getDeclContext();
  if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
    Result.Classes.insert(EnclosingClass);

  // Add the associated namespace for this class.
  CollectEnclosingNamespace(Result.Namespaces, Ctx);

  // -- If T is a template-id, its associated namespaces and classes are
  //    the namespace in which the template is defined; for member
  //    templates, the member template's class; the namespaces and classes
  //    associated with the types of the template arguments provided for
  //    template type parameters (excluding template template parameters); the
  //    namespaces in which any template template arguments are defined; and
  //    the classes in which any member templates used as template template
  //    arguments are defined. [Note: non-type template arguments do not
  //    contribute to the set of associated namespaces. ]
  if (ClassTemplateSpecializationDecl *Spec
        = dyn_cast<ClassTemplateSpecializationDecl>(Class)) {
    DeclContext *Ctx = Spec->getSpecializedTemplate()->getDeclContext();
    if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
      Result.Classes.insert(EnclosingClass);
    // Add the associated namespace for this class.
    CollectEnclosingNamespace(Result.Namespaces, Ctx);

    const TemplateArgumentList &TemplateArgs = Spec->getTemplateArgs();
    for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
      addAssociatedClassesAndNamespaces(Result, TemplateArgs[I]);
  }

  // Add the class itself. If we've already transitively visited this class,
  // we don't need to visit base classes.
  if (!Result.addClassTransitive(Class))
    return;

  // Only recurse into base classes for complete types.
  if (!Result.S.isCompleteType(Result.InstantiationLoc,
                               Result.S.Context.getRecordType(Class)))
    return;

  // Add direct and indirect base classes along with their associated
  // namespaces.
  SmallVector<CXXRecordDecl *, 32> Bases;
  Bases.push_back(Class);
  while (!Bases.empty()) {
    // Pop this class off the stack.
    Class = Bases.pop_back_val();

    // Visit the base classes.
    for (const auto &Base : Class->bases()) {
      const RecordType *BaseType = Base.getType()->getAs<RecordType>();
      // In dependent contexts, we do ADL twice, and the first time around,
      // the base type might be a dependent TemplateSpecializationType, or a
      // TemplateTypeParmType. If that happens, simply ignore it.
      // FIXME: If we want to support export, we probably need to add the
      // namespace of the template in a TemplateSpecializationType, or even
      // the classes and namespaces of known non-dependent arguments.
      if (!BaseType)
        continue;
      CXXRecordDecl *BaseDecl = cast<CXXRecordDecl>(BaseType->getDecl());
      if (Result.addClassTransitive(BaseDecl)) {
        // Find the associated namespace for this base class.
        DeclContext *BaseCtx = BaseDecl->getDeclContext();
        CollectEnclosingNamespace(Result.Namespaces, BaseCtx);

        // Make sure we visit the bases of this base class.
        if (BaseDecl->bases_begin() != BaseDecl->bases_end())
          Bases.push_back(BaseDecl);
      }
    }
  }
}

// Add the associated classes and namespaces for
// argument-dependent lookup with an argument of type T
// (C++ [basic.lookup.koenig]p2).
static void
addAssociatedClassesAndNamespaces(AssociatedLookup &Result, QualType Ty) {
  // C++ [basic.lookup.koenig]p2:
  //
  //   For each argument type T in the function call, there is a set
  //   of zero or more associated namespaces and a set of zero or more
  //   associated classes to be considered. The sets of namespaces and
  //   classes is determined entirely by the types of the function
  //   arguments (and the namespace of any template template
  //   argument). Typedef names and using-declarations used to specify
  //   the types do not contribute to this set. The sets of namespaces
  //   and classes are determined in the following way:

  SmallVector<const Type *, 16> Queue;
  const Type *T = Ty->getCanonicalTypeInternal().getTypePtr();

  while (true) {
    switch (T->getTypeClass()) {

#define TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#define ABSTRACT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.inc"
      // T is canonical.  We can also ignore dependent types because
      // we don't need to do ADL at the definition point, but if we
      // wanted to implement template export (or if we find some other
      // use for associated classes and namespaces...) this would be
      // wrong.
      break;

    //    -- If T is a pointer to U or an array of U, its associated
    //       namespaces and classes are those associated with U.
    case Type::Pointer:
      T = cast<PointerType>(T)->getPointeeType().getTypePtr();
      continue;
    case Type::ConstantArray:
    case Type::IncompleteArray:
    case Type::VariableArray:
      T = cast<ArrayType>(T)->getElementType().getTypePtr();
      continue;

    //     -- If T is a fundamental type, its associated sets of
    //        namespaces and classes are both empty.
    case Type::Builtin:
      break;

    //     -- If T is a class type (including unions), its associated
    //        classes are: the class itself; the class of which it is
    //        a member, if any; and its direct and indirect base classes.
    //        Its associated namespaces are the innermost enclosing
    //        namespaces of its associated classes.
    case Type::Record: {
      CXXRecordDecl *Class =
          cast<CXXRecordDecl>(cast<RecordType>(T)->getDecl());
      addAssociatedClassesAndNamespaces(Result, Class);
      break;
    }

    //     -- If T is an enumeration type, its associated namespace
    //        is the innermost enclosing namespace of its declaration.
    //        If it is a class member, its associated class is the
    //        member’s class; else it has no associated class.
    case Type::Enum: {
      EnumDecl *Enum = cast<EnumType>(T)->getDecl();

      DeclContext *Ctx = Enum->getDeclContext();
      if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
        Result.Classes.insert(EnclosingClass);

      // Add the associated namespace for this enumeration.
      CollectEnclosingNamespace(Result.Namespaces, Ctx);

      break;
    }

    //     -- If T is a function type, its associated namespaces and
    //        classes are those associated with the function parameter
    //        types and those associated with the return type.
    case Type::FunctionProto: {
      const FunctionProtoType *Proto = cast<FunctionProtoType>(T);
      for (const auto &Arg : Proto->param_types())
        Queue.push_back(Arg.getTypePtr());
      // fallthrough
      [[fallthrough]];
    }
    case Type::FunctionNoProto: {
      const FunctionType *FnType = cast<FunctionType>(T);
      T = FnType->getReturnType().getTypePtr();
      continue;
    }

    //     -- If T is a pointer to a member function of a class X, its
    //        associated namespaces and classes are those associated
    //        with the function parameter types and return type,
    //        together with those associated with X.
    //
    //     -- If T is a pointer to a data member of class X, its
    //        associated namespaces and classes are those associated
    //        with the member type together with those associated with
    //        X.
    case Type::MemberPointer: {
      const MemberPointerType *MemberPtr = cast<MemberPointerType>(T);

      // Queue up the class type into which this points.
      Queue.push_back(MemberPtr->getClass());

      // And directly continue with the pointee type.
      T = MemberPtr->getPointeeType().getTypePtr();
      continue;
    }

    // As an extension, treat this like a normal pointer.
    case Type::BlockPointer:
      T = cast<BlockPointerType>(T)->getPointeeType().getTypePtr();
      continue;

    // References aren't covered by the standard, but that's such an
    // obvious defect that we cover them anyway.
    case Type::LValueReference:
    case Type::RValueReference:
      T = cast<ReferenceType>(T)->getPointeeType().getTypePtr();
      continue;

    // These are fundamental types.
    case Type::Vector:
    case Type::ExtVector:
    case Type::ConstantMatrix:
    case Type::Complex:
    case Type::BitInt:
      break;

    // Non-deduced auto types only get here for error cases.
    case Type::Auto:
    case Type::DeducedTemplateSpecialization:
      break;

    // If T is an Objective-C object or interface type, or a pointer to an
    // object or interface type, the associated namespace is the global
    // namespace.
    case Type::ObjCObject:
    case Type::ObjCInterface:
    case Type::ObjCObjectPointer:
      Result.Namespaces.insert(Result.S.Context.getTranslationUnitDecl());
      break;

    // Atomic types are just wrappers; use the associations of the
    // contained type.
    case Type::Atomic:
      T = cast<AtomicType>(T)->getValueType().getTypePtr();
      continue;
    case Type::Pipe:
      T = cast<PipeType>(T)->getElementType().getTypePtr();
      continue;

    // Array parameter types are treated as fundamental types.
    case Type::ArrayParameter:
      break;
    }

    if (Queue.empty())
      break;
    T = Queue.pop_back_val();
  }
}

void Sema::FindAssociatedClassesAndNamespaces(
    SourceLocation InstantiationLoc, ArrayRef<Expr *> Args,
    AssociatedNamespaceSet &AssociatedNamespaces,
    AssociatedClassSet &AssociatedClasses) {
  AssociatedNamespaces.clear();
  AssociatedClasses.clear();

  AssociatedLookup Result(*this, InstantiationLoc,
                          AssociatedNamespaces, AssociatedClasses);

  // C++ [basic.lookup.koenig]p2:
  //   For each argument type T in the function call, there is a set
  //   of zero or more associated namespaces and a set of zero or more
  //   associated classes to be considered. The sets of namespaces and
  //   classes is determined entirely by the types of the function
  //   arguments (and the namespace of any template template
  //   argument).
  for (unsigned ArgIdx = 0; ArgIdx != Args.size(); ++ArgIdx) {
    Expr *Arg = Args[ArgIdx];

    if (Arg->getType() != Context.OverloadTy) {
      addAssociatedClassesAndNamespaces(Result, Arg->getType());
      continue;
    }

    // [...] In addition, if the argument is the name or address of a
    // set of overloaded functions and/or function templates, its
    // associated classes and namespaces are the union of those
    // associated with each of the members of the set: the namespace
    // in which the function or function template is defined and the
    // classes and namespaces associated with its (non-dependent)
    // parameter types and return type.
    OverloadExpr *OE = OverloadExpr::find(Arg).Expression;

    for (const NamedDecl *D : OE->decls()) {
      // Look through any using declarations to find the underlying function.
      const FunctionDecl *FDecl = D->getUnderlyingDecl()->getAsFunction();

      // Add the classes and namespaces associated with the parameter
      // types and return type of this function.
      addAssociatedClassesAndNamespaces(Result, FDecl->getType());
    }
  }
}

NamedDecl *Sema::LookupSingleName(Scope *S, DeclarationName Name,
                                  SourceLocation Loc,
                                  LookupNameKind NameKind,
                                  RedeclarationKind Redecl) {
  LookupResult R(*this, Name, Loc, NameKind, Redecl);
  LookupName(R, S);
  return R.getAsSingle<NamedDecl>();
}

void Sema::LookupOverloadedOperatorName(OverloadedOperatorKind Op, Scope *S,
                                        UnresolvedSetImpl &Functions) {
  // C++ [over.match.oper]p3:
  //     -- The set of non-member candidates is the result of the
  //        unqualified lookup of operator@ in the context of the
  //        expression according to the usual rules for name lookup in
  //        unqualified function calls (3.4.2) except that all member
  //        functions are ignored.
  DeclarationName OpName = Context.DeclarationNames.getCXXOperatorName(Op);
  LookupResult Operators(*this, OpName, SourceLocation(), LookupOperatorName);
  LookupName(Operators, S);

  assert(!Operators.isAmbiguous() && "Operator lookup cannot be ambiguous");
  Functions.append(Operators.begin(), Operators.end());
}

Sema::SpecialMemberOverloadResult
Sema::LookupSpecialMember(CXXRecordDecl *RD, CXXSpecialMemberKind SM,
                          bool ConstArg, bool VolatileArg, bool RValueThis,
                          bool ConstThis, bool VolatileThis) {
  assert(CanDeclareSpecialMemberFunction(RD) &&
         "doing special member lookup into record that isn't fully complete");
  RD = RD->getDefinition();
  if (RValueThis || ConstThis || VolatileThis)
    assert((SM == CXXSpecialMemberKind::CopyAssignment ||
            SM == CXXSpecialMemberKind::MoveAssignment) &&
           "constructors and destructors always have unqualified lvalue this");
  if (ConstArg || VolatileArg)
    assert((SM != CXXSpecialMemberKind::DefaultConstructor &&
            SM != CXXSpecialMemberKind::Destructor) &&
           "parameter-less special members can't have qualified arguments");

  // FIXME: Get the caller to pass in a location for the lookup.
  SourceLocation LookupLoc = RD->getLocation();

  llvm::FoldingSetNodeID ID;
  ID.AddPointer(RD);
  ID.AddInteger(llvm::to_underlying(SM));
  ID.AddInteger(ConstArg);
  ID.AddInteger(VolatileArg);
  ID.AddInteger(RValueThis);
  ID.AddInteger(ConstThis);
  ID.AddInteger(VolatileThis);

  void *InsertPoint;
  SpecialMemberOverloadResultEntry *Result =
    SpecialMemberCache.FindNodeOrInsertPos(ID, InsertPoint);

  // This was already cached
  if (Result)
    return *Result;

  Result = BumpAlloc.Allocate<SpecialMemberOverloadResultEntry>();
  Result = new (Result) SpecialMemberOverloadResultEntry(ID);
  SpecialMemberCache.InsertNode(Result, InsertPoint);

  if (SM == CXXSpecialMemberKind::Destructor) {
    if (RD->needsImplicitDestructor()) {
      runWithSufficientStackSpace(RD->getLocation(), [&] {
        DeclareImplicitDestructor(RD);
      });
    }
    CXXDestructorDecl *DD = RD->getDestructor();
    Result->setMethod(DD);
    Result->setKind(DD && !DD->isDeleted()
                        ? SpecialMemberOverloadResult::Success
                        : SpecialMemberOverloadResult::NoMemberOrDeleted);
    return *Result;
  }

  // Prepare for overload resolution. Here we construct a synthetic argument
  // if necessary and make sure that implicit functions are declared.
  CanQualType CanTy = Context.getCanonicalType(Context.getTagDeclType(RD));
  DeclarationName Name;
  Expr *Arg = nullptr;
  unsigned NumArgs;

  QualType ArgType = CanTy;
  ExprValueKind VK = VK_LValue;

  if (SM == CXXSpecialMemberKind::DefaultConstructor) {
    Name = Context.DeclarationNames.getCXXConstructorName(CanTy);
    NumArgs = 0;
    if (RD->needsImplicitDefaultConstructor()) {
      runWithSufficientStackSpace(RD->getLocation(), [&] {
        DeclareImplicitDefaultConstructor(RD);
      });
    }
  } else {
    if (SM == CXXSpecialMemberKind::CopyConstructor ||
        SM == CXXSpecialMemberKind::MoveConstructor) {
      Name = Context.DeclarationNames.getCXXConstructorName(CanTy);
      if (RD->needsImplicitCopyConstructor()) {
        runWithSufficientStackSpace(RD->getLocation(), [&] {
          DeclareImplicitCopyConstructor(RD);
        });
      }
      if (getLangOpts().CPlusPlus11 && RD->needsImplicitMoveConstructor()) {
        runWithSufficientStackSpace(RD->getLocation(), [&] {
          DeclareImplicitMoveConstructor(RD);
        });
      }
    } else {
      Name = Context.DeclarationNames.getCXXOperatorName(OO_Equal);
      if (RD->needsImplicitCopyAssignment()) {
        runWithSufficientStackSpace(RD->getLocation(), [&] {
          DeclareImplicitCopyAssignment(RD);
        });
      }
      if (getLangOpts().CPlusPlus11 && RD->needsImplicitMoveAssignment()) {
        runWithSufficientStackSpace(RD->getLocation(), [&] {
          DeclareImplicitMoveAssignment(RD);
        });
      }
    }

    if (ConstArg)
      ArgType.addConst();
    if (VolatileArg)
      ArgType.addVolatile();

    // This isn't /really/ specified by the standard, but it's implied
    // we should be working from a PRValue in the case of move to ensure
    // that we prefer to bind to rvalue references, and an LValue in the
    // case of copy to ensure we don't bind to rvalue references.
    // Possibly an XValue is actually correct in the case of move, but
    // there is no semantic difference for class types in this restricted
    // case.
    if (SM == CXXSpecialMemberKind::CopyConstructor ||
        SM == CXXSpecialMemberKind::CopyAssignment)
      VK = VK_LValue;
    else
      VK = VK_PRValue;
  }

  OpaqueValueExpr FakeArg(LookupLoc, ArgType, VK);

  if (SM != CXXSpecialMemberKind::DefaultConstructor) {
    NumArgs = 1;
    Arg = &FakeArg;
  }

  // Create the object argument
  QualType ThisTy = CanTy;
  if (ConstThis)
    ThisTy.addConst();
  if (VolatileThis)
    ThisTy.addVolatile();
  Expr::Classification Classification =
      OpaqueValueExpr(LookupLoc, ThisTy, RValueThis ? VK_PRValue : VK_LValue)
          .Classify(Context);

  // Now we perform lookup on the name we computed earlier and do overload
  // resolution. Lookup is only performed directly into the class since there
  // will always be a (possibly implicit) declaration to shadow any others.
  OverloadCandidateSet OCS(LookupLoc, OverloadCandidateSet::CSK_Normal);
  DeclContext::lookup_result R = RD->lookup(Name);

  if (R.empty()) {
    // We might have no default constructor because we have a lambda's closure
    // type, rather than because there's some other declared constructor.
    // Every class has a copy/move constructor, copy/move assignment, and
    // destructor.
    assert(SM == CXXSpecialMemberKind::DefaultConstructor &&
           "lookup for a constructor or assignment operator was empty");
    Result->setMethod(nullptr);
    Result->setKind(SpecialMemberOverloadResult::NoMemberOrDeleted);
    return *Result;
  }

  // Copy the candidates as our processing of them may load new declarations
  // from an external source and invalidate lookup_result.
  SmallVector<NamedDecl *, 8> Candidates(R.begin(), R.end());

  for (NamedDecl *CandDecl : Candidates) {
    if (CandDecl->isInvalidDecl())
      continue;

    DeclAccessPair Cand = DeclAccessPair::make(CandDecl, AS_public);
    auto CtorInfo = getConstructorInfo(Cand);
    if (CXXMethodDecl *M = dyn_cast<CXXMethodDecl>(Cand->getUnderlyingDecl())) {
      if (SM == CXXSpecialMemberKind::CopyAssignment ||
          SM == CXXSpecialMemberKind::MoveAssignment)
        AddMethodCandidate(M, Cand, RD, ThisTy, Classification,
                           llvm::ArrayRef(&Arg, NumArgs), OCS, true);
      else if (CtorInfo)
        AddOverloadCandidate(CtorInfo.Constructor, CtorInfo.FoundDecl,
                             llvm::ArrayRef(&Arg, NumArgs), OCS,
                             /*SuppressUserConversions*/ true);
      else
        AddOverloadCandidate(M, Cand, llvm::ArrayRef(&Arg, NumArgs), OCS,
                             /*SuppressUserConversions*/ true);
    } else if (FunctionTemplateDecl *Tmpl =
                 dyn_cast<FunctionTemplateDecl>(Cand->getUnderlyingDecl())) {
      if (SM == CXXSpecialMemberKind::CopyAssignment ||
          SM == CXXSpecialMemberKind::MoveAssignment)
        AddMethodTemplateCandidate(Tmpl, Cand, RD, nullptr, ThisTy,
                                   Classification,
                                   llvm::ArrayRef(&Arg, NumArgs), OCS, true);
      else if (CtorInfo)
        AddTemplateOverloadCandidate(CtorInfo.ConstructorTmpl,
                                     CtorInfo.FoundDecl, nullptr,
                                     llvm::ArrayRef(&Arg, NumArgs), OCS, true);
      else
        AddTemplateOverloadCandidate(Tmpl, Cand, nullptr,
                                     llvm::ArrayRef(&Arg, NumArgs), OCS, true);
    } else {
      assert(isa<UsingDecl>(Cand.getDecl()) &&
             "illegal Kind of operator = Decl");
    }
  }

  OverloadCandidateSet::iterator Best;
  switch (OCS.BestViableFunction(*this, LookupLoc, Best)) {
    case OR_Success:
      Result->setMethod(cast<CXXMethodDecl>(Best->Function));
      Result->setKind(SpecialMemberOverloadResult::Success);
      break;

    case OR_Deleted:
      Result->setMethod(cast<CXXMethodDecl>(Best->Function));
      Result->setKind(SpecialMemberOverloadResult::NoMemberOrDeleted);
      break;

    case OR_Ambiguous:
      Result->setMethod(nullptr);
      Result->setKind(SpecialMemberOverloadResult::Ambiguous);
      break;

    case OR_No_Viable_Function:
      Result->setMethod(nullptr);
      Result->setKind(SpecialMemberOverloadResult::NoMemberOrDeleted);
      break;
  }

  return *Result;
}

CXXConstructorDecl *Sema::LookupDefaultConstructor(CXXRecordDecl *Class) {
  SpecialMemberOverloadResult Result =
      LookupSpecialMember(Class, CXXSpecialMemberKind::DefaultConstructor,
                          false, false, false, false, false);

  return cast_or_null<CXXConstructorDecl>(Result.getMethod());
}

CXXConstructorDecl *Sema::LookupCopyingConstructor(CXXRecordDecl *Class,
                                                   unsigned Quals) {
  assert(!(Quals & ~(Qualifiers::Const | Qualifiers::Volatile)) &&
         "non-const, non-volatile qualifiers for copy ctor arg");
  SpecialMemberOverloadResult Result = LookupSpecialMember(
      Class, CXXSpecialMemberKind::CopyConstructor, Quals & Qualifiers::Const,
      Quals & Qualifiers::Volatile, false, false, false);

  return cast_or_null<CXXConstructorDecl>(Result.getMethod());
}

CXXConstructorDecl *Sema::LookupMovingConstructor(CXXRecordDecl *Class,
                                                  unsigned Quals) {
  SpecialMemberOverloadResult Result = LookupSpecialMember(
      Class, CXXSpecialMemberKind::MoveConstructor, Quals & Qualifiers::Const,
      Quals & Qualifiers::Volatile, false, false, false);

  return cast_or_null<CXXConstructorDecl>(Result.getMethod());
}

DeclContext::lookup_result Sema::LookupConstructors(CXXRecordDecl *Class) {
  // If the implicit constructors have not yet been declared, do so now.
  if (CanDeclareSpecialMemberFunction(Class)) {
    runWithSufficientStackSpace(Class->getLocation(), [&] {
      if (Class->needsImplicitDefaultConstructor())
        DeclareImplicitDefaultConstructor(Class);
      if (Class->needsImplicitCopyConstructor())
        DeclareImplicitCopyConstructor(Class);
      if (getLangOpts().CPlusPlus11 && Class->needsImplicitMoveConstructor())
        DeclareImplicitMoveConstructor(Class);
    });
  }

  CanQualType T = Context.getCanonicalType(Context.getTypeDeclType(Class));
  DeclarationName Name = Context.DeclarationNames.getCXXConstructorName(T);
  return Class->lookup(Name);
}

CXXMethodDecl *Sema::LookupCopyingAssignment(CXXRecordDecl *Class,
                                             unsigned Quals, bool RValueThis,
                                             unsigned ThisQuals) {
  assert(!(Quals & ~(Qualifiers::Const | Qualifiers::Volatile)) &&
         "non-const, non-volatile qualifiers for copy assignment arg");
  assert(!(ThisQuals & ~(Qualifiers::Const | Qualifiers::Volatile)) &&
         "non-const, non-volatile qualifiers for copy assignment this");
  SpecialMemberOverloadResult Result = LookupSpecialMember(
      Class, CXXSpecialMemberKind::CopyAssignment, Quals & Qualifiers::Const,
      Quals & Qualifiers::Volatile, RValueThis, ThisQuals & Qualifiers::Const,
      ThisQuals & Qualifiers::Volatile);

  return Result.getMethod();
}

CXXMethodDecl *Sema::LookupMovingAssignment(CXXRecordDecl *Class,
                                            unsigned Quals,
                                            bool RValueThis,
                                            unsigned ThisQuals) {
  assert(!(ThisQuals & ~(Qualifiers::Const | Qualifiers::Volatile)) &&
         "non-const, non-volatile qualifiers for copy assignment this");
  SpecialMemberOverloadResult Result = LookupSpecialMember(
      Class, CXXSpecialMemberKind::MoveAssignment, Quals & Qualifiers::Const,
      Quals & Qualifiers::Volatile, RValueThis, ThisQuals & Qualifiers::Const,
      ThisQuals & Qualifiers::Volatile);

  return Result.getMethod();
}

CXXDestructorDecl *Sema::LookupDestructor(CXXRecordDecl *Class) {
  return cast_or_null<CXXDestructorDecl>(
      LookupSpecialMember(Class, CXXSpecialMemberKind::Destructor, false, false,
                          false, false, false)
          .getMethod());
}

Sema::LiteralOperatorLookupResult
Sema::LookupLiteralOperator(Scope *S, LookupResult &R,
                            ArrayRef<QualType> ArgTys, bool AllowRaw,
                            bool AllowTemplate, bool AllowStringTemplatePack,
                            bool DiagnoseMissing, StringLiteral *StringLit) {
  LookupName(R, S);
  assert(R.getResultKind() != LookupResult::Ambiguous &&
         "literal operator lookup can't be ambiguous");

  // Filter the lookup results appropriately.
  LookupResult::Filter F = R.makeFilter();

  bool AllowCooked = true;
  bool FoundRaw = false;
  bool FoundTemplate = false;
  bool FoundStringTemplatePack = false;
  bool FoundCooked = false;

  while (F.hasNext()) {
    Decl *D = F.next();
    if (UsingShadowDecl *USD = dyn_cast<UsingShadowDecl>(D))
      D = USD->getTargetDecl();

    // If the declaration we found is invalid, skip it.
    if (D->isInvalidDecl()) {
      F.erase();
      continue;
    }

    bool IsRaw = false;
    bool IsTemplate = false;
    bool IsStringTemplatePack = false;
    bool IsCooked = false;

    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->getNumParams() == 1 &&
          FD->getParamDecl(0)->getType()->getAs<PointerType>())
        IsRaw = true;
      else if (FD->getNumParams() == ArgTys.size()) {
        IsCooked = true;
        for (unsigned ArgIdx = 0; ArgIdx != ArgTys.size(); ++ArgIdx) {
          QualType ParamTy = FD->getParamDecl(ArgIdx)->getType();
          if (!Context.hasSameUnqualifiedType(ArgTys[ArgIdx], ParamTy)) {
            IsCooked = false;
            break;
          }
        }
      }
    }
    if (FunctionTemplateDecl *FD = dyn_cast<FunctionTemplateDecl>(D)) {
      TemplateParameterList *Params = FD->getTemplateParameters();
      if (Params->size() == 1) {
        IsTemplate = true;
        if (!Params->getParam(0)->isTemplateParameterPack() && !StringLit) {
          // Implied but not stated: user-defined integer and floating literals
          // only ever use numeric literal operator templates, not templates
          // taking a parameter of class type.
          F.erase();
          continue;
        }

        // A string literal template is only considered if the string literal
        // is a well-formed template argument for the template parameter.
        if (StringLit) {
          SFINAETrap Trap(*this);
          SmallVector<TemplateArgument, 1> SugaredChecked, CanonicalChecked;
          TemplateArgumentLoc Arg(TemplateArgument(StringLit), StringLit);
          if (CheckTemplateArgument(
                  Params->getParam(0), Arg, FD, R.getNameLoc(), R.getNameLoc(),
                  0, SugaredChecked, CanonicalChecked, CTAK_Specified) ||
              Trap.hasErrorOccurred())
            IsTemplate = false;
        }
      } else {
        IsStringTemplatePack = true;
      }
    }

    if (AllowTemplate && StringLit && IsTemplate) {
      FoundTemplate = true;
      AllowRaw = false;
      AllowCooked = false;
      AllowStringTemplatePack = false;
      if (FoundRaw || FoundCooked || FoundStringTemplatePack) {
        F.restart();
        FoundRaw = FoundCooked = FoundStringTemplatePack = false;
      }
    } else if (AllowCooked && IsCooked) {
      FoundCooked = true;
      AllowRaw = false;
      AllowTemplate = StringLit;
      AllowStringTemplatePack = false;
      if (FoundRaw || FoundTemplate || FoundStringTemplatePack) {
        // Go through again and remove the raw and template decls we've
        // already found.
        F.restart();
        FoundRaw = FoundTemplate = FoundStringTemplatePack = false;
      }
    } else if (AllowRaw && IsRaw) {
      FoundRaw = true;
    } else if (AllowTemplate && IsTemplate) {
      FoundTemplate = true;
    } else if (AllowStringTemplatePack && IsStringTemplatePack) {
      FoundStringTemplatePack = true;
    } else {
      F.erase();
    }
  }

  F.done();

  // Per C++20 [lex.ext]p5, we prefer the template form over the non-template
  // form for string literal operator templates.
  if (StringLit && FoundTemplate)
    return LOLR_Template;

  // C++11 [lex.ext]p3, p4: If S contains a literal operator with a matching
  // parameter type, that is used in preference to a raw literal operator
  // or literal operator template.
  if (FoundCooked)
    return LOLR_Cooked;

  // C++11 [lex.ext]p3, p4: S shall contain a raw literal operator or a literal
  // operator template, but not both.
  if (FoundRaw && FoundTemplate) {
    Diag(R.getNameLoc(), diag::err_ovl_ambiguous_call) << R.getLookupName();
    for (const NamedDecl *D : R)
      NoteOverloadCandidate(D, D->getUnderlyingDecl()->getAsFunction());
    return LOLR_Error;
  }

  if (FoundRaw)
    return LOLR_Raw;

  if (FoundTemplate)
    return LOLR_Template;

  if (FoundStringTemplatePack)
    return LOLR_StringTemplatePack;

  // Didn't find anything we could use.
  if (DiagnoseMissing) {
    Diag(R.getNameLoc(), diag::err_ovl_no_viable_literal_operator)
        << R.getLookupName() << (int)ArgTys.size() << ArgTys[0]
        << (ArgTys.size() == 2 ? ArgTys[1] : QualType()) << AllowRaw
        << (AllowTemplate || AllowStringTemplatePack);
    return LOLR_Error;
  }

  return LOLR_ErrorNoDiagnostic;
}

void ADLResult::insert(NamedDecl *New) {
  NamedDecl *&Old = Decls[cast<NamedDecl>(New->getCanonicalDecl())];

  // If we haven't yet seen a decl for this key, or the last decl
  // was exactly this one, we're done.
  if (Old == nullptr || Old == New) {
    Old = New;
    return;
  }

  // Otherwise, decide which is a more recent redeclaration.
  FunctionDecl *OldFD = Old->getAsFunction();
  FunctionDecl *NewFD = New->getAsFunction();

  FunctionDecl *Cursor = NewFD;
  while (true) {
    Cursor = Cursor->getPreviousDecl();

    // If we got to the end without finding OldFD, OldFD is the newer
    // declaration;  leave things as they are.
    if (!Cursor) return;

    // If we do find OldFD, then NewFD is newer.
    if (Cursor == OldFD) break;

    // Otherwise, keep looking.
  }

  Old = New;
}

void Sema::ArgumentDependentLookup(DeclarationName Name, SourceLocation Loc,
                                   ArrayRef<Expr *> Args, ADLResult &Result) {
  // Find all of the associated namespaces and classes based on the
  // arguments we have.
  AssociatedNamespaceSet AssociatedNamespaces;
  AssociatedClassSet AssociatedClasses;
  FindAssociatedClassesAndNamespaces(Loc, Args,
                                     AssociatedNamespaces,
                                     AssociatedClasses);

  // C++ [basic.lookup.argdep]p3:
  //   Let X be the lookup set produced by unqualified lookup (3.4.1)
  //   and let Y be the lookup set produced by argument dependent
  //   lookup (defined as follows). If X contains [...] then Y is
  //   empty. Otherwise Y is the set of declarations found in the
  //   namespaces associated with the argument types as described
  //   below. The set of declarations found by the lookup of the name
  //   is the union of X and Y.
  //
  // Here, we compute Y and add its members to the overloaded
  // candidate set.
  for (auto *NS : AssociatedNamespaces) {
    //   When considering an associated namespace, the lookup is the
    //   same as the lookup performed when the associated namespace is
    //   used as a qualifier (3.4.3.2) except that:
    //
    //     -- Any using-directives in the associated namespace are
    //        ignored.
    //
    //     -- Any namespace-scope friend functions declared in
    //        associated classes are visible within their respective
    //        namespaces even if they are not visible during an ordinary
    //        lookup (11.4).
    //
    // C++20 [basic.lookup.argdep] p4.3
    //     -- are exported, are attached to a named module M, do not appear
    //        in the translation unit containing the point of the lookup, and
    //        have the same innermost enclosing non-inline namespace scope as
    //        a declaration of an associated entity attached to M.
    DeclContext::lookup_result R = NS->lookup(Name);
    for (auto *D : R) {
      auto *Underlying = D;
      if (auto *USD = dyn_cast<UsingShadowDecl>(D))
        Underlying = USD->getTargetDecl();

      if (!isa<FunctionDecl>(Underlying) &&
          !isa<FunctionTemplateDecl>(Underlying))
        continue;

      // The declaration is visible to argument-dependent lookup if either
      // it's ordinarily visible or declared as a friend in an associated
      // class.
      bool Visible = false;
      for (D = D->getMostRecentDecl(); D;
           D = cast_or_null<NamedDecl>(D->getPreviousDecl())) {
        if (D->getIdentifierNamespace() & Decl::IDNS_Ordinary) {
          if (isVisible(D)) {
            Visible = true;
            break;
          }

          if (!getLangOpts().CPlusPlusModules)
            continue;

          if (D->isInExportDeclContext()) {
            Module *FM = D->getOwningModule();
            // C++20 [basic.lookup.argdep] p4.3 .. are exported ...
            // exports are only valid in module purview and outside of any
            // PMF (although a PMF should not even be present in a module
            // with an import).
            assert(FM && FM->isNamedModule() && !FM->isPrivateModule() &&
                   "bad export context");
            // .. are attached to a named module M, do not appear in the
            // translation unit containing the point of the lookup..
            if (D->isInAnotherModuleUnit() &&
                llvm::any_of(AssociatedClasses, [&](auto *E) {
                  // ... and have the same innermost enclosing non-inline
                  // namespace scope as a declaration of an associated entity
                  // attached to M
                  if (E->getOwningModule() != FM)
                    return false;
                  // TODO: maybe this could be cached when generating the
                  // associated namespaces / entities.
                  DeclContext *Ctx = E->getDeclContext();
                  while (!Ctx->isFileContext() || Ctx->isInlineNamespace())
                    Ctx = Ctx->getParent();
                  return Ctx == NS;
                })) {
              Visible = true;
              break;
            }
          }
        } else if (D->getFriendObjectKind()) {
          auto *RD = cast<CXXRecordDecl>(D->getLexicalDeclContext());
          // [basic.lookup.argdep]p4:
          //   Argument-dependent lookup finds all declarations of functions and
          //   function templates that
          //  - ...
          //  - are declared as a friend ([class.friend]) of any class with a
          //  reachable definition in the set of associated entities,
          //
          // FIXME: If there's a merged definition of D that is reachable, then
          // the friend declaration should be considered.
          if (AssociatedClasses.count(RD) && isReachable(D)) {
            Visible = true;
            break;
          }
        }
      }

      // FIXME: Preserve D as the FoundDecl.
      if (Visible)
        Result.insert(Underlying);
    }
  }
}

//----------------------------------------------------------------------------
// Search for all visible declarations.
//----------------------------------------------------------------------------
VisibleDeclConsumer::~VisibleDeclConsumer() { }

bool VisibleDeclConsumer::includeHiddenDecls() const { return false; }

namespace {

class ShadowContextRAII;

class VisibleDeclsRecord {
public:
  /// An entry in the shadow map, which is optimized to store a
  /// single declaration (the common case) but can also store a list
  /// of declarations.
  typedef llvm::TinyPtrVector<NamedDecl*> ShadowMapEntry;

private:
  /// A mapping from declaration names to the declarations that have
  /// this name within a particular scope.
  typedef llvm::DenseMap<DeclarationName, ShadowMapEntry> ShadowMap;

  /// A list of shadow maps, which is used to model name hiding.
  std::list<ShadowMap> ShadowMaps;

  /// The declaration contexts we have already visited.
  llvm::SmallPtrSet<DeclContext *, 8> VisitedContexts;

  friend class ShadowContextRAII;

public:
  /// Determine whether we have already visited this context
  /// (and, if not, note that we are going to visit that context now).
  bool visitedContext(DeclContext *Ctx) {
    return !VisitedContexts.insert(Ctx).second;
  }

  bool alreadyVisitedContext(DeclContext *Ctx) {
    return VisitedContexts.count(Ctx);
  }

  /// Determine whether the given declaration is hidden in the
  /// current scope.
  ///
  /// \returns the declaration that hides the given declaration, or
  /// NULL if no such declaration exists.
  NamedDecl *checkHidden(NamedDecl *ND);

  /// Add a declaration to the current shadow map.
  void add(NamedDecl *ND) {
    ShadowMaps.back()[ND->getDeclName()].push_back(ND);
  }
};

/// RAII object that records when we've entered a shadow context.
class ShadowContextRAII {
  VisibleDeclsRecord &Visible;

  typedef VisibleDeclsRecord::ShadowMap ShadowMap;

public:
  ShadowContextRAII(VisibleDeclsRecord &Visible) : Visible(Visible) {
    Visible.ShadowMaps.emplace_back();
  }

  ~ShadowContextRAII() {
    Visible.ShadowMaps.pop_back();
  }
};

} // end anonymous namespace

NamedDecl *VisibleDeclsRecord::checkHidden(NamedDecl *ND) {
  unsigned IDNS = ND->getIdentifierNamespace();
  std::list<ShadowMap>::reverse_iterator SM = ShadowMaps.rbegin();
  for (std::list<ShadowMap>::reverse_iterator SMEnd = ShadowMaps.rend();
       SM != SMEnd; ++SM) {
    ShadowMap::iterator Pos = SM->find(ND->getDeclName());
    if (Pos == SM->end())
      continue;

    for (auto *D : Pos->second) {
      // A tag declaration does not hide a non-tag declaration.
      if (D->hasTagIdentifierNamespace() &&
          (IDNS & (Decl::IDNS_Member | Decl::IDNS_Ordinary |
                   Decl::IDNS_ObjCProtocol)))
        continue;

      // Protocols are in distinct namespaces from everything else.
      if (((D->getIdentifierNamespace() & Decl::IDNS_ObjCProtocol)
           || (IDNS & Decl::IDNS_ObjCProtocol)) &&
          D->getIdentifierNamespace() != IDNS)
        continue;

      // Functions and function templates in the same scope overload
      // rather than hide.  FIXME: Look for hiding based on function
      // signatures!
      if (D->getUnderlyingDecl()->isFunctionOrFunctionTemplate() &&
          ND->getUnderlyingDecl()->isFunctionOrFunctionTemplate() &&
          SM == ShadowMaps.rbegin())
        continue;

      // A shadow declaration that's created by a resolved using declaration
      // is not hidden by the same using declaration.
      if (isa<UsingShadowDecl>(ND) && isa<UsingDecl>(D) &&
          cast<UsingShadowDecl>(ND)->getIntroducer() == D)
        continue;

      // We've found a declaration that hides this one.
      return D;
    }
  }

  return nullptr;
}

namespace {
class LookupVisibleHelper {
public:
  LookupVisibleHelper(VisibleDeclConsumer &Consumer, bool IncludeDependentBases,
                      bool LoadExternal)
      : Consumer(Consumer), IncludeDependentBases(IncludeDependentBases),
        LoadExternal(LoadExternal) {}

  void lookupVisibleDecls(Sema &SemaRef, Scope *S, Sema::LookupNameKind Kind,
                          bool IncludeGlobalScope) {
    // Determine the set of using directives available during
    // unqualified name lookup.
    Scope *Initial = S;
    UnqualUsingDirectiveSet UDirs(SemaRef);
    if (SemaRef.getLangOpts().CPlusPlus) {
      // Find the first namespace or translation-unit scope.
      while (S && !isNamespaceOrTranslationUnitScope(S))
        S = S->getParent();

      UDirs.visitScopeChain(Initial, S);
    }
    UDirs.done();

    // Look for visible declarations.
    LookupResult Result(SemaRef, DeclarationName(), SourceLocation(), Kind);
    Result.setAllowHidden(Consumer.includeHiddenDecls());
    if (!IncludeGlobalScope)
      Visited.visitedContext(SemaRef.getASTContext().getTranslationUnitDecl());
    ShadowContextRAII Shadow(Visited);
    lookupInScope(Initial, Result, UDirs);
  }

  void lookupVisibleDecls(Sema &SemaRef, DeclContext *Ctx,
                          Sema::LookupNameKind Kind, bool IncludeGlobalScope) {
    LookupResult Result(SemaRef, DeclarationName(), SourceLocation(), Kind);
    Result.setAllowHidden(Consumer.includeHiddenDecls());
    if (!IncludeGlobalScope)
      Visited.visitedContext(SemaRef.getASTContext().getTranslationUnitDecl());

    ShadowContextRAII Shadow(Visited);
    lookupInDeclContext(Ctx, Result, /*QualifiedNameLookup=*/true,
                        /*InBaseClass=*/false);
  }

private:
  void lookupInDeclContext(DeclContext *Ctx, LookupResult &Result,
                           bool QualifiedNameLookup, bool InBaseClass) {
    if (!Ctx)
      return;

    // Make sure we don't visit the same context twice.
    if (Visited.visitedContext(Ctx->getPrimaryContext()))
      return;

    Consumer.EnteredContext(Ctx);

    // Outside C++, lookup results for the TU live on identifiers.
    if (isa<TranslationUnitDecl>(Ctx) &&
        !Result.getSema().getLangOpts().CPlusPlus) {
      auto &S = Result.getSema();
      auto &Idents = S.Context.Idents;

      // Ensure all external identifiers are in the identifier table.
      if (LoadExternal)
        if (IdentifierInfoLookup *External =
                Idents.getExternalIdentifierLookup()) {
          std::unique_ptr<IdentifierIterator> Iter(External->getIdentifiers());
          for (StringRef Name = Iter->Next(); !Name.empty();
               Name = Iter->Next())
            Idents.get(Name);
        }

      // Walk all lookup results in the TU for each identifier.
      for (const auto &Ident : Idents) {
        for (auto I = S.IdResolver.begin(Ident.getValue()),
                  E = S.IdResolver.end();
             I != E; ++I) {
          if (S.IdResolver.isDeclInScope(*I, Ctx)) {
            if (NamedDecl *ND = Result.getAcceptableDecl(*I)) {
              Consumer.FoundDecl(ND, Visited.checkHidden(ND), Ctx, InBaseClass);
              Visited.add(ND);
            }
          }
        }
      }

      return;
    }

    if (CXXRecordDecl *Class = dyn_cast<CXXRecordDecl>(Ctx))
      Result.getSema().ForceDeclarationOfImplicitMembers(Class);

    llvm::SmallVector<NamedDecl *, 4> DeclsToVisit;
    // We sometimes skip loading namespace-level results (they tend to be huge).
    bool Load = LoadExternal ||
                !(isa<TranslationUnitDecl>(Ctx) || isa<NamespaceDecl>(Ctx));
    // Enumerate all of the results in this context.
    for (DeclContextLookupResult R :
         Load ? Ctx->lookups()
              : Ctx->noload_lookups(/*PreserveInternalState=*/false))
      for (auto *D : R)
        // Rather than visit immediately, we put ND into a vector and visit
        // all decls, in order, outside of this loop. The reason is that
        // Consumer.FoundDecl() and LookupResult::getAcceptableDecl(D)
        // may invalidate the iterators used in the two
        // loops above.
        DeclsToVisit.push_back(D);

    for (auto *D : DeclsToVisit)
      if (auto *ND = Result.getAcceptableDecl(D)) {
        Consumer.FoundDecl(ND, Visited.checkHidden(ND), Ctx, InBaseClass);
        Visited.add(ND);
      }

    DeclsToVisit.clear();

    // Traverse using directives for qualified name lookup.
    if (QualifiedNameLookup) {
      ShadowContextRAII Shadow(Visited);
      for (auto *I : Ctx->using_directives()) {
        if (!Result.getSema().isVisible(I))
          continue;
        lookupInDeclContext(I->getNominatedNamespace(), Result,
                            QualifiedNameLookup, InBaseClass);
      }
    }

    // Traverse the contexts of inherited C++ classes.
    if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(Ctx)) {
      if (!Record->hasDefinition())
        return;

      for (const auto &B : Record->bases()) {
        QualType BaseType = B.getType();

        RecordDecl *RD;
        if (BaseType->isDependentType()) {
          if (!IncludeDependentBases) {
            // Don't look into dependent bases, because name lookup can't look
            // there anyway.
            continue;
          }
          const auto *TST = BaseType->getAs<TemplateSpecializationType>();
          if (!TST)
            continue;
          TemplateName TN = TST->getTemplateName();
          const auto *TD =
              dyn_cast_or_null<ClassTemplateDecl>(TN.getAsTemplateDecl());
          if (!TD)
            continue;
          RD = TD->getTemplatedDecl();
        } else {
          const auto *Record = BaseType->getAs<RecordType>();
          if (!Record)
            continue;
          RD = Record->getDecl();
        }

        // FIXME: It would be nice to be able to determine whether referencing
        // a particular member would be ambiguous. For example, given
        //
        //   struct A { int member; };
        //   struct B { int member; };
        //   struct C : A, B { };
        //
        //   void f(C *c) { c->### }
        //
        // accessing 'member' would result in an ambiguity. However, we
        // could be smart enough to qualify the member with the base
        // class, e.g.,
        //
        //   c->B::member
        //
        // or
        //
        //   c->A::member

        // Find results in this base class (and its bases).
        ShadowContextRAII Shadow(Visited);
        lookupInDeclContext(RD, Result, QualifiedNameLookup,
                            /*InBaseClass=*/true);
      }
    }

    // Traverse the contexts of Objective-C classes.
    if (ObjCInterfaceDecl *IFace = dyn_cast<ObjCInterfaceDecl>(Ctx)) {
      // Traverse categories.
      for (auto *Cat : IFace->visible_categories()) {
        ShadowContextRAII Shadow(Visited);
        lookupInDeclContext(Cat, Result, QualifiedNameLookup,
                            /*InBaseClass=*/false);
      }

      // Traverse protocols.
      for (auto *I : IFace->all_referenced_protocols()) {
        ShadowContextRAII Shadow(Visited);
        lookupInDeclContext(I, Result, QualifiedNameLookup,
                            /*InBaseClass=*/false);
      }

      // Traverse the superclass.
      if (IFace->getSuperClass()) {
        ShadowContextRAII Shadow(Visited);
        lookupInDeclContext(IFace->getSuperClass(), Result, QualifiedNameLookup,
                            /*InBaseClass=*/true);
      }

      // If there is an implementation, traverse it. We do this to find
      // synthesized ivars.
      if (IFace->getImplementation()) {
        ShadowContextRAII Shadow(Visited);
        lookupInDeclContext(IFace->getImplementation(), Result,
                            QualifiedNameLookup, InBaseClass);
      }
    } else if (ObjCProtocolDecl *Protocol = dyn_cast<ObjCProtocolDecl>(Ctx)) {
      for (auto *I : Protocol->protocols()) {
        ShadowContextRAII Shadow(Visited);
        lookupInDeclContext(I, Result, QualifiedNameLookup,
                            /*InBaseClass=*/false);
      }
    } else if (ObjCCategoryDecl *Category = dyn_cast<ObjCCategoryDecl>(Ctx)) {
      for (auto *I : Category->protocols()) {
        ShadowContextRAII Shadow(Visited);
        lookupInDeclContext(I, Result, QualifiedNameLookup,
                            /*InBaseClass=*/false);
      }

      // If there is an implementation, traverse it.
      if (Category->getImplementation()) {
        ShadowContextRAII Shadow(Visited);
        lookupInDeclContext(Category->getImplementation(), Result,
                            QualifiedNameLookup, /*InBaseClass=*/true);
      }
    }
  }

  void lookupInScope(Scope *S, LookupResult &Result,
                     UnqualUsingDirectiveSet &UDirs) {
    // No clients run in this mode and it's not supported. Please add tests and
    // remove the assertion if you start relying on it.
    assert(!IncludeDependentBases && "Unsupported flag for lookupInScope");

    if (!S)
      return;

    if (!S->getEntity() ||
        (!S->getParent() && !Visited.alreadyVisitedContext(S->getEntity())) ||
        (S->getEntity())->isFunctionOrMethod()) {
      FindLocalExternScope FindLocals(Result);
      // Walk through the declarations in this Scope. The consumer might add new
      // decls to the scope as part of deserialization, so make a copy first.
      SmallVector<Decl *, 8> ScopeDecls(S->decls().begin(), S->decls().end());
      for (Decl *D : ScopeDecls) {
        if (NamedDecl *ND = dyn_cast<NamedDecl>(D))
          if ((ND = Result.getAcceptableDecl(ND))) {
            Consumer.FoundDecl(ND, Visited.checkHidden(ND), nullptr, false);
            Visited.add(ND);
          }
      }
    }

    DeclContext *Entity = S->getLookupEntity();
    if (Entity) {
      // Look into this scope's declaration context, along with any of its
      // parent lookup contexts (e.g., enclosing classes), up to the point
      // where we hit the context stored in the next outer scope.
      DeclContext *OuterCtx = findOuterContext(S);

      for (DeclContext *Ctx = Entity; Ctx && !Ctx->Equals(OuterCtx);
           Ctx = Ctx->getLookupParent()) {
        if (ObjCMethodDecl *Method = dyn_cast<ObjCMethodDecl>(Ctx)) {
          if (Method->isInstanceMethod()) {
            // For instance methods, look for ivars in the method's interface.
            LookupResult IvarResult(Result.getSema(), Result.getLookupName(),
                                    Result.getNameLoc(),
                                    Sema::LookupMemberName);
            if (ObjCInterfaceDecl *IFace = Method->getClassInterface()) {
              lookupInDeclContext(IFace, IvarResult,
                                  /*QualifiedNameLookup=*/false,
                                  /*InBaseClass=*/false);
            }
          }

          // We've already performed all of the name lookup that we need
          // to for Objective-C methods; the next context will be the
          // outer scope.
          break;
        }

        if (Ctx->isFunctionOrMethod())
          continue;

        lookupInDeclContext(Ctx, Result, /*QualifiedNameLookup=*/false,
                            /*InBaseClass=*/false);
      }
    } else if (!S->getParent()) {
      // Look into the translation unit scope. We walk through the translation
      // unit's declaration context, because the Scope itself won't have all of
      // the declarations if we loaded a precompiled header.
      // FIXME: We would like the translation unit's Scope object to point to
      // the translation unit, so we don't need this special "if" branch.
      // However, doing so would force the normal C++ name-lookup code to look
      // into the translation unit decl when the IdentifierInfo chains would
      // suffice. Once we fix that problem (which is part of a more general
      // "don't look in DeclContexts unless we have to" optimization), we can
      // eliminate this.
      Entity = Result.getSema().Context.getTranslationUnitDecl();
      lookupInDeclContext(Entity, Result, /*QualifiedNameLookup=*/false,
                          /*InBaseClass=*/false);
    }

    if (Entity) {
      // Lookup visible declarations in any namespaces found by using
      // directives.
      for (const UnqualUsingEntry &UUE : UDirs.getNamespacesFor(Entity))
        lookupInDeclContext(
            const_cast<DeclContext *>(UUE.getNominatedNamespace()), Result,
            /*QualifiedNameLookup=*/false,
            /*InBaseClass=*/false);
    }

    // Lookup names in the parent scope.
    ShadowContextRAII Shadow(Visited);
    lookupInScope(S->getParent(), Result, UDirs);
  }

private:
  VisibleDeclsRecord Visited;
  VisibleDeclConsumer &Consumer;
  bool IncludeDependentBases;
  bool LoadExternal;
};
} // namespace

void Sema::LookupVisibleDecls(Scope *S, LookupNameKind Kind,
                              VisibleDeclConsumer &Consumer,
                              bool IncludeGlobalScope, bool LoadExternal) {
  LookupVisibleHelper H(Consumer, /*IncludeDependentBases=*/false,
                        LoadExternal);
  H.lookupVisibleDecls(*this, S, Kind, IncludeGlobalScope);
}

void Sema::LookupVisibleDecls(DeclContext *Ctx, LookupNameKind Kind,
                              VisibleDeclConsumer &Consumer,
                              bool IncludeGlobalScope,
                              bool IncludeDependentBases, bool LoadExternal) {
  LookupVisibleHelper H(Consumer, IncludeDependentBases, LoadExternal);
  H.lookupVisibleDecls(*this, Ctx, Kind, IncludeGlobalScope);
}

LabelDecl *Sema::LookupOrCreateLabel(IdentifierInfo *II, SourceLocation Loc,
                                     SourceLocation GnuLabelLoc) {
  // Do a lookup to see if we have a label with this name already.
  NamedDecl *Res = nullptr;

  if (GnuLabelLoc.isValid()) {
    // Local label definitions always shadow existing labels.
    Res = LabelDecl::Create(Context, CurContext, Loc, II, GnuLabelLoc);
    Scope *S = CurScope;
    PushOnScopeChains(Res, S, true);
    return cast<LabelDecl>(Res);
  }

  // Not a GNU local label.
  Res = LookupSingleName(CurScope, II, Loc, LookupLabel,
                         RedeclarationKind::NotForRedeclaration);
  // If we found a label, check to see if it is in the same context as us.
  // When in a Block, we don't want to reuse a label in an enclosing function.
  if (Res && Res->getDeclContext() != CurContext)
    Res = nullptr;
  if (!Res) {
    // If not forward referenced or defined already, create the backing decl.
    Res = LabelDecl::Create(Context, CurContext, Loc, II);
    Scope *S = CurScope->getFnParent();
    assert(S && "Not in a function?");
    PushOnScopeChains(Res, S, true);
  }
  return cast<LabelDecl>(Res);
}

//===----------------------------------------------------------------------===//
// Typo correction
//===----------------------------------------------------------------------===//

static bool isCandidateViable(CorrectionCandidateCallback &CCC,
                              TypoCorrection &Candidate) {
  Candidate.setCallbackDistance(CCC.RankCandidate(Candidate));
  return Candidate.getEditDistance(false) != TypoCorrection::InvalidDistance;
}

static void LookupPotentialTypoResult(Sema &SemaRef,
                                      LookupResult &Res,
                                      IdentifierInfo *Name,
                                      Scope *S, CXXScopeSpec *SS,
                                      DeclContext *MemberContext,
                                      bool EnteringContext,
                                      bool isObjCIvarLookup,
                                      bool FindHidden);

/// Check whether the declarations found for a typo correction are
/// visible. Set the correction's RequiresImport flag to true if none of the
/// declarations are visible, false otherwise.
static void checkCorrectionVisibility(Sema &SemaRef, TypoCorrection &TC) {
  TypoCorrection::decl_iterator DI = TC.begin(), DE = TC.end();

  for (/**/; DI != DE; ++DI)
    if (!LookupResult::isVisible(SemaRef, *DI))
      break;
  // No filtering needed if all decls are visible.
  if (DI == DE) {
    TC.setRequiresImport(false);
    return;
  }

  llvm::SmallVector<NamedDecl*, 4> NewDecls(TC.begin(), DI);
  bool AnyVisibleDecls = !NewDecls.empty();

  for (/**/; DI != DE; ++DI) {
    if (LookupResult::isVisible(SemaRef, *DI)) {
      if (!AnyVisibleDecls) {
        // Found a visible decl, discard all hidden ones.
        AnyVisibleDecls = true;
        NewDecls.clear();
      }
      NewDecls.push_back(*DI);
    } else if (!AnyVisibleDecls && !(*DI)->isModulePrivate())
      NewDecls.push_back(*DI);
  }

  if (NewDecls.empty())
    TC = TypoCorrection();
  else {
    TC.setCorrectionDecls(NewDecls);
    TC.setRequiresImport(!AnyVisibleDecls);
  }
}

// Fill the supplied vector with the IdentifierInfo pointers for each piece of
// the given NestedNameSpecifier (i.e. given a NestedNameSpecifier "foo::bar::",
// fill the vector with the IdentifierInfo pointers for "foo" and "bar").
static void getNestedNameSpecifierIdentifiers(
    NestedNameSpecifier *NNS,
    SmallVectorImpl<const IdentifierInfo*> &Identifiers) {
  if (NestedNameSpecifier *Prefix = NNS->getPrefix())
    getNestedNameSpecifierIdentifiers(Prefix, Identifiers);
  else
    Identifiers.clear();

  const IdentifierInfo *II = nullptr;

  switch (NNS->getKind()) {
  case NestedNameSpecifier::Identifier:
    II = NNS->getAsIdentifier();
    break;

  case NestedNameSpecifier::Namespace:
    if (NNS->getAsNamespace()->isAnonymousNamespace())
      return;
    II = NNS->getAsNamespace()->getIdentifier();
    break;

  case NestedNameSpecifier::NamespaceAlias:
    II = NNS->getAsNamespaceAlias()->getIdentifier();
    break;

  case NestedNameSpecifier::TypeSpecWithTemplate:
  case NestedNameSpecifier::TypeSpec:
    II = QualType(NNS->getAsType(), 0).getBaseTypeIdentifier();
    break;

  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
    return;
  }

  if (II)
    Identifiers.push_back(II);
}

void TypoCorrectionConsumer::FoundDecl(NamedDecl *ND, NamedDecl *Hiding,
                                       DeclContext *Ctx, bool InBaseClass) {
  // Don't consider hidden names for typo correction.
  if (Hiding)
    return;

  // Only consider entities with identifiers for names, ignoring
  // special names (constructors, overloaded operators, selectors,
  // etc.).
  IdentifierInfo *Name = ND->getIdentifier();
  if (!Name)
    return;

  // Only consider visible declarations and declarations from modules with
  // names that exactly match.
  if (!LookupResult::isVisible(SemaRef, ND) && Name != Typo)
    return;

  FoundName(Name->getName());
}

void TypoCorrectionConsumer::FoundName(StringRef Name) {
  // Compute the edit distance between the typo and the name of this
  // entity, and add the identifier to the list of results.
  addName(Name, nullptr);
}

void TypoCorrectionConsumer::addKeywordResult(StringRef Keyword) {
  // Compute the edit distance between the typo and this keyword,
  // and add the keyword to the list of results.
  addName(Keyword, nullptr, nullptr, true);
}

void TypoCorrectionConsumer::addName(StringRef Name, NamedDecl *ND,
                                     NestedNameSpecifier *NNS, bool isKeyword) {
  // Use a simple length-based heuristic to determine the minimum possible
  // edit distance. If the minimum isn't good enough, bail out early.
  StringRef TypoStr = Typo->getName();
  unsigned MinED = abs((int)Name.size() - (int)TypoStr.size());
  if (MinED && TypoStr.size() / MinED < 3)
    return;

  // Compute an upper bound on the allowable edit distance, so that the
  // edit-distance algorithm can short-circuit.
  unsigned UpperBound = (TypoStr.size() + 2) / 3;
  unsigned ED = TypoStr.edit_distance(Name, true, UpperBound);
  if (ED > UpperBound) return;

  TypoCorrection TC(&SemaRef.Context.Idents.get(Name), ND, NNS, ED);
  if (isKeyword) TC.makeKeyword();
  TC.setCorrectionRange(nullptr, Result.getLookupNameInfo());
  addCorrection(TC);
}

static const unsigned MaxTypoDistanceResultSets = 5;

void TypoCorrectionConsumer::addCorrection(TypoCorrection Correction) {
  StringRef TypoStr = Typo->getName();
  StringRef Name = Correction.getCorrectionAsIdentifierInfo()->getName();

  // For very short typos, ignore potential corrections that have a different
  // base identifier from the typo or which have a normalized edit distance
  // longer than the typo itself.
  if (TypoStr.size() < 3 &&
      (Name != TypoStr || Correction.getEditDistance(true) > TypoStr.size()))
    return;

  // If the correction is resolved but is not viable, ignore it.
  if (Correction.isResolved()) {
    checkCorrectionVisibility(SemaRef, Correction);
    if (!Correction || !isCandidateViable(*CorrectionValidator, Correction))
      return;
  }

  TypoResultList &CList =
      CorrectionResults[Correction.getEditDistance(false)][Name];

  if (!CList.empty() && !CList.back().isResolved())
    CList.pop_back();
  if (NamedDecl *NewND = Correction.getCorrectionDecl()) {
    auto RI = llvm::find_if(CList, [NewND](const TypoCorrection &TypoCorr) {
      return TypoCorr.getCorrectionDecl() == NewND;
    });
    if (RI != CList.end()) {
      // The Correction refers to a decl already in the list. No insertion is
      // necessary and all further cases will return.

      auto IsDeprecated = [](Decl *D) {
        while (D) {
          if (D->isDeprecated())
            return true;
          D = llvm::dyn_cast_or_null<NamespaceDecl>(D->getDeclContext());
        }
        return false;
      };

      // Prefer non deprecated Corrections over deprecated and only then
      // sort using an alphabetical order.
      std::pair<bool, std::string> NewKey = {
          IsDeprecated(Correction.getFoundDecl()),
          Correction.getAsString(SemaRef.getLangOpts())};

      std::pair<bool, std::string> PrevKey = {
          IsDeprecated(RI->getFoundDecl()),
          RI->getAsString(SemaRef.getLangOpts())};

      if (NewKey < PrevKey)
        *RI = Correction;
      return;
    }
  }
  if (CList.empty() || Correction.isResolved())
    CList.push_back(Correction);

  while (CorrectionResults.size() > MaxTypoDistanceResultSets)
    CorrectionResults.erase(std::prev(CorrectionResults.end()));
}

void TypoCorrectionConsumer::addNamespaces(
    const llvm::MapVector<NamespaceDecl *, bool> &KnownNamespaces) {
  SearchNamespaces = true;

  for (auto KNPair : KnownNamespaces)
    Namespaces.addNameSpecifier(KNPair.first);

  bool SSIsTemplate = false;
  if (NestedNameSpecifier *NNS =
          (SS && SS->isValid()) ? SS->getScopeRep() : nullptr) {
    if (const Type *T = NNS->getAsType())
      SSIsTemplate = T->getTypeClass() == Type::TemplateSpecialization;
  }
  // Do not transform this into an iterator-based loop. The loop body can
  // trigger the creation of further types (through lazy deserialization) and
  // invalid iterators into this list.
  auto &Types = SemaRef.getASTContext().getTypes();
  for (unsigned I = 0; I != Types.size(); ++I) {
    const auto *TI = Types[I];
    if (CXXRecordDecl *CD = TI->getAsCXXRecordDecl()) {
      CD = CD->getCanonicalDecl();
      if (!CD->isDependentType() && !CD->isAnonymousStructOrUnion() &&
          !CD->isUnion() && CD->getIdentifier() &&
          (SSIsTemplate || !isa<ClassTemplateSpecializationDecl>(CD)) &&
          (CD->isBeingDefined() || CD->isCompleteDefinition()))
        Namespaces.addNameSpecifier(CD);
    }
  }
}

const TypoCorrection &TypoCorrectionConsumer::getNextCorrection() {
  if (++CurrentTCIndex < ValidatedCorrections.size())
    return ValidatedCorrections[CurrentTCIndex];

  CurrentTCIndex = ValidatedCorrections.size();
  while (!CorrectionResults.empty()) {
    auto DI = CorrectionResults.begin();
    if (DI->second.empty()) {
      CorrectionResults.erase(DI);
      continue;
    }

    auto RI = DI->second.begin();
    if (RI->second.empty()) {
      DI->second.erase(RI);
      performQualifiedLookups();
      continue;
    }

    TypoCorrection TC = RI->second.pop_back_val();
    if (TC.isResolved() || TC.requiresImport() || resolveCorrection(TC)) {
      ValidatedCorrections.push_back(TC);
      return ValidatedCorrections[CurrentTCIndex];
    }
  }
  return ValidatedCorrections[0];  // The empty correction.
}

bool TypoCorrectionConsumer::resolveCorrection(TypoCorrection &Candidate) {
  IdentifierInfo *Name = Candidate.getCorrectionAsIdentifierInfo();
  DeclContext *TempMemberContext = MemberContext;
  CXXScopeSpec *TempSS = SS.get();
retry_lookup:
  LookupPotentialTypoResult(SemaRef, Result, Name, S, TempSS, TempMemberContext,
                            EnteringContext,
                            CorrectionValidator->IsObjCIvarLookup,
                            Name == Typo && !Candidate.WillReplaceSpecifier());
  switch (Result.getResultKind()) {
  case LookupResult::NotFound:
  case LookupResult::NotFoundInCurrentInstantiation:
  case LookupResult::FoundUnresolvedValue:
    if (TempSS) {
      // Immediately retry the lookup without the given CXXScopeSpec
      TempSS = nullptr;
      Candidate.WillReplaceSpecifier(true);
      goto retry_lookup;
    }
    if (TempMemberContext) {
      if (SS && !TempSS)
        TempSS = SS.get();
      TempMemberContext = nullptr;
      goto retry_lookup;
    }
    if (SearchNamespaces)
      QualifiedResults.push_back(Candidate);
    break;

  case LookupResult::Ambiguous:
    // We don't deal with ambiguities.
    break;

  case LookupResult::Found:
  case LookupResult::FoundOverloaded:
    // Store all of the Decls for overloaded symbols
    for (auto *TRD : Result)
      Candidate.addCorrectionDecl(TRD);
    checkCorrectionVisibility(SemaRef, Candidate);
    if (!isCandidateViable(*CorrectionValidator, Candidate)) {
      if (SearchNamespaces)
        QualifiedResults.push_back(Candidate);
      break;
    }
    Candidate.setCorrectionRange(SS.get(), Result.getLookupNameInfo());
    return true;
  }
  return false;
}

void TypoCorrectionConsumer::performQualifiedLookups() {
  unsigned TypoLen = Typo->getName().size();
  for (const TypoCorrection &QR : QualifiedResults) {
    for (const auto &NSI : Namespaces) {
      DeclContext *Ctx = NSI.DeclCtx;
      const Type *NSType = NSI.NameSpecifier->getAsType();

      // If the current NestedNameSpecifier refers to a class and the
      // current correction candidate is the name of that class, then skip
      // it as it is unlikely a qualified version of the class' constructor
      // is an appropriate correction.
      if (CXXRecordDecl *NSDecl = NSType ? NSType->getAsCXXRecordDecl() :
                                           nullptr) {
        if (NSDecl->getIdentifier() == QR.getCorrectionAsIdentifierInfo())
          continue;
      }

      TypoCorrection TC(QR);
      TC.ClearCorrectionDecls();
      TC.setCorrectionSpecifier(NSI.NameSpecifier);
      TC.setQualifierDistance(NSI.EditDistance);
      TC.setCallbackDistance(0); // Reset the callback distance

      // If the current correction candidate and namespace combination are
      // too far away from the original typo based on the normalized edit
      // distance, then skip performing a qualified name lookup.
      unsigned TmpED = TC.getEditDistance(true);
      if (QR.getCorrectionAsIdentifierInfo() != Typo && TmpED &&
          TypoLen / TmpED < 3)
        continue;

      Result.clear();
      Result.setLookupName(QR.getCorrectionAsIdentifierInfo());
      if (!SemaRef.LookupQualifiedName(Result, Ctx))
        continue;

      // Any corrections added below will be validated in subsequent
      // iterations of the main while() loop over the Consumer's contents.
      switch (Result.getResultKind()) {
      case LookupResult::Found:
      case LookupResult::FoundOverloaded: {
        if (SS && SS->isValid()) {
          std::string NewQualified = TC.getAsString(SemaRef.getLangOpts());
          std::string OldQualified;
          llvm::raw_string_ostream OldOStream(OldQualified);
          SS->getScopeRep()->print(OldOStream, SemaRef.getPrintingPolicy());
          OldOStream << Typo->getName();
          // If correction candidate would be an identical written qualified
          // identifier, then the existing CXXScopeSpec probably included a
          // typedef that didn't get accounted for properly.
          if (OldOStream.str() == NewQualified)
            break;
        }
        for (LookupResult::iterator TRD = Result.begin(), TRDEnd = Result.end();
             TRD != TRDEnd; ++TRD) {
          if (SemaRef.CheckMemberAccess(TC.getCorrectionRange().getBegin(),
                                        NSType ? NSType->getAsCXXRecordDecl()
                                               : nullptr,
                                        TRD.getPair()) == Sema::AR_accessible)
            TC.addCorrectionDecl(*TRD);
        }
        if (TC.isResolved()) {
          TC.setCorrectionRange(SS.get(), Result.getLookupNameInfo());
          addCorrection(TC);
        }
        break;
      }
      case LookupResult::NotFound:
      case LookupResult::NotFoundInCurrentInstantiation:
      case LookupResult::Ambiguous:
      case LookupResult::FoundUnresolvedValue:
        break;
      }
    }
  }
  QualifiedResults.clear();
}

TypoCorrectionConsumer::NamespaceSpecifierSet::NamespaceSpecifierSet(
    ASTContext &Context, DeclContext *CurContext, CXXScopeSpec *CurScopeSpec)
    : Context(Context), CurContextChain(buildContextChain(CurContext)) {
  if (NestedNameSpecifier *NNS =
          CurScopeSpec ? CurScopeSpec->getScopeRep() : nullptr) {
    llvm::raw_string_ostream SpecifierOStream(CurNameSpecifier);
    NNS->print(SpecifierOStream, Context.getPrintingPolicy());

    getNestedNameSpecifierIdentifiers(NNS, CurNameSpecifierIdentifiers);
  }
  // Build the list of identifiers that would be used for an absolute
  // (from the global context) NestedNameSpecifier referring to the current
  // context.
  for (DeclContext *C : llvm::reverse(CurContextChain)) {
    if (auto *ND = dyn_cast_or_null<NamespaceDecl>(C))
      CurContextIdentifiers.push_back(ND->getIdentifier());
  }

  // Add the global context as a NestedNameSpecifier
  SpecifierInfo SI = {cast<DeclContext>(Context.getTranslationUnitDecl()),
                      NestedNameSpecifier::GlobalSpecifier(Context), 1};
  DistanceMap[1].push_back(SI);
}

auto TypoCorrectionConsumer::NamespaceSpecifierSet::buildContextChain(
    DeclContext *Start) -> DeclContextList {
  assert(Start && "Building a context chain from a null context");
  DeclContextList Chain;
  for (DeclContext *DC = Start->getPrimaryContext(); DC != nullptr;
       DC = DC->getLookupParent()) {
    NamespaceDecl *ND = dyn_cast_or_null<NamespaceDecl>(DC);
    if (!DC->isInlineNamespace() && !DC->isTransparentContext() &&
        !(ND && ND->isAnonymousNamespace()))
      Chain.push_back(DC->getPrimaryContext());
  }
  return Chain;
}

unsigned
TypoCorrectionConsumer::NamespaceSpecifierSet::buildNestedNameSpecifier(
    DeclContextList &DeclChain, NestedNameSpecifier *&NNS) {
  unsigned NumSpecifiers = 0;
  for (DeclContext *C : llvm::reverse(DeclChain)) {
    if (auto *ND = dyn_cast_or_null<NamespaceDecl>(C)) {
      NNS = NestedNameSpecifier::Create(Context, NNS, ND);
      ++NumSpecifiers;
    } else if (auto *RD = dyn_cast_or_null<RecordDecl>(C)) {
      NNS = NestedNameSpecifier::Create(Context, NNS, RD->isTemplateDecl(),
                                        RD->getTypeForDecl());
      ++NumSpecifiers;
    }
  }
  return NumSpecifiers;
}

void TypoCorrectionConsumer::NamespaceSpecifierSet::addNameSpecifier(
    DeclContext *Ctx) {
  NestedNameSpecifier *NNS = nullptr;
  unsigned NumSpecifiers = 0;
  DeclContextList NamespaceDeclChain(buildContextChain(Ctx));
  DeclContextList FullNamespaceDeclChain(NamespaceDeclChain);

  // Eliminate common elements from the two DeclContext chains.
  for (DeclContext *C : llvm::reverse(CurContextChain)) {
    if (NamespaceDeclChain.empty() || NamespaceDeclChain.back() != C)
      break;
    NamespaceDeclChain.pop_back();
  }

  // Build the NestedNameSpecifier from what is left of the NamespaceDeclChain
  NumSpecifiers = buildNestedNameSpecifier(NamespaceDeclChain, NNS);

  // Add an explicit leading '::' specifier if needed.
  if (NamespaceDeclChain.empty()) {
    // Rebuild the NestedNameSpecifier as a globally-qualified specifier.
    NNS = NestedNameSpecifier::GlobalSpecifier(Context);
    NumSpecifiers =
        buildNestedNameSpecifier(FullNamespaceDeclChain, NNS);
  } else if (NamedDecl *ND =
                 dyn_cast_or_null<NamedDecl>(NamespaceDeclChain.back())) {
    IdentifierInfo *Name = ND->getIdentifier();
    bool SameNameSpecifier = false;
    if (llvm::is_contained(CurNameSpecifierIdentifiers, Name)) {
      std::string NewNameSpecifier;
      llvm::raw_string_ostream SpecifierOStream(NewNameSpecifier);
      SmallVector<const IdentifierInfo *, 4> NewNameSpecifierIdentifiers;
      getNestedNameSpecifierIdentifiers(NNS, NewNameSpecifierIdentifiers);
      NNS->print(SpecifierOStream, Context.getPrintingPolicy());
      SpecifierOStream.flush();
      SameNameSpecifier = NewNameSpecifier == CurNameSpecifier;
    }
    if (SameNameSpecifier || llvm::is_contained(CurContextIdentifiers, Name)) {
      // Rebuild the NestedNameSpecifier as a globally-qualified specifier.
      NNS = NestedNameSpecifier::GlobalSpecifier(Context);
      NumSpecifiers =
          buildNestedNameSpecifier(FullNamespaceDeclChain, NNS);
    }
  }

  // If the built NestedNameSpecifier would be replacing an existing
  // NestedNameSpecifier, use the number of component identifiers that
  // would need to be changed as the edit distance instead of the number
  // of components in the built NestedNameSpecifier.
  if (NNS && !CurNameSpecifierIdentifiers.empty()) {
    SmallVector<const IdentifierInfo*, 4> NewNameSpecifierIdentifiers;
    getNestedNameSpecifierIdentifiers(NNS, NewNameSpecifierIdentifiers);
    NumSpecifiers =
        llvm::ComputeEditDistance(llvm::ArrayRef(CurNameSpecifierIdentifiers),
                                  llvm::ArrayRef(NewNameSpecifierIdentifiers));
  }

  SpecifierInfo SI = {Ctx, NNS, NumSpecifiers};
  DistanceMap[NumSpecifiers].push_back(SI);
}

/// Perform name lookup for a possible result for typo correction.
static void LookupPotentialTypoResult(Sema &SemaRef,
                                      LookupResult &Res,
                                      IdentifierInfo *Name,
                                      Scope *S, CXXScopeSpec *SS,
                                      DeclContext *MemberContext,
                                      bool EnteringContext,
                                      bool isObjCIvarLookup,
                                      bool FindHidden) {
  Res.suppressDiagnostics();
  Res.clear();
  Res.setLookupName(Name);
  Res.setAllowHidden(FindHidden);
  if (MemberContext) {
    if (ObjCInterfaceDecl *Class = dyn_cast<ObjCInterfaceDecl>(MemberContext)) {
      if (isObjCIvarLookup) {
        if (ObjCIvarDecl *Ivar = Class->lookupInstanceVariable(Name)) {
          Res.addDecl(Ivar);
          Res.resolveKind();
          return;
        }
      }

      if (ObjCPropertyDecl *Prop = Class->FindPropertyDeclaration(
              Name, ObjCPropertyQueryKind::OBJC_PR_query_instance)) {
        Res.addDecl(Prop);
        Res.resolveKind();
        return;
      }
    }

    SemaRef.LookupQualifiedName(Res, MemberContext);
    return;
  }

  SemaRef.LookupParsedName(Res, S, SS,
                           /*ObjectType=*/QualType(),
                           /*AllowBuiltinCreation=*/false, EnteringContext);

  // Fake ivar lookup; this should really be part of
  // LookupParsedName.
  if (ObjCMethodDecl *Method = SemaRef.getCurMethodDecl()) {
    if (Method->isInstanceMethod() && Method->getClassInterface() &&
        (Res.empty() ||
         (Res.isSingleResult() &&
          Res.getFoundDecl()->isDefinedOutsideFunctionOrMethod()))) {
       if (ObjCIvarDecl *IV
             = Method->getClassInterface()->lookupInstanceVariable(Name)) {
         Res.addDecl(IV);
         Res.resolveKind();
       }
     }
  }
}

/// Add keywords to the consumer as possible typo corrections.
static void AddKeywordsToConsumer(Sema &SemaRef,
                                  TypoCorrectionConsumer &Consumer,
                                  Scope *S, CorrectionCandidateCallback &CCC,
                                  bool AfterNestedNameSpecifier) {
  if (AfterNestedNameSpecifier) {
    // For 'X::', we know exactly which keywords can appear next.
    Consumer.addKeywordResult("template");
    if (CCC.WantExpressionKeywords)
      Consumer.addKeywordResult("operator");
    return;
  }

  if (CCC.WantObjCSuper)
    Consumer.addKeywordResult("super");

  if (CCC.WantTypeSpecifiers) {
    // Add type-specifier keywords to the set of results.
    static const char *const CTypeSpecs[] = {
      "char", "const", "double", "enum", "float", "int", "long", "short",
      "signed", "struct", "union", "unsigned", "void", "volatile",
      "_Complex",
      // storage-specifiers as well
      "extern", "inline", "static", "typedef"
    };

    for (const auto *CTS : CTypeSpecs)
      Consumer.addKeywordResult(CTS);

    if (SemaRef.getLangOpts().C99 && !SemaRef.getLangOpts().C2y)
      Consumer.addKeywordResult("_Imaginary");

    if (SemaRef.getLangOpts().C99)
      Consumer.addKeywordResult("restrict");
    if (SemaRef.getLangOpts().Bool || SemaRef.getLangOpts().CPlusPlus)
      Consumer.addKeywordResult("bool");
    else if (SemaRef.getLangOpts().C99)
      Consumer.addKeywordResult("_Bool");

    if (SemaRef.getLangOpts().CPlusPlus) {
      Consumer.addKeywordResult("class");
      Consumer.addKeywordResult("typename");
      Consumer.addKeywordResult("wchar_t");

      if (SemaRef.getLangOpts().CPlusPlus11) {
        Consumer.addKeywordResult("char16_t");
        Consumer.addKeywordResult("char32_t");
        Consumer.addKeywordResult("constexpr");
        Consumer.addKeywordResult("decltype");
        Consumer.addKeywordResult("thread_local");
      }
    }

    if (SemaRef.getLangOpts().GNUKeywords)
      Consumer.addKeywordResult("typeof");
  } else if (CCC.WantFunctionLikeCasts) {
    static const char *const CastableTypeSpecs[] = {
      "char", "double", "float", "int", "long", "short",
      "signed", "unsigned", "void"
    };
    for (auto *kw : CastableTypeSpecs)
      Consumer.addKeywordResult(kw);
  }

  if (CCC.WantCXXNamedCasts && SemaRef.getLangOpts().CPlusPlus) {
    Consumer.addKeywordResult("const_cast");
    Consumer.addKeywordResult("dynamic_cast");
    Consumer.addKeywordResult("reinterpret_cast");
    Consumer.addKeywordResult("static_cast");
  }

  if (CCC.WantExpressionKeywords) {
    Consumer.addKeywordResult("sizeof");
    if (SemaRef.getLangOpts().Bool || SemaRef.getLangOpts().CPlusPlus) {
      Consumer.addKeywordResult("false");
      Consumer.addKeywordResult("true");
    }

    if (SemaRef.getLangOpts().CPlusPlus) {
      static const char *const CXXExprs[] = {
        "delete", "new", "operator", "throw", "typeid"
      };
      for (const auto *CE : CXXExprs)
        Consumer.addKeywordResult(CE);

      if (isa<CXXMethodDecl>(SemaRef.CurContext) &&
          cast<CXXMethodDecl>(SemaRef.CurContext)->isInstance())
        Consumer.addKeywordResult("this");

      if (SemaRef.getLangOpts().CPlusPlus11) {
        Consumer.addKeywordResult("alignof");
        Consumer.addKeywordResult("nullptr");
      }
    }

    if (SemaRef.getLangOpts().C11) {
      // FIXME: We should not suggest _Alignof if the alignof macro
      // is present.
      Consumer.addKeywordResult("_Alignof");
    }
  }

  if (CCC.WantRemainingKeywords) {
    if (SemaRef.getCurFunctionOrMethodDecl() || SemaRef.getCurBlock()) {
      // Statements.
      static const char *const CStmts[] = {
        "do", "else", "for", "goto", "if", "return", "switch", "while" };
      for (const auto *CS : CStmts)
        Consumer.addKeywordResult(CS);

      if (SemaRef.getLangOpts().CPlusPlus) {
        Consumer.addKeywordResult("catch");
        Consumer.addKeywordResult("try");
      }

      if (S && S->getBreakParent())
        Consumer.addKeywordResult("break");

      if (S && S->getContinueParent())
        Consumer.addKeywordResult("continue");

      if (SemaRef.getCurFunction() &&
          !SemaRef.getCurFunction()->SwitchStack.empty()) {
        Consumer.addKeywordResult("case");
        Consumer.addKeywordResult("default");
      }
    } else {
      if (SemaRef.getLangOpts().CPlusPlus) {
        Consumer.addKeywordResult("namespace");
        Consumer.addKeywordResult("template");
      }

      if (S && S->isClassScope()) {
        Consumer.addKeywordResult("explicit");
        Consumer.addKeywordResult("friend");
        Consumer.addKeywordResult("mutable");
        Consumer.addKeywordResult("private");
        Consumer.addKeywordResult("protected");
        Consumer.addKeywordResult("public");
        Consumer.addKeywordResult("virtual");
      }
    }

    if (SemaRef.getLangOpts().CPlusPlus) {
      Consumer.addKeywordResult("using");

      if (SemaRef.getLangOpts().CPlusPlus11)
        Consumer.addKeywordResult("static_assert");
    }
  }
}

std::unique_ptr<TypoCorrectionConsumer> Sema::makeTypoCorrectionConsumer(
    const DeclarationNameInfo &TypoName, Sema::LookupNameKind LookupKind,
    Scope *S, CXXScopeSpec *SS, CorrectionCandidateCallback &CCC,
    DeclContext *MemberContext, bool EnteringContext,
    const ObjCObjectPointerType *OPT, bool ErrorRecovery) {

  if (Diags.hasFatalErrorOccurred() || !getLangOpts().SpellChecking ||
      DisableTypoCorrection)
    return nullptr;

  // In Microsoft mode, don't perform typo correction in a template member
  // function dependent context because it interferes with the "lookup into
  // dependent bases of class templates" feature.
  if (getLangOpts().MSVCCompat && CurContext->isDependentContext() &&
      isa<CXXMethodDecl>(CurContext))
    return nullptr;

  // We only attempt to correct typos for identifiers.
  IdentifierInfo *Typo = TypoName.getName().getAsIdentifierInfo();
  if (!Typo)
    return nullptr;

  // If the scope specifier itself was invalid, don't try to correct
  // typos.
  if (SS && SS->isInvalid())
    return nullptr;

  // Never try to correct typos during any kind of code synthesis.
  if (!CodeSynthesisContexts.empty())
    return nullptr;

  // Don't try to correct 'super'.
  if (S && S->isInObjcMethodScope() && Typo == getSuperIdentifier())
    return nullptr;

  // Abort if typo correction already failed for this specific typo.
  IdentifierSourceLocations::iterator locs = TypoCorrectionFailures.find(Typo);
  if (locs != TypoCorrectionFailures.end() &&
      locs->second.count(TypoName.getLoc()))
    return nullptr;

  // Don't try to correct the identifier "vector" when in AltiVec mode.
  // TODO: Figure out why typo correction misbehaves in this case, fix it, and
  // remove this workaround.
  if ((getLangOpts().AltiVec || getLangOpts().ZVector) && Typo->isStr("vector"))
    return nullptr;

  // Provide a stop gap for files that are just seriously broken.  Trying
  // to correct all typos can turn into a HUGE performance penalty, causing
  // some files to take minutes to get rejected by the parser.
  unsigned Limit = getDiagnostics().getDiagnosticOptions().SpellCheckingLimit;
  if (Limit && TyposCorrected >= Limit)
    return nullptr;
  ++TyposCorrected;

  // If we're handling a missing symbol error, using modules, and the
  // special search all modules option is used, look for a missing import.
  if (ErrorRecovery && getLangOpts().Modules &&
      getLangOpts().ModulesSearchAll) {
    // The following has the side effect of loading the missing module.
    getModuleLoader().lookupMissingImports(Typo->getName(),
                                           TypoName.getBeginLoc());
  }

  // Extend the lifetime of the callback. We delayed this until here
  // to avoid allocations in the hot path (which is where no typo correction
  // occurs). Note that CorrectionCandidateCallback is polymorphic and
  // initially stack-allocated.
  std::unique_ptr<CorrectionCandidateCallback> ClonedCCC = CCC.clone();
  auto Consumer = std::make_unique<TypoCorrectionConsumer>(
      *this, TypoName, LookupKind, S, SS, std::move(ClonedCCC), MemberContext,
      EnteringContext);

  // Perform name lookup to find visible, similarly-named entities.
  bool IsUnqualifiedLookup = false;
  DeclContext *QualifiedDC = MemberContext;
  if (MemberContext) {
    LookupVisibleDecls(MemberContext, LookupKind, *Consumer);

    // Look in qualified interfaces.
    if (OPT) {
      for (auto *I : OPT->quals())
        LookupVisibleDecls(I, LookupKind, *Consumer);
    }
  } else if (SS && SS->isSet()) {
    QualifiedDC = computeDeclContext(*SS, EnteringContext);
    if (!QualifiedDC)
      return nullptr;

    LookupVisibleDecls(QualifiedDC, LookupKind, *Consumer);
  } else {
    IsUnqualifiedLookup = true;
  }

  // Determine whether we are going to search in the various namespaces for
  // corrections.
  bool SearchNamespaces
    = getLangOpts().CPlusPlus &&
      (IsUnqualifiedLookup || (SS && SS->isSet()));

  if (IsUnqualifiedLookup || SearchNamespaces) {
    // For unqualified lookup, look through all of the names that we have
    // seen in this translation unit.
    // FIXME: Re-add the ability to skip very unlikely potential corrections.
    for (const auto &I : Context.Idents)
      Consumer->FoundName(I.getKey());

    // Walk through identifiers in external identifier sources.
    // FIXME: Re-add the ability to skip very unlikely potential corrections.
    if (IdentifierInfoLookup *External
                            = Context.Idents.getExternalIdentifierLookup()) {
      std::unique_ptr<IdentifierIterator> Iter(External->getIdentifiers());
      do {
        StringRef Name = Iter->Next();
        if (Name.empty())
          break;

        Consumer->FoundName(Name);
      } while (true);
    }
  }

  AddKeywordsToConsumer(*this, *Consumer, S,
                        *Consumer->getCorrectionValidator(),
                        SS && SS->isNotEmpty());

  // Build the NestedNameSpecifiers for the KnownNamespaces, if we're going
  // to search those namespaces.
  if (SearchNamespaces) {
    // Load any externally-known namespaces.
    if (ExternalSource && !LoadedExternalKnownNamespaces) {
      SmallVector<NamespaceDecl *, 4> ExternalKnownNamespaces;
      LoadedExternalKnownNamespaces = true;
      ExternalSource->ReadKnownNamespaces(ExternalKnownNamespaces);
      for (auto *N : ExternalKnownNamespaces)
        KnownNamespaces[N] = true;
    }

    Consumer->addNamespaces(KnownNamespaces);
  }

  return Consumer;
}

TypoCorrection Sema::CorrectTypo(const DeclarationNameInfo &TypoName,
                                 Sema::LookupNameKind LookupKind,
                                 Scope *S, CXXScopeSpec *SS,
                                 CorrectionCandidateCallback &CCC,
                                 CorrectTypoKind Mode,
                                 DeclContext *MemberContext,
                                 bool EnteringContext,
                                 const ObjCObjectPointerType *OPT,
                                 bool RecordFailure) {
  // Always let the ExternalSource have the first chance at correction, even
  // if we would otherwise have given up.
  if (ExternalSource) {
    if (TypoCorrection Correction =
            ExternalSource->CorrectTypo(TypoName, LookupKind, S, SS, CCC,
                                        MemberContext, EnteringContext, OPT))
      return Correction;
  }

  // Ugly hack equivalent to CTC == CTC_ObjCMessageReceiver;
  // WantObjCSuper is only true for CTC_ObjCMessageReceiver and for
  // some instances of CTC_Unknown, while WantRemainingKeywords is true
  // for CTC_Unknown but not for CTC_ObjCMessageReceiver.
  bool ObjCMessageReceiver = CCC.WantObjCSuper && !CCC.WantRemainingKeywords;

  IdentifierInfo *Typo = TypoName.getName().getAsIdentifierInfo();
  auto Consumer = makeTypoCorrectionConsumer(TypoName, LookupKind, S, SS, CCC,
                                             MemberContext, EnteringContext,
                                             OPT, Mode == CTK_ErrorRecovery);

  if (!Consumer)
    return TypoCorrection();

  // If we haven't found anything, we're done.
  if (Consumer->empty())
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  // Make sure the best edit distance (prior to adding any namespace qualifiers)
  // is not more that about a third of the length of the typo's identifier.
  unsigned ED = Consumer->getBestEditDistance(true);
  unsigned TypoLen = Typo->getName().size();
  if (ED > 0 && TypoLen / ED < 3)
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  TypoCorrection BestTC = Consumer->getNextCorrection();
  TypoCorrection SecondBestTC = Consumer->getNextCorrection();
  if (!BestTC)
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  ED = BestTC.getEditDistance();

  if (TypoLen >= 3 && ED > 0 && TypoLen / ED < 3) {
    // If this was an unqualified lookup and we believe the callback
    // object wouldn't have filtered out possible corrections, note
    // that no correction was found.
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);
  }

  // If only a single name remains, return that result.
  if (!SecondBestTC ||
      SecondBestTC.getEditDistance(false) > BestTC.getEditDistance(false)) {
    const TypoCorrection &Result = BestTC;

    // Don't correct to a keyword that's the same as the typo; the keyword
    // wasn't actually in scope.
    if (ED == 0 && Result.isKeyword())
      return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

    TypoCorrection TC = Result;
    TC.setCorrectionRange(SS, TypoName);
    checkCorrectionVisibility(*this, TC);
    return TC;
  } else if (SecondBestTC && ObjCMessageReceiver) {
    // Prefer 'super' when we're completing in a message-receiver
    // context.

    if (BestTC.getCorrection().getAsString() != "super") {
      if (SecondBestTC.getCorrection().getAsString() == "super")
        BestTC = SecondBestTC;
      else if ((*Consumer)["super"].front().isKeyword())
        BestTC = (*Consumer)["super"].front();
    }
    // Don't correct to a keyword that's the same as the typo; the keyword
    // wasn't actually in scope.
    if (BestTC.getEditDistance() == 0 ||
        BestTC.getCorrection().getAsString() != "super")
      return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

    BestTC.setCorrectionRange(SS, TypoName);
    return BestTC;
  }

  // Record the failure's location if needed and return an empty correction. If
  // this was an unqualified lookup and we believe the callback object did not
  // filter out possible corrections, also cache the failure for the typo.
  return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure && !SecondBestTC);
}

TypoExpr *Sema::CorrectTypoDelayed(
    const DeclarationNameInfo &TypoName, Sema::LookupNameKind LookupKind,
    Scope *S, CXXScopeSpec *SS, CorrectionCandidateCallback &CCC,
    TypoDiagnosticGenerator TDG, TypoRecoveryCallback TRC, CorrectTypoKind Mode,
    DeclContext *MemberContext, bool EnteringContext,
    const ObjCObjectPointerType *OPT) {
  auto Consumer = makeTypoCorrectionConsumer(TypoName, LookupKind, S, SS, CCC,
                                             MemberContext, EnteringContext,
                                             OPT, Mode == CTK_ErrorRecovery);

  // Give the external sema source a chance to correct the typo.
  TypoCorrection ExternalTypo;
  if (ExternalSource && Consumer) {
    ExternalTypo = ExternalSource->CorrectTypo(
        TypoName, LookupKind, S, SS, *Consumer->getCorrectionValidator(),
        MemberContext, EnteringContext, OPT);
    if (ExternalTypo)
      Consumer->addCorrection(ExternalTypo);
  }

  if (!Consumer || Consumer->empty())
    return nullptr;

  // Make sure the best edit distance (prior to adding any namespace qualifiers)
  // is not more that about a third of the length of the typo's identifier.
  unsigned ED = Consumer->getBestEditDistance(true);
  IdentifierInfo *Typo = TypoName.getName().getAsIdentifierInfo();
  if (!ExternalTypo && ED > 0 && Typo->getName().size() / ED < 3)
    return nullptr;
  ExprEvalContexts.back().NumTypos++;
  return createDelayedTypo(std::move(Consumer), std::move(TDG), std::move(TRC),
                           TypoName.getLoc());
}

void TypoCorrection::addCorrectionDecl(NamedDecl *CDecl) {
  if (!CDecl) return;

  if (isKeyword())
    CorrectionDecls.clear();

  CorrectionDecls.push_back(CDecl);

  if (!CorrectionName)
    CorrectionName = CDecl->getDeclName();
}

std::string TypoCorrection::getAsString(const LangOptions &LO) const {
  if (CorrectionNameSpec) {
    std::string tmpBuffer;
    llvm::raw_string_ostream PrefixOStream(tmpBuffer);
    CorrectionNameSpec->print(PrefixOStream, PrintingPolicy(LO));
    PrefixOStream << CorrectionName;
    return PrefixOStream.str();
  }

  return CorrectionName.getAsString();
}

bool CorrectionCandidateCallback::ValidateCandidate(
    const TypoCorrection &candidate) {
  if (!candidate.isResolved())
    return true;

  if (candidate.isKeyword())
    return WantTypeSpecifiers || WantExpressionKeywords || WantCXXNamedCasts ||
           WantRemainingKeywords || WantObjCSuper;

  bool HasNonType = false;
  bool HasStaticMethod = false;
  bool HasNonStaticMethod = false;
  for (Decl *D : candidate) {
    if (FunctionTemplateDecl *FTD = dyn_cast<FunctionTemplateDecl>(D))
      D = FTD->getTemplatedDecl();
    if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(D)) {
      if (Method->isStatic())
        HasStaticMethod = true;
      else
        HasNonStaticMethod = true;
    }
    if (!isa<TypeDecl>(D))
      HasNonType = true;
  }

  if (IsAddressOfOperand && HasNonStaticMethod && !HasStaticMethod &&
      !candidate.getCorrectionSpecifier())
    return false;

  return WantTypeSpecifiers || HasNonType;
}

FunctionCallFilterCCC::FunctionCallFilterCCC(Sema &SemaRef, unsigned NumArgs,
                                             bool HasExplicitTemplateArgs,
                                             MemberExpr *ME)
    : NumArgs(NumArgs), HasExplicitTemplateArgs(HasExplicitTemplateArgs),
      CurContext(SemaRef.CurContext), MemberFn(ME) {
  WantTypeSpecifiers = false;
  WantFunctionLikeCasts = SemaRef.getLangOpts().CPlusPlus &&
                          !HasExplicitTemplateArgs && NumArgs == 1;
  WantCXXNamedCasts = HasExplicitTemplateArgs && NumArgs == 1;
  WantRemainingKeywords = false;
}

bool FunctionCallFilterCCC::ValidateCandidate(const TypoCorrection &candidate) {
  if (!candidate.getCorrectionDecl())
    return candidate.isKeyword();

  for (auto *C : candidate) {
    FunctionDecl *FD = nullptr;
    NamedDecl *ND = C->getUnderlyingDecl();
    if (FunctionTemplateDecl *FTD = dyn_cast<FunctionTemplateDecl>(ND))
      FD = FTD->getTemplatedDecl();
    if (!HasExplicitTemplateArgs && !FD) {
      if (!(FD = dyn_cast<FunctionDecl>(ND)) && isa<ValueDecl>(ND)) {
        // If the Decl is neither a function nor a template function,
        // determine if it is a pointer or reference to a function. If so,
        // check against the number of arguments expected for the pointee.
        QualType ValType = cast<ValueDecl>(ND)->getType();
        if (ValType.isNull())
          continue;
        if (ValType->isAnyPointerType() || ValType->isReferenceType())
          ValType = ValType->getPointeeType();
        if (const FunctionProtoType *FPT = ValType->getAs<FunctionProtoType>())
          if (FPT->getNumParams() == NumArgs)
            return true;
      }
    }

    // A typo for a function-style cast can look like a function call in C++.
    if ((HasExplicitTemplateArgs ? getAsTypeTemplateDecl(ND) != nullptr
                                 : isa<TypeDecl>(ND)) &&
        CurContext->getParentASTContext().getLangOpts().CPlusPlus)
      // Only a class or class template can take two or more arguments.
      return NumArgs <= 1 || HasExplicitTemplateArgs || isa<CXXRecordDecl>(ND);

    // Skip the current candidate if it is not a FunctionDecl or does not accept
    // the current number of arguments.
    if (!FD || !(FD->getNumParams() >= NumArgs &&
                 FD->getMinRequiredArguments() <= NumArgs))
      continue;

    // If the current candidate is a non-static C++ method, skip the candidate
    // unless the method being corrected--or the current DeclContext, if the
    // function being corrected is not a method--is a method in the same class
    // or a descendent class of the candidate's parent class.
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
      if (MemberFn || !MD->isStatic()) {
        const auto *CurMD =
            MemberFn
                ? dyn_cast_if_present<CXXMethodDecl>(MemberFn->getMemberDecl())
                : dyn_cast_if_present<CXXMethodDecl>(CurContext);
        const CXXRecordDecl *CurRD =
            CurMD ? CurMD->getParent()->getCanonicalDecl() : nullptr;
        const CXXRecordDecl *RD = MD->getParent()->getCanonicalDecl();
        if (!CurRD || (CurRD != RD && !CurRD->isDerivedFrom(RD)))
          continue;
      }
    }
    return true;
  }
  return false;
}

void Sema::diagnoseTypo(const TypoCorrection &Correction,
                        const PartialDiagnostic &TypoDiag,
                        bool ErrorRecovery) {
  diagnoseTypo(Correction, TypoDiag, PDiag(diag::note_previous_decl),
               ErrorRecovery);
}

/// Find which declaration we should import to provide the definition of
/// the given declaration.
static const NamedDecl *getDefinitionToImport(const NamedDecl *D) {
  if (const auto *VD = dyn_cast<VarDecl>(D))
    return VD->getDefinition();
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->getDefinition();
  if (const auto *TD = dyn_cast<TagDecl>(D))
    return TD->getDefinition();
  if (const auto *ID = dyn_cast<ObjCInterfaceDecl>(D))
    return ID->getDefinition();
  if (const auto *PD = dyn_cast<ObjCProtocolDecl>(D))
    return PD->getDefinition();
  if (const auto *TD = dyn_cast<TemplateDecl>(D))
    if (const NamedDecl *TTD = TD->getTemplatedDecl())
      return getDefinitionToImport(TTD);
  return nullptr;
}

void Sema::diagnoseMissingImport(SourceLocation Loc, const NamedDecl *Decl,
                                 MissingImportKind MIK, bool Recover) {
  // Suggest importing a module providing the definition of this entity, if
  // possible.
  const NamedDecl *Def = getDefinitionToImport(Decl);
  if (!Def)
    Def = Decl;

  Module *Owner = getOwningModule(Def);
  assert(Owner && "definition of hidden declaration is not in a module");

  llvm::SmallVector<Module*, 8> OwningModules;
  OwningModules.push_back(Owner);
  auto Merged = Context.getModulesWithMergedDefinition(Def);
  OwningModules.insert(OwningModules.end(), Merged.begin(), Merged.end());

  diagnoseMissingImport(Loc, Def, Def->getLocation(), OwningModules, MIK,
                        Recover);
}

/// Get a "quoted.h" or <angled.h> include path to use in a diagnostic
/// suggesting the addition of a #include of the specified file.
static std::string getHeaderNameForHeader(Preprocessor &PP, FileEntryRef E,
                                          llvm::StringRef IncludingFile) {
  bool IsAngled = false;
  auto Path = PP.getHeaderSearchInfo().suggestPathToFileForDiagnostics(
      E, IncludingFile, &IsAngled);
  return (IsAngled ? '<' : '"') + Path + (IsAngled ? '>' : '"');
}

void Sema::diagnoseMissingImport(SourceLocation UseLoc, const NamedDecl *Decl,
                                 SourceLocation DeclLoc,
                                 ArrayRef<Module *> Modules,
                                 MissingImportKind MIK, bool Recover) {
  assert(!Modules.empty());

  // See https://github.com/llvm/llvm-project/issues/73893. It is generally
  // confusing than helpful to show the namespace is not visible.
  if (isa<NamespaceDecl>(Decl))
    return;

  auto NotePrevious = [&] {
    // FIXME: Suppress the note backtrace even under
    // -fdiagnostics-show-note-include-stack. We don't care how this
    // declaration was previously reached.
    Diag(DeclLoc, diag::note_unreachable_entity) << (int)MIK;
  };

  // Weed out duplicates from module list.
  llvm::SmallVector<Module*, 8> UniqueModules;
  llvm::SmallDenseSet<Module*, 8> UniqueModuleSet;
  for (auto *M : Modules) {
    if (M->isExplicitGlobalModule() || M->isPrivateModule())
      continue;
    if (UniqueModuleSet.insert(M).second)
      UniqueModules.push_back(M);
  }

  // Try to find a suitable header-name to #include.
  std::string HeaderName;
  if (OptionalFileEntryRef Header =
          PP.getHeaderToIncludeForDiagnostics(UseLoc, DeclLoc)) {
    if (const FileEntry *FE =
            SourceMgr.getFileEntryForID(SourceMgr.getFileID(UseLoc)))
      HeaderName =
          getHeaderNameForHeader(PP, *Header, FE->tryGetRealPathName());
  }

  // If we have a #include we should suggest, or if all definition locations
  // were in global module fragments, don't suggest an import.
  if (!HeaderName.empty() || UniqueModules.empty()) {
    // FIXME: Find a smart place to suggest inserting a #include, and add
    // a FixItHint there.
    Diag(UseLoc, diag::err_module_unimported_use_header)
        << (int)MIK << Decl << !HeaderName.empty() << HeaderName;
    // Produce a note showing where the entity was declared.
    NotePrevious();
    if (Recover)
      createImplicitModuleImportForErrorRecovery(UseLoc, Modules[0]);
    return;
  }

  Modules = UniqueModules;

  auto GetModuleNameForDiagnostic = [this](const Module *M) -> std::string {
    if (M->isModuleMapModule())
      return M->getFullModuleName();

    if (M->isImplicitGlobalModule())
      M = M->getTopLevelModule();

    // If the current module unit is in the same module with M, it is OK to show
    // the partition name. Otherwise, it'll be sufficient to show the primary
    // module name.
    if (getASTContext().isInSameModule(M, getCurrentModule()))
      return M->getTopLevelModuleName().str();
    else
      return M->getPrimaryModuleInterfaceName().str();
  };

  if (Modules.size() > 1) {
    std::string ModuleList;
    unsigned N = 0;
    for (const auto *M : Modules) {
      ModuleList += "\n        ";
      if (++N == 5 && N != Modules.size()) {
        ModuleList += "[...]";
        break;
      }
      ModuleList += GetModuleNameForDiagnostic(M);
    }

    Diag(UseLoc, diag::err_module_unimported_use_multiple)
      << (int)MIK << Decl << ModuleList;
  } else {
    // FIXME: Add a FixItHint that imports the corresponding module.
    Diag(UseLoc, diag::err_module_unimported_use)
        << (int)MIK << Decl << GetModuleNameForDiagnostic(Modules[0]);
  }

  NotePrevious();

  // Try to recover by implicitly importing this module.
  if (Recover)
    createImplicitModuleImportForErrorRecovery(UseLoc, Modules[0]);
}

void Sema::diagnoseTypo(const TypoCorrection &Correction,
                        const PartialDiagnostic &TypoDiag,
                        const PartialDiagnostic &PrevNote,
                        bool ErrorRecovery) {
  std::string CorrectedStr = Correction.getAsString(getLangOpts());
  std::string CorrectedQuotedStr = Correction.getQuoted(getLangOpts());
  FixItHint FixTypo = FixItHint::CreateReplacement(
      Correction.getCorrectionRange(), CorrectedStr);

  // Maybe we're just missing a module import.
  if (Correction.requiresImport()) {
    NamedDecl *Decl = Correction.getFoundDecl();
    assert(Decl && "import required but no declaration to import");

    diagnoseMissingImport(Correction.getCorrectionRange().getBegin(), Decl,
                          MissingImportKind::Declaration, ErrorRecovery);
    return;
  }

  Diag(Correction.getCorrectionRange().getBegin(), TypoDiag)
    << CorrectedQuotedStr << (ErrorRecovery ? FixTypo : FixItHint());

  NamedDecl *ChosenDecl =
      Correction.isKeyword() ? nullptr : Correction.getFoundDecl();

  // For builtin functions which aren't declared anywhere in source,
  // don't emit the "declared here" note.
  if (const auto *FD = dyn_cast_if_present<FunctionDecl>(ChosenDecl);
      FD && FD->getBuiltinID() &&
      PrevNote.getDiagID() == diag::note_previous_decl &&
      Correction.getCorrectionRange().getBegin() == FD->getBeginLoc()) {
    ChosenDecl = nullptr;
  }

  if (PrevNote.getDiagID() && ChosenDecl)
    Diag(ChosenDecl->getLocation(), PrevNote)
      << CorrectedQuotedStr << (ErrorRecovery ? FixItHint() : FixTypo);

  // Add any extra diagnostics.
  for (const PartialDiagnostic &PD : Correction.getExtraDiagnostics())
    Diag(Correction.getCorrectionRange().getBegin(), PD);
}

TypoExpr *Sema::createDelayedTypo(std::unique_ptr<TypoCorrectionConsumer> TCC,
                                  TypoDiagnosticGenerator TDG,
                                  TypoRecoveryCallback TRC,
                                  SourceLocation TypoLoc) {
  assert(TCC && "createDelayedTypo requires a valid TypoCorrectionConsumer");
  auto TE = new (Context) TypoExpr(Context.DependentTy, TypoLoc);
  auto &State = DelayedTypos[TE];
  State.Consumer = std::move(TCC);
  State.DiagHandler = std::move(TDG);
  State.RecoveryHandler = std::move(TRC);
  if (TE)
    TypoExprs.push_back(TE);
  return TE;
}

const Sema::TypoExprState &Sema::getTypoExprState(TypoExpr *TE) const {
  auto Entry = DelayedTypos.find(TE);
  assert(Entry != DelayedTypos.end() &&
         "Failed to get the state for a TypoExpr!");
  return Entry->second;
}

void Sema::clearDelayedTypo(TypoExpr *TE) {
  DelayedTypos.erase(TE);
}

void Sema::ActOnPragmaDump(Scope *S, SourceLocation IILoc, IdentifierInfo *II) {
  DeclarationNameInfo Name(II, IILoc);
  LookupResult R(*this, Name, LookupAnyName,
                 RedeclarationKind::NotForRedeclaration);
  R.suppressDiagnostics();
  R.setHideTags(false);
  LookupName(R, S);
  R.dump();
}

void Sema::ActOnPragmaDump(Expr *E) {
  E->dump();
}

RedeclarationKind Sema::forRedeclarationInCurContext() const {
  // A declaration with an owning module for linkage can never link against
  // anything that is not visible. We don't need to check linkage here; if
  // the context has internal linkage, redeclaration lookup won't find things
  // from other TUs, and we can't safely compute linkage yet in general.
  if (cast<Decl>(CurContext)->getOwningModuleForLinkage())
    return RedeclarationKind::ForVisibleRedeclaration;
  return RedeclarationKind::ForExternalRedeclaration;
}
