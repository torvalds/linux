//===- Lookup.h - Classes for name lookup -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LookupResult class, which is integral to
// Sema's name-lookup subsystem.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_LOOKUP_H
#define LLVM_CLANG_SEMA_LOOKUP_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Type.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <optional>
#include <utility>

namespace clang {

class CXXBasePaths;

/// Represents the results of name lookup.
///
/// An instance of the LookupResult class captures the results of a
/// single name lookup, which can return no result (nothing found),
/// a single declaration, a set of overloaded functions, or an
/// ambiguity. Use the getKind() method to determine which of these
/// results occurred for a given lookup.
class LookupResult {
public:
  enum LookupResultKind {
    /// No entity found met the criteria.
    NotFound = 0,

    /// No entity found met the criteria within the current
    /// instantiation,, but there were dependent base classes of the
    /// current instantiation that could not be searched.
    NotFoundInCurrentInstantiation,

    /// Name lookup found a single declaration that met the
    /// criteria.  getFoundDecl() will return this declaration.
    Found,

    /// Name lookup found a set of overloaded functions that
    /// met the criteria.
    FoundOverloaded,

    /// Name lookup found an unresolvable value declaration
    /// and cannot yet complete.  This only happens in C++ dependent
    /// contexts with dependent using declarations.
    FoundUnresolvedValue,

    /// Name lookup results in an ambiguity; use
    /// getAmbiguityKind to figure out what kind of ambiguity
    /// we have.
    Ambiguous
  };

  enum AmbiguityKind {
    /// Name lookup results in an ambiguity because multiple
    /// entities that meet the lookup criteria were found in
    /// subobjects of different types. For example:
    /// @code
    /// struct A { void f(int); }
    /// struct B { void f(double); }
    /// struct C : A, B { };
    /// void test(C c) {
    ///   c.f(0); // error: A::f and B::f come from subobjects of different
    ///           // types. overload resolution is not performed.
    /// }
    /// @endcode
    AmbiguousBaseSubobjectTypes,

    /// Name lookup results in an ambiguity because multiple
    /// nonstatic entities that meet the lookup criteria were found
    /// in different subobjects of the same type. For example:
    /// @code
    /// struct A { int x; };
    /// struct B : A { };
    /// struct C : A { };
    /// struct D : B, C { };
    /// int test(D d) {
    ///   return d.x; // error: 'x' is found in two A subobjects (of B and C)
    /// }
    /// @endcode
    AmbiguousBaseSubobjects,

    /// Name lookup results in an ambiguity because multiple definitions
    /// of entity that meet the lookup criteria were found in different
    /// declaration contexts.
    /// @code
    /// namespace A {
    ///   int i;
    ///   namespace B { int i; }
    ///   int test() {
    ///     using namespace B;
    ///     return i; // error 'i' is found in namespace A and A::B
    ///    }
    /// }
    /// @endcode
    AmbiguousReference,

    /// Name lookup results in an ambiguity because multiple placeholder
    /// variables were found in the same scope.
    /// @code
    /// void f() {
    ///    int _ = 0;
    ///    int _ = 0;
    ///    return _; // ambiguous use of placeholder variable
    /// }
    /// @endcode
    AmbiguousReferenceToPlaceholderVariable,

    /// Name lookup results in an ambiguity because an entity with a
    /// tag name was hidden by an entity with an ordinary name from
    /// a different context.
    /// @code
    /// namespace A { struct Foo {}; }
    /// namespace B { void Foo(); }
    /// namespace C {
    ///   using namespace A;
    ///   using namespace B;
    /// }
    /// void test() {
    ///   C::Foo(); // error: tag 'A::Foo' is hidden by an object in a
    ///             // different namespace
    /// }
    /// @endcode
    AmbiguousTagHiding
  };

  /// A little identifier for flagging temporary lookup results.
  enum TemporaryToken {
    Temporary
  };

  using iterator = UnresolvedSetImpl::iterator;

