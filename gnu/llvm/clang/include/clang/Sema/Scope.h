//===- Scope.h - Scope interface --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Scope interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SCOPE_H
#define LLVM_CLANG_SEMA_SCOPE_H

#include "clang/AST/Decl.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include <cassert>
#include <optional>

namespace llvm {

class raw_ostream;

} // namespace llvm

namespace clang {

class Decl;
class DeclContext;
class UsingDirectiveDecl;
class VarDecl;

/// Scope - A scope is a transient data structure that is used while parsing the
/// program.  It assists with resolving identifiers to the appropriate
/// declaration.
class Scope {
public:
  /// ScopeFlags - These are bitfields that are or'd together when creating a
  /// scope, which defines the sorts of things the scope contains.
  enum ScopeFlags {
    // A bitfield value representing no scopes.
    NoScope = 0,

    /// This indicates that the scope corresponds to a function, which
    /// means that labels are set here.
    FnScope = 0x01,

    /// This is a while, do, switch, for, etc that can have break
    /// statements embedded into it.
    BreakScope = 0x02,

    /// This is a while, do, for, which can have continue statements
    /// embedded into it.
    ContinueScope = 0x04,

    /// This is a scope that can contain a declaration.  Some scopes
    /// just contain loop constructs but don't contain decls.
    DeclScope = 0x08,

    /// The controlling scope in a if/switch/while/for statement.
    ControlScope = 0x10,

    /// The scope of a struct/union/class definition.
    ClassScope = 0x20,

    /// This is a scope that corresponds to a block/closure object.
    /// Blocks serve as top-level scopes for some objects like labels, they
    /// also prevent things like break and continue.  BlockScopes always have
    /// the FnScope and DeclScope flags set as well.
    BlockScope = 0x40,

    /// This is a scope that corresponds to the
    /// template parameters of a C++ template. Template parameter
    /// scope starts at the 'template' keyword and ends when the
    /// template declaration ends.
    TemplateParamScope = 0x80,

    /// This is a scope that corresponds to the
    /// parameters within a function prototype.
    FunctionPrototypeScope = 0x100,

    /// This is a scope that corresponds to the parameters within
    /// a function prototype for a function declaration (as opposed to any
    /// other kind of function declarator). Always has FunctionPrototypeScope
    /// set as well.
    FunctionDeclarationScope = 0x200,

    /// This is a scope that corresponds to the Objective-C
    /// \@catch statement.
    AtCatchScope = 0x400,

    /// This scope corresponds to an Objective-C method body.
    /// It always has FnScope and DeclScope set as well.
    ObjCMethodScope = 0x800,

    /// This is a scope that corresponds to a switch statement.
    SwitchScope = 0x1000,

    /// This is the scope of a C++ try statement.
    TryScope = 0x2000,

    /// This is the scope for a function-level C++ try or catch scope.
    FnTryCatchScope = 0x4000,

    /// This is the scope of OpenMP executable directive.
    OpenMPDirectiveScope = 0x8000,

    /// This is the scope of some OpenMP loop directive.
    OpenMPLoopDirectiveScope = 0x10000,

    /// This is the scope of some OpenMP simd directive.
    /// For example, it is used for 'omp simd', 'omp for simd'.
    /// This flag is propagated to children scopes.
    OpenMPSimdDirectiveScope = 0x20000,

    /// This scope corresponds to an enum.
    EnumScope = 0x40000,

    /// This scope corresponds to an SEH try.
    SEHTryScope = 0x80000,

    /// This scope corresponds to an SEH except.
    SEHExceptScope = 0x100000,

    /// We are currently in the filter expression of an SEH except block.
    SEHFilterScope = 0x200000,

    /// This is a compound statement scope.
    CompoundStmtScope = 0x400000,

    /// We are between inheritance colon and the real class/struct definition
    /// scope.
    ClassInheritanceScope = 0x800000,

    /// This is the scope of a C++ catch statement.
    CatchScope = 0x1000000,

    /// This is a scope in which a condition variable is currently being
    /// parsed. If such a scope is a ContinueScope, it's invalid to jump to the
    /// continue block from here.
    ConditionVarScope = 0x2000000,