  LookupResult(
      Sema &SemaRef, const DeclarationNameInfo &NameInfo,
      Sema::LookupNameKind LookupKind,
      RedeclarationKind Redecl = RedeclarationKind::NotForRedeclaration)
      : SemaPtr(&SemaRef), NameInfo(NameInfo), LookupKind(LookupKind),
        Redecl(Redecl != RedeclarationKind::NotForRedeclaration),
        ExternalRedecl(Redecl == RedeclarationKind::ForExternalRedeclaration),
        DiagnoseAccess(Redecl == RedeclarationKind::NotForRedeclaration),
        DiagnoseAmbiguous(Redecl == RedeclarationKind::NotForRedeclaration) {
    configure();
  }

  // TODO: consider whether this constructor should be restricted to take
  // as input a const IdentifierInfo* (instead of Name),
  // forcing other cases towards the constructor taking a DNInfo.
  LookupResult(
      Sema &SemaRef, DeclarationName Name, SourceLocation NameLoc,
      Sema::LookupNameKind LookupKind,
      RedeclarationKind Redecl = RedeclarationKind::NotForRedeclaration)
      : SemaPtr(&SemaRef), NameInfo(Name, NameLoc), LookupKind(LookupKind),
        Redecl(Redecl != RedeclarationKind::NotForRedeclaration),
        ExternalRedecl(Redecl == RedeclarationKind::ForExternalRedeclaration),
        DiagnoseAccess(Redecl == RedeclarationKind::NotForRedeclaration),
        DiagnoseAmbiguous(Redecl == RedeclarationKind::NotForRedeclaration) {
    configure();
  }

  /// Creates a temporary lookup result, initializing its core data
  /// using the information from another result.  Diagnostics are always
  /// disabled.
  LookupResult(TemporaryToken _, const LookupResult &Other)
      : SemaPtr(Other.SemaPtr), NameInfo(Other.NameInfo),
        LookupKind(Other.LookupKind), IDNS(Other.IDNS), Redecl(Other.Redecl),
        ExternalRedecl(Other.ExternalRedecl), HideTags(Other.HideTags),
        AllowHidden(Other.AllowHidden),
        TemplateNameLookup(Other.TemplateNameLookup) {}

  // FIXME: Remove these deleted methods once the default build includes
  // -Wdeprecated.
  LookupResult(const LookupResult &) = delete;
  LookupResult &operator=(const LookupResult &) = delete;

  LookupResult(LookupResult &&Other)
      : ResultKind(std::move(Other.ResultKind)),
        Ambiguity(std::move(Other.Ambiguity)), Decls(std::move(Other.Decls)),
        Paths(std::move(Other.Paths)),
        NamingClass(std::move(Other.NamingClass)),
        BaseObjectType(std::move(Other.BaseObjectType)),
        SemaPtr(std::move(Other.SemaPtr)), NameInfo(std::move(Other.NameInfo)),
        NameContextRange(std::move(Other.NameContextRange)),
        LookupKind(std::move(Other.LookupKind)), IDNS(std::move(Other.IDNS)),
        Redecl(std::move(Other.Redecl)),
        ExternalRedecl(std::move(Other.ExternalRedecl)),
        HideTags(std::move(Other.HideTags)),
        DiagnoseAccess(std::move(Other.DiagnoseAccess)),
        DiagnoseAmbiguous(std::move(Other.DiagnoseAmbiguous)),
        AllowHidden(std::move(Other.AllowHidden)),
        Shadowed(std::move(Other.Shadowed)),
        TemplateNameLookup(std::move(Other.TemplateNameLookup)) {
    Other.Paths = nullptr;
    Other.DiagnoseAccess = false;
    Other.DiagnoseAmbiguous = false;
  }

  LookupResult &operator=(LookupResult &&Other) {
    ResultKind = std::move(Other.ResultKind);
    Ambiguity = std::move(Other.Ambiguity);
    Decls = std::move(Other.Decls);
    Paths = std::move(Other.Paths);
    NamingClass = std::move(Other.NamingClass);
    BaseObjectType = std::move(Other.BaseObjectType);
    SemaPtr = std::move(Other.SemaPtr);
    NameInfo = std::move(Other.NameInfo);
    NameContextRange = std::move(Other.NameContextRange);
    LookupKind = std::move(Other.LookupKind);
    IDNS = std::move(Other.IDNS);
    Redecl = std::move(Other.Redecl);
    ExternalRedecl = std::move(Other.ExternalRedecl);
    HideTags = std::move(Other.HideTags);
    DiagnoseAccess = std::move(Other.DiagnoseAccess);
    DiagnoseAmbiguous = std::move(Other.DiagnoseAmbiguous);
    AllowHidden = std::move(Other.AllowHidden);
    Shadowed = std::move(Other.Shadowed);
    TemplateNameLookup = std::move(Other.TemplateNameLookup);
    Other.Paths = nullptr;
    Other.DiagnoseAccess = false;
    Other.DiagnoseAmbiguous = false;
    return *this;
  }

  ~LookupResult() {
    if (DiagnoseAccess)
      diagnoseAccess();
    if (DiagnoseAmbiguous)
      diagnoseAmbiguous();
    if (Paths) deletePaths(Paths);
  }

  /// Gets the name info to look up.
  const DeclarationNameInfo &getLookupNameInfo() const {
    return NameInfo;
  }

  /// Sets the name info to look up.
  void setLookupNameInfo(const DeclarationNameInfo &NameInfo) {
    this->NameInfo = NameInfo;
  }

  /// Gets the name to look up.
  DeclarationName getLookupName() const {
    return NameInfo.getName();
  }

  /// Sets the name to look up.
  void setLookupName(DeclarationName Name) {
    NameInfo.setName(Name);
  }

  /// Gets the kind of lookup to perform.
  Sema::LookupNameKind getLookupKind() const {
    return LookupKind;
  }

  /// True if this lookup is just looking for an existing declaration.
  bool isForRedeclaration() const {
    return Redecl;
  }

  /// True if this lookup is just looking for an existing declaration to link
  /// against a declaration with external linkage.
  bool isForExternalRedeclaration() const {
    return ExternalRedecl;
  }

  RedeclarationKind redeclarationKind() const {
    return ExternalRedecl ? RedeclarationKind::ForExternalRedeclaration
           : Redecl       ? RedeclarationKind::ForVisibleRedeclaration
                          : RedeclarationKind::NotForRedeclaration;
  }

  /// Specify whether hidden declarations are visible, e.g.,
  /// for recovery reasons.
  void setAllowHidden(bool AH) {
    AllowHidden = AH;
  }

  /// Determine whether this lookup is permitted to see hidden
  /// declarations, such as those in modules that have not yet been imported.
  bool isHiddenDeclarationVisible(NamedDecl *ND) const {
    return AllowHidden ||
           (isForExternalRedeclaration() && ND->isExternallyDeclarable());
  }

  /// Sets whether tag declarations should be hidden by non-tag
  /// declarations during resolution.  The default is true.
  void setHideTags(bool Hide) {
    HideTags = Hide;
  }

  /// Sets whether this is a template-name lookup. For template-name lookups,
  /// injected-class-names are treated as naming a template rather than a
  /// template specialization.
  void setTemplateNameLookup(bool TemplateName) {
    TemplateNameLookup = TemplateName;
  }

  bool isTemplateNameLookup() const { return TemplateNameLookup; }

  bool isAmbiguous() const {
    return getResultKind() == Ambiguous;
  }

  /// Determines if this names a single result which is not an
  /// unresolved value using decl.  If so, it is safe to call
  /// getFoundDecl().
  bool isSingleResult() const {
    return getResultKind() == Found;
  }

  /// Determines if the results are overloaded.
  bool isOverloadedResult() const {
    return getResultKind() == FoundOverloaded;
  }

  bool isUnresolvableResult() const {
    return getResultKind() == FoundUnresolvedValue;
  }

  LookupResultKind getResultKind() const {
    assert(checkDebugAssumptions());
    return ResultKind;
  }

  AmbiguityKind getAmbiguityKind() const {
    assert(isAmbiguous());
    return Ambiguity;
  }

  const UnresolvedSetImpl &asUnresolvedSet() const {
    return Decls;
  }

  iterator begin() const { return iterator(Decls.begin()); }
  iterator end() const { return iterator(Decls.end()); }

  /// Return true if no decls were found
  bool empty() const { return Decls.empty(); }

  /// Return the base paths structure that's associated with
  /// these results, or null if none is.
  CXXBasePaths *getBasePaths() const {
    return Paths;
  }