    /// This is a scope of some OpenMP directive with
    /// order clause which specifies concurrent
    OpenMPOrderClauseScope = 0x4000000,
    /// This is the scope for a lambda, after the lambda introducer.
    /// Lambdas need two FunctionPrototypeScope scopes (because there is a
    /// template scope in between), the outer scope does not increase the
    /// depth of recursion.
    LambdaScope = 0x8000000,
    /// This is the scope of an OpenACC Compute Construct, which restricts
    /// jumping into/out of it.
    OpenACCComputeConstructScope = 0x10000000,

    /// This is a scope of type alias declaration.
    TypeAliasScope = 0x20000000,

    /// This is a scope of friend declaration.
    FriendScope = 0x40000000,
  };

private:
  /// The parent scope for this scope.  This is null for the translation-unit
  /// scope.
  Scope *AnyParent;

  /// Flags - This contains a set of ScopeFlags, which indicates how the scope
  /// interrelates with other control flow statements.
  unsigned Flags;

  /// Depth - This is the depth of this scope.  The translation-unit scope has
  /// depth 0.
  unsigned short Depth;

  /// Declarations with static linkage are mangled with the number of
  /// scopes seen as a component.
  unsigned short MSLastManglingNumber;

  unsigned short MSCurManglingNumber;

  /// PrototypeDepth - This is the number of function prototype scopes
  /// enclosing this scope, including this scope.
  unsigned short PrototypeDepth;

  /// PrototypeIndex - This is the number of parameters currently
  /// declared in this scope.
  unsigned short PrototypeIndex;

  /// FnParent - If this scope has a parent scope that is a function body, this
  /// pointer is non-null and points to it.  This is used for label processing.
  Scope *FnParent;
  Scope *MSLastManglingParent;

  /// BreakParent/ContinueParent - This is a direct link to the innermost
  /// BreakScope/ContinueScope which contains the contents of this scope
  /// for control flow purposes (and might be this scope itself), or null
  /// if there is no such scope.
  Scope *BreakParent, *ContinueParent;

  /// BlockParent - This is a direct link to the immediately containing
  /// BlockScope if this scope is not one, or null if there is none.
  Scope *BlockParent;

  /// TemplateParamParent - This is a direct link to the
  /// immediately containing template parameter scope. In the
  /// case of nested templates, template parameter scopes can have
  /// other template parameter scopes as parents.
  Scope *TemplateParamParent;

  /// DeclScopeParent - This is a direct link to the immediately containing
  /// DeclScope, i.e. scope which can contain declarations.
  Scope *DeclParent;

  /// DeclsInScope - This keeps track of all declarations in this scope.  When
  /// the declaration is added to the scope, it is set as the current
  /// declaration for the identifier in the IdentifierTable.  When the scope is
  /// popped, these declarations are removed from the IdentifierTable's notion
  /// of current declaration.  It is up to the current Action implementation to
  /// implement these semantics.
  using DeclSetTy = llvm::SmallPtrSet<Decl *, 32>;
  DeclSetTy DeclsInScope;

  /// The DeclContext with which this scope is associated. For
  /// example, the entity of a class scope is the class itself, the
  /// entity of a function scope is a function, etc.
  DeclContext *Entity;

  using UsingDirectivesTy = SmallVector<UsingDirectiveDecl *, 2>;
  UsingDirectivesTy UsingDirectives;

  /// Used to determine if errors occurred in this scope.
  DiagnosticErrorTrap ErrorTrap;

  /// A single NRVO candidate variable in this scope.
  /// There are three possible values:
  ///  1) pointer to VarDecl that denotes NRVO candidate itself.
  ///  2) nullptr value means that NRVO is not allowed in this scope
  ///     (e.g. return a function parameter).
  ///  3) std::nullopt value means that there is no NRVO candidate in this scope
  ///     (i.e. there are no return statements in this scope).
  std::optional<VarDecl *> NRVO;

  /// Represents return slots for NRVO candidates in the current scope.
  /// If a variable is present in this set, it means that a return slot is
  /// available for this variable in the current scope.
  llvm::SmallPtrSet<VarDecl *, 8> ReturnSlots;

  void setFlags(Scope *Parent, unsigned F);

public:
  Scope(Scope *Parent, unsigned ScopeFlags, DiagnosticsEngine &Diag)
      : ErrorTrap(Diag) {
    Init(Parent, ScopeFlags);
  }

  /// getFlags - Return the flags for this scope.
  unsigned getFlags() const { return Flags; }

  void setFlags(unsigned F) { setFlags(getParent(), F); }