  /// Determine whether the given declaration is visible to the
  /// program.
  static bool isVisible(Sema &SemaRef, NamedDecl *D);

  static bool isReachable(Sema &SemaRef, NamedDecl *D);

  static bool isAcceptable(Sema &SemaRef, NamedDecl *D,
                           Sema::AcceptableKind Kind) {
    return Kind == Sema::AcceptableKind::Visible ? isVisible(SemaRef, D)
                                                 : isReachable(SemaRef, D);
  }

  /// Determine whether this lookup is permitted to see the declaration.
  /// Note that a reachable but not visible declaration inhabiting a namespace
  /// is not allowed to be seen during name lookup.
  ///
  /// For example:
  /// ```
  /// // m.cppm
  /// export module m;
  /// struct reachable { int v; }
  /// export auto func() { return reachable{43}; }
  /// // Use.cpp
  /// import m;
  /// auto Use() {
  ///   // Not valid. We couldn't see reachable here.
  ///   // So isAvailableForLookup would return false when we look
  ///   up 'reachable' here.
  ///   // return reachable(43).v;
  ///   // Valid. The field name 'v' is allowed during name lookup.
  ///   // So isAvailableForLookup would return true when we look up 'v' here.
  ///   return func().v;
  /// }
  /// ```
  static bool isAvailableForLookup(Sema &SemaRef, NamedDecl *ND);

  /// Retrieve the accepted (re)declaration of the given declaration,
  /// if there is one.
  NamedDecl *getAcceptableDecl(NamedDecl *D) const {
    if (!D->isInIdentifierNamespace(IDNS))
      return nullptr;

    if (isAvailableForLookup(getSema(), D) || isHiddenDeclarationVisible(D))
      return D;

    return getAcceptableDeclSlow(D);
  }

private:
  static bool isAcceptableSlow(Sema &SemaRef, NamedDecl *D,
                               Sema::AcceptableKind Kind);
  static bool isReachableSlow(Sema &SemaRef, NamedDecl *D);
  NamedDecl *getAcceptableDeclSlow(NamedDecl *D) const;

public:
  /// Returns the identifier namespace mask for this lookup.
  unsigned getIdentifierNamespace() const {
    return IDNS;
  }

  /// Returns whether these results arose from performing a
  /// lookup into a class.
  bool isClassLookup() const {
    return NamingClass != nullptr;
  }

  /// Returns the 'naming class' for this lookup, i.e. the
  /// class which was looked into to find these results.
  ///
  /// C++0x [class.access.base]p5:
  ///   The access to a member is affected by the class in which the
  ///   member is named. This naming class is the class in which the
  ///   member name was looked up and found. [Note: this class can be
  ///   explicit, e.g., when a qualified-id is used, or implicit,
  ///   e.g., when a class member access operator (5.2.5) is used
  ///   (including cases where an implicit "this->" is added). If both
  ///   a class member access operator and a qualified-id are used to
  ///   name the member (as in p->T::m), the class naming the member
  ///   is the class named by the nested-name-specifier of the
  ///   qualified-id (that is, T). -- end note ]
  ///
  /// This is set by the lookup routines when they find results in a class.
  CXXRecordDecl *getNamingClass() const {
    return NamingClass;
  }

  /// Sets the 'naming class' for this lookup.
  void setNamingClass(CXXRecordDecl *Record) {
    NamingClass = Record;
  }

  /// Returns the base object type associated with this lookup;
  /// important for [class.protected].  Most lookups do not have an
  /// associated base object.
  QualType getBaseObjectType() const {
    return BaseObjectType;
  }

  /// Sets the base object type for this lookup.
  void setBaseObjectType(QualType T) {
    BaseObjectType = T;
  }

  /// Add a declaration to these results with its natural access.
  /// Does not test the acceptance criteria.
  void addDecl(NamedDecl *D) {
    addDecl(D, D->getAccess());
  }

  /// Add a declaration to these results with the given access.
  /// Does not test the acceptance criteria.
  void addDecl(NamedDecl *D, AccessSpecifier AS) {
    Decls.addDecl(D, AS);
    ResultKind = Found;
  }