  /// isBlockScope - Return true if this scope correspond to a closure.
  bool isBlockScope() const { return Flags & BlockScope; }

  /// getParent - Return the scope that this is nested in.
  const Scope *getParent() const { return AnyParent; }
  Scope *getParent() { return AnyParent; }

  /// getFnParent - Return the closest scope that is a function body.
  const Scope *getFnParent() const { return FnParent; }
  Scope *getFnParent() { return FnParent; }

  const Scope *getMSLastManglingParent() const {
    return MSLastManglingParent;
  }
  Scope *getMSLastManglingParent() { return MSLastManglingParent; }

  /// getContinueParent - Return the closest scope that a continue statement
  /// would be affected by.
  Scope *getContinueParent() {
    return ContinueParent;
  }

  const Scope *getContinueParent() const {
    return const_cast<Scope*>(this)->getContinueParent();
  }

  // Set whether we're in the scope of a condition variable, where 'continue'
  // is disallowed despite being a continue scope.
  void setIsConditionVarScope(bool InConditionVarScope) {
    Flags = (Flags & ~ConditionVarScope) |
            (InConditionVarScope ? ConditionVarScope : 0);
  }

  bool isConditionVarScope() const {
    return Flags & ConditionVarScope;
  }

  /// getBreakParent - Return the closest scope that a break statement
  /// would be affected by.
  Scope *getBreakParent() {
    return BreakParent;
  }
  const Scope *getBreakParent() const {
    return const_cast<Scope*>(this)->getBreakParent();
  }

  Scope *getBlockParent() { return BlockParent; }
  const Scope *getBlockParent() const { return BlockParent; }

  Scope *getTemplateParamParent() { return TemplateParamParent; }
  const Scope *getTemplateParamParent() const { return TemplateParamParent; }

  Scope *getDeclParent() { return DeclParent; }
  const Scope *getDeclParent() const { return DeclParent; }

  /// Returns the depth of this scope. The translation-unit has scope depth 0.
  unsigned getDepth() const { return Depth; }

  /// Returns the number of function prototype scopes in this scope
  /// chain.
  unsigned getFunctionPrototypeDepth() const {
    return PrototypeDepth;
  }

  /// Return the number of parameters declared in this function
  /// prototype, increasing it by one for the next call.
  unsigned getNextFunctionPrototypeIndex() {
    assert(isFunctionPrototypeScope());
    return PrototypeIndex++;
  }

  using decl_range = llvm::iterator_range<DeclSetTy::iterator>;

  decl_range decls() const {
    return decl_range(DeclsInScope.begin(), DeclsInScope.end());
  }

  bool decl_empty() const { return DeclsInScope.empty(); }

  void AddDecl(Decl *D) {
    if (auto *VD = dyn_cast<VarDecl>(D))
      if (!isa<ParmVarDecl>(VD))
        ReturnSlots.insert(VD);

    DeclsInScope.insert(D);
  }

  void RemoveDecl(Decl *D) { DeclsInScope.erase(D); }

  void incrementMSManglingNumber() {
    if (Scope *MSLMP = getMSLastManglingParent()) {
      MSLMP->MSLastManglingNumber += 1;
      MSCurManglingNumber += 1;
    }
  }

  void decrementMSManglingNumber() {
    if (Scope *MSLMP = getMSLastManglingParent()) {
      MSLMP->MSLastManglingNumber -= 1;
      MSCurManglingNumber -= 1;
    }
  }

  unsigned getMSLastManglingNumber() const {
    if (const Scope *MSLMP = getMSLastManglingParent())
      return MSLMP->MSLastManglingNumber;
    return 1;
  }

  unsigned getMSCurManglingNumber() const {
    return MSCurManglingNumber;
  }

  /// isDeclScope - Return true if this is the scope that the specified decl is
  /// declared in.
  bool isDeclScope(const Decl *D) const { return DeclsInScope.contains(D); }

  /// Get the entity corresponding to this scope.
  DeclContext *getEntity() const {
    return isTemplateParamScope() ? nullptr : Entity;
  }

  /// Get the DeclContext in which to continue unqualified lookup after a
  /// lookup in this scope.
  DeclContext *getLookupEntity() const { return Entity; }

  void setEntity(DeclContext *E) {
    assert(!isTemplateParamScope() &&
           "entity associated with template param scope");
    Entity = E;
  }
  void setLookupEntity(DeclContext *E) { Entity = E; }