  /// Add all the declarations from another set of lookup
  /// results.
  void addAllDecls(const LookupResult &Other) {
    Decls.append(Other.Decls.begin(), Other.Decls.end());
    ResultKind = Found;
  }

  /// Determine whether no result was found because we could not
  /// search into dependent base classes of the current instantiation.
  bool wasNotFoundInCurrentInstantiation() const {
    return ResultKind == NotFoundInCurrentInstantiation;
  }

  /// Note that while no result was found in the current instantiation,
  /// there were dependent base classes that could not be searched.
  void setNotFoundInCurrentInstantiation() {
    assert((ResultKind == NotFound ||
            ResultKind == NotFoundInCurrentInstantiation) &&
           Decls.empty());
    ResultKind = NotFoundInCurrentInstantiation;
  }

  /// Determine whether the lookup result was shadowed by some other
  /// declaration that lookup ignored.
  bool isShadowed() const { return Shadowed; }

  /// Note that we found and ignored a declaration while performing
  /// lookup.
  void setShadowed() { Shadowed = true; }

  /// Resolves the result kind of the lookup, possibly hiding
  /// decls.
  ///
  /// This should be called in any environment where lookup might
  /// generate multiple lookup results.
  void resolveKind();

  /// Re-resolves the result kind of the lookup after a set of
  /// removals has been performed.
  void resolveKindAfterFilter() {
    if (Decls.empty()) {
      if (ResultKind != NotFoundInCurrentInstantiation)
        ResultKind = NotFound;

      if (Paths) {
        deletePaths(Paths);
        Paths = nullptr;
      }
    } else {
      std::optional<AmbiguityKind> SavedAK;
      bool WasAmbiguous = false;
      if (ResultKind == Ambiguous) {
        SavedAK = Ambiguity;
        WasAmbiguous = true;
      }
      ResultKind = Found;
      resolveKind();

      // If we didn't make the lookup unambiguous, restore the old
      // ambiguity kind.
      if (ResultKind == Ambiguous) {
        (void)WasAmbiguous;
        assert(WasAmbiguous);
        Ambiguity = *SavedAK;
      } else if (Paths) {
        deletePaths(Paths);
        Paths = nullptr;
      }
    }
  }

  template <class DeclClass>
  DeclClass *getAsSingle() const {
    if (getResultKind() != Found) return nullptr;
    return dyn_cast<DeclClass>(getFoundDecl());
  }

  /// Fetch the unique decl found by this lookup.  Asserts
  /// that one was found.
  ///
  /// This is intended for users who have examined the result kind
  /// and are certain that there is only one result.
  NamedDecl *getFoundDecl() const {
    assert(getResultKind() == Found
           && "getFoundDecl called on non-unique result");
    return (*begin())->getUnderlyingDecl();
  }

  /// Fetches a representative decl.  Useful for lazy diagnostics.
  NamedDecl *getRepresentativeDecl() const {
    assert(!Decls.empty() && "cannot get representative of empty set");
    return *begin();
  }

  /// Asks if the result is a single tag decl.
  bool isSingleTagDecl() const {
    return getResultKind() == Found && isa<TagDecl>(getFoundDecl());
  }

  /// Make these results show that the name was found in
  /// base classes of different types.
  ///
  /// The given paths object is copied and invalidated.
  void setAmbiguousBaseSubobjectTypes(CXXBasePaths &P);

  /// Make these results show that the name was found in
  /// distinct base classes of the same type.
  ///
  /// The given paths object is copied and invalidated.
  void setAmbiguousBaseSubobjects(CXXBasePaths &P);

  /// Make these results show that the name was found in
  /// different contexts and a tag decl was hidden by an ordinary
  /// decl in a different context.
  void setAmbiguousQualifiedTagHiding() {
    setAmbiguous(AmbiguousTagHiding);
  }

  /// Clears out any current state.
  LLVM_ATTRIBUTE_REINITIALIZES void clear() {
    ResultKind = NotFound;
    Decls.clear();
    if (Paths) deletePaths(Paths);
    Paths = nullptr;
    NamingClass = nullptr;
    Shadowed = false;
  }

  /// Clears out any current state and re-initializes for a
  /// different kind of lookup.
  void clear(Sema::LookupNameKind Kind) {
    clear();
    LookupKind = Kind;
    configure();
  }