  /// Determine whether any unrecoverable errors have occurred within this
  /// scope. Note that this may return false even if the scope contains invalid
  /// declarations or statements, if the errors for those invalid constructs
  /// were suppressed because some prior invalid construct was referenced.
  bool hasUnrecoverableErrorOccurred() const {
    return ErrorTrap.hasUnrecoverableErrorOccurred();
  }

  /// isFunctionScope() - Return true if this scope is a function scope.
  bool isFunctionScope() const { return getFlags() & Scope::FnScope; }

  /// isClassScope - Return true if this scope is a class/struct/union scope.
  bool isClassScope() const { return getFlags() & Scope::ClassScope; }

  /// Determines whether this scope is between inheritance colon and the real
  /// class/struct definition.
  bool isClassInheritanceScope() const {
    return getFlags() & Scope::ClassInheritanceScope;
  }

  /// isInCXXInlineMethodScope - Return true if this scope is a C++ inline
  /// method scope or is inside one.
  bool isInCXXInlineMethodScope() const {
    if (const Scope *FnS = getFnParent()) {
      assert(FnS->getParent() && "TUScope not created?");
      return FnS->getParent()->isClassScope();
    }
    return false;
  }

  /// isInObjcMethodScope - Return true if this scope is, or is contained in, an
  /// Objective-C method body.  Note that this method is not constant time.
  bool isInObjcMethodScope() const {
    for (const Scope *S = this; S; S = S->getParent()) {
      // If this scope is an objc method scope, then we succeed.
      if (S->getFlags() & ObjCMethodScope)
        return true;
    }
    return false;
  }

  /// isInObjcMethodOuterScope - Return true if this scope is an
  /// Objective-C method outer most body.
  bool isInObjcMethodOuterScope() const {
    if (const Scope *S = this) {
      // If this scope is an objc method scope, then we succeed.
      if (S->getFlags() & ObjCMethodScope)
        return true;
    }
    return false;
  }

  /// isTemplateParamScope - Return true if this scope is a C++
  /// template parameter scope.
  bool isTemplateParamScope() const {
    return getFlags() & Scope::TemplateParamScope;
  }

  /// isFunctionPrototypeScope - Return true if this scope is a
  /// function prototype scope.
  bool isFunctionPrototypeScope() const {
    return getFlags() & Scope::FunctionPrototypeScope;
  }

  /// isFunctionDeclarationScope - Return true if this scope is a
  /// function prototype scope.
  bool isFunctionDeclarationScope() const {
    return getFlags() & Scope::FunctionDeclarationScope;
  }

  /// isAtCatchScope - Return true if this scope is \@catch.
  bool isAtCatchScope() const {
    return getFlags() & Scope::AtCatchScope;
  }

  /// isCatchScope - Return true if this scope is a C++ catch statement.
  bool isCatchScope() const { return getFlags() & Scope::CatchScope; }

  /// isSwitchScope - Return true if this scope is a switch scope.
  bool isSwitchScope() const {
    for (const Scope *S = this; S; S = S->getParent()) {
      if (S->getFlags() & Scope::SwitchScope)
        return true;
      else if (S->getFlags() & (Scope::FnScope | Scope::ClassScope |
                                Scope::BlockScope | Scope::TemplateParamScope |
                                Scope::FunctionPrototypeScope |
                                Scope::AtCatchScope | Scope::ObjCMethodScope))
        return false;
    }
    return false;
  }

  /// Return true if this scope is a loop.
  bool isLoopScope() const {
    // 'switch' is the only loop that is not a 'break' scope as well, so we can
    // just check BreakScope and not SwitchScope.
    return (getFlags() & Scope::BreakScope) &&
           !(getFlags() & Scope::SwitchScope);
  }

  /// Determines whether this scope is the OpenMP directive scope
  bool isOpenMPDirectiveScope() const {
    return (getFlags() & Scope::OpenMPDirectiveScope);
  }

  /// Determine whether this scope is some OpenMP loop directive scope
  /// (for example, 'omp for', 'omp simd').
  bool isOpenMPLoopDirectiveScope() const {
    if (getFlags() & Scope::OpenMPLoopDirectiveScope) {
      assert(isOpenMPDirectiveScope() &&
             "OpenMP loop directive scope is not a directive scope");
      return true;
    }
    return false;
  }