  /// Change this lookup's redeclaration kind.
  void setRedeclarationKind(RedeclarationKind RK) {
    Redecl = (RK != RedeclarationKind::NotForRedeclaration);
    ExternalRedecl = (RK == RedeclarationKind::ForExternalRedeclaration);
    configure();
  }

  void dump();
  void print(raw_ostream &);

  /// Suppress the diagnostics that would normally fire because of this
  /// lookup.  This happens during (e.g.) redeclaration lookups.
  void suppressDiagnostics() {
    DiagnoseAccess = false;
    DiagnoseAmbiguous = false;
  }

  /// Suppress the diagnostics that would normally fire because of this
  /// lookup due to access control violations.
  void suppressAccessDiagnostics() { DiagnoseAccess = false; }

  /// Determines whether this lookup is suppressing access control diagnostics.
  bool isSuppressingAccessDiagnostics() const { return !DiagnoseAccess; }

  /// Determines whether this lookup is suppressing ambiguous lookup
  /// diagnostics.
  bool isSuppressingAmbiguousDiagnostics() const { return !DiagnoseAmbiguous; }

  /// Sets a 'context' source range.
  void setContextRange(SourceRange SR) {
    NameContextRange = SR;
  }

  /// Gets the source range of the context of this name; for C++
  /// qualified lookups, this is the source range of the scope
  /// specifier.
  SourceRange getContextRange() const {
    return NameContextRange;
  }

  /// Gets the location of the identifier.  This isn't always defined:
  /// sometimes we're doing lookups on synthesized names.
  SourceLocation getNameLoc() const {
    return NameInfo.getLoc();
  }

  /// Get the Sema object that this lookup result is searching
  /// with.
  Sema &getSema() const { return *SemaPtr; }

  /// A class for iterating through a result set and possibly
  /// filtering out results.  The results returned are possibly
  /// sugared.
  class Filter {
    friend class LookupResult;

    LookupResult &Results;
    LookupResult::iterator I;
    bool Changed = false;
    bool CalledDone = false;

    Filter(LookupResult &Results) : Results(Results), I(Results.begin()) {}

  public:
    Filter(Filter &&F)
        : Results(F.Results), I(F.I), Changed(F.Changed),
          CalledDone(F.CalledDone) {
      F.CalledDone = true;
    }

    // The move assignment operator is defined as deleted pending
    // further motivation.
    Filter &operator=(Filter &&) = delete;

    // The copy constrcutor and copy assignment operator is defined as deleted
    // pending further motivation.
    Filter(const Filter &) = delete;
    Filter &operator=(const Filter &) = delete;

    ~Filter() {
      assert(CalledDone &&
             "LookupResult::Filter destroyed without done() call");
    }

    bool hasNext() const {
      return I != Results.end();
    }

    NamedDecl *next() {
      assert(I != Results.end() && "next() called on empty filter");
      return *I++;
    }

    /// Restart the iteration.
    void restart() {
      I = Results.begin();
    }

    /// Erase the last element returned from this iterator.
    void erase() {
      Results.Decls.erase(--I);
      Changed = true;
    }

    /// Replaces the current entry with the given one, preserving the
    /// access bits.
    void replace(NamedDecl *D) {
      Results.Decls.replace(I-1, D);
      Changed = true;
    }

    /// Replaces the current entry with the given one.
    void replace(NamedDecl *D, AccessSpecifier AS) {
      Results.Decls.replace(I-1, D, AS);
      Changed = true;
    }

    void done() {
      assert(!CalledDone && "done() called twice");
      CalledDone = true;

      if (Changed)
        Results.resolveKindAfterFilter();
    }
  };

  /// Create a filter for this result set.
  Filter makeFilter() {
    return Filter(*this);
  }

  void setFindLocalExtern(bool FindLocalExtern) {
    if (FindLocalExtern)
      IDNS |= Decl::IDNS_LocalExtern;
    else
      IDNS &= ~Decl::IDNS_LocalExtern;
  }

private:
  void diagnoseAccess() {
    if (!isAmbiguous() && isClassLookup() &&
        getSema().getLangOpts().AccessControl)
      getSema().CheckLookupAccess(*this);
  }

  void diagnoseAmbiguous() {
    if (isAmbiguous())
      getSema().DiagnoseAmbiguousLookup(*this);
  }

  void setAmbiguous(AmbiguityKind AK) {
    ResultKind = Ambiguous;
    Ambiguity = AK;
  }

  void addDeclsFromBasePaths(const CXXBasePaths &P);
  void configure();

  bool checkDebugAssumptions() const;

  bool checkUnresolved() const {
    for (iterator I = begin(), E = end(); I != E; ++I)
      if (isa<UnresolvedUsingValueDecl>((*I)->getUnderlyingDecl()))
        return true;
    return false;
  }

  static void deletePaths(CXXBasePaths *);

  // Results.
  LookupResultKind ResultKind = NotFound;
  // ill-defined unless ambiguous. Still need to be initialized it will be
  // copied/moved.
  AmbiguityKind Ambiguity = {};
  UnresolvedSet<8> Decls;
  CXXBasePaths *Paths = nullptr;
  CXXRecordDecl *NamingClass = nullptr;
  QualType BaseObjectType;

  // Parameters.
  Sema *SemaPtr;
  DeclarationNameInfo NameInfo;
  SourceRange NameContextRange;
  Sema::LookupNameKind LookupKind;
  unsigned IDNS = 0; // set by configure()

  bool Redecl;
  bool ExternalRedecl;

  /// True if tag declarations should be hidden if non-tags
  ///   are present
  bool HideTags = true;

  bool DiagnoseAccess = false;
  bool DiagnoseAmbiguous = false;

  /// True if we should allow hidden declarations to be 'visible'.
  bool AllowHidden = false;

  /// True if the found declarations were shadowed by some other
  /// declaration that we skipped. This only happens when \c LookupKind
  /// is \c LookupRedeclarationWithLinkage.
  bool Shadowed = false;

  /// True if we're looking up a template-name.
  bool TemplateNameLookup = false;
};

/// Consumes visible declarations found when searching for
/// all visible names within a given scope or context.
///
/// This abstract class is meant to be subclassed by clients of \c
/// Sema::LookupVisibleDecls(), each of which should override the \c
/// FoundDecl() function to process declarations as they are found.
class VisibleDeclConsumer {
public:
  /// Destroys the visible declaration consumer.
  virtual ~VisibleDeclConsumer();

  /// Determine whether hidden declarations (from unimported
  /// modules) should be given to this consumer. By default, they
  /// are not included.
  virtual bool includeHiddenDecls() const;

  /// Invoked each time \p Sema::LookupVisibleDecls() finds a
  /// declaration visible from the current scope or context.
  ///
  /// \param ND the declaration found.
  ///
  /// \param Hiding a declaration that hides the declaration \p ND,
  /// or NULL if no such declaration exists.
  ///
  /// \param Ctx the original context from which the lookup started.
  ///
  /// \param InBaseClass whether this declaration was found in base
  /// class of the context we searched.
  virtual void FoundDecl(NamedDecl *ND, NamedDecl *Hiding, DeclContext *Ctx,
                         bool InBaseClass) = 0;

  /// Callback to inform the client that Sema entered into a new context
  /// to find a visible declaration.
  //
  /// \param Ctx the context which Sema entered.
  virtual void EnteredContext(DeclContext *Ctx) {}
};

/// A class for storing results from argument-dependent lookup.
class ADLResult {
private:
  /// A map from canonical decls to the 'most recent' decl.
  llvm::MapVector<NamedDecl*, NamedDecl*> Decls;

  struct select_second {
    NamedDecl *operator()(std::pair<NamedDecl*, NamedDecl*> P) const {
      return P.second;
    }
  };

public:
  /// Adds a new ADL candidate to this map.
  void insert(NamedDecl *D);

  /// Removes any data associated with a given decl.
  void erase(NamedDecl *D) {
    Decls.erase(cast<NamedDecl>(D->getCanonicalDecl()));
  }

  using iterator =
      llvm::mapped_iterator<decltype(Decls)::iterator, select_second>;

  iterator begin() { return iterator(Decls.begin(), select_second()); }
  iterator end() { return iterator(Decls.end(), select_second()); }
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_LOOKUP_H