  /// Determine whether this scope is (or is nested into) some OpenMP
  /// loop simd directive scope (for example, 'omp simd', 'omp for simd').
  bool isOpenMPSimdDirectiveScope() const {
    return getFlags() & Scope::OpenMPSimdDirectiveScope;
  }

  /// Determine whether this scope is a loop having OpenMP loop
  /// directive attached.
  bool isOpenMPLoopScope() const {
    const Scope *P = getParent();
    return P && P->isOpenMPLoopDirectiveScope();
  }

  /// Determine whether this scope is some OpenMP directive with
  /// order clause which specifies concurrent scope.
  bool isOpenMPOrderClauseScope() const {
    return getFlags() & Scope::OpenMPOrderClauseScope;
  }

  /// Determine whether this scope is the statement associated with an OpenACC
  /// Compute construct directive.
  bool isOpenACCComputeConstructScope() const {
    return getFlags() & Scope::OpenACCComputeConstructScope;
  }

  /// Determine if this scope (or its parents) are a compute construct. If the
  /// argument is provided, the search will stop at any of the specified scopes.
  /// Otherwise, it will stop only at the normal 'no longer search' scopes.
  bool isInOpenACCComputeConstructScope(ScopeFlags Flags = NoScope) const {
    for (const Scope *S = this; S; S = S->getParent()) {
      if (S->isOpenACCComputeConstructScope())
        return true;

      if (S->getFlags() & Flags)
        return false;

      else if (S->getFlags() &
               (Scope::FnScope | Scope::ClassScope | Scope::BlockScope |
                Scope::TemplateParamScope | Scope::FunctionPrototypeScope |
                Scope::AtCatchScope | Scope::ObjCMethodScope))
        return false;
    }
    return false;
  }

  /// Determine whether this scope is a while/do/for statement, which can have
  /// continue statements embedded into it.
  bool isContinueScope() const {
    return getFlags() & ScopeFlags::ContinueScope;
  }

  /// Determine whether this scope is a C++ 'try' block.
  bool isTryScope() const { return getFlags() & Scope::TryScope; }

  /// Determine whether this scope is a function-level C++ try or catch scope.
  bool isFnTryCatchScope() const {
    return getFlags() & ScopeFlags::FnTryCatchScope;
  }

  /// Determine whether this scope is a SEH '__try' block.
  bool isSEHTryScope() const { return getFlags() & Scope::SEHTryScope; }

  /// Determine whether this scope is a SEH '__except' block.
  bool isSEHExceptScope() const { return getFlags() & Scope::SEHExceptScope; }

  /// Determine whether this scope is a compound statement scope.
  bool isCompoundStmtScope() const {
    return getFlags() & Scope::CompoundStmtScope;
  }

  /// Determine whether this scope is a controlling scope in a
  /// if/switch/while/for statement.
  bool isControlScope() const { return getFlags() & Scope::ControlScope; }

  /// Determine whether this scope is a type alias scope.
  bool isTypeAliasScope() const { return getFlags() & Scope::TypeAliasScope; }

  /// Determine whether this scope is a friend scope.
  bool isFriendScope() const { return getFlags() & Scope::FriendScope; }

  /// Returns if rhs has a higher scope depth than this.
  ///
  /// The caller is responsible for calling this only if one of the two scopes
  /// is an ancestor of the other.
  bool Contains(const Scope& rhs) const { return Depth < rhs.Depth; }

  /// containedInPrototypeScope - Return true if this or a parent scope
  /// is a FunctionPrototypeScope.
  bool containedInPrototypeScope() const;

  void PushUsingDirective(UsingDirectiveDecl *UDir) {
    UsingDirectives.push_back(UDir);
  }

  using using_directives_range =
      llvm::iterator_range<UsingDirectivesTy::iterator>;

  using_directives_range using_directives() {
    return using_directives_range(UsingDirectives.begin(),
                                  UsingDirectives.end());
  }

  void updateNRVOCandidate(VarDecl *VD);

  void applyNRVO();

  /// Init - This is used by the parser to implement scope caching.
  void Init(Scope *parent, unsigned flags);

  /// Sets up the specified scope flags and adjusts the scope state
  /// variables accordingly.
  void AddFlags(unsigned Flags);

  void dumpImpl(raw_ostream &OS) const;
  void dump() const;
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SCOPE_H
