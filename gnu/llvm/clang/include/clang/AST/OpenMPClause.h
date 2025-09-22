//===- OpenMPClause.h - Classes for OpenMP clauses --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file defines OpenMP AST classes for clauses.
/// There are clauses for executable directives, clauses for declarative
/// directives and clauses which can be used in both kinds of directives.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_OPENMPCLAUSE_H
#define LLVM_CLANG_AST_OPENMPCLAUSE_H

#include "clang/AST/ASTFwd.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtIterator.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Frontend/OpenMP/OMPAssume.h"
#include "llvm/Frontend/OpenMP/OMPConstants.h"
#include "llvm/Frontend/OpenMP/OMPContext.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <utility>

namespace clang {

class ASTContext;

//===----------------------------------------------------------------------===//
// AST classes for clauses.
//===----------------------------------------------------------------------===//

/// This is a basic class for representing single OpenMP clause.
class OMPClause {
  /// Starting location of the clause (the clause keyword).
  SourceLocation StartLoc;

  /// Ending location of the clause.
  SourceLocation EndLoc;

  /// Kind of the clause.
  OpenMPClauseKind Kind;

protected:
  OMPClause(OpenMPClauseKind K, SourceLocation StartLoc, SourceLocation EndLoc)
      : StartLoc(StartLoc), EndLoc(EndLoc), Kind(K) {}

public:
  /// Returns the starting location of the clause.
  SourceLocation getBeginLoc() const { return StartLoc; }

  /// Returns the ending location of the clause.
  SourceLocation getEndLoc() const { return EndLoc; }

  /// Sets the starting location of the clause.
  void setLocStart(SourceLocation Loc) { StartLoc = Loc; }

  /// Sets the ending location of the clause.
  void setLocEnd(SourceLocation Loc) { EndLoc = Loc; }

  /// Returns kind of OpenMP clause (private, shared, reduction, etc.).
  OpenMPClauseKind getClauseKind() const { return Kind; }

  bool isImplicit() const { return StartLoc.isInvalid(); }

  using child_iterator = StmtIterator;
  using const_child_iterator = ConstStmtIterator;
  using child_range = llvm::iterator_range<child_iterator>;
  using const_child_range = llvm::iterator_range<const_child_iterator>;

  child_range children();
  const_child_range children() const {
    auto Children = const_cast<OMPClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  /// Get the iterator range for the expressions used in the clauses. Used
  /// expressions include only the children that must be evaluated at the
  /// runtime before entering the construct.
  child_range used_children();
  const_child_range used_children() const {
    auto Children = const_cast<OMPClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const OMPClause *) { return true; }
};

template <OpenMPClauseKind ClauseKind>
struct OMPNoChildClause : public OMPClause {
  /// Build '\p ClauseKind' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPNoChildClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(ClauseKind, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPNoChildClause()
      : OMPClause(ClauseKind, SourceLocation(), SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == ClauseKind;
  }
};

template <OpenMPClauseKind ClauseKind, class Base>
class OMPOneStmtClause : public Base {

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Sub-expression.
  Stmt *S = nullptr;

protected:
  void setStmt(Stmt *S) { this->S = S; }

public:
  OMPOneStmtClause(Stmt *S, SourceLocation StartLoc, SourceLocation LParenLoc,
                   SourceLocation EndLoc)
      : Base(ClauseKind, StartLoc, EndLoc), LParenLoc(LParenLoc), S(S) {}

  OMPOneStmtClause() : Base(ClauseKind, SourceLocation(), SourceLocation()) {}

  /// Return the associated statement, potentially casted to \p T.
  template <typename T> T *getStmtAs() const { return cast_or_null<T>(S); }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  using child_iterator = StmtIterator;
  using const_child_iterator = ConstStmtIterator;
  using child_range = llvm::iterator_range<child_iterator>;
  using const_child_range = llvm::iterator_range<const_child_iterator>;

  child_range children() { return child_range(&S, &S + 1); }

  const_child_range children() const { return const_child_range(&S, &S + 1); }

  // TODO: Consider making the getAddrOfExprAsWritten version the default.
  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == ClauseKind;
  }
};

/// Class that handles pre-initialization statement for some clauses, like
/// 'shedule', 'firstprivate' etc.
class OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Pre-initialization statement for the clause.
  Stmt *PreInit = nullptr;

  /// Region that captures the associated stmt.
  OpenMPDirectiveKind CaptureRegion = llvm::omp::OMPD_unknown;

protected:
  OMPClauseWithPreInit(const OMPClause *This) {
    assert(get(This) && "get is not tuned for pre-init.");
  }

  /// Set pre-initialization statement for the clause.
  void
  setPreInitStmt(Stmt *S,
                 OpenMPDirectiveKind ThisRegion = llvm::omp::OMPD_unknown) {
    PreInit = S;
    CaptureRegion = ThisRegion;
  }

public:
  /// Get pre-initialization statement for the clause.
  const Stmt *getPreInitStmt() const { return PreInit; }

  /// Get pre-initialization statement for the clause.
  Stmt *getPreInitStmt() { return PreInit; }

  /// Get capture region for the stmt in the clause.
  OpenMPDirectiveKind getCaptureRegion() const { return CaptureRegion; }

  static OMPClauseWithPreInit *get(OMPClause *C);
  static const OMPClauseWithPreInit *get(const OMPClause *C);
};

/// Class that handles post-update expression for some clauses, like
/// 'lastprivate', 'reduction' etc.
class OMPClauseWithPostUpdate : public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Post-update expression for the clause.
  Expr *PostUpdate = nullptr;

protected:
  OMPClauseWithPostUpdate(const OMPClause *This) : OMPClauseWithPreInit(This) {
    assert(get(This) && "get is not tuned for post-update.");
  }

  /// Set pre-initialization statement for the clause.
  void setPostUpdateExpr(Expr *S) { PostUpdate = S; }

public:
  /// Get post-update expression for the clause.
  const Expr *getPostUpdateExpr() const { return PostUpdate; }

  /// Get post-update expression for the clause.
  Expr *getPostUpdateExpr() { return PostUpdate; }

  static OMPClauseWithPostUpdate *get(OMPClause *C);
  static const OMPClauseWithPostUpdate *get(const OMPClause *C);
};

/// This structure contains most locations needed for by an OMPVarListClause.
struct OMPVarListLocTy {
  /// Starting location of the clause (the clause keyword).
  SourceLocation StartLoc;
  /// Location of '('.
  SourceLocation LParenLoc;
  /// Ending location of the clause.
  SourceLocation EndLoc;
  OMPVarListLocTy() = default;
  OMPVarListLocTy(SourceLocation StartLoc, SourceLocation LParenLoc,
                  SourceLocation EndLoc)
      : StartLoc(StartLoc), LParenLoc(LParenLoc), EndLoc(EndLoc) {}
};

/// This represents clauses with the list of variables like 'private',
/// 'firstprivate', 'copyin', 'shared', or 'reduction' clauses in the
/// '#pragma omp ...' directives.
template <class T> class OMPVarListClause : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Number of variables in the list.
  unsigned NumVars;

protected:
  /// Build a clause with \a N variables
  ///
  /// \param K Kind of the clause.
  /// \param StartLoc Starting location of the clause (the clause keyword).
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPVarListClause(OpenMPClauseKind K, SourceLocation StartLoc,
                   SourceLocation LParenLoc, SourceLocation EndLoc, unsigned N)
      : OMPClause(K, StartLoc, EndLoc), LParenLoc(LParenLoc), NumVars(N) {}

  /// Fetches list of variables associated with this clause.
  MutableArrayRef<Expr *> getVarRefs() {
    return MutableArrayRef<Expr *>(
        static_cast<T *>(this)->template getTrailingObjects<Expr *>(), NumVars);
  }

  /// Sets the list of variables for this clause.
  void setVarRefs(ArrayRef<Expr *> VL) {
    assert(VL.size() == NumVars &&
           "Number of variables is not the same as the preallocated buffer");
    std::copy(VL.begin(), VL.end(),
              static_cast<T *>(this)->template getTrailingObjects<Expr *>());
  }

public:
  using varlist_iterator = MutableArrayRef<Expr *>::iterator;
  using varlist_const_iterator = ArrayRef<const Expr *>::iterator;
  using varlist_range = llvm::iterator_range<varlist_iterator>;
  using varlist_const_range = llvm::iterator_range<varlist_const_iterator>;

  unsigned varlist_size() const { return NumVars; }
  bool varlist_empty() const { return NumVars == 0; }

  varlist_range varlists() {
    return varlist_range(varlist_begin(), varlist_end());
  }
  varlist_const_range varlists() const {
    return varlist_const_range(varlist_begin(), varlist_end());
  }

  varlist_iterator varlist_begin() { return getVarRefs().begin(); }
  varlist_iterator varlist_end() { return getVarRefs().end(); }
  varlist_const_iterator varlist_begin() const { return getVarRefs().begin(); }
  varlist_const_iterator varlist_end() const { return getVarRefs().end(); }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Fetches list of all variables in the clause.
  ArrayRef<const Expr *> getVarRefs() const {
    return llvm::ArrayRef(
        static_cast<const T *>(this)->template getTrailingObjects<Expr *>(),
        NumVars);
  }
};

/// This represents 'allocator' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp allocate(a) allocator(omp_default_mem_alloc)
/// \endcode
/// In this example directive '#pragma omp allocate' has simple 'allocator'
/// clause with the allocator 'omp_default_mem_alloc'.
class OMPAllocatorClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_allocator, OMPClause> {
  friend class OMPClauseReader;

  /// Set allocator.
  void setAllocator(Expr *A) { setStmt(A); }

public:
  /// Build 'allocator' clause with the given allocator.
  ///
  /// \param A Allocator.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPAllocatorClause(Expr *A, SourceLocation StartLoc, SourceLocation LParenLoc,
                     SourceLocation EndLoc)
      : OMPOneStmtClause(A, StartLoc, LParenLoc, EndLoc) {}

  /// Build an empty clause.
  OMPAllocatorClause() : OMPOneStmtClause() {}

  /// Returns allocator.
  Expr *getAllocator() const { return getStmtAs<Expr>(); }
};

/// This represents the 'align' clause in the '#pragma omp allocate'
/// directive.
///
/// \code
/// #pragma omp allocate(a) allocator(omp_default_mem_alloc) align(8)
/// \endcode
/// In this example directive '#pragma omp allocate' has simple 'allocator'
/// clause with the allocator 'omp_default_mem_alloc' and align clause with
/// value of 8.
class OMPAlignClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_align, OMPClause> {
  friend class OMPClauseReader;

  /// Set alignment value.
  void setAlignment(Expr *A) { setStmt(A); }

  /// Build 'align' clause with the given alignment
  ///
  /// \param A Alignment value.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPAlignClause(Expr *A, SourceLocation StartLoc, SourceLocation LParenLoc,
                 SourceLocation EndLoc)
      : OMPOneStmtClause(A, StartLoc, LParenLoc, EndLoc) {}

  /// Build an empty clause.
  OMPAlignClause() : OMPOneStmtClause() {}

public:
  /// Build 'align' clause with the given alignment
  ///
  /// \param A Alignment value.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  static OMPAlignClause *Create(const ASTContext &C, Expr *A,
                                SourceLocation StartLoc,
                                SourceLocation LParenLoc,
                                SourceLocation EndLoc);

  /// Returns alignment
  Expr *getAlignment() const { return getStmtAs<Expr>(); }
};

/// This represents clause 'allocate' in the '#pragma omp ...' directives.
///
/// \code
/// #pragma omp parallel private(a) allocate(omp_default_mem_alloc :a)
/// \endcode
/// In this example directive '#pragma omp parallel' has clause 'private'
/// and clause 'allocate' for the variable 'a'.
class OMPAllocateClause final
    : public OMPVarListClause<OMPAllocateClause>,
      private llvm::TrailingObjects<OMPAllocateClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Allocator specified in the clause, or 'nullptr' if the default one is
  /// used.
  Expr *Allocator = nullptr;
  /// Position of the ':' delimiter in the clause;
  SourceLocation ColonLoc;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param Allocator Allocator expression.
  /// \param ColonLoc Location of ':' delimiter.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPAllocateClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                    Expr *Allocator, SourceLocation ColonLoc,
                    SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPAllocateClause>(llvm::omp::OMPC_allocate, StartLoc,
                                            LParenLoc, EndLoc, N),
        Allocator(Allocator), ColonLoc(ColonLoc) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPAllocateClause(unsigned N)
      : OMPVarListClause<OMPAllocateClause>(llvm::omp::OMPC_allocate,
                                            SourceLocation(), SourceLocation(),
                                            SourceLocation(), N) {}

  /// Sets location of ':' symbol in clause.
  void setColonLoc(SourceLocation CL) { ColonLoc = CL; }

  void setAllocator(Expr *A) { Allocator = A; }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param Allocator Allocator expression.
  /// \param ColonLoc Location of ':' delimiter.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  static OMPAllocateClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                   SourceLocation LParenLoc, Expr *Allocator,
                                   SourceLocation ColonLoc,
                                   SourceLocation EndLoc, ArrayRef<Expr *> VL);

  /// Returns the allocator expression or nullptr, if no allocator is specified.
  Expr *getAllocator() const { return Allocator; }

  /// Returns the location of the ':' delimiter.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPAllocateClause *CreateEmpty(const ASTContext &C, unsigned N);

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPAllocateClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_allocate;
  }
};

/// This represents 'if' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp parallel if(parallel:a > 5)
/// \endcode
/// In this example directive '#pragma omp parallel' has simple 'if' clause with
/// condition 'a > 5' and directive name modifier 'parallel'.
class OMPIfClause : public OMPClause, public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Condition of the 'if' clause.
  Stmt *Condition = nullptr;

  /// Location of ':' (if any).
  SourceLocation ColonLoc;

  /// Directive name modifier for the clause.
  OpenMPDirectiveKind NameModifier = llvm::omp::OMPD_unknown;

  /// Name modifier location.
  SourceLocation NameModifierLoc;

  /// Set condition.
  void setCondition(Expr *Cond) { Condition = Cond; }

  /// Set directive name modifier for the clause.
  void setNameModifier(OpenMPDirectiveKind NM) { NameModifier = NM; }

  /// Set location of directive name modifier for the clause.
  void setNameModifierLoc(SourceLocation Loc) { NameModifierLoc = Loc; }

  /// Set location of ':'.
  void setColonLoc(SourceLocation Loc) { ColonLoc = Loc; }

public:
  /// Build 'if' clause with condition \a Cond.
  ///
  /// \param NameModifier [OpenMP 4.1] Directive name modifier of clause.
  /// \param Cond Condition of the clause.
  /// \param HelperCond Helper condition for the clause.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param NameModifierLoc Location of directive name modifier.
  /// \param ColonLoc [OpenMP 4.1] Location of ':'.
  /// \param EndLoc Ending location of the clause.
  OMPIfClause(OpenMPDirectiveKind NameModifier, Expr *Cond, Stmt *HelperCond,
              OpenMPDirectiveKind CaptureRegion, SourceLocation StartLoc,
              SourceLocation LParenLoc, SourceLocation NameModifierLoc,
              SourceLocation ColonLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_if, StartLoc, EndLoc),
        OMPClauseWithPreInit(this), LParenLoc(LParenLoc), Condition(Cond),
        ColonLoc(ColonLoc), NameModifier(NameModifier),
        NameModifierLoc(NameModifierLoc) {
    setPreInitStmt(HelperCond, CaptureRegion);
  }

  /// Build an empty clause.
  OMPIfClause()
      : OMPClause(llvm::omp::OMPC_if, SourceLocation(), SourceLocation()),
        OMPClauseWithPreInit(this) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Return the location of ':'.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Returns condition.
  Expr *getCondition() const { return cast_or_null<Expr>(Condition); }

  /// Return directive name modifier associated with the clause.
  OpenMPDirectiveKind getNameModifier() const { return NameModifier; }

  /// Return the location of directive name modifier.
  SourceLocation getNameModifierLoc() const { return NameModifierLoc; }

  child_range children() { return child_range(&Condition, &Condition + 1); }

  const_child_range children() const {
    return const_child_range(&Condition, &Condition + 1);
  }

  child_range used_children();
  const_child_range used_children() const {
    auto Children = const_cast<OMPIfClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_if;
  }
};

/// This represents 'final' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp task final(a > 5)
/// \endcode
/// In this example directive '#pragma omp task' has simple 'final'
/// clause with condition 'a > 5'.
class OMPFinalClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_final, OMPClause>,
      public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Set condition.
  void setCondition(Expr *Cond) { setStmt(Cond); }

public:
  /// Build 'final' clause with condition \a Cond.
  ///
  /// \param Cond Condition of the clause.
  /// \param HelperCond Helper condition for the construct.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPFinalClause(Expr *Cond, Stmt *HelperCond,
                 OpenMPDirectiveKind CaptureRegion, SourceLocation StartLoc,
                 SourceLocation LParenLoc, SourceLocation EndLoc)
      : OMPOneStmtClause(Cond, StartLoc, LParenLoc, EndLoc),
        OMPClauseWithPreInit(this) {
    setPreInitStmt(HelperCond, CaptureRegion);
  }

  /// Build an empty clause.
  OMPFinalClause() : OMPOneStmtClause(), OMPClauseWithPreInit(this) {}

  /// Returns condition.
  Expr *getCondition() const { return getStmtAs<Expr>(); }

  child_range used_children();
  const_child_range used_children() const {
    auto Children = const_cast<OMPFinalClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }
};
/// This represents 'num_threads' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp parallel num_threads(6)
/// \endcode
/// In this example directive '#pragma omp parallel' has simple 'num_threads'
/// clause with number of threads '6'.
class OMPNumThreadsClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_num_threads, OMPClause>,
      public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Set condition.
  void setNumThreads(Expr *NThreads) { setStmt(NThreads); }

public:
  /// Build 'num_threads' clause with condition \a NumThreads.
  ///
  /// \param NumThreads Number of threads for the construct.
  /// \param HelperNumThreads Helper Number of threads for the construct.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPNumThreadsClause(Expr *NumThreads, Stmt *HelperNumThreads,
                      OpenMPDirectiveKind CaptureRegion,
                      SourceLocation StartLoc, SourceLocation LParenLoc,
                      SourceLocation EndLoc)
      : OMPOneStmtClause(NumThreads, StartLoc, LParenLoc, EndLoc),
        OMPClauseWithPreInit(this) {
    setPreInitStmt(HelperNumThreads, CaptureRegion);
  }

  /// Build an empty clause.
  OMPNumThreadsClause() : OMPOneStmtClause(), OMPClauseWithPreInit(this) {}

  /// Returns number of threads.
  Expr *getNumThreads() const { return getStmtAs<Expr>(); }
};

/// This represents 'safelen' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp simd safelen(4)
/// \endcode
/// In this example directive '#pragma omp simd' has clause 'safelen'
/// with single expression '4'.
/// If the safelen clause is used then no two iterations executed
/// concurrently with SIMD instructions can have a greater distance
/// in the logical iteration space than its value. The parameter of
/// the safelen clause must be a constant positive integer expression.
class OMPSafelenClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_safelen, OMPClause> {
  friend class OMPClauseReader;

  /// Set safelen.
  void setSafelen(Expr *Len) { setStmt(Len); }

public:
  /// Build 'safelen' clause.
  ///
  /// \param Len Expression associated with this clause.
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPSafelenClause(Expr *Len, SourceLocation StartLoc, SourceLocation LParenLoc,
                   SourceLocation EndLoc)
      : OMPOneStmtClause(Len, StartLoc, LParenLoc, EndLoc) {}

  /// Build an empty clause.
  explicit OMPSafelenClause() : OMPOneStmtClause() {}

  /// Return safe iteration space distance.
  Expr *getSafelen() const { return getStmtAs<Expr>(); }
};

/// This represents 'simdlen' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp simd simdlen(4)
/// \endcode
/// In this example directive '#pragma omp simd' has clause 'simdlen'
/// with single expression '4'.
/// If the 'simdlen' clause is used then it specifies the preferred number of
/// iterations to be executed concurrently. The parameter of the 'simdlen'
/// clause must be a constant positive integer expression.
class OMPSimdlenClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_simdlen, OMPClause> {
  friend class OMPClauseReader;

  /// Set simdlen.
  void setSimdlen(Expr *Len) { setStmt(Len); }

public:
  /// Build 'simdlen' clause.
  ///
  /// \param Len Expression associated with this clause.
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPSimdlenClause(Expr *Len, SourceLocation StartLoc, SourceLocation LParenLoc,
                   SourceLocation EndLoc)
      : OMPOneStmtClause(Len, StartLoc, LParenLoc, EndLoc) {}

  /// Build an empty clause.
  explicit OMPSimdlenClause() : OMPOneStmtClause() {}

  /// Return safe iteration space distance.
  Expr *getSimdlen() const { return getStmtAs<Expr>(); }
};

/// This represents the 'sizes' clause in the '#pragma omp tile' directive.
///
/// \code
/// #pragma omp tile sizes(5,5)
/// for (int i = 0; i < 64; ++i)
///   for (int j = 0; j < 64; ++j)
/// \endcode
class OMPSizesClause final
    : public OMPClause,
      private llvm::TrailingObjects<OMPSizesClause, Expr *> {
  friend class OMPClauseReader;
  friend class llvm::TrailingObjects<OMPSizesClause, Expr *>;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Number of tile sizes in the clause.
  unsigned NumSizes;

  /// Build an empty clause.
  explicit OMPSizesClause(int NumSizes)
      : OMPClause(llvm::omp::OMPC_sizes, SourceLocation(), SourceLocation()),
        NumSizes(NumSizes) {}

public:
  /// Build a 'sizes' AST node.
  ///
  /// \param C         Context of the AST.
  /// \param StartLoc  Location of the 'sizes' identifier.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc    Location of ')'.
  /// \param Sizes     Content of the clause.
  static OMPSizesClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                SourceLocation LParenLoc, SourceLocation EndLoc,
                                ArrayRef<Expr *> Sizes);

  /// Build an empty 'sizes' AST node for deserialization.
  ///
  /// \param C     Context of the AST.
  /// \param NumSizes Number of items in the clause.
  static OMPSizesClause *CreateEmpty(const ASTContext &C, unsigned NumSizes);

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns the number of list items.
  unsigned getNumSizes() const { return NumSizes; }

  /// Returns the tile size expressions.
  MutableArrayRef<Expr *> getSizesRefs() {
    return MutableArrayRef<Expr *>(static_cast<OMPSizesClause *>(this)
                                       ->template getTrailingObjects<Expr *>(),
                                   NumSizes);
  }
  ArrayRef<Expr *> getSizesRefs() const {
    return ArrayRef<Expr *>(static_cast<const OMPSizesClause *>(this)
                                ->template getTrailingObjects<Expr *>(),
                            NumSizes);
  }

  /// Sets the tile size expressions.
  void setSizesRefs(ArrayRef<Expr *> VL) {
    assert(VL.size() == NumSizes);
    std::copy(VL.begin(), VL.end(),
              static_cast<OMPSizesClause *>(this)
                  ->template getTrailingObjects<Expr *>());
  }

  child_range children() {
    MutableArrayRef<Expr *> Sizes = getSizesRefs();
    return child_range(reinterpret_cast<Stmt **>(Sizes.begin()),
                       reinterpret_cast<Stmt **>(Sizes.end()));
  }
  const_child_range children() const {
    ArrayRef<Expr *> Sizes = getSizesRefs();
    return const_child_range(reinterpret_cast<Stmt *const *>(Sizes.begin()),
                             reinterpret_cast<Stmt *const *>(Sizes.end()));
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_sizes;
  }
};

/// Representation of the 'full' clause of the '#pragma omp unroll' directive.
///
/// \code
/// #pragma omp unroll full
/// for (int i = 0; i < 64; ++i)
/// \endcode
class OMPFullClause final : public OMPNoChildClause<llvm::omp::OMPC_full> {
  friend class OMPClauseReader;

  /// Build an empty clause.
  explicit OMPFullClause() : OMPNoChildClause() {}

public:
  /// Build an AST node for a 'full' clause.
  ///
  /// \param C        Context of the AST.
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc   Ending location of the clause.
  static OMPFullClause *Create(const ASTContext &C, SourceLocation StartLoc,
                               SourceLocation EndLoc);

  /// Build an empty 'full' AST node for deserialization.
  ///
  /// \param C Context of the AST.
  static OMPFullClause *CreateEmpty(const ASTContext &C);
};

/// Representation of the 'partial' clause of the '#pragma omp unroll'
/// directive.
///
/// \code
/// #pragma omp unroll partial(4)
/// for (int i = start; i < end; ++i)
/// \endcode
class OMPPartialClause final : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Optional argument to the clause (unroll factor).
  Stmt *Factor;

  /// Build an empty clause.
  explicit OMPPartialClause() : OMPClause(llvm::omp::OMPC_partial, {}, {}) {}

  /// Set the unroll factor.
  void setFactor(Expr *E) { Factor = E; }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

public:
  /// Build an AST node for a 'partial' clause.
  ///
  /// \param C         Context of the AST.
  /// \param StartLoc  Location of the 'partial' identifier.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc    Location of ')'.
  /// \param Factor    Clause argument.
  static OMPPartialClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation LParenLoc,
                                  SourceLocation EndLoc, Expr *Factor);

  /// Build an empty 'partial' AST node for deserialization.
  ///
  /// \param C     Context of the AST.
  static OMPPartialClause *CreateEmpty(const ASTContext &C);

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns the argument of the clause or nullptr if not set.
  Expr *getFactor() const { return cast_or_null<Expr>(Factor); }

  child_range children() { return child_range(&Factor, &Factor + 1); }
  const_child_range children() const {
    return const_child_range(&Factor, &Factor + 1);
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_partial;
  }
};

/// This represents 'collapse' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp simd collapse(3)
/// \endcode
/// In this example directive '#pragma omp simd' has clause 'collapse'
/// with single expression '3'.
/// The parameter must be a constant positive integer expression, it specifies
/// the number of nested loops that should be collapsed into a single iteration
/// space.
class OMPCollapseClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_collapse, OMPClause> {
  friend class OMPClauseReader;

  /// Set the number of associated for-loops.
  void setNumForLoops(Expr *Num) { setStmt(Num); }

public:
  /// Build 'collapse' clause.
  ///
  /// \param Num Expression associated with this clause.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPCollapseClause(Expr *Num, SourceLocation StartLoc,
                    SourceLocation LParenLoc, SourceLocation EndLoc)
      : OMPOneStmtClause(Num, StartLoc, LParenLoc, EndLoc) {}

  /// Build an empty clause.
  explicit OMPCollapseClause() : OMPOneStmtClause() {}

  /// Return the number of associated for-loops.
  Expr *getNumForLoops() const { return getStmtAs<Expr>(); }
};

/// This represents 'default' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp parallel default(shared)
/// \endcode
/// In this example directive '#pragma omp parallel' has simple 'default'
/// clause with kind 'shared'.
class OMPDefaultClause : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// A kind of the 'default' clause.
  llvm::omp::DefaultKind Kind = llvm::omp::OMP_DEFAULT_unknown;

  /// Start location of the kind in source code.
  SourceLocation KindKwLoc;

  /// Set kind of the clauses.
  ///
  /// \param K Argument of clause.
  void setDefaultKind(llvm::omp::DefaultKind K) { Kind = K; }

  /// Set argument location.
  ///
  /// \param KLoc Argument location.
  void setDefaultKindKwLoc(SourceLocation KLoc) { KindKwLoc = KLoc; }

public:
  /// Build 'default' clause with argument \a A ('none' or 'shared').
  ///
  /// \param A Argument of the clause ('none' or 'shared').
  /// \param ALoc Starting location of the argument.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPDefaultClause(llvm::omp::DefaultKind A, SourceLocation ALoc,
                   SourceLocation StartLoc, SourceLocation LParenLoc,
                   SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_default, StartLoc, EndLoc),
        LParenLoc(LParenLoc), Kind(A), KindKwLoc(ALoc) {}

  /// Build an empty clause.
  OMPDefaultClause()
      : OMPClause(llvm::omp::OMPC_default, SourceLocation(), SourceLocation()) {
  }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns kind of the clause.
  llvm::omp::DefaultKind getDefaultKind() const { return Kind; }

  /// Returns location of clause kind.
  SourceLocation getDefaultKindKwLoc() const { return KindKwLoc; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_default;
  }
};

/// This represents 'proc_bind' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp parallel proc_bind(master)
/// \endcode
/// In this example directive '#pragma omp parallel' has simple 'proc_bind'
/// clause with kind 'master'.
class OMPProcBindClause : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// A kind of the 'proc_bind' clause.
  llvm::omp::ProcBindKind Kind = llvm::omp::OMP_PROC_BIND_unknown;

  /// Start location of the kind in source code.
  SourceLocation KindKwLoc;

  /// Set kind of the clause.
  ///
  /// \param K Kind of clause.
  void setProcBindKind(llvm::omp::ProcBindKind K) { Kind = K; }

  /// Set clause kind location.
  ///
  /// \param KLoc Kind location.
  void setProcBindKindKwLoc(SourceLocation KLoc) { KindKwLoc = KLoc; }

public:
  /// Build 'proc_bind' clause with argument \a A ('master', 'close' or
  ///        'spread').
  ///
  /// \param A Argument of the clause ('master', 'close' or 'spread').
  /// \param ALoc Starting location of the argument.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPProcBindClause(llvm::omp::ProcBindKind A, SourceLocation ALoc,
                    SourceLocation StartLoc, SourceLocation LParenLoc,
                    SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_proc_bind, StartLoc, EndLoc),
        LParenLoc(LParenLoc), Kind(A), KindKwLoc(ALoc) {}

  /// Build an empty clause.
  OMPProcBindClause()
      : OMPClause(llvm::omp::OMPC_proc_bind, SourceLocation(),
                  SourceLocation()) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns kind of the clause.
  llvm::omp::ProcBindKind getProcBindKind() const { return Kind; }

  /// Returns location of clause kind.
  SourceLocation getProcBindKindKwLoc() const { return KindKwLoc; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_proc_bind;
  }
};

/// This represents 'unified_address' clause in the '#pragma omp requires'
/// directive.
///
/// \code
/// #pragma omp requires unified_address
/// \endcode
/// In this example directive '#pragma omp requires' has 'unified_address'
/// clause.
class OMPUnifiedAddressClause final
    : public OMPNoChildClause<llvm::omp::OMPC_unified_address> {
public:
  friend class OMPClauseReader;
  /// Build 'unified_address' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPUnifiedAddressClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPNoChildClause(StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPUnifiedAddressClause() : OMPNoChildClause() {}
};

/// This represents 'unified_shared_memory' clause in the '#pragma omp requires'
/// directive.
///
/// \code
/// #pragma omp requires unified_shared_memory
/// \endcode
/// In this example directive '#pragma omp requires' has 'unified_shared_memory'
/// clause.
class OMPUnifiedSharedMemoryClause final : public OMPClause {
public:
  friend class OMPClauseReader;
  /// Build 'unified_shared_memory' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPUnifiedSharedMemoryClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_unified_shared_memory, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPUnifiedSharedMemoryClause()
      : OMPClause(llvm::omp::OMPC_unified_shared_memory, SourceLocation(),
                  SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_unified_shared_memory;
  }
};

/// This represents 'reverse_offload' clause in the '#pragma omp requires'
/// directive.
///
/// \code
/// #pragma omp requires reverse_offload
/// \endcode
/// In this example directive '#pragma omp requires' has 'reverse_offload'
/// clause.
class OMPReverseOffloadClause final : public OMPClause {
public:
  friend class OMPClauseReader;
  /// Build 'reverse_offload' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPReverseOffloadClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_reverse_offload, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPReverseOffloadClause()
      : OMPClause(llvm::omp::OMPC_reverse_offload, SourceLocation(),
                  SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_reverse_offload;
  }
};

/// This represents 'dynamic_allocators' clause in the '#pragma omp requires'
/// directive.
///
/// \code
/// #pragma omp requires dynamic_allocators
/// \endcode
/// In this example directive '#pragma omp requires' has 'dynamic_allocators'
/// clause.
class OMPDynamicAllocatorsClause final : public OMPClause {
public:
  friend class OMPClauseReader;
  /// Build 'dynamic_allocators' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPDynamicAllocatorsClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_dynamic_allocators, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPDynamicAllocatorsClause()
      : OMPClause(llvm::omp::OMPC_dynamic_allocators, SourceLocation(),
                  SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_dynamic_allocators;
  }
};

/// This represents 'atomic_default_mem_order' clause in the '#pragma omp
/// requires'  directive.
///
/// \code
/// #pragma omp requires atomic_default_mem_order(seq_cst)
/// \endcode
/// In this example directive '#pragma omp requires' has simple
/// atomic_default_mem_order' clause with kind 'seq_cst'.
class OMPAtomicDefaultMemOrderClause final : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('
  SourceLocation LParenLoc;

  /// A kind of the 'atomic_default_mem_order' clause.
  OpenMPAtomicDefaultMemOrderClauseKind Kind =
      OMPC_ATOMIC_DEFAULT_MEM_ORDER_unknown;

  /// Start location of the kind in source code.
  SourceLocation KindKwLoc;

  /// Set kind of the clause.
  ///
  /// \param K Kind of clause.
  void setAtomicDefaultMemOrderKind(OpenMPAtomicDefaultMemOrderClauseKind K) {
    Kind = K;
  }

  /// Set clause kind location.
  ///
  /// \param KLoc Kind location.
  void setAtomicDefaultMemOrderKindKwLoc(SourceLocation KLoc) {
    KindKwLoc = KLoc;
  }

public:
  /// Build 'atomic_default_mem_order' clause with argument \a A ('seq_cst',
  /// 'acq_rel' or 'relaxed').
  ///
  /// \param A Argument of the clause ('seq_cst', 'acq_rel' or 'relaxed').
  /// \param ALoc Starting location of the argument.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPAtomicDefaultMemOrderClause(OpenMPAtomicDefaultMemOrderClauseKind A,
                                 SourceLocation ALoc, SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_atomic_default_mem_order, StartLoc, EndLoc),
        LParenLoc(LParenLoc), Kind(A), KindKwLoc(ALoc) {}

  /// Build an empty clause.
  OMPAtomicDefaultMemOrderClause()
      : OMPClause(llvm::omp::OMPC_atomic_default_mem_order, SourceLocation(),
                  SourceLocation()) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the locaiton of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns kind of the clause.
  OpenMPAtomicDefaultMemOrderClauseKind getAtomicDefaultMemOrderKind() const {
    return Kind;
  }

  /// Returns location of clause kind.
  SourceLocation getAtomicDefaultMemOrderKindKwLoc() const { return KindKwLoc; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_atomic_default_mem_order;
  }
};

/// This represents 'at' clause in the '#pragma omp error' directive
///
/// \code
/// #pragma omp error at(compilation)
/// \endcode
/// In this example directive '#pragma omp error' has simple
/// 'at' clause with kind 'complilation'.
class OMPAtClause final : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('
  SourceLocation LParenLoc;

  /// A kind of the 'at' clause.
  OpenMPAtClauseKind Kind = OMPC_AT_unknown;

  /// Start location of the kind in source code.
  SourceLocation KindKwLoc;

  /// Set kind of the clause.
  ///
  /// \param K Kind of clause.
  void setAtKind(OpenMPAtClauseKind K) { Kind = K; }

  /// Set clause kind location.
  ///
  /// \param KLoc Kind location.
  void setAtKindKwLoc(SourceLocation KLoc) { KindKwLoc = KLoc; }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

public:
  /// Build 'at' clause with argument \a A ('compilation' or 'execution').
  ///
  /// \param A Argument of the clause ('compilation' or 'execution').
  /// \param ALoc Starting location of the argument.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPAtClause(OpenMPAtClauseKind A, SourceLocation ALoc,
              SourceLocation StartLoc, SourceLocation LParenLoc,
              SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_at, StartLoc, EndLoc), LParenLoc(LParenLoc),
        Kind(A), KindKwLoc(ALoc) {}

  /// Build an empty clause.
  OMPAtClause()
      : OMPClause(llvm::omp::OMPC_at, SourceLocation(), SourceLocation()) {}

  /// Returns the locaiton of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns kind of the clause.
  OpenMPAtClauseKind getAtKind() const { return Kind; }

  /// Returns location of clause kind.
  SourceLocation getAtKindKwLoc() const { return KindKwLoc; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_at;
  }
};

/// This represents 'severity' clause in the '#pragma omp error' directive
///
/// \code
/// #pragma omp error severity(fatal)
/// \endcode
/// In this example directive '#pragma omp error' has simple
/// 'severity' clause with kind 'fatal'.
class OMPSeverityClause final : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('
  SourceLocation LParenLoc;

  /// A kind of the 'severity' clause.
  OpenMPSeverityClauseKind Kind = OMPC_SEVERITY_unknown;

  /// Start location of the kind in source code.
  SourceLocation KindKwLoc;

  /// Set kind of the clause.
  ///
  /// \param K Kind of clause.
  void setSeverityKind(OpenMPSeverityClauseKind K) { Kind = K; }

  /// Set clause kind location.
  ///
  /// \param KLoc Kind location.
  void setSeverityKindKwLoc(SourceLocation KLoc) { KindKwLoc = KLoc; }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

public:
  /// Build 'severity' clause with argument \a A ('fatal' or 'warning').
  ///
  /// \param A Argument of the clause ('fatal' or 'warning').
  /// \param ALoc Starting location of the argument.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPSeverityClause(OpenMPSeverityClauseKind A, SourceLocation ALoc,
                    SourceLocation StartLoc, SourceLocation LParenLoc,
                    SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_severity, StartLoc, EndLoc),
        LParenLoc(LParenLoc), Kind(A), KindKwLoc(ALoc) {}

  /// Build an empty clause.
  OMPSeverityClause()
      : OMPClause(llvm::omp::OMPC_severity, SourceLocation(),
                  SourceLocation()) {}

  /// Returns the locaiton of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns kind of the clause.
  OpenMPSeverityClauseKind getSeverityKind() const { return Kind; }

  /// Returns location of clause kind.
  SourceLocation getSeverityKindKwLoc() const { return KindKwLoc; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_severity;
  }
};

/// This represents 'message' clause in the '#pragma omp error' directive
///
/// \code
/// #pragma omp error message("GNU compiler required.")
/// \endcode
/// In this example directive '#pragma omp error' has simple
/// 'message' clause with user error message of "GNU compiler required.".
class OMPMessageClause final : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('
  SourceLocation LParenLoc;

  // Expression of the 'message' clause.
  Stmt *MessageString = nullptr;

  /// Set message string of the clause.
  void setMessageString(Expr *MS) { MessageString = MS; }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

public:
  /// Build 'message' clause with message string argument
  ///
  /// \param MS Argument of the clause (message string).
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPMessageClause(Expr *MS, SourceLocation StartLoc, SourceLocation LParenLoc,
                   SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_message, StartLoc, EndLoc),
        LParenLoc(LParenLoc), MessageString(MS) {}

  /// Build an empty clause.
  OMPMessageClause()
      : OMPClause(llvm::omp::OMPC_message, SourceLocation(), SourceLocation()) {
  }

  /// Returns the locaiton of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns message string of the clause.
  Expr *getMessageString() const { return cast_or_null<Expr>(MessageString); }

  child_range children() {
    return child_range(&MessageString, &MessageString + 1);
  }

  const_child_range children() const {
    return const_child_range(&MessageString, &MessageString + 1);
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_message;
  }
};

/// This represents 'schedule' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp for schedule(static, 3)
/// \endcode
/// In this example directive '#pragma omp for' has 'schedule' clause with
/// arguments 'static' and '3'.
class OMPScheduleClause : public OMPClause, public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// A kind of the 'schedule' clause.
  OpenMPScheduleClauseKind Kind = OMPC_SCHEDULE_unknown;

  /// Modifiers for 'schedule' clause.
  enum {FIRST, SECOND, NUM_MODIFIERS};
  OpenMPScheduleClauseModifier Modifiers[NUM_MODIFIERS];

  /// Locations of modifiers.
  SourceLocation ModifiersLoc[NUM_MODIFIERS];

  /// Start location of the schedule ind in source code.
  SourceLocation KindLoc;

  /// Location of ',' (if any).
  SourceLocation CommaLoc;

  /// Chunk size.
  Expr *ChunkSize = nullptr;

  /// Set schedule kind.
  ///
  /// \param K Schedule kind.
  void setScheduleKind(OpenMPScheduleClauseKind K) { Kind = K; }

  /// Set the first schedule modifier.
  ///
  /// \param M Schedule modifier.
  void setFirstScheduleModifier(OpenMPScheduleClauseModifier M) {
    Modifiers[FIRST] = M;
  }

  /// Set the second schedule modifier.
  ///
  /// \param M Schedule modifier.
  void setSecondScheduleModifier(OpenMPScheduleClauseModifier M) {
    Modifiers[SECOND] = M;
  }

  /// Set location of the first schedule modifier.
  void setFirstScheduleModifierLoc(SourceLocation Loc) {
    ModifiersLoc[FIRST] = Loc;
  }

  /// Set location of the second schedule modifier.
  void setSecondScheduleModifierLoc(SourceLocation Loc) {
    ModifiersLoc[SECOND] = Loc;
  }

  /// Set schedule modifier location.
  ///
  /// \param M Schedule modifier location.
  void setScheduleModifer(OpenMPScheduleClauseModifier M) {
    if (Modifiers[FIRST] == OMPC_SCHEDULE_MODIFIER_unknown)
      Modifiers[FIRST] = M;
    else {
      assert(Modifiers[SECOND] == OMPC_SCHEDULE_MODIFIER_unknown);
      Modifiers[SECOND] = M;
    }
  }

  /// Sets the location of '('.
  ///
  /// \param Loc Location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Set schedule kind start location.
  ///
  /// \param KLoc Schedule kind location.
  void setScheduleKindLoc(SourceLocation KLoc) { KindLoc = KLoc; }

  /// Set location of ','.
  ///
  /// \param Loc Location of ','.
  void setCommaLoc(SourceLocation Loc) { CommaLoc = Loc; }

  /// Set chunk size.
  ///
  /// \param E Chunk size.
  void setChunkSize(Expr *E) { ChunkSize = E; }

public:
  /// Build 'schedule' clause with schedule kind \a Kind and chunk size
  /// expression \a ChunkSize.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param KLoc Starting location of the argument.
  /// \param CommaLoc Location of ','.
  /// \param EndLoc Ending location of the clause.
  /// \param Kind Schedule kind.
  /// \param ChunkSize Chunk size.
  /// \param HelperChunkSize Helper chunk size for combined directives.
  /// \param M1 The first modifier applied to 'schedule' clause.
  /// \param M1Loc Location of the first modifier
  /// \param M2 The second modifier applied to 'schedule' clause.
  /// \param M2Loc Location of the second modifier
  OMPScheduleClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                    SourceLocation KLoc, SourceLocation CommaLoc,
                    SourceLocation EndLoc, OpenMPScheduleClauseKind Kind,
                    Expr *ChunkSize, Stmt *HelperChunkSize,
                    OpenMPScheduleClauseModifier M1, SourceLocation M1Loc,
                    OpenMPScheduleClauseModifier M2, SourceLocation M2Loc)
      : OMPClause(llvm::omp::OMPC_schedule, StartLoc, EndLoc),
        OMPClauseWithPreInit(this), LParenLoc(LParenLoc), Kind(Kind),
        KindLoc(KLoc), CommaLoc(CommaLoc), ChunkSize(ChunkSize) {
    setPreInitStmt(HelperChunkSize);
    Modifiers[FIRST] = M1;
    Modifiers[SECOND] = M2;
    ModifiersLoc[FIRST] = M1Loc;
    ModifiersLoc[SECOND] = M2Loc;
  }

  /// Build an empty clause.
  explicit OMPScheduleClause()
      : OMPClause(llvm::omp::OMPC_schedule, SourceLocation(), SourceLocation()),
        OMPClauseWithPreInit(this) {
    Modifiers[FIRST] = OMPC_SCHEDULE_MODIFIER_unknown;
    Modifiers[SECOND] = OMPC_SCHEDULE_MODIFIER_unknown;
  }

  /// Get kind of the clause.
  OpenMPScheduleClauseKind getScheduleKind() const { return Kind; }

  /// Get the first modifier of the clause.
  OpenMPScheduleClauseModifier getFirstScheduleModifier() const {
    return Modifiers[FIRST];
  }

  /// Get the second modifier of the clause.
  OpenMPScheduleClauseModifier getSecondScheduleModifier() const {
    return Modifiers[SECOND];
  }

  /// Get location of '('.
  SourceLocation getLParenLoc() { return LParenLoc; }

  /// Get kind location.
  SourceLocation getScheduleKindLoc() { return KindLoc; }

  /// Get the first modifier location.
  SourceLocation getFirstScheduleModifierLoc() const {
    return ModifiersLoc[FIRST];
  }

  /// Get the second modifier location.
  SourceLocation getSecondScheduleModifierLoc() const {
    return ModifiersLoc[SECOND];
  }

  /// Get location of ','.
  SourceLocation getCommaLoc() { return CommaLoc; }

  /// Get chunk size.
  Expr *getChunkSize() { return ChunkSize; }

  /// Get chunk size.
  const Expr *getChunkSize() const { return ChunkSize; }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(&ChunkSize),
                       reinterpret_cast<Stmt **>(&ChunkSize) + 1);
  }

  const_child_range children() const {
    auto Children = const_cast<OMPScheduleClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_schedule;
  }
};

/// This represents 'ordered' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp for ordered (2)
/// \endcode
/// In this example directive '#pragma omp for' has 'ordered' clause with
/// parameter 2.
class OMPOrderedClause final
    : public OMPClause,
      private llvm::TrailingObjects<OMPOrderedClause, Expr *> {
  friend class OMPClauseReader;
  friend TrailingObjects;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Number of for-loops.
  Stmt *NumForLoops = nullptr;

  /// Real number of loops.
  unsigned NumberOfLoops = 0;

  /// Build 'ordered' clause.
  ///
  /// \param Num Expression, possibly associated with this clause.
  /// \param NumLoops Number of loops, associated with this clause.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPOrderedClause(Expr *Num, unsigned NumLoops, SourceLocation StartLoc,
                   SourceLocation LParenLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_ordered, StartLoc, EndLoc),
        LParenLoc(LParenLoc), NumForLoops(Num), NumberOfLoops(NumLoops) {}

  /// Build an empty clause.
  explicit OMPOrderedClause(unsigned NumLoops)
      : OMPClause(llvm::omp::OMPC_ordered, SourceLocation(), SourceLocation()),
        NumberOfLoops(NumLoops) {}

  /// Set the number of associated for-loops.
  void setNumForLoops(Expr *Num) { NumForLoops = Num; }

public:
  /// Build 'ordered' clause.
  ///
  /// \param Num Expression, possibly associated with this clause.
  /// \param NumLoops Number of loops, associated with this clause.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  static OMPOrderedClause *Create(const ASTContext &C, Expr *Num,
                                  unsigned NumLoops, SourceLocation StartLoc,
                                  SourceLocation LParenLoc,
                                  SourceLocation EndLoc);

  /// Build an empty clause.
  static OMPOrderedClause* CreateEmpty(const ASTContext &C, unsigned NumLoops);

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Return the number of associated for-loops.
  Expr *getNumForLoops() const { return cast_or_null<Expr>(NumForLoops); }

  /// Set number of iterations for the specified loop.
  void setLoopNumIterations(unsigned NumLoop, Expr *NumIterations);
  /// Get number of iterations for all the loops.
  ArrayRef<Expr *> getLoopNumIterations() const;

  /// Set loop counter for the specified loop.
  void setLoopCounter(unsigned NumLoop, Expr *Counter);
  /// Get loops counter for the specified loop.
  Expr *getLoopCounter(unsigned NumLoop);
  const Expr *getLoopCounter(unsigned NumLoop) const;

  child_range children() { return child_range(&NumForLoops, &NumForLoops + 1); }

  const_child_range children() const {
    return const_child_range(&NumForLoops, &NumForLoops + 1);
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_ordered;
  }
};

/// This represents 'nowait' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp for nowait
/// \endcode
/// In this example directive '#pragma omp for' has 'nowait' clause.
class OMPNowaitClause final : public OMPNoChildClause<llvm::omp::OMPC_nowait> {
public:
  /// Build 'nowait' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPNowaitClause(SourceLocation StartLoc = SourceLocation(),
                  SourceLocation EndLoc = SourceLocation())
      : OMPNoChildClause(StartLoc, EndLoc) {}
};

/// This represents 'untied' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp task untied
/// \endcode
/// In this example directive '#pragma omp task' has 'untied' clause.
class OMPUntiedClause : public OMPClause {
public:
  /// Build 'untied' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPUntiedClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_untied, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPUntiedClause()
      : OMPClause(llvm::omp::OMPC_untied, SourceLocation(), SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_untied;
  }
};

/// This represents 'mergeable' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp task mergeable
/// \endcode
/// In this example directive '#pragma omp task' has 'mergeable' clause.
class OMPMergeableClause : public OMPClause {
public:
  /// Build 'mergeable' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPMergeableClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_mergeable, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPMergeableClause()
      : OMPClause(llvm::omp::OMPC_mergeable, SourceLocation(),
                  SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_mergeable;
  }
};

/// This represents 'read' clause in the '#pragma omp atomic' directive.
///
/// \code
/// #pragma omp atomic read
/// \endcode
/// In this example directive '#pragma omp atomic' has 'read' clause.
class OMPReadClause : public OMPClause {
public:
  /// Build 'read' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPReadClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_read, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPReadClause()
      : OMPClause(llvm::omp::OMPC_read, SourceLocation(), SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_read;
  }
};

/// This represents 'write' clause in the '#pragma omp atomic' directive.
///
/// \code
/// #pragma omp atomic write
/// \endcode
/// In this example directive '#pragma omp atomic' has 'write' clause.
class OMPWriteClause : public OMPClause {
public:
  /// Build 'write' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPWriteClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_write, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPWriteClause()
      : OMPClause(llvm::omp::OMPC_write, SourceLocation(), SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_write;
  }
};

/// This represents 'update' clause in the '#pragma omp atomic'
/// directive.
///
/// \code
/// #pragma omp atomic update
/// \endcode
/// In this example directive '#pragma omp atomic' has 'update' clause.
/// Also, this class represents 'update' clause in  '#pragma omp depobj'
/// directive.
///
/// \code
/// #pragma omp depobj(a) update(in)
/// \endcode
/// In this example directive '#pragma omp depobj' has 'update' clause with 'in'
/// dependence kind.
class OMPUpdateClause final
    : public OMPClause,
      private llvm::TrailingObjects<OMPUpdateClause, SourceLocation,
                                    OpenMPDependClauseKind> {
  friend class OMPClauseReader;
  friend TrailingObjects;

  /// true if extended version of the clause for 'depobj' directive.
  bool IsExtended = false;

  /// Define the sizes of each trailing object array except the last one. This
  /// is required for TrailingObjects to work properly.
  size_t numTrailingObjects(OverloadToken<SourceLocation>) const {
    // 2 locations: for '(' and argument location.
    return IsExtended ? 2 : 0;
  }

  /// Sets the location of '(' in clause for 'depobj' directive.
  void setLParenLoc(SourceLocation Loc) {
    assert(IsExtended && "Expected extended clause.");
    *getTrailingObjects<SourceLocation>() = Loc;
  }

  /// Sets the location of '(' in clause for 'depobj' directive.
  void setArgumentLoc(SourceLocation Loc) {
    assert(IsExtended && "Expected extended clause.");
    *std::next(getTrailingObjects<SourceLocation>(), 1) = Loc;
  }

  /// Sets the dependence kind for the clause for 'depobj' directive.
  void setDependencyKind(OpenMPDependClauseKind DK) {
    assert(IsExtended && "Expected extended clause.");
    *getTrailingObjects<OpenMPDependClauseKind>() = DK;
  }

  /// Build 'update' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPUpdateClause(SourceLocation StartLoc, SourceLocation EndLoc,
                  bool IsExtended)
      : OMPClause(llvm::omp::OMPC_update, StartLoc, EndLoc),
        IsExtended(IsExtended) {}

  /// Build an empty clause.
  OMPUpdateClause(bool IsExtended)
      : OMPClause(llvm::omp::OMPC_update, SourceLocation(), SourceLocation()),
        IsExtended(IsExtended) {}

public:
  /// Creates clause for 'atomic' directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  static OMPUpdateClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                 SourceLocation EndLoc);

  /// Creates clause for 'depobj' directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ArgumentLoc Location of the argument.
  /// \param DK Dependence kind.
  /// \param EndLoc Ending location of the clause.
  static OMPUpdateClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation ArgumentLoc,
                                 OpenMPDependClauseKind DK,
                                 SourceLocation EndLoc);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param IsExtended true if extended clause for 'depobj' directive must be
  /// created.
  static OMPUpdateClause *CreateEmpty(const ASTContext &C, bool IsExtended);

  /// Checks if the clause is the extended clauses for 'depobj' directive.
  bool isExtended() const { return IsExtended; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  /// Gets the location of '(' in clause for 'depobj' directive.
  SourceLocation getLParenLoc() const {
    assert(IsExtended && "Expected extended clause.");
    return *getTrailingObjects<SourceLocation>();
  }

  /// Gets the location of argument in clause for 'depobj' directive.
  SourceLocation getArgumentLoc() const {
    assert(IsExtended && "Expected extended clause.");
    return *std::next(getTrailingObjects<SourceLocation>(), 1);
  }

  /// Gets the dependence kind in clause for 'depobj' directive.
  OpenMPDependClauseKind getDependencyKind() const {
    assert(IsExtended && "Expected extended clause.");
    return *getTrailingObjects<OpenMPDependClauseKind>();
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_update;
  }
};

/// This represents 'capture' clause in the '#pragma omp atomic'
/// directive.
///
/// \code
/// #pragma omp atomic capture
/// \endcode
/// In this example directive '#pragma omp atomic' has 'capture' clause.
class OMPCaptureClause : public OMPClause {
public:
  /// Build 'capture' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPCaptureClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_capture, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPCaptureClause()
      : OMPClause(llvm::omp::OMPC_capture, SourceLocation(), SourceLocation()) {
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_capture;
  }
};

/// This represents 'compare' clause in the '#pragma omp atomic'
/// directive.
///
/// \code
/// #pragma omp atomic compare
/// \endcode
/// In this example directive '#pragma omp atomic' has 'compare' clause.
class OMPCompareClause final : public OMPClause {
public:
  /// Build 'compare' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPCompareClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_compare, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPCompareClause()
      : OMPClause(llvm::omp::OMPC_compare, SourceLocation(), SourceLocation()) {
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_compare;
  }
};

/// This represents 'seq_cst' clause in the '#pragma omp atomic'
/// directive.
///
/// \code
/// #pragma omp atomic seq_cst
/// \endcode
/// In this example directive '#pragma omp atomic' has 'seq_cst' clause.
class OMPSeqCstClause : public OMPClause {
public:
  /// Build 'seq_cst' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPSeqCstClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_seq_cst, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPSeqCstClause()
      : OMPClause(llvm::omp::OMPC_seq_cst, SourceLocation(), SourceLocation()) {
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_seq_cst;
  }
};

/// This represents 'acq_rel' clause in the '#pragma omp atomic|flush'
/// directives.
///
/// \code
/// #pragma omp flush acq_rel
/// \endcode
/// In this example directive '#pragma omp flush' has 'acq_rel' clause.
class OMPAcqRelClause final : public OMPClause {
public:
  /// Build 'ack_rel' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPAcqRelClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_acq_rel, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPAcqRelClause()
      : OMPClause(llvm::omp::OMPC_acq_rel, SourceLocation(), SourceLocation()) {
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_acq_rel;
  }
};

/// This represents 'acquire' clause in the '#pragma omp atomic|flush'
/// directives.
///
/// \code
/// #pragma omp flush acquire
/// \endcode
/// In this example directive '#pragma omp flush' has 'acquire' clause.
class OMPAcquireClause final : public OMPClause {
public:
  /// Build 'acquire' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPAcquireClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_acquire, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPAcquireClause()
      : OMPClause(llvm::omp::OMPC_acquire, SourceLocation(), SourceLocation()) {
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_acquire;
  }
};

/// This represents 'release' clause in the '#pragma omp atomic|flush'
/// directives.
///
/// \code
/// #pragma omp flush release
/// \endcode
/// In this example directive '#pragma omp flush' has 'release' clause.
class OMPReleaseClause final : public OMPClause {
public:
  /// Build 'release' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPReleaseClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_release, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPReleaseClause()
      : OMPClause(llvm::omp::OMPC_release, SourceLocation(), SourceLocation()) {
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_release;
  }
};

/// This represents 'relaxed' clause in the '#pragma omp atomic'
/// directives.
///
/// \code
/// #pragma omp atomic relaxed
/// \endcode
/// In this example directive '#pragma omp atomic' has 'relaxed' clause.
class OMPRelaxedClause final : public OMPClause {
public:
  /// Build 'relaxed' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPRelaxedClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_relaxed, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPRelaxedClause()
      : OMPClause(llvm::omp::OMPC_relaxed, SourceLocation(), SourceLocation()) {
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_relaxed;
  }
};

/// This represents 'weak' clause in the '#pragma omp atomic'
/// directives.
///
/// \code
/// #pragma omp atomic compare weak
/// \endcode
/// In this example directive '#pragma omp atomic' has 'weak' clause.
class OMPWeakClause final : public OMPClause {
public:
  /// Build 'weak' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPWeakClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_weak, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPWeakClause()
      : OMPClause(llvm::omp::OMPC_weak, SourceLocation(), SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_weak;
  }
};

/// This represents 'fail' clause in the '#pragma omp atomic'
/// directive.
///
/// \code
/// #pragma omp atomic compare fail
/// \endcode
/// In this example directive '#pragma omp atomic compare' has 'fail' clause.
class OMPFailClause final : public OMPClause {

  // FailParameter is a memory-order-clause. Storing the ClauseKind is
  // sufficient for our purpose.
  OpenMPClauseKind FailParameter = llvm::omp::Clause::OMPC_unknown;
  SourceLocation FailParameterLoc;
  SourceLocation LParenLoc;

  friend class OMPClauseReader;

  /// Sets the location of '(' in fail clause.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Sets the location of memoryOrder clause argument in fail clause.
  void setFailParameterLoc(SourceLocation Loc) { FailParameterLoc = Loc; }

  /// Sets the mem_order clause for 'atomic compare fail' directive.
  void setFailParameter(OpenMPClauseKind FailParameter) {
    this->FailParameter = FailParameter;
    assert(checkFailClauseParameter(FailParameter) &&
           "Invalid fail clause parameter");
  }

public:
  /// Build 'fail' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPFailClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_fail, StartLoc, EndLoc) {}

  OMPFailClause(OpenMPClauseKind FailParameter, SourceLocation FailParameterLoc,
                SourceLocation StartLoc, SourceLocation LParenLoc,
                SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_fail, StartLoc, EndLoc),
        FailParameterLoc(FailParameterLoc), LParenLoc(LParenLoc) {

    setFailParameter(FailParameter);
  }

  /// Build an empty clause.
  OMPFailClause()
      : OMPClause(llvm::omp::OMPC_fail, SourceLocation(), SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_fail;
  }

  /// Gets the location of '(' (for the parameter) in fail clause.
  SourceLocation getLParenLoc() const {
    return LParenLoc;
  }

  /// Gets the location of Fail Parameter (type memory-order-clause) in
  /// fail clause.
  SourceLocation getFailParameterLoc() const { return FailParameterLoc; }

  /// Gets the parameter (type memory-order-clause) in Fail clause.
  OpenMPClauseKind getFailParameter() const { return FailParameter; }
};

/// This represents clause 'private' in the '#pragma omp ...' directives.
///
/// \code
/// #pragma omp parallel private(a,b)
/// \endcode
/// In this example directive '#pragma omp parallel' has clause 'private'
/// with the variables 'a' and 'b'.
class OMPPrivateClause final
    : public OMPVarListClause<OMPPrivateClause>,
      private llvm::TrailingObjects<OMPPrivateClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPPrivateClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                   SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPPrivateClause>(llvm::omp::OMPC_private, StartLoc,
                                           LParenLoc, EndLoc, N) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPPrivateClause(unsigned N)
      : OMPVarListClause<OMPPrivateClause>(llvm::omp::OMPC_private,
                                           SourceLocation(), SourceLocation(),
                                           SourceLocation(), N) {}

  /// Sets the list of references to private copies with initializers for
  /// new private variables.
  /// \param VL List of references.
  void setPrivateCopies(ArrayRef<Expr *> VL);

  /// Gets the list of references to private copies with initializers for
  /// new private variables.
  MutableArrayRef<Expr *> getPrivateCopies() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getPrivateCopies() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  /// \param PrivateVL List of references to private copies with initializers.
  static OMPPrivateClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation LParenLoc,
                                  SourceLocation EndLoc, ArrayRef<Expr *> VL,
                                  ArrayRef<Expr *> PrivateVL);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPPrivateClause *CreateEmpty(const ASTContext &C, unsigned N);

  using private_copies_iterator = MutableArrayRef<Expr *>::iterator;
  using private_copies_const_iterator = ArrayRef<const Expr *>::iterator;
  using private_copies_range = llvm::iterator_range<private_copies_iterator>;
  using private_copies_const_range =
      llvm::iterator_range<private_copies_const_iterator>;

  private_copies_range private_copies() {
    return private_copies_range(getPrivateCopies().begin(),
                                getPrivateCopies().end());
  }

  private_copies_const_range private_copies() const {
    return private_copies_const_range(getPrivateCopies().begin(),
                                      getPrivateCopies().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPPrivateClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_private;
  }
};

/// This represents clause 'firstprivate' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp parallel firstprivate(a,b)
/// \endcode
/// In this example directive '#pragma omp parallel' has clause 'firstprivate'
/// with the variables 'a' and 'b'.
class OMPFirstprivateClause final
    : public OMPVarListClause<OMPFirstprivateClause>,
      public OMPClauseWithPreInit,
      private llvm::TrailingObjects<OMPFirstprivateClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPFirstprivateClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                        SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPFirstprivateClause>(llvm::omp::OMPC_firstprivate,
                                                StartLoc, LParenLoc, EndLoc, N),
        OMPClauseWithPreInit(this) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPFirstprivateClause(unsigned N)
      : OMPVarListClause<OMPFirstprivateClause>(
            llvm::omp::OMPC_firstprivate, SourceLocation(), SourceLocation(),
            SourceLocation(), N),
        OMPClauseWithPreInit(this) {}

  /// Sets the list of references to private copies with initializers for
  /// new private variables.
  /// \param VL List of references.
  void setPrivateCopies(ArrayRef<Expr *> VL);

  /// Gets the list of references to private copies with initializers for
  /// new private variables.
  MutableArrayRef<Expr *> getPrivateCopies() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getPrivateCopies() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

  /// Sets the list of references to initializer variables for new
  /// private variables.
  /// \param VL List of references.
  void setInits(ArrayRef<Expr *> VL);

  /// Gets the list of references to initializer variables for new
  /// private variables.
  MutableArrayRef<Expr *> getInits() {
    return MutableArrayRef<Expr *>(getPrivateCopies().end(), varlist_size());
  }
  ArrayRef<const Expr *> getInits() const {
    return llvm::ArrayRef(getPrivateCopies().end(), varlist_size());
  }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the original variables.
  /// \param PrivateVL List of references to private copies with initializers.
  /// \param InitVL List of references to auto generated variables used for
  /// initialization of a single array element. Used if firstprivate variable is
  /// of array type.
  /// \param PreInit Statement that must be executed before entering the OpenMP
  /// region with this clause.
  static OMPFirstprivateClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation EndLoc, ArrayRef<Expr *> VL, ArrayRef<Expr *> PrivateVL,
         ArrayRef<Expr *> InitVL, Stmt *PreInit);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPFirstprivateClause *CreateEmpty(const ASTContext &C, unsigned N);

  using private_copies_iterator = MutableArrayRef<Expr *>::iterator;
  using private_copies_const_iterator = ArrayRef<const Expr *>::iterator;
  using private_copies_range = llvm::iterator_range<private_copies_iterator>;
  using private_copies_const_range =
      llvm::iterator_range<private_copies_const_iterator>;

  private_copies_range private_copies() {
    return private_copies_range(getPrivateCopies().begin(),
                                getPrivateCopies().end());
  }
  private_copies_const_range private_copies() const {
    return private_copies_const_range(getPrivateCopies().begin(),
                                      getPrivateCopies().end());
  }

  using inits_iterator = MutableArrayRef<Expr *>::iterator;
  using inits_const_iterator = ArrayRef<const Expr *>::iterator;
  using inits_range = llvm::iterator_range<inits_iterator>;
  using inits_const_range = llvm::iterator_range<inits_const_iterator>;

  inits_range inits() {
    return inits_range(getInits().begin(), getInits().end());
  }
  inits_const_range inits() const {
    return inits_const_range(getInits().begin(), getInits().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPFirstprivateClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }
  const_child_range used_children() const {
    auto Children = const_cast<OMPFirstprivateClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_firstprivate;
  }
};

/// This represents clause 'lastprivate' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp simd lastprivate(a,b)
/// \endcode
/// In this example directive '#pragma omp simd' has clause 'lastprivate'
/// with the variables 'a' and 'b'.
class OMPLastprivateClause final
    : public OMPVarListClause<OMPLastprivateClause>,
      public OMPClauseWithPostUpdate,
      private llvm::TrailingObjects<OMPLastprivateClause, Expr *> {
  // There are 4 additional tail-allocated arrays at the end of the class:
  // 1. Contains list of pseudo variables with the default initialization for
  // each non-firstprivate variables. Used in codegen for initialization of
  // lastprivate copies.
  // 2. List of helper expressions for proper generation of assignment operation
  // required for lastprivate clause. This list represents private variables
  // (for arrays, single array element).
  // 3. List of helper expressions for proper generation of assignment operation
  // required for lastprivate clause. This list represents original variables
  // (for arrays, single array element).
  // 4. List of helper expressions that represents assignment operation:
  // \code
  // DstExprs = SrcExprs;
  // \endcode
  // Required for proper codegen of final assignment performed by the
  // lastprivate clause.
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Optional lastprivate kind, e.g. 'conditional', if specified by user.
  OpenMPLastprivateModifier LPKind;
  /// Optional location of the lasptrivate kind, if specified by user.
  SourceLocation LPKindLoc;
  /// Optional colon location, if specified by user.
  SourceLocation ColonLoc;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPLastprivateClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                       SourceLocation EndLoc, OpenMPLastprivateModifier LPKind,
                       SourceLocation LPKindLoc, SourceLocation ColonLoc,
                       unsigned N)
      : OMPVarListClause<OMPLastprivateClause>(llvm::omp::OMPC_lastprivate,
                                               StartLoc, LParenLoc, EndLoc, N),
        OMPClauseWithPostUpdate(this), LPKind(LPKind), LPKindLoc(LPKindLoc),
        ColonLoc(ColonLoc) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPLastprivateClause(unsigned N)
      : OMPVarListClause<OMPLastprivateClause>(
            llvm::omp::OMPC_lastprivate, SourceLocation(), SourceLocation(),
            SourceLocation(), N),
        OMPClauseWithPostUpdate(this) {}

  /// Get the list of helper expressions for initialization of private
  /// copies for lastprivate variables.
  MutableArrayRef<Expr *> getPrivateCopies() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getPrivateCopies() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the
  /// clause. These expressions represent private variables (for arrays, single
  /// array element) in the final assignment statement performed by the
  /// lastprivate clause.
  void setSourceExprs(ArrayRef<Expr *> SrcExprs);

  /// Get the list of helper source expressions.
  MutableArrayRef<Expr *> getSourceExprs() {
    return MutableArrayRef<Expr *>(getPrivateCopies().end(), varlist_size());
  }
  ArrayRef<const Expr *> getSourceExprs() const {
    return llvm::ArrayRef(getPrivateCopies().end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the
  /// clause. These expressions represent original variables (for arrays, single
  /// array element) in the final assignment statement performed by the
  /// lastprivate clause.
  void setDestinationExprs(ArrayRef<Expr *> DstExprs);

  /// Get the list of helper destination expressions.
  MutableArrayRef<Expr *> getDestinationExprs() {
    return MutableArrayRef<Expr *>(getSourceExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getDestinationExprs() const {
    return llvm::ArrayRef(getSourceExprs().end(), varlist_size());
  }

  /// Set list of helper assignment expressions, required for proper
  /// codegen of the clause. These expressions are assignment expressions that
  /// assign private copy of the variable to original variable.
  void setAssignmentOps(ArrayRef<Expr *> AssignmentOps);

  /// Get the list of helper assignment expressions.
  MutableArrayRef<Expr *> getAssignmentOps() {
    return MutableArrayRef<Expr *>(getDestinationExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getAssignmentOps() const {
    return llvm::ArrayRef(getDestinationExprs().end(), varlist_size());
  }

  /// Sets lastprivate kind.
  void setKind(OpenMPLastprivateModifier Kind) { LPKind = Kind; }
  /// Sets location of the lastprivate kind.
  void setKindLoc(SourceLocation Loc) { LPKindLoc = Loc; }
  /// Sets colon symbol location.
  void setColonLoc(SourceLocation Loc) { ColonLoc = Loc; }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  /// \param SrcExprs List of helper expressions for proper generation of
  /// assignment operation required for lastprivate clause. This list represents
  /// private variables (for arrays, single array element).
  /// \param DstExprs List of helper expressions for proper generation of
  /// assignment operation required for lastprivate clause. This list represents
  /// original variables (for arrays, single array element).
  /// \param AssignmentOps List of helper expressions that represents assignment
  /// operation:
  /// \code
  /// DstExprs = SrcExprs;
  /// \endcode
  /// Required for proper codegen of final assignment performed by the
  /// lastprivate clause.
  /// \param LPKind Lastprivate kind, e.g. 'conditional'.
  /// \param LPKindLoc Location of the lastprivate kind.
  /// \param ColonLoc Location of the ':' symbol if lastprivate kind is used.
  /// \param PreInit Statement that must be executed before entering the OpenMP
  /// region with this clause.
  /// \param PostUpdate Expression that must be executed after exit from the
  /// OpenMP region with this clause.
  static OMPLastprivateClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation EndLoc, ArrayRef<Expr *> VL, ArrayRef<Expr *> SrcExprs,
         ArrayRef<Expr *> DstExprs, ArrayRef<Expr *> AssignmentOps,
         OpenMPLastprivateModifier LPKind, SourceLocation LPKindLoc,
         SourceLocation ColonLoc, Stmt *PreInit, Expr *PostUpdate);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPLastprivateClause *CreateEmpty(const ASTContext &C, unsigned N);

  /// Lastprivate kind.
  OpenMPLastprivateModifier getKind() const { return LPKind; }
  /// Returns the location of the lastprivate kind.
  SourceLocation getKindLoc() const { return LPKindLoc; }
  /// Returns the location of the ':' symbol, if any.
  SourceLocation getColonLoc() const { return ColonLoc; }

  using helper_expr_iterator = MutableArrayRef<Expr *>::iterator;
  using helper_expr_const_iterator = ArrayRef<const Expr *>::iterator;
  using helper_expr_range = llvm::iterator_range<helper_expr_iterator>;
  using helper_expr_const_range =
      llvm::iterator_range<helper_expr_const_iterator>;

  /// Set list of helper expressions, required for generation of private
  /// copies of original lastprivate variables.
  void setPrivateCopies(ArrayRef<Expr *> PrivateCopies);

  helper_expr_const_range private_copies() const {
    return helper_expr_const_range(getPrivateCopies().begin(),
                                   getPrivateCopies().end());
  }

  helper_expr_range private_copies() {
    return helper_expr_range(getPrivateCopies().begin(),
                             getPrivateCopies().end());
  }

  helper_expr_const_range source_exprs() const {
    return helper_expr_const_range(getSourceExprs().begin(),
                                   getSourceExprs().end());
  }

  helper_expr_range source_exprs() {
    return helper_expr_range(getSourceExprs().begin(), getSourceExprs().end());
  }

  helper_expr_const_range destination_exprs() const {
    return helper_expr_const_range(getDestinationExprs().begin(),
                                   getDestinationExprs().end());
  }

  helper_expr_range destination_exprs() {
    return helper_expr_range(getDestinationExprs().begin(),
                             getDestinationExprs().end());
  }

  helper_expr_const_range assignment_ops() const {
    return helper_expr_const_range(getAssignmentOps().begin(),
                                   getAssignmentOps().end());
  }

  helper_expr_range assignment_ops() {
    return helper_expr_range(getAssignmentOps().begin(),
                             getAssignmentOps().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPLastprivateClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_lastprivate;
  }
};

/// This represents clause 'shared' in the '#pragma omp ...' directives.
///
/// \code
/// #pragma omp parallel shared(a,b)
/// \endcode
/// In this example directive '#pragma omp parallel' has clause 'shared'
/// with the variables 'a' and 'b'.
class OMPSharedClause final
    : public OMPVarListClause<OMPSharedClause>,
      private llvm::TrailingObjects<OMPSharedClause, Expr *> {
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPSharedClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                  SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPSharedClause>(llvm::omp::OMPC_shared, StartLoc,
                                          LParenLoc, EndLoc, N) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPSharedClause(unsigned N)
      : OMPVarListClause<OMPSharedClause>(llvm::omp::OMPC_shared,
                                          SourceLocation(), SourceLocation(),
                                          SourceLocation(), N) {}

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  static OMPSharedClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation EndLoc, ArrayRef<Expr *> VL);

  /// Creates an empty clause with \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPSharedClause *CreateEmpty(const ASTContext &C, unsigned N);

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPSharedClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_shared;
  }
};

/// This represents clause 'reduction' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp parallel reduction(+:a,b)
/// \endcode
/// In this example directive '#pragma omp parallel' has clause 'reduction'
/// with operator '+' and the variables 'a' and 'b'.
class OMPReductionClause final
    : public OMPVarListClause<OMPReductionClause>,
      public OMPClauseWithPostUpdate,
      private llvm::TrailingObjects<OMPReductionClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Reduction modifier.
  OpenMPReductionClauseModifier Modifier = OMPC_REDUCTION_unknown;

  /// Reduction modifier location.
  SourceLocation ModifierLoc;

  /// Location of ':'.
  SourceLocation ColonLoc;

  /// Nested name specifier for C++.
  NestedNameSpecifierLoc QualifierLoc;

  /// Name of custom operator.
  DeclarationNameInfo NameInfo;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ModifierLoc Modifier location.
  /// \param ColonLoc Location of ':'.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  /// \param QualifierLoc The nested-name qualifier with location information
  /// \param NameInfo The full name info for reduction identifier.
  OMPReductionClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                     SourceLocation ModifierLoc, SourceLocation ColonLoc,
                     SourceLocation EndLoc,
                     OpenMPReductionClauseModifier Modifier, unsigned N,
                     NestedNameSpecifierLoc QualifierLoc,
                     const DeclarationNameInfo &NameInfo)
      : OMPVarListClause<OMPReductionClause>(llvm::omp::OMPC_reduction,
                                             StartLoc, LParenLoc, EndLoc, N),
        OMPClauseWithPostUpdate(this), Modifier(Modifier),
        ModifierLoc(ModifierLoc), ColonLoc(ColonLoc),
        QualifierLoc(QualifierLoc), NameInfo(NameInfo) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPReductionClause(unsigned N)
      : OMPVarListClause<OMPReductionClause>(llvm::omp::OMPC_reduction,
                                             SourceLocation(), SourceLocation(),
                                             SourceLocation(), N),
        OMPClauseWithPostUpdate(this) {}

  /// Sets reduction modifier.
  void setModifier(OpenMPReductionClauseModifier M) { Modifier = M; }

  /// Sets location of the modifier.
  void setModifierLoc(SourceLocation Loc) { ModifierLoc = Loc; }

  /// Sets location of ':' symbol in clause.
  void setColonLoc(SourceLocation CL) { ColonLoc = CL; }

  /// Sets the name info for specified reduction identifier.
  void setNameInfo(DeclarationNameInfo DNI) { NameInfo = DNI; }

  /// Sets the nested name specifier.
  void setQualifierLoc(NestedNameSpecifierLoc NSL) { QualifierLoc = NSL; }

  /// Set list of helper expressions, required for proper codegen of the
  /// clause. These expressions represent private copy of the reduction
  /// variable.
  void setPrivates(ArrayRef<Expr *> Privates);

  /// Get the list of helper privates.
  MutableArrayRef<Expr *> getPrivates() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getPrivates() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the
  /// clause. These expressions represent LHS expression in the final
  /// reduction expression performed by the reduction clause.
  void setLHSExprs(ArrayRef<Expr *> LHSExprs);

  /// Get the list of helper LHS expressions.
  MutableArrayRef<Expr *> getLHSExprs() {
    return MutableArrayRef<Expr *>(getPrivates().end(), varlist_size());
  }
  ArrayRef<const Expr *> getLHSExprs() const {
    return llvm::ArrayRef(getPrivates().end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the
  /// clause. These expressions represent RHS expression in the final
  /// reduction expression performed by the reduction clause.
  /// Also, variables in these expressions are used for proper initialization of
  /// reduction copies.
  void setRHSExprs(ArrayRef<Expr *> RHSExprs);

  /// Get the list of helper destination expressions.
  MutableArrayRef<Expr *> getRHSExprs() {
    return MutableArrayRef<Expr *>(getLHSExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getRHSExprs() const {
    return llvm::ArrayRef(getLHSExprs().end(), varlist_size());
  }

  /// Set list of helper reduction expressions, required for proper
  /// codegen of the clause. These expressions are binary expressions or
  /// operator/custom reduction call that calculates new value from source
  /// helper expressions to destination helper expressions.
  void setReductionOps(ArrayRef<Expr *> ReductionOps);

  /// Get the list of helper reduction expressions.
  MutableArrayRef<Expr *> getReductionOps() {
    return MutableArrayRef<Expr *>(getRHSExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getReductionOps() const {
    return llvm::ArrayRef(getRHSExprs().end(), varlist_size());
  }

  /// Set list of helper copy operations for inscan reductions.
  /// The form is: Temps[i] = LHS[i];
  void setInscanCopyOps(ArrayRef<Expr *> Ops);

  /// Get the list of helper inscan copy operations.
  MutableArrayRef<Expr *> getInscanCopyOps() {
    return MutableArrayRef<Expr *>(getReductionOps().end(), varlist_size());
  }
  ArrayRef<const Expr *> getInscanCopyOps() const {
    return llvm::ArrayRef(getReductionOps().end(), varlist_size());
  }

  /// Set list of helper temp vars for inscan copy array operations.
  void setInscanCopyArrayTemps(ArrayRef<Expr *> CopyArrayTemps);

  /// Get the list of helper inscan copy temps.
  MutableArrayRef<Expr *> getInscanCopyArrayTemps() {
    return MutableArrayRef<Expr *>(getInscanCopyOps().end(), varlist_size());
  }
  ArrayRef<const Expr *> getInscanCopyArrayTemps() const {
    return llvm::ArrayRef(getInscanCopyOps().end(), varlist_size());
  }

  /// Set list of helper temp elements vars for inscan copy array operations.
  void setInscanCopyArrayElems(ArrayRef<Expr *> CopyArrayElems);

  /// Get the list of helper inscan copy temps.
  MutableArrayRef<Expr *> getInscanCopyArrayElems() {
    return MutableArrayRef<Expr *>(getInscanCopyArrayTemps().end(),
                                   varlist_size());
  }
  ArrayRef<const Expr *> getInscanCopyArrayElems() const {
    return llvm::ArrayRef(getInscanCopyArrayTemps().end(), varlist_size());
  }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ModifierLoc Modifier location.
  /// \param ColonLoc Location of ':'.
  /// \param EndLoc Ending location of the clause.
  /// \param VL The variables in the clause.
  /// \param QualifierLoc The nested-name qualifier with location information
  /// \param NameInfo The full name info for reduction identifier.
  /// \param Privates List of helper expressions for proper generation of
  /// private copies.
  /// \param LHSExprs List of helper expressions for proper generation of
  /// assignment operation required for copyprivate clause. This list represents
  /// LHSs of the reduction expressions.
  /// \param RHSExprs List of helper expressions for proper generation of
  /// assignment operation required for copyprivate clause. This list represents
  /// RHSs of the reduction expressions.
  /// Also, variables in these expressions are used for proper initialization of
  /// reduction copies.
  /// \param ReductionOps List of helper expressions that represents reduction
  /// expressions:
  /// \code
  /// LHSExprs binop RHSExprs;
  /// operator binop(LHSExpr, RHSExpr);
  /// <CutomReduction>(LHSExpr, RHSExpr);
  /// \endcode
  /// Required for proper codegen of final reduction operation performed by the
  /// reduction clause.
  /// \param CopyOps List of copy operations for inscan reductions:
  /// \code
  /// TempExprs = LHSExprs;
  /// \endcode
  /// \param CopyArrayTemps Temp arrays for prefix sums.
  /// \param CopyArrayElems Temp arrays for prefix sums.
  /// \param PreInit Statement that must be executed before entering the OpenMP
  /// region with this clause.
  /// \param PostUpdate Expression that must be executed after exit from the
  /// OpenMP region with this clause.
  static OMPReductionClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation ModifierLoc, SourceLocation ColonLoc,
         SourceLocation EndLoc, OpenMPReductionClauseModifier Modifier,
         ArrayRef<Expr *> VL, NestedNameSpecifierLoc QualifierLoc,
         const DeclarationNameInfo &NameInfo, ArrayRef<Expr *> Privates,
         ArrayRef<Expr *> LHSExprs, ArrayRef<Expr *> RHSExprs,
         ArrayRef<Expr *> ReductionOps, ArrayRef<Expr *> CopyOps,
         ArrayRef<Expr *> CopyArrayTemps, ArrayRef<Expr *> CopyArrayElems,
         Stmt *PreInit, Expr *PostUpdate);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  /// \param Modifier Reduction modifier.
  static OMPReductionClause *
  CreateEmpty(const ASTContext &C, unsigned N,
              OpenMPReductionClauseModifier Modifier);

  /// Returns modifier.
  OpenMPReductionClauseModifier getModifier() const { return Modifier; }

  /// Returns modifier location.
  SourceLocation getModifierLoc() const { return ModifierLoc; }

  /// Gets location of ':' symbol in clause.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Gets the name info for specified reduction identifier.
  const DeclarationNameInfo &getNameInfo() const { return NameInfo; }

  /// Gets the nested name specifier.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  using helper_expr_iterator = MutableArrayRef<Expr *>::iterator;
  using helper_expr_const_iterator = ArrayRef<const Expr *>::iterator;
  using helper_expr_range = llvm::iterator_range<helper_expr_iterator>;
  using helper_expr_const_range =
      llvm::iterator_range<helper_expr_const_iterator>;

  helper_expr_const_range privates() const {
    return helper_expr_const_range(getPrivates().begin(), getPrivates().end());
  }

  helper_expr_range privates() {
    return helper_expr_range(getPrivates().begin(), getPrivates().end());
  }

  helper_expr_const_range lhs_exprs() const {
    return helper_expr_const_range(getLHSExprs().begin(), getLHSExprs().end());
  }

  helper_expr_range lhs_exprs() {
    return helper_expr_range(getLHSExprs().begin(), getLHSExprs().end());
  }

  helper_expr_const_range rhs_exprs() const {
    return helper_expr_const_range(getRHSExprs().begin(), getRHSExprs().end());
  }

  helper_expr_range rhs_exprs() {
    return helper_expr_range(getRHSExprs().begin(), getRHSExprs().end());
  }

  helper_expr_const_range reduction_ops() const {
    return helper_expr_const_range(getReductionOps().begin(),
                                   getReductionOps().end());
  }

  helper_expr_range reduction_ops() {
    return helper_expr_range(getReductionOps().begin(),
                             getReductionOps().end());
  }

  helper_expr_const_range copy_ops() const {
    return helper_expr_const_range(getInscanCopyOps().begin(),
                                   getInscanCopyOps().end());
  }

  helper_expr_range copy_ops() {
    return helper_expr_range(getInscanCopyOps().begin(),
                             getInscanCopyOps().end());
  }

  helper_expr_const_range copy_array_temps() const {
    return helper_expr_const_range(getInscanCopyArrayTemps().begin(),
                                   getInscanCopyArrayTemps().end());
  }

  helper_expr_range copy_array_temps() {
    return helper_expr_range(getInscanCopyArrayTemps().begin(),
                             getInscanCopyArrayTemps().end());
  }

  helper_expr_const_range copy_array_elems() const {
    return helper_expr_const_range(getInscanCopyArrayElems().begin(),
                                   getInscanCopyArrayElems().end());
  }

  helper_expr_range copy_array_elems() {
    return helper_expr_range(getInscanCopyArrayElems().begin(),
                             getInscanCopyArrayElems().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPReductionClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }
  const_child_range used_children() const {
    auto Children = const_cast<OMPReductionClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_reduction;
  }
};

/// This represents clause 'task_reduction' in the '#pragma omp taskgroup'
/// directives.
///
/// \code
/// #pragma omp taskgroup task_reduction(+:a,b)
/// \endcode
/// In this example directive '#pragma omp taskgroup' has clause
/// 'task_reduction' with operator '+' and the variables 'a' and 'b'.
class OMPTaskReductionClause final
    : public OMPVarListClause<OMPTaskReductionClause>,
      public OMPClauseWithPostUpdate,
      private llvm::TrailingObjects<OMPTaskReductionClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Location of ':'.
  SourceLocation ColonLoc;

  /// Nested name specifier for C++.
  NestedNameSpecifierLoc QualifierLoc;

  /// Name of custom operator.
  DeclarationNameInfo NameInfo;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param ColonLoc Location of ':'.
  /// \param N Number of the variables in the clause.
  /// \param QualifierLoc The nested-name qualifier with location information
  /// \param NameInfo The full name info for reduction identifier.
  OMPTaskReductionClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                         SourceLocation ColonLoc, SourceLocation EndLoc,
                         unsigned N, NestedNameSpecifierLoc QualifierLoc,
                         const DeclarationNameInfo &NameInfo)
      : OMPVarListClause<OMPTaskReductionClause>(
            llvm::omp::OMPC_task_reduction, StartLoc, LParenLoc, EndLoc, N),
        OMPClauseWithPostUpdate(this), ColonLoc(ColonLoc),
        QualifierLoc(QualifierLoc), NameInfo(NameInfo) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPTaskReductionClause(unsigned N)
      : OMPVarListClause<OMPTaskReductionClause>(
            llvm::omp::OMPC_task_reduction, SourceLocation(), SourceLocation(),
            SourceLocation(), N),
        OMPClauseWithPostUpdate(this) {}

  /// Sets location of ':' symbol in clause.
  void setColonLoc(SourceLocation CL) { ColonLoc = CL; }

  /// Sets the name info for specified reduction identifier.
  void setNameInfo(DeclarationNameInfo DNI) { NameInfo = DNI; }

  /// Sets the nested name specifier.
  void setQualifierLoc(NestedNameSpecifierLoc NSL) { QualifierLoc = NSL; }

  /// Set list of helper expressions, required for proper codegen of the clause.
  /// These expressions represent private copy of the reduction variable.
  void setPrivates(ArrayRef<Expr *> Privates);

  /// Get the list of helper privates.
  MutableArrayRef<Expr *> getPrivates() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getPrivates() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the clause.
  /// These expressions represent LHS expression in the final reduction
  /// expression performed by the reduction clause.
  void setLHSExprs(ArrayRef<Expr *> LHSExprs);

  /// Get the list of helper LHS expressions.
  MutableArrayRef<Expr *> getLHSExprs() {
    return MutableArrayRef<Expr *>(getPrivates().end(), varlist_size());
  }
  ArrayRef<const Expr *> getLHSExprs() const {
    return llvm::ArrayRef(getPrivates().end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the clause.
  /// These expressions represent RHS expression in the final reduction
  /// expression performed by the reduction clause. Also, variables in these
  /// expressions are used for proper initialization of reduction copies.
  void setRHSExprs(ArrayRef<Expr *> RHSExprs);

  ///  Get the list of helper destination expressions.
  MutableArrayRef<Expr *> getRHSExprs() {
    return MutableArrayRef<Expr *>(getLHSExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getRHSExprs() const {
    return llvm::ArrayRef(getLHSExprs().end(), varlist_size());
  }

  /// Set list of helper reduction expressions, required for proper
  /// codegen of the clause. These expressions are binary expressions or
  /// operator/custom reduction call that calculates new value from source
  /// helper expressions to destination helper expressions.
  void setReductionOps(ArrayRef<Expr *> ReductionOps);

  ///  Get the list of helper reduction expressions.
  MutableArrayRef<Expr *> getReductionOps() {
    return MutableArrayRef<Expr *>(getRHSExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getReductionOps() const {
    return llvm::ArrayRef(getRHSExprs().end(), varlist_size());
  }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ColonLoc Location of ':'.
  /// \param EndLoc Ending location of the clause.
  /// \param VL The variables in the clause.
  /// \param QualifierLoc The nested-name qualifier with location information
  /// \param NameInfo The full name info for reduction identifier.
  /// \param Privates List of helper expressions for proper generation of
  /// private copies.
  /// \param LHSExprs List of helper expressions for proper generation of
  /// assignment operation required for copyprivate clause. This list represents
  /// LHSs of the reduction expressions.
  /// \param RHSExprs List of helper expressions for proper generation of
  /// assignment operation required for copyprivate clause. This list represents
  /// RHSs of the reduction expressions.
  /// Also, variables in these expressions are used for proper initialization of
  /// reduction copies.
  /// \param ReductionOps List of helper expressions that represents reduction
  /// expressions:
  /// \code
  /// LHSExprs binop RHSExprs;
  /// operator binop(LHSExpr, RHSExpr);
  /// <CutomReduction>(LHSExpr, RHSExpr);
  /// \endcode
  /// Required for proper codegen of final reduction operation performed by the
  /// reduction clause.
  /// \param PreInit Statement that must be executed before entering the OpenMP
  /// region with this clause.
  /// \param PostUpdate Expression that must be executed after exit from the
  /// OpenMP region with this clause.
  static OMPTaskReductionClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation ColonLoc, SourceLocation EndLoc, ArrayRef<Expr *> VL,
         NestedNameSpecifierLoc QualifierLoc,
         const DeclarationNameInfo &NameInfo, ArrayRef<Expr *> Privates,
         ArrayRef<Expr *> LHSExprs, ArrayRef<Expr *> RHSExprs,
         ArrayRef<Expr *> ReductionOps, Stmt *PreInit, Expr *PostUpdate);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPTaskReductionClause *CreateEmpty(const ASTContext &C, unsigned N);

  /// Gets location of ':' symbol in clause.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Gets the name info for specified reduction identifier.
  const DeclarationNameInfo &getNameInfo() const { return NameInfo; }

  /// Gets the nested name specifier.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  using helper_expr_iterator = MutableArrayRef<Expr *>::iterator;
  using helper_expr_const_iterator = ArrayRef<const Expr *>::iterator;
  using helper_expr_range = llvm::iterator_range<helper_expr_iterator>;
  using helper_expr_const_range =
      llvm::iterator_range<helper_expr_const_iterator>;

  helper_expr_const_range privates() const {
    return helper_expr_const_range(getPrivates().begin(), getPrivates().end());
  }

  helper_expr_range privates() {
    return helper_expr_range(getPrivates().begin(), getPrivates().end());
  }

  helper_expr_const_range lhs_exprs() const {
    return helper_expr_const_range(getLHSExprs().begin(), getLHSExprs().end());
  }

  helper_expr_range lhs_exprs() {
    return helper_expr_range(getLHSExprs().begin(), getLHSExprs().end());
  }

  helper_expr_const_range rhs_exprs() const {
    return helper_expr_const_range(getRHSExprs().begin(), getRHSExprs().end());
  }

  helper_expr_range rhs_exprs() {
    return helper_expr_range(getRHSExprs().begin(), getRHSExprs().end());
  }

  helper_expr_const_range reduction_ops() const {
    return helper_expr_const_range(getReductionOps().begin(),
                                   getReductionOps().end());
  }

  helper_expr_range reduction_ops() {
    return helper_expr_range(getReductionOps().begin(),
                             getReductionOps().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPTaskReductionClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_task_reduction;
  }
};

/// This represents clause 'in_reduction' in the '#pragma omp task' directives.
///
/// \code
/// #pragma omp task in_reduction(+:a,b)
/// \endcode
/// In this example directive '#pragma omp task' has clause 'in_reduction' with
/// operator '+' and the variables 'a' and 'b'.
class OMPInReductionClause final
    : public OMPVarListClause<OMPInReductionClause>,
      public OMPClauseWithPostUpdate,
      private llvm::TrailingObjects<OMPInReductionClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Location of ':'.
  SourceLocation ColonLoc;

  /// Nested name specifier for C++.
  NestedNameSpecifierLoc QualifierLoc;

  /// Name of custom operator.
  DeclarationNameInfo NameInfo;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param ColonLoc Location of ':'.
  /// \param N Number of the variables in the clause.
  /// \param QualifierLoc The nested-name qualifier with location information
  /// \param NameInfo The full name info for reduction identifier.
  OMPInReductionClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                       SourceLocation ColonLoc, SourceLocation EndLoc,
                       unsigned N, NestedNameSpecifierLoc QualifierLoc,
                       const DeclarationNameInfo &NameInfo)
      : OMPVarListClause<OMPInReductionClause>(llvm::omp::OMPC_in_reduction,
                                               StartLoc, LParenLoc, EndLoc, N),
        OMPClauseWithPostUpdate(this), ColonLoc(ColonLoc),
        QualifierLoc(QualifierLoc), NameInfo(NameInfo) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPInReductionClause(unsigned N)
      : OMPVarListClause<OMPInReductionClause>(
            llvm::omp::OMPC_in_reduction, SourceLocation(), SourceLocation(),
            SourceLocation(), N),
        OMPClauseWithPostUpdate(this) {}

  /// Sets location of ':' symbol in clause.
  void setColonLoc(SourceLocation CL) { ColonLoc = CL; }

  /// Sets the name info for specified reduction identifier.
  void setNameInfo(DeclarationNameInfo DNI) { NameInfo = DNI; }

  /// Sets the nested name specifier.
  void setQualifierLoc(NestedNameSpecifierLoc NSL) { QualifierLoc = NSL; }

  /// Set list of helper expressions, required for proper codegen of the clause.
  /// These expressions represent private copy of the reduction variable.
  void setPrivates(ArrayRef<Expr *> Privates);

  /// Get the list of helper privates.
  MutableArrayRef<Expr *> getPrivates() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getPrivates() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the clause.
  /// These expressions represent LHS expression in the final reduction
  /// expression performed by the reduction clause.
  void setLHSExprs(ArrayRef<Expr *> LHSExprs);

  /// Get the list of helper LHS expressions.
  MutableArrayRef<Expr *> getLHSExprs() {
    return MutableArrayRef<Expr *>(getPrivates().end(), varlist_size());
  }
  ArrayRef<const Expr *> getLHSExprs() const {
    return llvm::ArrayRef(getPrivates().end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the clause.
  /// These expressions represent RHS expression in the final reduction
  /// expression performed by the reduction clause. Also, variables in these
  /// expressions are used for proper initialization of reduction copies.
  void setRHSExprs(ArrayRef<Expr *> RHSExprs);

  ///  Get the list of helper destination expressions.
  MutableArrayRef<Expr *> getRHSExprs() {
    return MutableArrayRef<Expr *>(getLHSExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getRHSExprs() const {
    return llvm::ArrayRef(getLHSExprs().end(), varlist_size());
  }

  /// Set list of helper reduction expressions, required for proper
  /// codegen of the clause. These expressions are binary expressions or
  /// operator/custom reduction call that calculates new value from source
  /// helper expressions to destination helper expressions.
  void setReductionOps(ArrayRef<Expr *> ReductionOps);

  ///  Get the list of helper reduction expressions.
  MutableArrayRef<Expr *> getReductionOps() {
    return MutableArrayRef<Expr *>(getRHSExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getReductionOps() const {
    return llvm::ArrayRef(getRHSExprs().end(), varlist_size());
  }

  /// Set list of helper reduction taskgroup descriptors.
  void setTaskgroupDescriptors(ArrayRef<Expr *> ReductionOps);

  ///  Get the list of helper reduction taskgroup descriptors.
  MutableArrayRef<Expr *> getTaskgroupDescriptors() {
    return MutableArrayRef<Expr *>(getReductionOps().end(), varlist_size());
  }
  ArrayRef<const Expr *> getTaskgroupDescriptors() const {
    return llvm::ArrayRef(getReductionOps().end(), varlist_size());
  }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ColonLoc Location of ':'.
  /// \param EndLoc Ending location of the clause.
  /// \param VL The variables in the clause.
  /// \param QualifierLoc The nested-name qualifier with location information
  /// \param NameInfo The full name info for reduction identifier.
  /// \param Privates List of helper expressions for proper generation of
  /// private copies.
  /// \param LHSExprs List of helper expressions for proper generation of
  /// assignment operation required for copyprivate clause. This list represents
  /// LHSs of the reduction expressions.
  /// \param RHSExprs List of helper expressions for proper generation of
  /// assignment operation required for copyprivate clause. This list represents
  /// RHSs of the reduction expressions.
  /// Also, variables in these expressions are used for proper initialization of
  /// reduction copies.
  /// \param ReductionOps List of helper expressions that represents reduction
  /// expressions:
  /// \code
  /// LHSExprs binop RHSExprs;
  /// operator binop(LHSExpr, RHSExpr);
  /// <CutomReduction>(LHSExpr, RHSExpr);
  /// \endcode
  /// Required for proper codegen of final reduction operation performed by the
  /// reduction clause.
  /// \param TaskgroupDescriptors List of helper taskgroup descriptors for
  /// corresponding items in parent taskgroup task_reduction clause.
  /// \param PreInit Statement that must be executed before entering the OpenMP
  /// region with this clause.
  /// \param PostUpdate Expression that must be executed after exit from the
  /// OpenMP region with this clause.
  static OMPInReductionClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation ColonLoc, SourceLocation EndLoc, ArrayRef<Expr *> VL,
         NestedNameSpecifierLoc QualifierLoc,
         const DeclarationNameInfo &NameInfo, ArrayRef<Expr *> Privates,
         ArrayRef<Expr *> LHSExprs, ArrayRef<Expr *> RHSExprs,
         ArrayRef<Expr *> ReductionOps, ArrayRef<Expr *> TaskgroupDescriptors,
         Stmt *PreInit, Expr *PostUpdate);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPInReductionClause *CreateEmpty(const ASTContext &C, unsigned N);

  /// Gets location of ':' symbol in clause.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Gets the name info for specified reduction identifier.
  const DeclarationNameInfo &getNameInfo() const { return NameInfo; }

  /// Gets the nested name specifier.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  using helper_expr_iterator = MutableArrayRef<Expr *>::iterator;
  using helper_expr_const_iterator = ArrayRef<const Expr *>::iterator;
  using helper_expr_range = llvm::iterator_range<helper_expr_iterator>;
  using helper_expr_const_range =
      llvm::iterator_range<helper_expr_const_iterator>;

  helper_expr_const_range privates() const {
    return helper_expr_const_range(getPrivates().begin(), getPrivates().end());
  }

  helper_expr_range privates() {
    return helper_expr_range(getPrivates().begin(), getPrivates().end());
  }

  helper_expr_const_range lhs_exprs() const {
    return helper_expr_const_range(getLHSExprs().begin(), getLHSExprs().end());
  }

  helper_expr_range lhs_exprs() {
    return helper_expr_range(getLHSExprs().begin(), getLHSExprs().end());
  }

  helper_expr_const_range rhs_exprs() const {
    return helper_expr_const_range(getRHSExprs().begin(), getRHSExprs().end());
  }

  helper_expr_range rhs_exprs() {
    return helper_expr_range(getRHSExprs().begin(), getRHSExprs().end());
  }

  helper_expr_const_range reduction_ops() const {
    return helper_expr_const_range(getReductionOps().begin(),
                                   getReductionOps().end());
  }

  helper_expr_range reduction_ops() {
    return helper_expr_range(getReductionOps().begin(),
                             getReductionOps().end());
  }

  helper_expr_const_range taskgroup_descriptors() const {
    return helper_expr_const_range(getTaskgroupDescriptors().begin(),
                                   getTaskgroupDescriptors().end());
  }

  helper_expr_range taskgroup_descriptors() {
    return helper_expr_range(getTaskgroupDescriptors().begin(),
                             getTaskgroupDescriptors().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPInReductionClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_in_reduction;
  }
};

/// This represents clause 'linear' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp simd linear(a,b : 2)
/// \endcode
/// In this example directive '#pragma omp simd' has clause 'linear'
/// with variables 'a', 'b' and linear step '2'.
class OMPLinearClause final
    : public OMPVarListClause<OMPLinearClause>,
      public OMPClauseWithPostUpdate,
      private llvm::TrailingObjects<OMPLinearClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Modifier of 'linear' clause.
  OpenMPLinearClauseKind Modifier = OMPC_LINEAR_val;

  /// Location of linear modifier if any.
  SourceLocation ModifierLoc;

  /// Location of ':'.
  SourceLocation ColonLoc;

  /// Location of 'step' modifier.
  SourceLocation StepModifierLoc;

  /// Sets the linear step for clause.
  void setStep(Expr *Step) { *(getFinals().end()) = Step; }

  /// Sets the expression to calculate linear step for clause.
  void setCalcStep(Expr *CalcStep) { *(getFinals().end() + 1) = CalcStep; }

  /// Build 'linear' clause with given number of variables \a NumVars.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ColonLoc Location of ':'.
  /// \param StepModifierLoc Location of 'step' modifier.
  /// \param EndLoc Ending location of the clause.
  /// \param NumVars Number of variables.
  OMPLinearClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                  OpenMPLinearClauseKind Modifier, SourceLocation ModifierLoc,
                  SourceLocation ColonLoc, SourceLocation StepModifierLoc,
                  SourceLocation EndLoc, unsigned NumVars)
      : OMPVarListClause<OMPLinearClause>(llvm::omp::OMPC_linear, StartLoc,
                                          LParenLoc, EndLoc, NumVars),
        OMPClauseWithPostUpdate(this), Modifier(Modifier),
        ModifierLoc(ModifierLoc), ColonLoc(ColonLoc),
        StepModifierLoc(StepModifierLoc) {}

  /// Build an empty clause.
  ///
  /// \param NumVars Number of variables.
  explicit OMPLinearClause(unsigned NumVars)
      : OMPVarListClause<OMPLinearClause>(llvm::omp::OMPC_linear,
                                          SourceLocation(), SourceLocation(),
                                          SourceLocation(), NumVars),
        OMPClauseWithPostUpdate(this) {}

  /// Gets the list of initial values for linear variables.
  ///
  /// There are NumVars expressions with initial values allocated after the
  /// varlist, they are followed by NumVars update expressions (used to update
  /// the linear variable's value on current iteration) and they are followed by
  /// NumVars final expressions (used to calculate the linear variable's
  /// value after the loop body). After these lists, there are 2 helper
  /// expressions - linear step and a helper to calculate it before the
  /// loop body (used when the linear step is not constant):
  ///
  /// { Vars[] /* in OMPVarListClause */; Privates[]; Inits[]; Updates[];
  /// Finals[]; Step; CalcStep; }
  MutableArrayRef<Expr *> getPrivates() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getPrivates() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

  MutableArrayRef<Expr *> getInits() {
    return MutableArrayRef<Expr *>(getPrivates().end(), varlist_size());
  }
  ArrayRef<const Expr *> getInits() const {
    return llvm::ArrayRef(getPrivates().end(), varlist_size());
  }

  /// Sets the list of update expressions for linear variables.
  MutableArrayRef<Expr *> getUpdates() {
    return MutableArrayRef<Expr *>(getInits().end(), varlist_size());
  }
  ArrayRef<const Expr *> getUpdates() const {
    return llvm::ArrayRef(getInits().end(), varlist_size());
  }

  /// Sets the list of final update expressions for linear variables.
  MutableArrayRef<Expr *> getFinals() {
    return MutableArrayRef<Expr *>(getUpdates().end(), varlist_size());
  }
  ArrayRef<const Expr *> getFinals() const {
    return llvm::ArrayRef(getUpdates().end(), varlist_size());
  }

  /// Gets the list of used expressions for linear variables.
  MutableArrayRef<Expr *> getUsedExprs() {
    return MutableArrayRef<Expr *>(getFinals().end() + 2, varlist_size() + 1);
  }
  ArrayRef<const Expr *> getUsedExprs() const {
    return llvm::ArrayRef(getFinals().end() + 2, varlist_size() + 1);
  }

  /// Sets the list of the copies of original linear variables.
  /// \param PL List of expressions.
  void setPrivates(ArrayRef<Expr *> PL);

  /// Sets the list of the initial values for linear variables.
  /// \param IL List of expressions.
  void setInits(ArrayRef<Expr *> IL);

public:
  /// Creates clause with a list of variables \a VL and a linear step
  /// \a Step.
  ///
  /// \param C AST Context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param Modifier Modifier of 'linear' clause.
  /// \param ModifierLoc Modifier location.
  /// \param ColonLoc Location of ':'.
  /// \param StepModifierLoc Location of 'step' modifier.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  /// \param PL List of private copies of original variables.
  /// \param IL List of initial values for the variables.
  /// \param Step Linear step.
  /// \param CalcStep Calculation of the linear step.
  /// \param PreInit Statement that must be executed before entering the OpenMP
  /// region with this clause.
  /// \param PostUpdate Expression that must be executed after exit from the
  /// OpenMP region with this clause.
  static OMPLinearClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         OpenMPLinearClauseKind Modifier, SourceLocation ModifierLoc,
         SourceLocation ColonLoc, SourceLocation StepModifierLoc,
         SourceLocation EndLoc, ArrayRef<Expr *> VL, ArrayRef<Expr *> PL,
         ArrayRef<Expr *> IL, Expr *Step, Expr *CalcStep, Stmt *PreInit,
         Expr *PostUpdate);

  /// Creates an empty clause with the place for \a NumVars variables.
  ///
  /// \param C AST context.
  /// \param NumVars Number of variables.
  static OMPLinearClause *CreateEmpty(const ASTContext &C, unsigned NumVars);

  /// Set modifier.
  void setModifier(OpenMPLinearClauseKind Kind) { Modifier = Kind; }

  /// Return modifier.
  OpenMPLinearClauseKind getModifier() const { return Modifier; }

  /// Set modifier location.
  void setModifierLoc(SourceLocation Loc) { ModifierLoc = Loc; }

  /// Return modifier location.
  SourceLocation getModifierLoc() const { return ModifierLoc; }

  /// Sets the location of ':'.
  void setColonLoc(SourceLocation Loc) { ColonLoc = Loc; }

  /// Sets the location of 'step' modifier.
  void setStepModifierLoc(SourceLocation Loc) { StepModifierLoc = Loc; }

  /// Returns the location of ':'.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Returns the location of 'step' modifier.
  SourceLocation getStepModifierLoc() const { return StepModifierLoc; }

  /// Returns linear step.
  Expr *getStep() { return *(getFinals().end()); }

  /// Returns linear step.
  const Expr *getStep() const { return *(getFinals().end()); }

  /// Returns expression to calculate linear step.
  Expr *getCalcStep() { return *(getFinals().end() + 1); }

  /// Returns expression to calculate linear step.
  const Expr *getCalcStep() const { return *(getFinals().end() + 1); }

  /// Sets the list of update expressions for linear variables.
  /// \param UL List of expressions.
  void setUpdates(ArrayRef<Expr *> UL);

  /// Sets the list of final update expressions for linear variables.
  /// \param FL List of expressions.
  void setFinals(ArrayRef<Expr *> FL);

  /// Sets the list of used expressions for the linear clause.
  void setUsedExprs(ArrayRef<Expr *> UE);

  using privates_iterator = MutableArrayRef<Expr *>::iterator;
  using privates_const_iterator = ArrayRef<const Expr *>::iterator;
  using privates_range = llvm::iterator_range<privates_iterator>;
  using privates_const_range = llvm::iterator_range<privates_const_iterator>;

  privates_range privates() {
    return privates_range(getPrivates().begin(), getPrivates().end());
  }

  privates_const_range privates() const {
    return privates_const_range(getPrivates().begin(), getPrivates().end());
  }

  using inits_iterator = MutableArrayRef<Expr *>::iterator;
  using inits_const_iterator = ArrayRef<const Expr *>::iterator;
  using inits_range = llvm::iterator_range<inits_iterator>;
  using inits_const_range = llvm::iterator_range<inits_const_iterator>;

  inits_range inits() {
    return inits_range(getInits().begin(), getInits().end());
  }

  inits_const_range inits() const {
    return inits_const_range(getInits().begin(), getInits().end());
  }

  using updates_iterator = MutableArrayRef<Expr *>::iterator;
  using updates_const_iterator = ArrayRef<const Expr *>::iterator;
  using updates_range = llvm::iterator_range<updates_iterator>;
  using updates_const_range = llvm::iterator_range<updates_const_iterator>;

  updates_range updates() {
    return updates_range(getUpdates().begin(), getUpdates().end());
  }

  updates_const_range updates() const {
    return updates_const_range(getUpdates().begin(), getUpdates().end());
  }

  using finals_iterator = MutableArrayRef<Expr *>::iterator;
  using finals_const_iterator = ArrayRef<const Expr *>::iterator;
  using finals_range = llvm::iterator_range<finals_iterator>;
  using finals_const_range = llvm::iterator_range<finals_const_iterator>;

  finals_range finals() {
    return finals_range(getFinals().begin(), getFinals().end());
  }

  finals_const_range finals() const {
    return finals_const_range(getFinals().begin(), getFinals().end());
  }

  using used_expressions_iterator = MutableArrayRef<Expr *>::iterator;
  using used_expressions_const_iterator = ArrayRef<const Expr *>::iterator;
  using used_expressions_range =
      llvm::iterator_range<used_expressions_iterator>;
  using used_expressions_const_range =
      llvm::iterator_range<used_expressions_const_iterator>;

  used_expressions_range used_expressions() {
    return finals_range(getUsedExprs().begin(), getUsedExprs().end());
  }

  used_expressions_const_range used_expressions() const {
    return finals_const_range(getUsedExprs().begin(), getUsedExprs().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPLinearClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children();

  const_child_range used_children() const {
    auto Children = const_cast<OMPLinearClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_linear;
  }
};

/// This represents clause 'aligned' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp simd aligned(a,b : 8)
/// \endcode
/// In this example directive '#pragma omp simd' has clause 'aligned'
/// with variables 'a', 'b' and alignment '8'.
class OMPAlignedClause final
    : public OMPVarListClause<OMPAlignedClause>,
      private llvm::TrailingObjects<OMPAlignedClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Location of ':'.
  SourceLocation ColonLoc;

  /// Sets the alignment for clause.
  void setAlignment(Expr *A) { *varlist_end() = A; }

  /// Build 'aligned' clause with given number of variables \a NumVars.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ColonLoc Location of ':'.
  /// \param EndLoc Ending location of the clause.
  /// \param NumVars Number of variables.
  OMPAlignedClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                   SourceLocation ColonLoc, SourceLocation EndLoc,
                   unsigned NumVars)
      : OMPVarListClause<OMPAlignedClause>(llvm::omp::OMPC_aligned, StartLoc,
                                           LParenLoc, EndLoc, NumVars),
        ColonLoc(ColonLoc) {}

  /// Build an empty clause.
  ///
  /// \param NumVars Number of variables.
  explicit OMPAlignedClause(unsigned NumVars)
      : OMPVarListClause<OMPAlignedClause>(llvm::omp::OMPC_aligned,
                                           SourceLocation(), SourceLocation(),
                                           SourceLocation(), NumVars) {}

public:
  /// Creates clause with a list of variables \a VL and alignment \a A.
  ///
  /// \param C AST Context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ColonLoc Location of ':'.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  /// \param A Alignment.
  static OMPAlignedClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation LParenLoc,
                                  SourceLocation ColonLoc,
                                  SourceLocation EndLoc, ArrayRef<Expr *> VL,
                                  Expr *A);

  /// Creates an empty clause with the place for \a NumVars variables.
  ///
  /// \param C AST context.
  /// \param NumVars Number of variables.
  static OMPAlignedClause *CreateEmpty(const ASTContext &C, unsigned NumVars);

  /// Sets the location of ':'.
  void setColonLoc(SourceLocation Loc) { ColonLoc = Loc; }

  /// Returns the location of ':'.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Returns alignment.
  Expr *getAlignment() { return *varlist_end(); }

  /// Returns alignment.
  const Expr *getAlignment() const { return *varlist_end(); }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPAlignedClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_aligned;
  }
};

/// This represents clause 'copyin' in the '#pragma omp ...' directives.
///
/// \code
/// #pragma omp parallel copyin(a,b)
/// \endcode
/// In this example directive '#pragma omp parallel' has clause 'copyin'
/// with the variables 'a' and 'b'.
class OMPCopyinClause final
    : public OMPVarListClause<OMPCopyinClause>,
      private llvm::TrailingObjects<OMPCopyinClause, Expr *> {
  // Class has 3 additional tail allocated arrays:
  // 1. List of helper expressions for proper generation of assignment operation
  // required for copyin clause. This list represents sources.
  // 2. List of helper expressions for proper generation of assignment operation
  // required for copyin clause. This list represents destinations.
  // 3. List of helper expressions that represents assignment operation:
  // \code
  // DstExprs = SrcExprs;
  // \endcode
  // Required for proper codegen of propagation of master's thread values of
  // threadprivate variables to local instances of that variables in other
  // implicit threads.

  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPCopyinClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                  SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPCopyinClause>(llvm::omp::OMPC_copyin, StartLoc,
                                          LParenLoc, EndLoc, N) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPCopyinClause(unsigned N)
      : OMPVarListClause<OMPCopyinClause>(llvm::omp::OMPC_copyin,
                                          SourceLocation(), SourceLocation(),
                                          SourceLocation(), N) {}

  /// Set list of helper expressions, required for proper codegen of the
  /// clause. These expressions represent source expression in the final
  /// assignment statement performed by the copyin clause.
  void setSourceExprs(ArrayRef<Expr *> SrcExprs);

  /// Get the list of helper source expressions.
  MutableArrayRef<Expr *> getSourceExprs() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getSourceExprs() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the
  /// clause. These expressions represent destination expression in the final
  /// assignment statement performed by the copyin clause.
  void setDestinationExprs(ArrayRef<Expr *> DstExprs);

  /// Get the list of helper destination expressions.
  MutableArrayRef<Expr *> getDestinationExprs() {
    return MutableArrayRef<Expr *>(getSourceExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getDestinationExprs() const {
    return llvm::ArrayRef(getSourceExprs().end(), varlist_size());
  }

  /// Set list of helper assignment expressions, required for proper
  /// codegen of the clause. These expressions are assignment expressions that
  /// assign source helper expressions to destination helper expressions
  /// correspondingly.
  void setAssignmentOps(ArrayRef<Expr *> AssignmentOps);

  /// Get the list of helper assignment expressions.
  MutableArrayRef<Expr *> getAssignmentOps() {
    return MutableArrayRef<Expr *>(getDestinationExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getAssignmentOps() const {
    return llvm::ArrayRef(getDestinationExprs().end(), varlist_size());
  }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  /// \param SrcExprs List of helper expressions for proper generation of
  /// assignment operation required for copyin clause. This list represents
  /// sources.
  /// \param DstExprs List of helper expressions for proper generation of
  /// assignment operation required for copyin clause. This list represents
  /// destinations.
  /// \param AssignmentOps List of helper expressions that represents assignment
  /// operation:
  /// \code
  /// DstExprs = SrcExprs;
  /// \endcode
  /// Required for proper codegen of propagation of master's thread values of
  /// threadprivate variables to local instances of that variables in other
  /// implicit threads.
  static OMPCopyinClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation EndLoc, ArrayRef<Expr *> VL, ArrayRef<Expr *> SrcExprs,
         ArrayRef<Expr *> DstExprs, ArrayRef<Expr *> AssignmentOps);

  /// Creates an empty clause with \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPCopyinClause *CreateEmpty(const ASTContext &C, unsigned N);

  using helper_expr_iterator = MutableArrayRef<Expr *>::iterator;
  using helper_expr_const_iterator = ArrayRef<const Expr *>::iterator;
  using helper_expr_range = llvm::iterator_range<helper_expr_iterator>;
  using helper_expr_const_range =
      llvm::iterator_range<helper_expr_const_iterator>;

  helper_expr_const_range source_exprs() const {
    return helper_expr_const_range(getSourceExprs().begin(),
                                   getSourceExprs().end());
  }

  helper_expr_range source_exprs() {
    return helper_expr_range(getSourceExprs().begin(), getSourceExprs().end());
  }

  helper_expr_const_range destination_exprs() const {
    return helper_expr_const_range(getDestinationExprs().begin(),
                                   getDestinationExprs().end());
  }

  helper_expr_range destination_exprs() {
    return helper_expr_range(getDestinationExprs().begin(),
                             getDestinationExprs().end());
  }

  helper_expr_const_range assignment_ops() const {
    return helper_expr_const_range(getAssignmentOps().begin(),
                                   getAssignmentOps().end());
  }

  helper_expr_range assignment_ops() {
    return helper_expr_range(getAssignmentOps().begin(),
                             getAssignmentOps().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPCopyinClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_copyin;
  }
};

/// This represents clause 'copyprivate' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp single copyprivate(a,b)
/// \endcode
/// In this example directive '#pragma omp single' has clause 'copyprivate'
/// with the variables 'a' and 'b'.
class OMPCopyprivateClause final
    : public OMPVarListClause<OMPCopyprivateClause>,
      private llvm::TrailingObjects<OMPCopyprivateClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPCopyprivateClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                       SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPCopyprivateClause>(llvm::omp::OMPC_copyprivate,
                                               StartLoc, LParenLoc, EndLoc, N) {
  }

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPCopyprivateClause(unsigned N)
      : OMPVarListClause<OMPCopyprivateClause>(
            llvm::omp::OMPC_copyprivate, SourceLocation(), SourceLocation(),
            SourceLocation(), N) {}

  /// Set list of helper expressions, required for proper codegen of the
  /// clause. These expressions represent source expression in the final
  /// assignment statement performed by the copyprivate clause.
  void setSourceExprs(ArrayRef<Expr *> SrcExprs);

  /// Get the list of helper source expressions.
  MutableArrayRef<Expr *> getSourceExprs() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getSourceExprs() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

  /// Set list of helper expressions, required for proper codegen of the
  /// clause. These expressions represent destination expression in the final
  /// assignment statement performed by the copyprivate clause.
  void setDestinationExprs(ArrayRef<Expr *> DstExprs);

  /// Get the list of helper destination expressions.
  MutableArrayRef<Expr *> getDestinationExprs() {
    return MutableArrayRef<Expr *>(getSourceExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getDestinationExprs() const {
    return llvm::ArrayRef(getSourceExprs().end(), varlist_size());
  }

  /// Set list of helper assignment expressions, required for proper
  /// codegen of the clause. These expressions are assignment expressions that
  /// assign source helper expressions to destination helper expressions
  /// correspondingly.
  void setAssignmentOps(ArrayRef<Expr *> AssignmentOps);

  /// Get the list of helper assignment expressions.
  MutableArrayRef<Expr *> getAssignmentOps() {
    return MutableArrayRef<Expr *>(getDestinationExprs().end(), varlist_size());
  }
  ArrayRef<const Expr *> getAssignmentOps() const {
    return llvm::ArrayRef(getDestinationExprs().end(), varlist_size());
  }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  /// \param SrcExprs List of helper expressions for proper generation of
  /// assignment operation required for copyprivate clause. This list represents
  /// sources.
  /// \param DstExprs List of helper expressions for proper generation of
  /// assignment operation required for copyprivate clause. This list represents
  /// destinations.
  /// \param AssignmentOps List of helper expressions that represents assignment
  /// operation:
  /// \code
  /// DstExprs = SrcExprs;
  /// \endcode
  /// Required for proper codegen of final assignment performed by the
  /// copyprivate clause.
  static OMPCopyprivateClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation EndLoc, ArrayRef<Expr *> VL, ArrayRef<Expr *> SrcExprs,
         ArrayRef<Expr *> DstExprs, ArrayRef<Expr *> AssignmentOps);

  /// Creates an empty clause with \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPCopyprivateClause *CreateEmpty(const ASTContext &C, unsigned N);

  using helper_expr_iterator = MutableArrayRef<Expr *>::iterator;
  using helper_expr_const_iterator = ArrayRef<const Expr *>::iterator;
  using helper_expr_range = llvm::iterator_range<helper_expr_iterator>;
  using helper_expr_const_range =
      llvm::iterator_range<helper_expr_const_iterator>;

  helper_expr_const_range source_exprs() const {
    return helper_expr_const_range(getSourceExprs().begin(),
                                   getSourceExprs().end());
  }

  helper_expr_range source_exprs() {
    return helper_expr_range(getSourceExprs().begin(), getSourceExprs().end());
  }

  helper_expr_const_range destination_exprs() const {
    return helper_expr_const_range(getDestinationExprs().begin(),
                                   getDestinationExprs().end());
  }

  helper_expr_range destination_exprs() {
    return helper_expr_range(getDestinationExprs().begin(),
                             getDestinationExprs().end());
  }

  helper_expr_const_range assignment_ops() const {
    return helper_expr_const_range(getAssignmentOps().begin(),
                                   getAssignmentOps().end());
  }

  helper_expr_range assignment_ops() {
    return helper_expr_range(getAssignmentOps().begin(),
                             getAssignmentOps().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPCopyprivateClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_copyprivate;
  }
};

/// This represents implicit clause 'flush' for the '#pragma omp flush'
/// directive.
/// This clause does not exist by itself, it can be only as a part of 'omp
/// flush' directive. This clause is introduced to keep the original structure
/// of \a OMPExecutableDirective class and its derivatives and to use the
/// existing infrastructure of clauses with the list of variables.
///
/// \code
/// #pragma omp flush(a,b)
/// \endcode
/// In this example directive '#pragma omp flush' has implicit clause 'flush'
/// with the variables 'a' and 'b'.
class OMPFlushClause final
    : public OMPVarListClause<OMPFlushClause>,
      private llvm::TrailingObjects<OMPFlushClause, Expr *> {
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPFlushClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                 SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPFlushClause>(llvm::omp::OMPC_flush, StartLoc,
                                         LParenLoc, EndLoc, N) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPFlushClause(unsigned N)
      : OMPVarListClause<OMPFlushClause>(llvm::omp::OMPC_flush,
                                         SourceLocation(), SourceLocation(),
                                         SourceLocation(), N) {}

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  static OMPFlushClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                SourceLocation LParenLoc, SourceLocation EndLoc,
                                ArrayRef<Expr *> VL);

  /// Creates an empty clause with \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPFlushClause *CreateEmpty(const ASTContext &C, unsigned N);

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPFlushClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_flush;
  }
};

/// This represents implicit clause 'depobj' for the '#pragma omp depobj'
/// directive.
/// This clause does not exist by itself, it can be only as a part of 'omp
/// depobj' directive. This clause is introduced to keep the original structure
/// of \a OMPExecutableDirective class and its derivatives and to use the
/// existing infrastructure of clauses with the list of variables.
///
/// \code
/// #pragma omp depobj(a) destroy
/// \endcode
/// In this example directive '#pragma omp depobj' has implicit clause 'depobj'
/// with the depobj 'a'.
class OMPDepobjClause final : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Chunk size.
  Expr *Depobj = nullptr;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPDepobjClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                  SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_depobj, StartLoc, EndLoc),
        LParenLoc(LParenLoc) {}

  /// Build an empty clause.
  ///
  explicit OMPDepobjClause()
      : OMPClause(llvm::omp::OMPC_depobj, SourceLocation(), SourceLocation()) {}

  void setDepobj(Expr *E) { Depobj = E; }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

public:
  /// Creates clause.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param Depobj depobj expression associated with the 'depobj' directive.
  static OMPDepobjClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation EndLoc, Expr *Depobj);

  /// Creates an empty clause.
  ///
  /// \param C AST context.
  static OMPDepobjClause *CreateEmpty(const ASTContext &C);

  /// Returns depobj expression associated with the clause.
  Expr *getDepobj() { return Depobj; }
  const Expr *getDepobj() const { return Depobj; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(&Depobj),
                       reinterpret_cast<Stmt **>(&Depobj) + 1);
  }

  const_child_range children() const {
    auto Children = const_cast<OMPDepobjClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_depobj;
  }
};

/// This represents implicit clause 'depend' for the '#pragma omp task'
/// directive.
///
/// \code
/// #pragma omp task depend(in:a,b)
/// \endcode
/// In this example directive '#pragma omp task' with clause 'depend' with the
/// variables 'a' and 'b' with dependency 'in'.
class OMPDependClause final
    : public OMPVarListClause<OMPDependClause>,
      private llvm::TrailingObjects<OMPDependClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

public:
  struct DependDataTy final {
    /// Dependency type (one of in, out, inout).
    OpenMPDependClauseKind DepKind = OMPC_DEPEND_unknown;

    /// Dependency type location.
    SourceLocation DepLoc;

    /// Colon location.
    SourceLocation ColonLoc;

    /// Location of 'omp_all_memory'.
    SourceLocation OmpAllMemoryLoc;
  };

private:
  /// Dependency type and source locations.
  DependDataTy Data;

  /// Number of loops, associated with the depend clause.
  unsigned NumLoops = 0;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  /// \param NumLoops Number of loops that is associated with this depend
  /// clause.
  OMPDependClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                  SourceLocation EndLoc, unsigned N, unsigned NumLoops)
      : OMPVarListClause<OMPDependClause>(llvm::omp::OMPC_depend, StartLoc,
                                          LParenLoc, EndLoc, N),
        NumLoops(NumLoops) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  /// \param NumLoops Number of loops that is associated with this depend
  /// clause.
  explicit OMPDependClause(unsigned N, unsigned NumLoops)
      : OMPVarListClause<OMPDependClause>(llvm::omp::OMPC_depend,
                                          SourceLocation(), SourceLocation(),
                                          SourceLocation(), N),
        NumLoops(NumLoops) {}

  /// Set dependency kind.
  void setDependencyKind(OpenMPDependClauseKind K) { Data.DepKind = K; }

  /// Set dependency kind and its location.
  void setDependencyLoc(SourceLocation Loc) { Data.DepLoc = Loc; }

  /// Set colon location.
  void setColonLoc(SourceLocation Loc) { Data.ColonLoc = Loc; }

  /// Set the 'omp_all_memory' location.
  void setOmpAllMemoryLoc(SourceLocation Loc) { Data.OmpAllMemoryLoc = Loc; }

  /// Sets optional dependency modifier.
  void setModifier(Expr *DepModifier);

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param Data Dependency type and source locations.
  /// \param VL List of references to the variables.
  /// \param NumLoops Number of loops that is associated with this depend
  /// clause.
  static OMPDependClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation EndLoc, DependDataTy Data,
                                 Expr *DepModifier, ArrayRef<Expr *> VL,
                                 unsigned NumLoops);

  /// Creates an empty clause with \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  /// \param NumLoops Number of loops that is associated with this depend
  /// clause.
  static OMPDependClause *CreateEmpty(const ASTContext &C, unsigned N,
                                      unsigned NumLoops);

  /// Get dependency type.
  OpenMPDependClauseKind getDependencyKind() const { return Data.DepKind; }

  /// Get dependency type location.
  SourceLocation getDependencyLoc() const { return Data.DepLoc; }

  /// Get colon location.
  SourceLocation getColonLoc() const { return Data.ColonLoc; }

  /// Get 'omp_all_memory' location.
  SourceLocation getOmpAllMemoryLoc() const { return Data.OmpAllMemoryLoc; }

  /// Return optional depend modifier.
  Expr *getModifier();
  const Expr *getModifier() const {
    return const_cast<OMPDependClause *>(this)->getModifier();
  }

  /// Get number of loops associated with the clause.
  unsigned getNumLoops() const { return NumLoops; }

  /// Set the loop data for the depend clauses with 'sink|source' kind of
  /// dependency.
  void setLoopData(unsigned NumLoop, Expr *Cnt);

  /// Get the loop data.
  Expr *getLoopData(unsigned NumLoop);
  const Expr *getLoopData(unsigned NumLoop) const;

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPDependClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_depend;
  }
};

/// This represents 'device' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp target device(a)
/// \endcode
/// In this example directive '#pragma omp target' has clause 'device'
/// with single expression 'a'.
class OMPDeviceClause : public OMPClause, public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Device clause modifier.
  OpenMPDeviceClauseModifier Modifier = OMPC_DEVICE_unknown;

  /// Location of the modifier.
  SourceLocation ModifierLoc;

  /// Device number.
  Stmt *Device = nullptr;

  /// Set the device number.
  ///
  /// \param E Device number.
  void setDevice(Expr *E) { Device = E; }

  /// Sets modifier.
  void setModifier(OpenMPDeviceClauseModifier M) { Modifier = M; }

  /// Setst modifier location.
  void setModifierLoc(SourceLocation Loc) { ModifierLoc = Loc; }

public:
  /// Build 'device' clause.
  ///
  /// \param Modifier Clause modifier.
  /// \param E Expression associated with this clause.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param ModifierLoc Modifier location.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPDeviceClause(OpenMPDeviceClauseModifier Modifier, Expr *E, Stmt *HelperE,
                  OpenMPDirectiveKind CaptureRegion, SourceLocation StartLoc,
                  SourceLocation LParenLoc, SourceLocation ModifierLoc,
                  SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_device, StartLoc, EndLoc),
        OMPClauseWithPreInit(this), LParenLoc(LParenLoc), Modifier(Modifier),
        ModifierLoc(ModifierLoc), Device(E) {
    setPreInitStmt(HelperE, CaptureRegion);
  }

  /// Build an empty clause.
  OMPDeviceClause()
      : OMPClause(llvm::omp::OMPC_device, SourceLocation(), SourceLocation()),
        OMPClauseWithPreInit(this) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Return device number.
  Expr *getDevice() { return cast<Expr>(Device); }

  /// Return device number.
  Expr *getDevice() const { return cast<Expr>(Device); }

  /// Gets modifier.
  OpenMPDeviceClauseModifier getModifier() const { return Modifier; }

  /// Gets modifier location.
  SourceLocation getModifierLoc() const { return ModifierLoc; }

  child_range children() { return child_range(&Device, &Device + 1); }

  const_child_range children() const {
    return const_child_range(&Device, &Device + 1);
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_device;
  }
};

/// This represents 'threads' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp ordered threads
/// \endcode
/// In this example directive '#pragma omp ordered' has simple 'threads' clause.
class OMPThreadsClause final
    : public OMPNoChildClause<llvm::omp::OMPC_threads> {
public:
  /// Build 'threads' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPThreadsClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPNoChildClause(StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPThreadsClause() : OMPNoChildClause() {}
};

/// This represents 'simd' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp ordered simd
/// \endcode
/// In this example directive '#pragma omp ordered' has simple 'simd' clause.
class OMPSIMDClause : public OMPClause {
public:
  /// Build 'simd' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPSIMDClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_simd, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPSIMDClause()
      : OMPClause(llvm::omp::OMPC_simd, SourceLocation(), SourceLocation()) {}

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_simd;
  }
};

/// Struct that defines common infrastructure to handle mappable
/// expressions used in OpenMP clauses.
class OMPClauseMappableExprCommon {
public:
  /// Class that represents a component of a mappable expression. E.g.
  /// for an expression S.a, the first component is a declaration reference
  /// expression associated with 'S' and the second is a member expression
  /// associated with the field declaration 'a'. If the expression is an array
  /// subscript it may not have any associated declaration. In that case the
  /// associated declaration is set to nullptr.
  class MappableComponent {
    /// Pair of Expression and Non-contiguous pair  associated with the
    /// component.
    llvm::PointerIntPair<Expr *, 1, bool> AssociatedExpressionNonContiguousPr;

    /// Declaration associated with the declaration. If the component does
    /// not have a declaration (e.g. array subscripts or section), this is set
    /// to nullptr.
    ValueDecl *AssociatedDeclaration = nullptr;

  public:
    explicit MappableComponent() = default;
    explicit MappableComponent(Expr *AssociatedExpression,
                               ValueDecl *AssociatedDeclaration,
                               bool IsNonContiguous)
        : AssociatedExpressionNonContiguousPr(AssociatedExpression,
                                              IsNonContiguous),
          AssociatedDeclaration(
              AssociatedDeclaration
                  ? cast<ValueDecl>(AssociatedDeclaration->getCanonicalDecl())
                  : nullptr) {}

    Expr *getAssociatedExpression() const {
      return AssociatedExpressionNonContiguousPr.getPointer();
    }

    bool isNonContiguous() const {
      return AssociatedExpressionNonContiguousPr.getInt();
    }

    ValueDecl *getAssociatedDeclaration() const {
      return AssociatedDeclaration;
    }
  };

  // List of components of an expression. This first one is the whole
  // expression and the last one is the base expression.
  using MappableExprComponentList = SmallVector<MappableComponent, 8>;
  using MappableExprComponentListRef = ArrayRef<MappableComponent>;

  // List of all component lists associated to the same base declaration.
  // E.g. if both 'S.a' and 'S.b' are a mappable expressions, each will have
  // their component list but the same base declaration 'S'.
  using MappableExprComponentLists = SmallVector<MappableExprComponentList, 8>;
  using MappableExprComponentListsRef = ArrayRef<MappableExprComponentList>;

protected:
  // Return the total number of elements in a list of component lists.
  static unsigned
  getComponentsTotalNumber(MappableExprComponentListsRef ComponentLists);

  // Return the total number of elements in a list of declarations. All
  // declarations are expected to be canonical.
  static unsigned
  getUniqueDeclarationsTotalNumber(ArrayRef<const ValueDecl *> Declarations);
};

/// This structure contains all sizes needed for by an
/// OMPMappableExprListClause.
struct OMPMappableExprListSizeTy {
  /// Number of expressions listed.
  unsigned NumVars;
  /// Number of unique base declarations.
  unsigned NumUniqueDeclarations;
  /// Number of component lists.
  unsigned NumComponentLists;
  /// Total number of expression components.
  unsigned NumComponents;
  OMPMappableExprListSizeTy() = default;
  OMPMappableExprListSizeTy(unsigned NumVars, unsigned NumUniqueDeclarations,
                            unsigned NumComponentLists, unsigned NumComponents)
      : NumVars(NumVars), NumUniqueDeclarations(NumUniqueDeclarations),
        NumComponentLists(NumComponentLists), NumComponents(NumComponents) {}
};

/// This represents clauses with a list of expressions that are mappable.
/// Examples of these clauses are 'map' in
/// '#pragma omp target [enter|exit] [data]...' directives, and  'to' and 'from
/// in '#pragma omp target update...' directives.
template <class T>
class OMPMappableExprListClause : public OMPVarListClause<T>,
                                  public OMPClauseMappableExprCommon {
  friend class OMPClauseReader;

  /// Number of unique declarations in this clause.
  unsigned NumUniqueDeclarations;

  /// Number of component lists in this clause.
  unsigned NumComponentLists;

  /// Total number of components in this clause.
  unsigned NumComponents;

  /// Whether this clause is possible to have user-defined mappers associated.
  /// It should be true for map, to, and from clauses, and false for
  /// use_device_ptr and is_device_ptr.
  const bool SupportsMapper;

  /// C++ nested name specifier for the associated user-defined mapper.
  NestedNameSpecifierLoc MapperQualifierLoc;

  /// The associated user-defined mapper identifier information.
  DeclarationNameInfo MapperIdInfo;

protected:
  /// Build a clause for \a NumUniqueDeclarations declarations, \a
  /// NumComponentLists total component lists, and \a NumComponents total
  /// components.
  ///
  /// \param K Kind of the clause.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  /// \param SupportsMapper Indicates whether this clause is possible to have
  /// user-defined mappers associated.
  /// \param MapperQualifierLocPtr C++ nested name specifier for the associated
  /// user-defined mapper.
  /// \param MapperIdInfoPtr The identifier of associated user-defined mapper.
  OMPMappableExprListClause(
      OpenMPClauseKind K, const OMPVarListLocTy &Locs,
      const OMPMappableExprListSizeTy &Sizes, bool SupportsMapper = false,
      NestedNameSpecifierLoc *MapperQualifierLocPtr = nullptr,
      DeclarationNameInfo *MapperIdInfoPtr = nullptr)
      : OMPVarListClause<T>(K, Locs.StartLoc, Locs.LParenLoc, Locs.EndLoc,
                            Sizes.NumVars),
        NumUniqueDeclarations(Sizes.NumUniqueDeclarations),
        NumComponentLists(Sizes.NumComponentLists),
        NumComponents(Sizes.NumComponents), SupportsMapper(SupportsMapper) {
    if (MapperQualifierLocPtr)
      MapperQualifierLoc = *MapperQualifierLocPtr;
    if (MapperIdInfoPtr)
      MapperIdInfo = *MapperIdInfoPtr;
  }

  /// Get the unique declarations that are in the trailing objects of the
  /// class.
  MutableArrayRef<ValueDecl *> getUniqueDeclsRef() {
    return MutableArrayRef<ValueDecl *>(
        static_cast<T *>(this)->template getTrailingObjects<ValueDecl *>(),
        NumUniqueDeclarations);
  }

  /// Get the unique declarations that are in the trailing objects of the
  /// class.
  ArrayRef<ValueDecl *> getUniqueDeclsRef() const {
    return ArrayRef<ValueDecl *>(
        static_cast<const T *>(this)
            ->template getTrailingObjects<ValueDecl *>(),
        NumUniqueDeclarations);
  }

  /// Set the unique declarations that are in the trailing objects of the
  /// class.
  void setUniqueDecls(ArrayRef<ValueDecl *> UDs) {
    assert(UDs.size() == NumUniqueDeclarations &&
           "Unexpected amount of unique declarations.");
    std::copy(UDs.begin(), UDs.end(), getUniqueDeclsRef().begin());
  }

  /// Get the number of lists per declaration that are in the trailing
  /// objects of the class.
  MutableArrayRef<unsigned> getDeclNumListsRef() {
    return MutableArrayRef<unsigned>(
        static_cast<T *>(this)->template getTrailingObjects<unsigned>(),
        NumUniqueDeclarations);
  }

  /// Get the number of lists per declaration that are in the trailing
  /// objects of the class.
  ArrayRef<unsigned> getDeclNumListsRef() const {
    return ArrayRef<unsigned>(
        static_cast<const T *>(this)->template getTrailingObjects<unsigned>(),
        NumUniqueDeclarations);
  }

  /// Set the number of lists per declaration that are in the trailing
  /// objects of the class.
  void setDeclNumLists(ArrayRef<unsigned> DNLs) {
    assert(DNLs.size() == NumUniqueDeclarations &&
           "Unexpected amount of list numbers.");
    std::copy(DNLs.begin(), DNLs.end(), getDeclNumListsRef().begin());
  }

  /// Get the cumulative component lists sizes that are in the trailing
  /// objects of the class. They are appended after the number of lists.
  MutableArrayRef<unsigned> getComponentListSizesRef() {
    return MutableArrayRef<unsigned>(
        static_cast<T *>(this)->template getTrailingObjects<unsigned>() +
            NumUniqueDeclarations,
        NumComponentLists);
  }

  /// Get the cumulative component lists sizes that are in the trailing
  /// objects of the class. They are appended after the number of lists.
  ArrayRef<unsigned> getComponentListSizesRef() const {
    return ArrayRef<unsigned>(
        static_cast<const T *>(this)->template getTrailingObjects<unsigned>() +
            NumUniqueDeclarations,
        NumComponentLists);
  }

  /// Set the cumulative component lists sizes that are in the trailing
  /// objects of the class.
  void setComponentListSizes(ArrayRef<unsigned> CLSs) {
    assert(CLSs.size() == NumComponentLists &&
           "Unexpected amount of component lists.");
    std::copy(CLSs.begin(), CLSs.end(), getComponentListSizesRef().begin());
  }

  /// Get the components that are in the trailing objects of the class.
  MutableArrayRef<MappableComponent> getComponentsRef() {
    return MutableArrayRef<MappableComponent>(
        static_cast<T *>(this)
            ->template getTrailingObjects<MappableComponent>(),
        NumComponents);
  }

  /// Get the components that are in the trailing objects of the class.
  ArrayRef<MappableComponent> getComponentsRef() const {
    return ArrayRef<MappableComponent>(
        static_cast<const T *>(this)
            ->template getTrailingObjects<MappableComponent>(),
        NumComponents);
  }

  /// Set the components that are in the trailing objects of the class.
  /// This requires the list sizes so that it can also fill the original
  /// expressions, which are the first component of each list.
  void setComponents(ArrayRef<MappableComponent> Components,
                     ArrayRef<unsigned> CLSs) {
    assert(Components.size() == NumComponents &&
           "Unexpected amount of component lists.");
    assert(CLSs.size() == NumComponentLists &&
           "Unexpected amount of list sizes.");
    std::copy(Components.begin(), Components.end(), getComponentsRef().begin());
  }

  /// Fill the clause information from the list of declarations and
  /// associated component lists.
  void setClauseInfo(ArrayRef<ValueDecl *> Declarations,
                     MappableExprComponentListsRef ComponentLists) {
    // Perform some checks to make sure the data sizes are consistent with the
    // information available when the clause was created.
    assert(getUniqueDeclarationsTotalNumber(Declarations) ==
               NumUniqueDeclarations &&
           "Unexpected number of mappable expression info entries!");
    assert(getComponentsTotalNumber(ComponentLists) == NumComponents &&
           "Unexpected total number of components!");
    assert(Declarations.size() == ComponentLists.size() &&
           "Declaration and component lists size is not consistent!");
    assert(Declarations.size() == NumComponentLists &&
           "Unexpected declaration and component lists size!");

    // Organize the components by declaration and retrieve the original
    // expression. Original expressions are always the first component of the
    // mappable component list.
    llvm::MapVector<ValueDecl *, SmallVector<MappableExprComponentListRef, 8>>
        ComponentListMap;
    {
      auto CI = ComponentLists.begin();
      for (auto DI = Declarations.begin(), DE = Declarations.end(); DI != DE;
           ++DI, ++CI) {
        assert(!CI->empty() && "Invalid component list!");
        ComponentListMap[*DI].push_back(*CI);
      }
    }

    // Iterators of the target storage.
    auto UniqueDeclarations = getUniqueDeclsRef();
    auto UDI = UniqueDeclarations.begin();

    auto DeclNumLists = getDeclNumListsRef();
    auto DNLI = DeclNumLists.begin();

    auto ComponentListSizes = getComponentListSizesRef();
    auto CLSI = ComponentListSizes.begin();

    auto Components = getComponentsRef();
    auto CI = Components.begin();

    // Variable to compute the accumulation of the number of components.
    unsigned PrevSize = 0u;

    // Scan all the declarations and associated component lists.
    for (auto &M : ComponentListMap) {
      // The declaration.
      auto *D = M.first;
      // The component lists.
      auto CL = M.second;

      // Initialize the entry.
      *UDI = D;
      ++UDI;

      *DNLI = CL.size();
      ++DNLI;

      // Obtain the cumulative sizes and concatenate all the components in the
      // reserved storage.
      for (auto C : CL) {
        // Accumulate with the previous size.
        PrevSize += C.size();

        // Save the size.
        *CLSI = PrevSize;
        ++CLSI;

        // Append components after the current components iterator.
        CI = std::copy(C.begin(), C.end(), CI);
      }
    }
  }

  /// Set the nested name specifier of associated user-defined mapper.
  void setMapperQualifierLoc(NestedNameSpecifierLoc NNSL) {
    MapperQualifierLoc = NNSL;
  }

  /// Set the name of associated user-defined mapper.
  void setMapperIdInfo(DeclarationNameInfo MapperId) {
    MapperIdInfo = MapperId;
  }

  /// Get the user-defined mapper references that are in the trailing objects of
  /// the class.
  MutableArrayRef<Expr *> getUDMapperRefs() {
    assert(SupportsMapper &&
           "Must be a clause that is possible to have user-defined mappers");
    return llvm::MutableArrayRef<Expr *>(
        static_cast<T *>(this)->template getTrailingObjects<Expr *>() +
            OMPVarListClause<T>::varlist_size(),
        OMPVarListClause<T>::varlist_size());
  }

  /// Get the user-defined mappers references that are in the trailing objects
  /// of the class.
  ArrayRef<Expr *> getUDMapperRefs() const {
    assert(SupportsMapper &&
           "Must be a clause that is possible to have user-defined mappers");
    return llvm::ArrayRef<Expr *>(
        static_cast<const T *>(this)->template getTrailingObjects<Expr *>() +
            OMPVarListClause<T>::varlist_size(),
        OMPVarListClause<T>::varlist_size());
  }

  /// Set the user-defined mappers that are in the trailing objects of the
  /// class.
  void setUDMapperRefs(ArrayRef<Expr *> DMDs) {
    assert(DMDs.size() == OMPVarListClause<T>::varlist_size() &&
           "Unexpected number of user-defined mappers.");
    assert(SupportsMapper &&
           "Must be a clause that is possible to have user-defined mappers");
    std::copy(DMDs.begin(), DMDs.end(), getUDMapperRefs().begin());
  }

public:
  /// Return the number of unique base declarations in this clause.
  unsigned getUniqueDeclarationsNum() const { return NumUniqueDeclarations; }

  /// Return the number of lists derived from the clause expressions.
  unsigned getTotalComponentListNum() const { return NumComponentLists; }

  /// Return the total number of components in all lists derived from the
  /// clause.
  unsigned getTotalComponentsNum() const { return NumComponents; }

  /// Gets the nested name specifier for associated user-defined mapper.
  NestedNameSpecifierLoc getMapperQualifierLoc() const {
    return MapperQualifierLoc;
  }

  /// Gets the name info for associated user-defined mapper.
  const DeclarationNameInfo &getMapperIdInfo() const { return MapperIdInfo; }

  /// Iterator that browse the components by lists. It also allows
  /// browsing components of a single declaration.
  class const_component_lists_iterator
      : public llvm::iterator_adaptor_base<
            const_component_lists_iterator,
            MappableExprComponentListRef::const_iterator,
            std::forward_iterator_tag, MappableComponent, ptrdiff_t,
            MappableComponent, MappableComponent> {
    // The declaration the iterator currently refers to.
    ArrayRef<ValueDecl *>::iterator DeclCur;

    // The list number associated with the current declaration.
    ArrayRef<unsigned>::iterator NumListsCur;

    // Whether this clause is possible to have user-defined mappers associated.
    const bool SupportsMapper;

    // The user-defined mapper associated with the current declaration.
    ArrayRef<Expr *>::iterator MapperCur;

    // Remaining lists for the current declaration.
    unsigned RemainingLists = 0;

    // The cumulative size of the previous list, or zero if there is no previous
    // list.
    unsigned PrevListSize = 0;

    // The cumulative sizes of the current list - it will delimit the remaining
    // range of interest.
    ArrayRef<unsigned>::const_iterator ListSizeCur;
    ArrayRef<unsigned>::const_iterator ListSizeEnd;

    // Iterator to the end of the components storage.
    MappableExprComponentListRef::const_iterator End;

  public:
    /// Construct an iterator that scans all lists.
    explicit const_component_lists_iterator(
        ArrayRef<ValueDecl *> UniqueDecls, ArrayRef<unsigned> DeclsListNum,
        ArrayRef<unsigned> CumulativeListSizes,
        MappableExprComponentListRef Components, bool SupportsMapper,
        ArrayRef<Expr *> Mappers)
        : const_component_lists_iterator::iterator_adaptor_base(
              Components.begin()),
          DeclCur(UniqueDecls.begin()), NumListsCur(DeclsListNum.begin()),
          SupportsMapper(SupportsMapper),
          ListSizeCur(CumulativeListSizes.begin()),
          ListSizeEnd(CumulativeListSizes.end()), End(Components.end()) {
      assert(UniqueDecls.size() == DeclsListNum.size() &&
             "Inconsistent number of declarations and list sizes!");
      if (!DeclsListNum.empty())
        RemainingLists = *NumListsCur;
      if (SupportsMapper)
        MapperCur = Mappers.begin();
    }

    /// Construct an iterator that scan lists for a given declaration \a
    /// Declaration.
    explicit const_component_lists_iterator(
        const ValueDecl *Declaration, ArrayRef<ValueDecl *> UniqueDecls,
        ArrayRef<unsigned> DeclsListNum, ArrayRef<unsigned> CumulativeListSizes,
        MappableExprComponentListRef Components, bool SupportsMapper,
        ArrayRef<Expr *> Mappers)
        : const_component_lists_iterator(UniqueDecls, DeclsListNum,
                                         CumulativeListSizes, Components,
                                         SupportsMapper, Mappers) {
      // Look for the desired declaration. While we are looking for it, we
      // update the state so that we know the component where a given list
      // starts.
      for (; DeclCur != UniqueDecls.end(); ++DeclCur, ++NumListsCur) {
        if (*DeclCur == Declaration)
          break;

        assert(*NumListsCur > 0 && "No lists associated with declaration??");

        // Skip the lists associated with the current declaration, but save the
        // last list size that was skipped.
        std::advance(ListSizeCur, *NumListsCur - 1);
        PrevListSize = *ListSizeCur;
        ++ListSizeCur;

        if (SupportsMapper)
          ++MapperCur;
      }

      // If we didn't find any declaration, advance the iterator to after the
      // last component and set remaining lists to zero.
      if (ListSizeCur == CumulativeListSizes.end()) {
        this->I = End;
        RemainingLists = 0u;
        return;
      }

      // Set the remaining lists with the total number of lists of the current
      // declaration.
      RemainingLists = *NumListsCur;

      // Adjust the list size end iterator to the end of the relevant range.
      ListSizeEnd = ListSizeCur;
      std::advance(ListSizeEnd, RemainingLists);

      // Given that the list sizes are cumulative, the index of the component
      // that start the list is the size of the previous list.
      std::advance(this->I, PrevListSize);
    }

    // Return the array with the current list. The sizes are cumulative, so the
    // array size is the difference between the current size and previous one.
    std::tuple<const ValueDecl *, MappableExprComponentListRef,
               const ValueDecl *>
    operator*() const {
      assert(ListSizeCur != ListSizeEnd && "Invalid iterator!");
      const ValueDecl *Mapper = nullptr;
      if (SupportsMapper && *MapperCur)
        Mapper = cast<ValueDecl>(cast<DeclRefExpr>(*MapperCur)->getDecl());
      return std::make_tuple(
          *DeclCur,
          MappableExprComponentListRef(&*this->I, *ListSizeCur - PrevListSize),
          Mapper);
    }
    std::tuple<const ValueDecl *, MappableExprComponentListRef,
               const ValueDecl *>
    operator->() const {
      return **this;
    }

    // Skip the components of the current list.
    const_component_lists_iterator &operator++() {
      assert(ListSizeCur != ListSizeEnd && RemainingLists &&
             "Invalid iterator!");

      // If we don't have more lists just skip all the components. Otherwise,
      // advance the iterator by the number of components in the current list.
      if (std::next(ListSizeCur) == ListSizeEnd) {
        this->I = End;
        RemainingLists = 0;
      } else {
        std::advance(this->I, *ListSizeCur - PrevListSize);
        PrevListSize = *ListSizeCur;

        // We are done with a declaration, move to the next one.
        if (!(--RemainingLists)) {
          ++DeclCur;
          ++NumListsCur;
          RemainingLists = *NumListsCur;
          assert(RemainingLists && "No lists in the following declaration??");
        }
      }

      ++ListSizeCur;
      if (SupportsMapper)
        ++MapperCur;
      return *this;
    }
  };

  using const_component_lists_range =
      llvm::iterator_range<const_component_lists_iterator>;

  /// Iterators for all component lists.
  const_component_lists_iterator component_lists_begin() const {
    return const_component_lists_iterator(
        getUniqueDeclsRef(), getDeclNumListsRef(), getComponentListSizesRef(),
        getComponentsRef(), SupportsMapper,
        SupportsMapper ? getUDMapperRefs() : std::nullopt);
  }
  const_component_lists_iterator component_lists_end() const {
    return const_component_lists_iterator(
        ArrayRef<ValueDecl *>(), ArrayRef<unsigned>(), ArrayRef<unsigned>(),
        MappableExprComponentListRef(getComponentsRef().end(),
                                     getComponentsRef().end()),
        SupportsMapper, std::nullopt);
  }
  const_component_lists_range component_lists() const {
    return {component_lists_begin(), component_lists_end()};
  }

  /// Iterators for component lists associated with the provided
  /// declaration.
  const_component_lists_iterator
  decl_component_lists_begin(const ValueDecl *VD) const {
    return const_component_lists_iterator(
        VD, getUniqueDeclsRef(), getDeclNumListsRef(),
        getComponentListSizesRef(), getComponentsRef(), SupportsMapper,
        SupportsMapper ? getUDMapperRefs() : std::nullopt);
  }
  const_component_lists_iterator decl_component_lists_end() const {
    return component_lists_end();
  }
  const_component_lists_range decl_component_lists(const ValueDecl *VD) const {
    return {decl_component_lists_begin(VD), decl_component_lists_end()};
  }

  /// Iterators to access all the declarations, number of lists, list sizes, and
  /// components.
  using const_all_decls_iterator = ArrayRef<ValueDecl *>::iterator;
  using const_all_decls_range = llvm::iterator_range<const_all_decls_iterator>;

  const_all_decls_range all_decls() const {
    auto A = getUniqueDeclsRef();
    return const_all_decls_range(A.begin(), A.end());
  }

  using const_all_num_lists_iterator = ArrayRef<unsigned>::iterator;
  using const_all_num_lists_range =
      llvm::iterator_range<const_all_num_lists_iterator>;

  const_all_num_lists_range all_num_lists() const {
    auto A = getDeclNumListsRef();
    return const_all_num_lists_range(A.begin(), A.end());
  }

  using const_all_lists_sizes_iterator = ArrayRef<unsigned>::iterator;
  using const_all_lists_sizes_range =
      llvm::iterator_range<const_all_lists_sizes_iterator>;

  const_all_lists_sizes_range all_lists_sizes() const {
    auto A = getComponentListSizesRef();
    return const_all_lists_sizes_range(A.begin(), A.end());
  }

  using const_all_components_iterator = ArrayRef<MappableComponent>::iterator;
  using const_all_components_range =
      llvm::iterator_range<const_all_components_iterator>;

  const_all_components_range all_components() const {
    auto A = getComponentsRef();
    return const_all_components_range(A.begin(), A.end());
  }

  using mapperlist_iterator = MutableArrayRef<Expr *>::iterator;
  using mapperlist_const_iterator = ArrayRef<const Expr *>::iterator;
  using mapperlist_range = llvm::iterator_range<mapperlist_iterator>;
  using mapperlist_const_range =
      llvm::iterator_range<mapperlist_const_iterator>;

  mapperlist_iterator mapperlist_begin() { return getUDMapperRefs().begin(); }
  mapperlist_iterator mapperlist_end() { return getUDMapperRefs().end(); }
  mapperlist_const_iterator mapperlist_begin() const {
    return getUDMapperRefs().begin();
  }
  mapperlist_const_iterator mapperlist_end() const {
    return getUDMapperRefs().end();
  }
  mapperlist_range mapperlists() {
    return mapperlist_range(mapperlist_begin(), mapperlist_end());
  }
  mapperlist_const_range mapperlists() const {
    return mapperlist_const_range(mapperlist_begin(), mapperlist_end());
  }
};

/// This represents clause 'map' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp target map(a,b)
/// \endcode
/// In this example directive '#pragma omp target' has clause 'map'
/// with the variables 'a' and 'b'.
class OMPMapClause final : public OMPMappableExprListClause<OMPMapClause>,
                           private llvm::TrailingObjects<
                               OMPMapClause, Expr *, ValueDecl *, unsigned,
                               OMPClauseMappableExprCommon::MappableComponent> {
  friend class OMPClauseReader;
  friend OMPMappableExprListClause;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Define the sizes of each trailing object array except the last one. This
  /// is required for TrailingObjects to work properly.
  size_t numTrailingObjects(OverloadToken<Expr *>) const {
    // There are varlist_size() of expressions, and varlist_size() of
    // user-defined mappers.
    return 2 * varlist_size() + 1;
  }
  size_t numTrailingObjects(OverloadToken<ValueDecl *>) const {
    return getUniqueDeclarationsNum();
  }
  size_t numTrailingObjects(OverloadToken<unsigned>) const {
    return getUniqueDeclarationsNum() + getTotalComponentListNum();
  }

private:
  /// Map-type-modifiers for the 'map' clause.
  OpenMPMapModifierKind MapTypeModifiers[NumberOfOMPMapClauseModifiers] = {
      OMPC_MAP_MODIFIER_unknown, OMPC_MAP_MODIFIER_unknown,
      OMPC_MAP_MODIFIER_unknown, OMPC_MAP_MODIFIER_unknown,
      OMPC_MAP_MODIFIER_unknown, OMPC_MAP_MODIFIER_unknown};

  /// Location of map-type-modifiers for the 'map' clause.
  SourceLocation MapTypeModifiersLoc[NumberOfOMPMapClauseModifiers];

  /// Map type for the 'map' clause.
  OpenMPMapClauseKind MapType = OMPC_MAP_unknown;

  /// Is this an implicit map type or not.
  bool MapTypeIsImplicit = false;

  /// Location of the map type.
  SourceLocation MapLoc;

  /// Colon location.
  SourceLocation ColonLoc;

  /// Build a clause for \a NumVars listed expressions, \a
  /// NumUniqueDeclarations declarations, \a NumComponentLists total component
  /// lists, and \a NumComponents total expression components.
  ///
  /// \param MapModifiers Map-type-modifiers.
  /// \param MapModifiersLoc Locations of map-type-modifiers.
  /// \param MapperQualifierLoc C++ nested name specifier for the associated
  /// user-defined mapper.
  /// \param MapperIdInfo The identifier of associated user-defined mapper.
  /// \param MapType Map type.
  /// \param MapTypeIsImplicit Map type is inferred implicitly.
  /// \param MapLoc Location of the map type.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPMapClause(ArrayRef<OpenMPMapModifierKind> MapModifiers,
                        ArrayRef<SourceLocation> MapModifiersLoc,
                        NestedNameSpecifierLoc MapperQualifierLoc,
                        DeclarationNameInfo MapperIdInfo,
                        OpenMPMapClauseKind MapType, bool MapTypeIsImplicit,
                        SourceLocation MapLoc, const OMPVarListLocTy &Locs,
                        const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_map, Locs, Sizes,
                                  /*SupportsMapper=*/true, &MapperQualifierLoc,
                                  &MapperIdInfo),
        MapType(MapType), MapTypeIsImplicit(MapTypeIsImplicit), MapLoc(MapLoc) {
    assert(std::size(MapTypeModifiers) == MapModifiers.size() &&
           "Unexpected number of map type modifiers.");
    llvm::copy(MapModifiers, std::begin(MapTypeModifiers));

    assert(std::size(MapTypeModifiersLoc) == MapModifiersLoc.size() &&
           "Unexpected number of map type modifier locations.");
    llvm::copy(MapModifiersLoc, std::begin(MapTypeModifiersLoc));
  }

  /// Build an empty clause.
  ///
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPMapClause(const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_map, OMPVarListLocTy(), Sizes,
                                  /*SupportsMapper=*/true) {}

  /// Set map-type-modifier for the clause.
  ///
  /// \param I index for map-type-modifier.
  /// \param T map-type-modifier for the clause.
  void setMapTypeModifier(unsigned I, OpenMPMapModifierKind T) {
    assert(I < NumberOfOMPMapClauseModifiers &&
           "Unexpected index to store map type modifier, exceeds array size.");
    MapTypeModifiers[I] = T;
  }

  /// Set location for the map-type-modifier.
  ///
  /// \param I index for map-type-modifier location.
  /// \param TLoc map-type-modifier location.
  void setMapTypeModifierLoc(unsigned I, SourceLocation TLoc) {
    assert(I < NumberOfOMPMapClauseModifiers &&
           "Index to store map type modifier location exceeds array size.");
    MapTypeModifiersLoc[I] = TLoc;
  }

  /// Set type for the clause.
  ///
  /// \param T Type for the clause.
  void setMapType(OpenMPMapClauseKind T) { MapType = T; }

  /// Set type location.
  ///
  /// \param TLoc Type location.
  void setMapLoc(SourceLocation TLoc) { MapLoc = TLoc; }

  /// Set colon location.
  void setColonLoc(SourceLocation Loc) { ColonLoc = Loc; }

  /// Set iterator modifier.
  void setIteratorModifier(Expr *IteratorModifier) {
    getTrailingObjects<Expr *>()[2 * varlist_size()] = IteratorModifier;
  }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Vars The original expression used in the clause.
  /// \param Declarations Declarations used in the clause.
  /// \param ComponentLists Component lists used in the clause.
  /// \param UDMapperRefs References to user-defined mappers associated with
  /// expressions used in the clause.
  /// \param IteratorModifier Iterator modifier.
  /// \param MapModifiers Map-type-modifiers.
  /// \param MapModifiersLoc Location of map-type-modifiers.
  /// \param UDMQualifierLoc C++ nested name specifier for the associated
  /// user-defined mapper.
  /// \param MapperId The identifier of associated user-defined mapper.
  /// \param Type Map type.
  /// \param TypeIsImplicit Map type is inferred implicitly.
  /// \param TypeLoc Location of the map type.
  static OMPMapClause *
  Create(const ASTContext &C, const OMPVarListLocTy &Locs,
         ArrayRef<Expr *> Vars, ArrayRef<ValueDecl *> Declarations,
         MappableExprComponentListsRef ComponentLists,
         ArrayRef<Expr *> UDMapperRefs, Expr *IteratorModifier,
         ArrayRef<OpenMPMapModifierKind> MapModifiers,
         ArrayRef<SourceLocation> MapModifiersLoc,
         NestedNameSpecifierLoc UDMQualifierLoc, DeclarationNameInfo MapperId,
         OpenMPMapClauseKind Type, bool TypeIsImplicit, SourceLocation TypeLoc);

  /// Creates an empty clause with the place for \a NumVars original
  /// expressions, \a NumUniqueDeclarations declarations, \NumComponentLists
  /// lists, and \a NumComponents expression components.
  ///
  /// \param C AST context.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  static OMPMapClause *CreateEmpty(const ASTContext &C,
                                   const OMPMappableExprListSizeTy &Sizes);

  /// Fetches Expr * of iterator modifier.
  Expr *getIteratorModifier() {
    return getTrailingObjects<Expr *>()[2 * varlist_size()];
  }

  /// Fetches mapping kind for the clause.
  OpenMPMapClauseKind getMapType() const LLVM_READONLY { return MapType; }

  /// Is this an implicit map type?
  /// We have to capture 'IsMapTypeImplicit' from the parser for more
  /// informative error messages.  It helps distinguish map(r) from
  /// map(tofrom: r), which is important to print more helpful error
  /// messages for some target directives.
  bool isImplicitMapType() const LLVM_READONLY { return MapTypeIsImplicit; }

  /// Fetches the map-type-modifier at 'Cnt' index of array of modifiers.
  ///
  /// \param Cnt index for map-type-modifier.
  OpenMPMapModifierKind getMapTypeModifier(unsigned Cnt) const LLVM_READONLY {
    assert(Cnt < NumberOfOMPMapClauseModifiers &&
           "Requested modifier exceeds the total number of modifiers.");
    return MapTypeModifiers[Cnt];
  }

  /// Fetches the map-type-modifier location at 'Cnt' index of array of
  /// modifiers' locations.
  ///
  /// \param Cnt index for map-type-modifier location.
  SourceLocation getMapTypeModifierLoc(unsigned Cnt) const LLVM_READONLY {
    assert(Cnt < NumberOfOMPMapClauseModifiers &&
           "Requested modifier location exceeds total number of modifiers.");
    return MapTypeModifiersLoc[Cnt];
  }

  /// Fetches ArrayRef of map-type-modifiers.
  ArrayRef<OpenMPMapModifierKind> getMapTypeModifiers() const LLVM_READONLY {
    return llvm::ArrayRef(MapTypeModifiers);
  }

  /// Fetches ArrayRef of location of map-type-modifiers.
  ArrayRef<SourceLocation> getMapTypeModifiersLoc() const LLVM_READONLY {
    return llvm::ArrayRef(MapTypeModifiersLoc);
  }

  /// Fetches location of clause mapping kind.
  SourceLocation getMapLoc() const LLVM_READONLY { return MapLoc; }

  /// Get colon location.
  SourceLocation getColonLoc() const { return ColonLoc; }

  child_range children() {
    return child_range(
        reinterpret_cast<Stmt **>(varlist_begin()),
        reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPMapClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    if (MapType == OMPC_MAP_to || MapType == OMPC_MAP_tofrom)
      return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                         reinterpret_cast<Stmt **>(varlist_end()));
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    auto Children = const_cast<OMPMapClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }


  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_map;
  }
};

/// This represents 'num_teams' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp teams num_teams(n)
/// \endcode
/// In this example directive '#pragma omp teams' has clause 'num_teams'
/// with single expression 'n'.
class OMPNumTeamsClause : public OMPClause, public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// NumTeams number.
  Stmt *NumTeams = nullptr;

  /// Set the NumTeams number.
  ///
  /// \param E NumTeams number.
  void setNumTeams(Expr *E) { NumTeams = E; }

public:
  /// Build 'num_teams' clause.
  ///
  /// \param E Expression associated with this clause.
  /// \param HelperE Helper Expression associated with this clause.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPNumTeamsClause(Expr *E, Stmt *HelperE, OpenMPDirectiveKind CaptureRegion,
                    SourceLocation StartLoc, SourceLocation LParenLoc,
                    SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_num_teams, StartLoc, EndLoc),
        OMPClauseWithPreInit(this), LParenLoc(LParenLoc), NumTeams(E) {
    setPreInitStmt(HelperE, CaptureRegion);
  }

  /// Build an empty clause.
  OMPNumTeamsClause()
      : OMPClause(llvm::omp::OMPC_num_teams, SourceLocation(),
                  SourceLocation()),
        OMPClauseWithPreInit(this) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Return NumTeams number.
  Expr *getNumTeams() { return cast<Expr>(NumTeams); }

  /// Return NumTeams number.
  Expr *getNumTeams() const { return cast<Expr>(NumTeams); }

  child_range children() { return child_range(&NumTeams, &NumTeams + 1); }

  const_child_range children() const {
    return const_child_range(&NumTeams, &NumTeams + 1);
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_num_teams;
  }
};

/// This represents 'thread_limit' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp teams thread_limit(n)
/// \endcode
/// In this example directive '#pragma omp teams' has clause 'thread_limit'
/// with single expression 'n'.
class OMPThreadLimitClause : public OMPClause, public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// ThreadLimit number.
  Stmt *ThreadLimit = nullptr;

  /// Set the ThreadLimit number.
  ///
  /// \param E ThreadLimit number.
  void setThreadLimit(Expr *E) { ThreadLimit = E; }

public:
  /// Build 'thread_limit' clause.
  ///
  /// \param E Expression associated with this clause.
  /// \param HelperE Helper Expression associated with this clause.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPThreadLimitClause(Expr *E, Stmt *HelperE,
                       OpenMPDirectiveKind CaptureRegion,
                       SourceLocation StartLoc, SourceLocation LParenLoc,
                       SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_thread_limit, StartLoc, EndLoc),
        OMPClauseWithPreInit(this), LParenLoc(LParenLoc), ThreadLimit(E) {
    setPreInitStmt(HelperE, CaptureRegion);
  }

  /// Build an empty clause.
  OMPThreadLimitClause()
      : OMPClause(llvm::omp::OMPC_thread_limit, SourceLocation(),
                  SourceLocation()),
        OMPClauseWithPreInit(this) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Return ThreadLimit number.
  Expr *getThreadLimit() { return cast<Expr>(ThreadLimit); }

  /// Return ThreadLimit number.
  Expr *getThreadLimit() const { return cast<Expr>(ThreadLimit); }

  child_range children() { return child_range(&ThreadLimit, &ThreadLimit + 1); }

  const_child_range children() const {
    return const_child_range(&ThreadLimit, &ThreadLimit + 1);
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_thread_limit;
  }
};

/// This represents 'priority' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp task priority(n)
/// \endcode
/// In this example directive '#pragma omp teams' has clause 'priority' with
/// single expression 'n'.
class OMPPriorityClause : public OMPClause, public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Priority number.
  Stmt *Priority = nullptr;

  /// Set the Priority number.
  ///
  /// \param E Priority number.
  void setPriority(Expr *E) { Priority = E; }

public:
  /// Build 'priority' clause.
  ///
  /// \param Priority Expression associated with this clause.
  /// \param HelperPriority Helper priority for the construct.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPPriorityClause(Expr *Priority, Stmt *HelperPriority,
                    OpenMPDirectiveKind CaptureRegion, SourceLocation StartLoc,
                    SourceLocation LParenLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_priority, StartLoc, EndLoc),
        OMPClauseWithPreInit(this), LParenLoc(LParenLoc), Priority(Priority) {
    setPreInitStmt(HelperPriority, CaptureRegion);
  }

  /// Build an empty clause.
  OMPPriorityClause()
      : OMPClause(llvm::omp::OMPC_priority, SourceLocation(), SourceLocation()),
        OMPClauseWithPreInit(this) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Return Priority number.
  Expr *getPriority() { return cast<Expr>(Priority); }

  /// Return Priority number.
  Expr *getPriority() const { return cast<Expr>(Priority); }

  child_range children() { return child_range(&Priority, &Priority + 1); }

  const_child_range children() const {
    return const_child_range(&Priority, &Priority + 1);
  }

  child_range used_children();
  const_child_range used_children() const {
    auto Children = const_cast<OMPPriorityClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_priority;
  }
};

/// This represents 'grainsize' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp taskloop grainsize(4)
/// \endcode
/// In this example directive '#pragma omp taskloop' has clause 'grainsize'
/// with single expression '4'.
class OMPGrainsizeClause : public OMPClause, public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Modifiers for 'grainsize' clause.
  OpenMPGrainsizeClauseModifier Modifier = OMPC_GRAINSIZE_unknown;

  /// Location of the modifier.
  SourceLocation ModifierLoc;

  /// Safe iteration space distance.
  Stmt *Grainsize = nullptr;

  /// Set safelen.
  void setGrainsize(Expr *Size) { Grainsize = Size; }

  /// Sets modifier.
  void setModifier(OpenMPGrainsizeClauseModifier M) { Modifier = M; }

  /// Sets modifier location.
  void setModifierLoc(SourceLocation Loc) { ModifierLoc = Loc; }

public:
  /// Build 'grainsize' clause.
  ///
  /// \param Modifier Clause modifier.
  /// \param Size Expression associated with this clause.
  /// \param HelperSize Helper grainsize for the construct.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param ModifierLoc Modifier location.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPGrainsizeClause(OpenMPGrainsizeClauseModifier Modifier, Expr *Size,
                     Stmt *HelperSize, OpenMPDirectiveKind CaptureRegion,
                     SourceLocation StartLoc, SourceLocation LParenLoc,
                     SourceLocation ModifierLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_grainsize, StartLoc, EndLoc),
        OMPClauseWithPreInit(this), LParenLoc(LParenLoc), Modifier(Modifier),
        ModifierLoc(ModifierLoc), Grainsize(Size) {
    setPreInitStmt(HelperSize, CaptureRegion);
  }

  /// Build an empty clause.
  explicit OMPGrainsizeClause()
      : OMPClause(llvm::omp::OMPC_grainsize, SourceLocation(),
                  SourceLocation()),
        OMPClauseWithPreInit(this) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Return safe iteration space distance.
  Expr *getGrainsize() const { return cast_or_null<Expr>(Grainsize); }

  /// Gets modifier.
  OpenMPGrainsizeClauseModifier getModifier() const { return Modifier; }

  /// Gets modifier location.
  SourceLocation getModifierLoc() const { return ModifierLoc; }

  child_range children() { return child_range(&Grainsize, &Grainsize + 1); }

  const_child_range children() const {
    return const_child_range(&Grainsize, &Grainsize + 1);
  }

  child_range used_children();
  const_child_range used_children() const {
    auto Children = const_cast<OMPGrainsizeClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_grainsize;
  }
};

/// This represents 'nogroup' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp taskloop nogroup
/// \endcode
/// In this example directive '#pragma omp taskloop' has 'nogroup' clause.
class OMPNogroupClause : public OMPClause {
public:
  /// Build 'nogroup' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPNogroupClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_nogroup, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPNogroupClause()
      : OMPClause(llvm::omp::OMPC_nogroup, SourceLocation(), SourceLocation()) {
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_nogroup;
  }
};

/// This represents 'num_tasks' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp taskloop num_tasks(4)
/// \endcode
/// In this example directive '#pragma omp taskloop' has clause 'num_tasks'
/// with single expression '4'.
class OMPNumTasksClause : public OMPClause, public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Modifiers for 'num_tasks' clause.
  OpenMPNumTasksClauseModifier Modifier = OMPC_NUMTASKS_unknown;

  /// Location of the modifier.
  SourceLocation ModifierLoc;

  /// Safe iteration space distance.
  Stmt *NumTasks = nullptr;

  /// Set safelen.
  void setNumTasks(Expr *Size) { NumTasks = Size; }

  /// Sets modifier.
  void setModifier(OpenMPNumTasksClauseModifier M) { Modifier = M; }

  /// Sets modifier location.
  void setModifierLoc(SourceLocation Loc) { ModifierLoc = Loc; }

public:
  /// Build 'num_tasks' clause.
  ///
  /// \param Modifier Clause modifier.
  /// \param Size Expression associated with this clause.
  /// \param HelperSize Helper grainsize for the construct.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  /// \param ModifierLoc Modifier location.
  /// \param LParenLoc Location of '('.
  OMPNumTasksClause(OpenMPNumTasksClauseModifier Modifier, Expr *Size,
                    Stmt *HelperSize, OpenMPDirectiveKind CaptureRegion,
                    SourceLocation StartLoc, SourceLocation LParenLoc,
                    SourceLocation ModifierLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_num_tasks, StartLoc, EndLoc),
        OMPClauseWithPreInit(this), LParenLoc(LParenLoc), Modifier(Modifier),
        ModifierLoc(ModifierLoc), NumTasks(Size) {
    setPreInitStmt(HelperSize, CaptureRegion);
  }

  /// Build an empty clause.
  explicit OMPNumTasksClause()
      : OMPClause(llvm::omp::OMPC_num_tasks, SourceLocation(),
                  SourceLocation()),
        OMPClauseWithPreInit(this) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Return safe iteration space distance.
  Expr *getNumTasks() const { return cast_or_null<Expr>(NumTasks); }

  /// Gets modifier.
  OpenMPNumTasksClauseModifier getModifier() const { return Modifier; }

  /// Gets modifier location.
  SourceLocation getModifierLoc() const { return ModifierLoc; }

  child_range children() { return child_range(&NumTasks, &NumTasks + 1); }

  const_child_range children() const {
    return const_child_range(&NumTasks, &NumTasks + 1);
  }

  child_range used_children();
  const_child_range used_children() const {
    auto Children = const_cast<OMPNumTasksClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_num_tasks;
  }
};

/// This represents 'hint' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp critical (name) hint(6)
/// \endcode
/// In this example directive '#pragma omp critical' has name 'name' and clause
/// 'hint' with argument '6'.
class OMPHintClause : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Hint expression of the 'hint' clause.
  Stmt *Hint = nullptr;

  /// Set hint expression.
  void setHint(Expr *H) { Hint = H; }

public:
  /// Build 'hint' clause with expression \a Hint.
  ///
  /// \param Hint Hint expression.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPHintClause(Expr *Hint, SourceLocation StartLoc, SourceLocation LParenLoc,
                SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_hint, StartLoc, EndLoc), LParenLoc(LParenLoc),
        Hint(Hint) {}

  /// Build an empty clause.
  OMPHintClause()
      : OMPClause(llvm::omp::OMPC_hint, SourceLocation(), SourceLocation()) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns number of threads.
  Expr *getHint() const { return cast_or_null<Expr>(Hint); }

  child_range children() { return child_range(&Hint, &Hint + 1); }

  const_child_range children() const {
    return const_child_range(&Hint, &Hint + 1);
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_hint;
  }
};

/// This represents 'dist_schedule' clause in the '#pragma omp ...'
/// directive.
///
/// \code
/// #pragma omp distribute dist_schedule(static, 3)
/// \endcode
/// In this example directive '#pragma omp distribute' has 'dist_schedule'
/// clause with arguments 'static' and '3'.
class OMPDistScheduleClause : public OMPClause, public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// A kind of the 'schedule' clause.
  OpenMPDistScheduleClauseKind Kind = OMPC_DIST_SCHEDULE_unknown;

  /// Start location of the schedule kind in source code.
  SourceLocation KindLoc;

  /// Location of ',' (if any).
  SourceLocation CommaLoc;

  /// Chunk size.
  Expr *ChunkSize = nullptr;

  /// Set schedule kind.
  ///
  /// \param K Schedule kind.
  void setDistScheduleKind(OpenMPDistScheduleClauseKind K) { Kind = K; }

  /// Sets the location of '('.
  ///
  /// \param Loc Location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Set schedule kind start location.
  ///
  /// \param KLoc Schedule kind location.
  void setDistScheduleKindLoc(SourceLocation KLoc) { KindLoc = KLoc; }

  /// Set location of ','.
  ///
  /// \param Loc Location of ','.
  void setCommaLoc(SourceLocation Loc) { CommaLoc = Loc; }

  /// Set chunk size.
  ///
  /// \param E Chunk size.
  void setChunkSize(Expr *E) { ChunkSize = E; }

public:
  /// Build 'dist_schedule' clause with schedule kind \a Kind and chunk
  /// size expression \a ChunkSize.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param KLoc Starting location of the argument.
  /// \param CommaLoc Location of ','.
  /// \param EndLoc Ending location of the clause.
  /// \param Kind DistSchedule kind.
  /// \param ChunkSize Chunk size.
  /// \param HelperChunkSize Helper chunk size for combined directives.
  OMPDistScheduleClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                        SourceLocation KLoc, SourceLocation CommaLoc,
                        SourceLocation EndLoc,
                        OpenMPDistScheduleClauseKind Kind, Expr *ChunkSize,
                        Stmt *HelperChunkSize)
      : OMPClause(llvm::omp::OMPC_dist_schedule, StartLoc, EndLoc),
        OMPClauseWithPreInit(this), LParenLoc(LParenLoc), Kind(Kind),
        KindLoc(KLoc), CommaLoc(CommaLoc), ChunkSize(ChunkSize) {
    setPreInitStmt(HelperChunkSize);
  }

  /// Build an empty clause.
  explicit OMPDistScheduleClause()
      : OMPClause(llvm::omp::OMPC_dist_schedule, SourceLocation(),
                  SourceLocation()),
        OMPClauseWithPreInit(this) {}

  /// Get kind of the clause.
  OpenMPDistScheduleClauseKind getDistScheduleKind() const { return Kind; }

  /// Get location of '('.
  SourceLocation getLParenLoc() { return LParenLoc; }

  /// Get kind location.
  SourceLocation getDistScheduleKindLoc() { return KindLoc; }

  /// Get location of ','.
  SourceLocation getCommaLoc() { return CommaLoc; }

  /// Get chunk size.
  Expr *getChunkSize() { return ChunkSize; }

  /// Get chunk size.
  const Expr *getChunkSize() const { return ChunkSize; }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(&ChunkSize),
                       reinterpret_cast<Stmt **>(&ChunkSize) + 1);
  }

  const_child_range children() const {
    auto Children = const_cast<OMPDistScheduleClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_dist_schedule;
  }
};

/// This represents 'defaultmap' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp target defaultmap(tofrom: scalar)
/// \endcode
/// In this example directive '#pragma omp target' has 'defaultmap' clause of kind
/// 'scalar' with modifier 'tofrom'.
class OMPDefaultmapClause : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Modifiers for 'defaultmap' clause.
  OpenMPDefaultmapClauseModifier Modifier = OMPC_DEFAULTMAP_MODIFIER_unknown;

  /// Locations of modifiers.
  SourceLocation ModifierLoc;

  /// A kind of the 'defaultmap' clause.
  OpenMPDefaultmapClauseKind Kind = OMPC_DEFAULTMAP_unknown;

  /// Start location of the defaultmap kind in source code.
  SourceLocation KindLoc;

  /// Set defaultmap kind.
  ///
  /// \param K Defaultmap kind.
  void setDefaultmapKind(OpenMPDefaultmapClauseKind K) { Kind = K; }

  /// Set the defaultmap modifier.
  ///
  /// \param M Defaultmap modifier.
  void setDefaultmapModifier(OpenMPDefaultmapClauseModifier M) {
    Modifier = M;
  }

  /// Set location of the defaultmap modifier.
  void setDefaultmapModifierLoc(SourceLocation Loc) {
    ModifierLoc = Loc;
  }

  /// Sets the location of '('.
  ///
  /// \param Loc Location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Set defaultmap kind start location.
  ///
  /// \param KLoc Defaultmap kind location.
  void setDefaultmapKindLoc(SourceLocation KLoc) { KindLoc = KLoc; }

public:
  /// Build 'defaultmap' clause with defaultmap kind \a Kind
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param KLoc Starting location of the argument.
  /// \param EndLoc Ending location of the clause.
  /// \param Kind Defaultmap kind.
  /// \param M The modifier applied to 'defaultmap' clause.
  /// \param MLoc Location of the modifier
  OMPDefaultmapClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                      SourceLocation MLoc, SourceLocation KLoc,
                      SourceLocation EndLoc, OpenMPDefaultmapClauseKind Kind,
                      OpenMPDefaultmapClauseModifier M)
      : OMPClause(llvm::omp::OMPC_defaultmap, StartLoc, EndLoc),
        LParenLoc(LParenLoc), Modifier(M), ModifierLoc(MLoc), Kind(Kind),
        KindLoc(KLoc) {}

  /// Build an empty clause.
  explicit OMPDefaultmapClause()
      : OMPClause(llvm::omp::OMPC_defaultmap, SourceLocation(),
                  SourceLocation()) {}

  /// Get kind of the clause.
  OpenMPDefaultmapClauseKind getDefaultmapKind() const { return Kind; }

  /// Get the modifier of the clause.
  OpenMPDefaultmapClauseModifier getDefaultmapModifier() const {
    return Modifier;
  }

  /// Get location of '('.
  SourceLocation getLParenLoc() { return LParenLoc; }

  /// Get kind location.
  SourceLocation getDefaultmapKindLoc() { return KindLoc; }

  /// Get the modifier location.
  SourceLocation getDefaultmapModifierLoc() const {
    return ModifierLoc;
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_defaultmap;
  }
};

/// This represents clause 'to' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp target update to(a,b)
/// \endcode
/// In this example directive '#pragma omp target update' has clause 'to'
/// with the variables 'a' and 'b'.
class OMPToClause final : public OMPMappableExprListClause<OMPToClause>,
                          private llvm::TrailingObjects<
                              OMPToClause, Expr *, ValueDecl *, unsigned,
                              OMPClauseMappableExprCommon::MappableComponent> {
  friend class OMPClauseReader;
  friend OMPMappableExprListClause;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Motion-modifiers for the 'to' clause.
  OpenMPMotionModifierKind MotionModifiers[NumberOfOMPMotionModifiers] = {
      OMPC_MOTION_MODIFIER_unknown, OMPC_MOTION_MODIFIER_unknown};

  /// Location of motion-modifiers for the 'to' clause.
  SourceLocation MotionModifiersLoc[NumberOfOMPMotionModifiers];

  /// Colon location.
  SourceLocation ColonLoc;

  /// Build clause with number of variables \a NumVars.
  ///
  /// \param TheMotionModifiers Motion-modifiers.
  /// \param TheMotionModifiersLoc Locations of motion-modifiers.
  /// \param MapperQualifierLoc C++ nested name specifier for the associated
  /// user-defined mapper.
  /// \param MapperIdInfo The identifier of associated user-defined mapper.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPToClause(ArrayRef<OpenMPMotionModifierKind> TheMotionModifiers,
                       ArrayRef<SourceLocation> TheMotionModifiersLoc,
                       NestedNameSpecifierLoc MapperQualifierLoc,
                       DeclarationNameInfo MapperIdInfo,
                       const OMPVarListLocTy &Locs,
                       const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_to, Locs, Sizes,
                                  /*SupportsMapper=*/true, &MapperQualifierLoc,
                                  &MapperIdInfo) {
    assert(std::size(MotionModifiers) == TheMotionModifiers.size() &&
           "Unexpected number of motion modifiers.");
    llvm::copy(TheMotionModifiers, std::begin(MotionModifiers));

    assert(std::size(MotionModifiersLoc) == TheMotionModifiersLoc.size() &&
           "Unexpected number of motion modifier locations.");
    llvm::copy(TheMotionModifiersLoc, std::begin(MotionModifiersLoc));
  }

  /// Build an empty clause.
  ///
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPToClause(const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_to, OMPVarListLocTy(), Sizes,
                                  /*SupportsMapper=*/true) {}

  /// Set motion-modifier for the clause.
  ///
  /// \param I index for motion-modifier.
  /// \param T motion-modifier for the clause.
  void setMotionModifier(unsigned I, OpenMPMotionModifierKind T) {
    assert(I < NumberOfOMPMotionModifiers &&
           "Unexpected index to store motion modifier, exceeds array size.");
    MotionModifiers[I] = T;
  }

  /// Set location for the motion-modifier.
  ///
  /// \param I index for motion-modifier location.
  /// \param TLoc motion-modifier location.
  void setMotionModifierLoc(unsigned I, SourceLocation TLoc) {
    assert(I < NumberOfOMPMotionModifiers &&
           "Index to store motion modifier location exceeds array size.");
    MotionModifiersLoc[I] = TLoc;
  }

  /// Set colon location.
  void setColonLoc(SourceLocation Loc) { ColonLoc = Loc; }

  /// Define the sizes of each trailing object array except the last one. This
  /// is required for TrailingObjects to work properly.
  size_t numTrailingObjects(OverloadToken<Expr *>) const {
    // There are varlist_size() of expressions, and varlist_size() of
    // user-defined mappers.
    return 2 * varlist_size();
  }
  size_t numTrailingObjects(OverloadToken<ValueDecl *>) const {
    return getUniqueDeclarationsNum();
  }
  size_t numTrailingObjects(OverloadToken<unsigned>) const {
    return getUniqueDeclarationsNum() + getTotalComponentListNum();
  }

public:
  /// Creates clause with a list of variables \a Vars.
  ///
  /// \param C AST context.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Vars The original expression used in the clause.
  /// \param Declarations Declarations used in the clause.
  /// \param ComponentLists Component lists used in the clause.
  /// \param MotionModifiers Motion-modifiers.
  /// \param MotionModifiersLoc Location of motion-modifiers.
  /// \param UDMapperRefs References to user-defined mappers associated with
  /// expressions used in the clause.
  /// \param UDMQualifierLoc C++ nested name specifier for the associated
  /// user-defined mapper.
  /// \param MapperId The identifier of associated user-defined mapper.
  static OMPToClause *Create(const ASTContext &C, const OMPVarListLocTy &Locs,
                             ArrayRef<Expr *> Vars,
                             ArrayRef<ValueDecl *> Declarations,
                             MappableExprComponentListsRef ComponentLists,
                             ArrayRef<Expr *> UDMapperRefs,
                             ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
                             ArrayRef<SourceLocation> MotionModifiersLoc,
                             NestedNameSpecifierLoc UDMQualifierLoc,
                             DeclarationNameInfo MapperId);

  /// Creates an empty clause with the place for \a NumVars variables.
  ///
  /// \param C AST context.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  static OMPToClause *CreateEmpty(const ASTContext &C,
                                  const OMPMappableExprListSizeTy &Sizes);

  /// Fetches the motion-modifier at 'Cnt' index of array of modifiers.
  ///
  /// \param Cnt index for motion-modifier.
  OpenMPMotionModifierKind getMotionModifier(unsigned Cnt) const LLVM_READONLY {
    assert(Cnt < NumberOfOMPMotionModifiers &&
           "Requested modifier exceeds the total number of modifiers.");
    return MotionModifiers[Cnt];
  }

  /// Fetches the motion-modifier location at 'Cnt' index of array of modifiers'
  /// locations.
  ///
  /// \param Cnt index for motion-modifier location.
  SourceLocation getMotionModifierLoc(unsigned Cnt) const LLVM_READONLY {
    assert(Cnt < NumberOfOMPMotionModifiers &&
           "Requested modifier location exceeds total number of modifiers.");
    return MotionModifiersLoc[Cnt];
  }

  /// Fetches ArrayRef of motion-modifiers.
  ArrayRef<OpenMPMotionModifierKind> getMotionModifiers() const LLVM_READONLY {
    return llvm::ArrayRef(MotionModifiers);
  }

  /// Fetches ArrayRef of location of motion-modifiers.
  ArrayRef<SourceLocation> getMotionModifiersLoc() const LLVM_READONLY {
    return llvm::ArrayRef(MotionModifiersLoc);
  }

  /// Get colon location.
  SourceLocation getColonLoc() const { return ColonLoc; }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPToClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_to;
  }
};

/// This represents clause 'from' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp target update from(a,b)
/// \endcode
/// In this example directive '#pragma omp target update' has clause 'from'
/// with the variables 'a' and 'b'.
class OMPFromClause final
    : public OMPMappableExprListClause<OMPFromClause>,
      private llvm::TrailingObjects<
          OMPFromClause, Expr *, ValueDecl *, unsigned,
          OMPClauseMappableExprCommon::MappableComponent> {
  friend class OMPClauseReader;
  friend OMPMappableExprListClause;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Motion-modifiers for the 'from' clause.
  OpenMPMotionModifierKind MotionModifiers[NumberOfOMPMotionModifiers] = {
      OMPC_MOTION_MODIFIER_unknown, OMPC_MOTION_MODIFIER_unknown};

  /// Location of motion-modifiers for the 'from' clause.
  SourceLocation MotionModifiersLoc[NumberOfOMPMotionModifiers];

  /// Colon location.
  SourceLocation ColonLoc;

  /// Build clause with number of variables \a NumVars.
  ///
  /// \param TheMotionModifiers Motion-modifiers.
  /// \param TheMotionModifiersLoc Locations of motion-modifiers.
  /// \param MapperQualifierLoc C++ nested name specifier for the associated
  /// user-defined mapper.
  /// \param MapperIdInfo The identifier of associated user-defined mapper.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPFromClause(ArrayRef<OpenMPMotionModifierKind> TheMotionModifiers,
                         ArrayRef<SourceLocation> TheMotionModifiersLoc,
                         NestedNameSpecifierLoc MapperQualifierLoc,
                         DeclarationNameInfo MapperIdInfo,
                         const OMPVarListLocTy &Locs,
                         const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_from, Locs, Sizes,
                                  /*SupportsMapper=*/true, &MapperQualifierLoc,
                                  &MapperIdInfo) {
    assert(std::size(MotionModifiers) == TheMotionModifiers.size() &&
           "Unexpected number of motion modifiers.");
    llvm::copy(TheMotionModifiers, std::begin(MotionModifiers));

    assert(std::size(MotionModifiersLoc) == TheMotionModifiersLoc.size() &&
           "Unexpected number of motion modifier locations.");
    llvm::copy(TheMotionModifiersLoc, std::begin(MotionModifiersLoc));
  }

  /// Build an empty clause.
  ///
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPFromClause(const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_from, OMPVarListLocTy(),
                                  Sizes, /*SupportsMapper=*/true) {}

  /// Set motion-modifier for the clause.
  ///
  /// \param I index for motion-modifier.
  /// \param T motion-modifier for the clause.
  void setMotionModifier(unsigned I, OpenMPMotionModifierKind T) {
    assert(I < NumberOfOMPMotionModifiers &&
           "Unexpected index to store motion modifier, exceeds array size.");
    MotionModifiers[I] = T;
  }

  /// Set location for the motion-modifier.
  ///
  /// \param I index for motion-modifier location.
  /// \param TLoc motion-modifier location.
  void setMotionModifierLoc(unsigned I, SourceLocation TLoc) {
    assert(I < NumberOfOMPMotionModifiers &&
           "Index to store motion modifier location exceeds array size.");
    MotionModifiersLoc[I] = TLoc;
  }

  /// Set colon location.
  void setColonLoc(SourceLocation Loc) { ColonLoc = Loc; }

  /// Define the sizes of each trailing object array except the last one. This
  /// is required for TrailingObjects to work properly.
  size_t numTrailingObjects(OverloadToken<Expr *>) const {
    // There are varlist_size() of expressions, and varlist_size() of
    // user-defined mappers.
    return 2 * varlist_size();
  }
  size_t numTrailingObjects(OverloadToken<ValueDecl *>) const {
    return getUniqueDeclarationsNum();
  }
  size_t numTrailingObjects(OverloadToken<unsigned>) const {
    return getUniqueDeclarationsNum() + getTotalComponentListNum();
  }

public:
  /// Creates clause with a list of variables \a Vars.
  ///
  /// \param C AST context.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Vars The original expression used in the clause.
  /// \param Declarations Declarations used in the clause.
  /// \param ComponentLists Component lists used in the clause.
  /// \param MotionModifiers Motion-modifiers.
  /// \param MotionModifiersLoc Location of motion-modifiers.
  /// \param UDMapperRefs References to user-defined mappers associated with
  /// expressions used in the clause.
  /// \param UDMQualifierLoc C++ nested name specifier for the associated
  /// user-defined mapper.
  /// \param MapperId The identifier of associated user-defined mapper.
  static OMPFromClause *
  Create(const ASTContext &C, const OMPVarListLocTy &Locs,
         ArrayRef<Expr *> Vars, ArrayRef<ValueDecl *> Declarations,
         MappableExprComponentListsRef ComponentLists,
         ArrayRef<Expr *> UDMapperRefs,
         ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
         ArrayRef<SourceLocation> MotionModifiersLoc,
         NestedNameSpecifierLoc UDMQualifierLoc, DeclarationNameInfo MapperId);

  /// Creates an empty clause with the place for \a NumVars variables.
  ///
  /// \param C AST context.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  static OMPFromClause *CreateEmpty(const ASTContext &C,
                                    const OMPMappableExprListSizeTy &Sizes);

  /// Fetches the motion-modifier at 'Cnt' index of array of modifiers.
  ///
  /// \param Cnt index for motion-modifier.
  OpenMPMotionModifierKind getMotionModifier(unsigned Cnt) const LLVM_READONLY {
    assert(Cnt < NumberOfOMPMotionModifiers &&
           "Requested modifier exceeds the total number of modifiers.");
    return MotionModifiers[Cnt];
  }

  /// Fetches the motion-modifier location at 'Cnt' index of array of modifiers'
  /// locations.
  ///
  /// \param Cnt index for motion-modifier location.
  SourceLocation getMotionModifierLoc(unsigned Cnt) const LLVM_READONLY {
    assert(Cnt < NumberOfOMPMotionModifiers &&
           "Requested modifier location exceeds total number of modifiers.");
    return MotionModifiersLoc[Cnt];
  }

  /// Fetches ArrayRef of motion-modifiers.
  ArrayRef<OpenMPMotionModifierKind> getMotionModifiers() const LLVM_READONLY {
    return llvm::ArrayRef(MotionModifiers);
  }

  /// Fetches ArrayRef of location of motion-modifiers.
  ArrayRef<SourceLocation> getMotionModifiersLoc() const LLVM_READONLY {
    return llvm::ArrayRef(MotionModifiersLoc);
  }

  /// Get colon location.
  SourceLocation getColonLoc() const { return ColonLoc; }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPFromClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_from;
  }
};

/// This represents clause 'use_device_ptr' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp target data use_device_ptr(a,b)
/// \endcode
/// In this example directive '#pragma omp target data' has clause
/// 'use_device_ptr' with the variables 'a' and 'b'.
class OMPUseDevicePtrClause final
    : public OMPMappableExprListClause<OMPUseDevicePtrClause>,
      private llvm::TrailingObjects<
          OMPUseDevicePtrClause, Expr *, ValueDecl *, unsigned,
          OMPClauseMappableExprCommon::MappableComponent> {
  friend class OMPClauseReader;
  friend OMPMappableExprListClause;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a NumVars.
  ///
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPUseDevicePtrClause(const OMPVarListLocTy &Locs,
                                 const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_use_device_ptr, Locs, Sizes) {
  }

  /// Build an empty clause.
  ///
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPUseDevicePtrClause(const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_use_device_ptr,
                                  OMPVarListLocTy(), Sizes) {}

  /// Define the sizes of each trailing object array except the last one. This
  /// is required for TrailingObjects to work properly.
  size_t numTrailingObjects(OverloadToken<Expr *>) const {
    return 3 * varlist_size();
  }
  size_t numTrailingObjects(OverloadToken<ValueDecl *>) const {
    return getUniqueDeclarationsNum();
  }
  size_t numTrailingObjects(OverloadToken<unsigned>) const {
    return getUniqueDeclarationsNum() + getTotalComponentListNum();
  }

  /// Sets the list of references to private copies with initializers for new
  /// private variables.
  /// \param VL List of references.
  void setPrivateCopies(ArrayRef<Expr *> VL);

  /// Gets the list of references to private copies with initializers for new
  /// private variables.
  MutableArrayRef<Expr *> getPrivateCopies() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getPrivateCopies() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

  /// Sets the list of references to initializer variables for new private
  /// variables.
  /// \param VL List of references.
  void setInits(ArrayRef<Expr *> VL);

  /// Gets the list of references to initializer variables for new private
  /// variables.
  MutableArrayRef<Expr *> getInits() {
    return MutableArrayRef<Expr *>(getPrivateCopies().end(), varlist_size());
  }
  ArrayRef<const Expr *> getInits() const {
    return llvm::ArrayRef(getPrivateCopies().end(), varlist_size());
  }

public:
  /// Creates clause with a list of variables \a Vars.
  ///
  /// \param C AST context.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Vars The original expression used in the clause.
  /// \param PrivateVars Expressions referring to private copies.
  /// \param Inits Expressions referring to private copy initializers.
  /// \param Declarations Declarations used in the clause.
  /// \param ComponentLists Component lists used in the clause.
  static OMPUseDevicePtrClause *
  Create(const ASTContext &C, const OMPVarListLocTy &Locs,
         ArrayRef<Expr *> Vars, ArrayRef<Expr *> PrivateVars,
         ArrayRef<Expr *> Inits, ArrayRef<ValueDecl *> Declarations,
         MappableExprComponentListsRef ComponentLists);

  /// Creates an empty clause with the place for \a NumVars variables.
  ///
  /// \param C AST context.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  static OMPUseDevicePtrClause *
  CreateEmpty(const ASTContext &C, const OMPMappableExprListSizeTy &Sizes);

  using private_copies_iterator = MutableArrayRef<Expr *>::iterator;
  using private_copies_const_iterator = ArrayRef<const Expr *>::iterator;
  using private_copies_range = llvm::iterator_range<private_copies_iterator>;
  using private_copies_const_range =
      llvm::iterator_range<private_copies_const_iterator>;

  private_copies_range private_copies() {
    return private_copies_range(getPrivateCopies().begin(),
                                getPrivateCopies().end());
  }

  private_copies_const_range private_copies() const {
    return private_copies_const_range(getPrivateCopies().begin(),
                                      getPrivateCopies().end());
  }

  using inits_iterator = MutableArrayRef<Expr *>::iterator;
  using inits_const_iterator = ArrayRef<const Expr *>::iterator;
  using inits_range = llvm::iterator_range<inits_iterator>;
  using inits_const_range = llvm::iterator_range<inits_const_iterator>;

  inits_range inits() {
    return inits_range(getInits().begin(), getInits().end());
  }

  inits_const_range inits() const {
    return inits_const_range(getInits().begin(), getInits().end());
  }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPUseDevicePtrClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_use_device_ptr;
  }
};

/// This represents clause 'use_device_addr' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp target data use_device_addr(a,b)
/// \endcode
/// In this example directive '#pragma omp target data' has clause
/// 'use_device_addr' with the variables 'a' and 'b'.
class OMPUseDeviceAddrClause final
    : public OMPMappableExprListClause<OMPUseDeviceAddrClause>,
      private llvm::TrailingObjects<
          OMPUseDeviceAddrClause, Expr *, ValueDecl *, unsigned,
          OMPClauseMappableExprCommon::MappableComponent> {
  friend class OMPClauseReader;
  friend OMPMappableExprListClause;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a NumVars.
  ///
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPUseDeviceAddrClause(const OMPVarListLocTy &Locs,
                                  const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_use_device_addr, Locs,
                                  Sizes) {}

  /// Build an empty clause.
  ///
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPUseDeviceAddrClause(const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_use_device_addr,
                                  OMPVarListLocTy(), Sizes) {}

  /// Define the sizes of each trailing object array except the last one. This
  /// is required for TrailingObjects to work properly.
  size_t numTrailingObjects(OverloadToken<Expr *>) const {
    return varlist_size();
  }
  size_t numTrailingObjects(OverloadToken<ValueDecl *>) const {
    return getUniqueDeclarationsNum();
  }
  size_t numTrailingObjects(OverloadToken<unsigned>) const {
    return getUniqueDeclarationsNum() + getTotalComponentListNum();
  }

public:
  /// Creates clause with a list of variables \a Vars.
  ///
  /// \param C AST context.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Vars The original expression used in the clause.
  /// \param Declarations Declarations used in the clause.
  /// \param ComponentLists Component lists used in the clause.
  static OMPUseDeviceAddrClause *
  Create(const ASTContext &C, const OMPVarListLocTy &Locs,
         ArrayRef<Expr *> Vars, ArrayRef<ValueDecl *> Declarations,
         MappableExprComponentListsRef ComponentLists);

  /// Creates an empty clause with the place for \a NumVars variables.
  ///
  /// \param C AST context.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  static OMPUseDeviceAddrClause *
  CreateEmpty(const ASTContext &C, const OMPMappableExprListSizeTy &Sizes);

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPUseDeviceAddrClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_use_device_addr;
  }
};

/// This represents clause 'is_device_ptr' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp target is_device_ptr(a,b)
/// \endcode
/// In this example directive '#pragma omp target' has clause
/// 'is_device_ptr' with the variables 'a' and 'b'.
class OMPIsDevicePtrClause final
    : public OMPMappableExprListClause<OMPIsDevicePtrClause>,
      private llvm::TrailingObjects<
          OMPIsDevicePtrClause, Expr *, ValueDecl *, unsigned,
          OMPClauseMappableExprCommon::MappableComponent> {
  friend class OMPClauseReader;
  friend OMPMappableExprListClause;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a NumVars.
  ///
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPIsDevicePtrClause(const OMPVarListLocTy &Locs,
                                const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_is_device_ptr, Locs, Sizes) {}

  /// Build an empty clause.
  ///
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPIsDevicePtrClause(const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_is_device_ptr,
                                  OMPVarListLocTy(), Sizes) {}

  /// Define the sizes of each trailing object array except the last one. This
  /// is required for TrailingObjects to work properly.
  size_t numTrailingObjects(OverloadToken<Expr *>) const {
    return varlist_size();
  }
  size_t numTrailingObjects(OverloadToken<ValueDecl *>) const {
    return getUniqueDeclarationsNum();
  }
  size_t numTrailingObjects(OverloadToken<unsigned>) const {
    return getUniqueDeclarationsNum() + getTotalComponentListNum();
  }

public:
  /// Creates clause with a list of variables \a Vars.
  ///
  /// \param C AST context.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Vars The original expression used in the clause.
  /// \param Declarations Declarations used in the clause.
  /// \param ComponentLists Component lists used in the clause.
  static OMPIsDevicePtrClause *
  Create(const ASTContext &C, const OMPVarListLocTy &Locs,
         ArrayRef<Expr *> Vars, ArrayRef<ValueDecl *> Declarations,
         MappableExprComponentListsRef ComponentLists);

  /// Creates an empty clause with the place for \a NumVars variables.
  ///
  /// \param C AST context.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  static OMPIsDevicePtrClause *
  CreateEmpty(const ASTContext &C, const OMPMappableExprListSizeTy &Sizes);

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPIsDevicePtrClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_is_device_ptr;
  }
};

/// This represents clause 'has_device_ptr' in the '#pragma omp ...'
/// directives.
///
/// \code
/// #pragma omp target has_device_addr(a,b)
/// \endcode
/// In this example directive '#pragma omp target' has clause
/// 'has_device_ptr' with the variables 'a' and 'b'.
class OMPHasDeviceAddrClause final
    : public OMPMappableExprListClause<OMPHasDeviceAddrClause>,
      private llvm::TrailingObjects<
          OMPHasDeviceAddrClause, Expr *, ValueDecl *, unsigned,
          OMPClauseMappableExprCommon::MappableComponent> {
  friend class OMPClauseReader;
  friend OMPMappableExprListClause;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a NumVars.
  ///
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPHasDeviceAddrClause(const OMPVarListLocTy &Locs,
                                  const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_has_device_addr, Locs,
                                  Sizes) {}

  /// Build an empty clause.
  ///
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  explicit OMPHasDeviceAddrClause(const OMPMappableExprListSizeTy &Sizes)
      : OMPMappableExprListClause(llvm::omp::OMPC_has_device_addr,
                                  OMPVarListLocTy(), Sizes) {}

  /// Define the sizes of each trailing object array except the last one. This
  /// is required for TrailingObjects to work properly.
  size_t numTrailingObjects(OverloadToken<Expr *>) const {
    return varlist_size();
  }
  size_t numTrailingObjects(OverloadToken<ValueDecl *>) const {
    return getUniqueDeclarationsNum();
  }
  size_t numTrailingObjects(OverloadToken<unsigned>) const {
    return getUniqueDeclarationsNum() + getTotalComponentListNum();
  }

public:
  /// Creates clause with a list of variables \a Vars.
  ///
  /// \param C AST context.
  /// \param Locs Locations needed to build a mappable clause. It includes 1)
  /// StartLoc: starting location of the clause (the clause keyword); 2)
  /// LParenLoc: location of '('; 3) EndLoc: ending location of the clause.
  /// \param Vars The original expression used in the clause.
  /// \param Declarations Declarations used in the clause.
  /// \param ComponentLists Component lists used in the clause.
  static OMPHasDeviceAddrClause *
  Create(const ASTContext &C, const OMPVarListLocTy &Locs,
         ArrayRef<Expr *> Vars, ArrayRef<ValueDecl *> Declarations,
         MappableExprComponentListsRef ComponentLists);

  /// Creates an empty clause with the place for \a NumVars variables.
  ///
  /// \param C AST context.
  /// \param Sizes All required sizes to build a mappable clause. It includes 1)
  /// NumVars: number of expressions listed in this clause; 2)
  /// NumUniqueDeclarations: number of unique base declarations in this clause;
  /// 3) NumComponentLists: number of component lists in this clause; and 4)
  /// NumComponents: total number of expression components in the clause.
  static OMPHasDeviceAddrClause *
  CreateEmpty(const ASTContext &C, const OMPMappableExprListSizeTy &Sizes);

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPHasDeviceAddrClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_has_device_addr;
  }
};

/// This represents clause 'nontemporal' in the '#pragma omp ...' directives.
///
/// \code
/// #pragma omp simd nontemporal(a)
/// \endcode
/// In this example directive '#pragma omp simd' has clause 'nontemporal' for
/// the variable 'a'.
class OMPNontemporalClause final
    : public OMPVarListClause<OMPNontemporalClause>,
      private llvm::TrailingObjects<OMPNontemporalClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPNontemporalClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                       SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPNontemporalClause>(llvm::omp::OMPC_nontemporal,
                                               StartLoc, LParenLoc, EndLoc, N) {
  }

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPNontemporalClause(unsigned N)
      : OMPVarListClause<OMPNontemporalClause>(
            llvm::omp::OMPC_nontemporal, SourceLocation(), SourceLocation(),
            SourceLocation(), N) {}

  /// Get the list of privatied copies if the member expression was captured by
  /// one of the privatization clauses.
  MutableArrayRef<Expr *> getPrivateRefs() {
    return MutableArrayRef<Expr *>(varlist_end(), varlist_size());
  }
  ArrayRef<const Expr *> getPrivateRefs() const {
    return llvm::ArrayRef(varlist_end(), varlist_size());
  }

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the variables.
  static OMPNontemporalClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation EndLoc, ArrayRef<Expr *> VL);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPNontemporalClause *CreateEmpty(const ASTContext &C, unsigned N);

  /// Sets the list of references to private copies created in private clauses.
  /// \param VL List of references.
  void setPrivateRefs(ArrayRef<Expr *> VL);

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPNontemporalClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range private_refs() {
    return child_range(reinterpret_cast<Stmt **>(getPrivateRefs().begin()),
                       reinterpret_cast<Stmt **>(getPrivateRefs().end()));
  }

  const_child_range private_refs() const {
    auto Children = const_cast<OMPNontemporalClause *>(this)->private_refs();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_nontemporal;
  }
};

/// This represents 'order' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp simd order(concurrent)
/// \endcode
/// In this example directive '#pragma omp parallel' has simple 'order'
/// clause with kind 'concurrent'.
class OMPOrderClause final : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// A kind of the 'order' clause.
  OpenMPOrderClauseKind Kind = OMPC_ORDER_unknown;

  /// Start location of the kind in source code.
  SourceLocation KindKwLoc;

  /// A modifier for order clause
  OpenMPOrderClauseModifier Modifier = OMPC_ORDER_MODIFIER_unknown;

  /// Start location of the modifier in source code.
  SourceLocation ModifierKwLoc;

  /// Set kind of the clause.
  ///
  /// \param K Argument of clause.
  void setKind(OpenMPOrderClauseKind K) { Kind = K; }

  /// Set argument location.
  ///
  /// \param KLoc Argument location.
  void setKindKwLoc(SourceLocation KLoc) { KindKwLoc = KLoc; }

  /// Set modifier of the clause.
  ///
  /// \param M Argument of clause.
  void setModifier(OpenMPOrderClauseModifier M) { Modifier = M; }

  /// Set modifier location.
  ///
  /// \param MLoc Modifier keyword location.
  void setModifierKwLoc(SourceLocation MLoc) { ModifierKwLoc = MLoc; }

public:
  /// Build 'order' clause with argument \p A ('concurrent').
  ///
  /// \param A Argument of the clause ('concurrent').
  /// \param ALoc Starting location of the argument.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param Modifier The modifier applied to 'order' clause.
  /// \param MLoc Location of the modifier
  OMPOrderClause(OpenMPOrderClauseKind A, SourceLocation ALoc,
                 SourceLocation StartLoc, SourceLocation LParenLoc,
                 SourceLocation EndLoc, OpenMPOrderClauseModifier Modifier,
                 SourceLocation MLoc)
      : OMPClause(llvm::omp::OMPC_order, StartLoc, EndLoc),
        LParenLoc(LParenLoc), Kind(A), KindKwLoc(ALoc), Modifier(Modifier),
        ModifierKwLoc(MLoc) {}

  /// Build an empty clause.
  OMPOrderClause()
      : OMPClause(llvm::omp::OMPC_order, SourceLocation(), SourceLocation()) {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns kind of the clause.
  OpenMPOrderClauseKind getKind() const { return Kind; }

  /// Returns location of clause kind.
  SourceLocation getKindKwLoc() const { return KindKwLoc; }

  /// Returns Modifier of the clause.
  OpenMPOrderClauseModifier getModifier() const { return Modifier; }

  /// Returns location of clause modifier.
  SourceLocation getModifierKwLoc() const { return ModifierKwLoc; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_order;
  }
};

/// This represents the 'init' clause in '#pragma omp ...' directives.
///
/// \code
/// #pragma omp interop init(target:obj)
/// \endcode
class OMPInitClause final
    : public OMPVarListClause<OMPInitClause>,
      private llvm::TrailingObjects<OMPInitClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Location of interop variable.
  SourceLocation VarLoc;

  bool IsTarget = false;
  bool IsTargetSync = false;

  void setInteropVar(Expr *E) { varlist_begin()[0] = E; }

  void setIsTarget(bool V) { IsTarget = V; }

  void setIsTargetSync(bool V) { IsTargetSync = V; }

  /// Sets the location of the interop variable.
  void setVarLoc(SourceLocation Loc) { VarLoc = Loc; }

  /// Build 'init' clause.
  ///
  /// \param IsTarget Uses the 'target' interop-type.
  /// \param IsTargetSync Uses the 'targetsync' interop-type.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param VarLoc Location of the interop variable.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of expressions.
  OMPInitClause(bool IsTarget, bool IsTargetSync, SourceLocation StartLoc,
                SourceLocation LParenLoc, SourceLocation VarLoc,
                SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPInitClause>(llvm::omp::OMPC_init, StartLoc,
                                        LParenLoc, EndLoc, N),
        VarLoc(VarLoc), IsTarget(IsTarget), IsTargetSync(IsTargetSync) {}

  /// Build an empty clause.
  OMPInitClause(unsigned N)
      : OMPVarListClause<OMPInitClause>(llvm::omp::OMPC_init, SourceLocation(),
                                        SourceLocation(), SourceLocation(), N) {
  }

public:
  /// Creates a fully specified clause.
  ///
  /// \param C AST context.
  /// \param InteropVar The interop variable.
  /// \param InteropInfo The interop-type and prefer_type list.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param VarLoc Location of the interop variable.
  /// \param EndLoc Ending location of the clause.
  static OMPInitClause *Create(const ASTContext &C, Expr *InteropVar,
                               OMPInteropInfo &InteropInfo,
                               SourceLocation StartLoc,
                               SourceLocation LParenLoc, SourceLocation VarLoc,
                               SourceLocation EndLoc);

  /// Creates an empty clause with \a N expressions.
  ///
  /// \param C AST context.
  /// \param N Number of expression items.
  static OMPInitClause *CreateEmpty(const ASTContext &C, unsigned N);

  /// Returns the location of the interop variable.
  SourceLocation getVarLoc() const { return VarLoc; }

  /// Returns the interop variable.
  Expr *getInteropVar() { return varlist_begin()[0]; }
  const Expr *getInteropVar() const { return varlist_begin()[0]; }

  /// Returns true is interop-type 'target' is used.
  bool getIsTarget() const { return IsTarget; }

  /// Returns true is interop-type 'targetsync' is used.
  bool getIsTargetSync() const { return IsTargetSync; }

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPInitClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  using prefs_iterator = MutableArrayRef<Expr *>::iterator;
  using const_prefs_iterator = ArrayRef<const Expr *>::iterator;
  using prefs_range = llvm::iterator_range<prefs_iterator>;
  using const_prefs_range = llvm::iterator_range<const_prefs_iterator>;

  prefs_range prefs() {
    return prefs_range(reinterpret_cast<Expr **>(std::next(varlist_begin())),
                       reinterpret_cast<Expr **>(varlist_end()));
  }

  const_prefs_range prefs() const {
    auto Prefs = const_cast<OMPInitClause *>(this)->prefs();
    return const_prefs_range(Prefs.begin(), Prefs.end());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_init;
  }
};

/// This represents the 'use' clause in '#pragma omp ...' directives.
///
/// \code
/// #pragma omp interop use(obj)
/// \endcode
class OMPUseClause final : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Location of interop variable.
  SourceLocation VarLoc;

  /// The interop variable.
  Stmt *InteropVar = nullptr;

  /// Set the interop variable.
  void setInteropVar(Expr *E) { InteropVar = E; }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Sets the location of the interop variable.
  void setVarLoc(SourceLocation Loc) { VarLoc = Loc; }

public:
  /// Build 'use' clause with and interop variable expression \a InteropVar.
  ///
  /// \param InteropVar The interop variable.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param VarLoc Location of the interop variable.
  /// \param EndLoc Ending location of the clause.
  OMPUseClause(Expr *InteropVar, SourceLocation StartLoc,
               SourceLocation LParenLoc, SourceLocation VarLoc,
               SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_use, StartLoc, EndLoc), LParenLoc(LParenLoc),
        VarLoc(VarLoc), InteropVar(InteropVar) {}

  /// Build an empty clause.
  OMPUseClause()
      : OMPClause(llvm::omp::OMPC_use, SourceLocation(), SourceLocation()) {}

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns the location of the interop variable.
  SourceLocation getVarLoc() const { return VarLoc; }

  /// Returns the interop variable.
  Expr *getInteropVar() const { return cast<Expr>(InteropVar); }

  child_range children() { return child_range(&InteropVar, &InteropVar + 1); }

  const_child_range children() const {
    return const_child_range(&InteropVar, &InteropVar + 1);
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_use;
  }
};

/// This represents 'destroy' clause in the '#pragma omp depobj'
/// directive or the '#pragma omp interop' directive..
///
/// \code
/// #pragma omp depobj(a) destroy
/// #pragma omp interop destroy(obj)
/// \endcode
/// In these examples directive '#pragma omp depobj' and '#pragma omp interop'
/// have a 'destroy' clause. The 'interop' directive includes an object.
class OMPDestroyClause final : public OMPClause {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// Location of interop variable.
  SourceLocation VarLoc;

  /// The interop variable.
  Stmt *InteropVar = nullptr;

  /// Set the interop variable.
  void setInteropVar(Expr *E) { InteropVar = E; }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Sets the location of the interop variable.
  void setVarLoc(SourceLocation Loc) { VarLoc = Loc; }

public:
  /// Build 'destroy' clause with an interop variable expression \a InteropVar.
  ///
  /// \param InteropVar The interop variable.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param VarLoc Location of the interop variable.
  /// \param EndLoc Ending location of the clause.
  OMPDestroyClause(Expr *InteropVar, SourceLocation StartLoc,
                   SourceLocation LParenLoc, SourceLocation VarLoc,
                   SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_destroy, StartLoc, EndLoc),
        LParenLoc(LParenLoc), VarLoc(VarLoc), InteropVar(InteropVar) {}

  /// Build 'destroy' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPDestroyClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPClause(llvm::omp::OMPC_destroy, StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPDestroyClause()
      : OMPClause(llvm::omp::OMPC_destroy, SourceLocation(), SourceLocation()) {
  }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns the location of the interop variable.
  SourceLocation getVarLoc() const { return VarLoc; }

  /// Returns the interop variable.
  Expr *getInteropVar() const { return cast_or_null<Expr>(InteropVar); }

  child_range children() {
    if (InteropVar)
      return child_range(&InteropVar, &InteropVar + 1);
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    if (InteropVar)
      return const_child_range(&InteropVar, &InteropVar + 1);
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_destroy;
  }
};

/// This represents 'novariants' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp dispatch novariants(a > 5)
/// \endcode
/// In this example directive '#pragma omp dispatch' has simple 'novariants'
/// clause with condition 'a > 5'.
class OMPNovariantsClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_novariants, OMPClause>,
      public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Set condition.
  void setCondition(Expr *Cond) { setStmt(Cond); }

public:
  /// Build 'novariants' clause with condition \a Cond.
  ///
  /// \param Cond Condition of the clause.
  /// \param HelperCond Helper condition for the construct.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPNovariantsClause(Expr *Cond, Stmt *HelperCond,
                      OpenMPDirectiveKind CaptureRegion,
                      SourceLocation StartLoc, SourceLocation LParenLoc,
                      SourceLocation EndLoc)
      : OMPOneStmtClause(Cond, StartLoc, LParenLoc, EndLoc),
        OMPClauseWithPreInit(this) {
    setPreInitStmt(HelperCond, CaptureRegion);
  }

  /// Build an empty clause.
  OMPNovariantsClause() : OMPOneStmtClause(), OMPClauseWithPreInit(this) {}

  /// Returns condition.
  Expr *getCondition() const { return getStmtAs<Expr>(); }

  child_range used_children();
  const_child_range used_children() const {
    auto Children = const_cast<OMPNovariantsClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }
};

/// This represents 'nocontext' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp dispatch nocontext(a > 5)
/// \endcode
/// In this example directive '#pragma omp dispatch' has simple 'nocontext'
/// clause with condition 'a > 5'.
class OMPNocontextClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_nocontext, OMPClause>,
      public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Set condition.
  void setCondition(Expr *Cond) { setStmt(Cond); }

public:
  /// Build 'nocontext' clause with condition \a Cond.
  ///
  /// \param Cond Condition of the clause.
  /// \param HelperCond Helper condition for the construct.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPNocontextClause(Expr *Cond, Stmt *HelperCond,
                     OpenMPDirectiveKind CaptureRegion, SourceLocation StartLoc,
                     SourceLocation LParenLoc, SourceLocation EndLoc)
      : OMPOneStmtClause(Cond, StartLoc, LParenLoc, EndLoc),
        OMPClauseWithPreInit(this) {
    setPreInitStmt(HelperCond, CaptureRegion);
  }

  /// Build an empty clause.
  OMPNocontextClause() : OMPOneStmtClause(), OMPClauseWithPreInit(this) {}

  /// Returns condition.
  Expr *getCondition() const { return getStmtAs<Expr>(); }

  child_range used_children();
  const_child_range used_children() const {
    auto Children = const_cast<OMPNocontextClause *>(this)->used_children();
    return const_child_range(Children.begin(), Children.end());
  }
};

/// This represents 'detach' clause in the '#pragma omp task' directive.
///
/// \code
/// #pragma omp task detach(evt)
/// \endcode
/// In this example directive '#pragma omp detach' has simple 'detach' clause
/// with the variable 'evt'.
class OMPDetachClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_detach, OMPClause> {
  friend class OMPClauseReader;

  /// Set condition.
  void setEventHandler(Expr *E) { setStmt(E); }

public:
  /// Build 'detach' clause with event-handler \a Evt.
  ///
  /// \param Evt Event handler expression.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPDetachClause(Expr *Evt, SourceLocation StartLoc, SourceLocation LParenLoc,
                  SourceLocation EndLoc)
      : OMPOneStmtClause(Evt, StartLoc, LParenLoc, EndLoc) {}

  /// Build an empty clause.
  OMPDetachClause() : OMPOneStmtClause() {}

  /// Returns event-handler expression.
  Expr *getEventHandler() const { return getStmtAs<Expr>(); }
};

/// This represents clause 'inclusive' in the '#pragma omp scan' directive.
///
/// \code
/// #pragma omp scan inclusive(a,b)
/// \endcode
/// In this example directive '#pragma omp scan' has clause 'inclusive'
/// with the variables 'a' and 'b'.
class OMPInclusiveClause final
    : public OMPVarListClause<OMPInclusiveClause>,
      private llvm::TrailingObjects<OMPInclusiveClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPInclusiveClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                     SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPInclusiveClause>(llvm::omp::OMPC_inclusive,
                                             StartLoc, LParenLoc, EndLoc, N) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPInclusiveClause(unsigned N)
      : OMPVarListClause<OMPInclusiveClause>(llvm::omp::OMPC_inclusive,
                                             SourceLocation(), SourceLocation(),
                                             SourceLocation(), N) {}

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the original variables.
  static OMPInclusiveClause *Create(const ASTContext &C,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc, ArrayRef<Expr *> VL);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPInclusiveClause *CreateEmpty(const ASTContext &C, unsigned N);

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPInclusiveClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_inclusive;
  }
};

/// This represents clause 'exclusive' in the '#pragma omp scan' directive.
///
/// \code
/// #pragma omp scan exclusive(a,b)
/// \endcode
/// In this example directive '#pragma omp scan' has clause 'exclusive'
/// with the variables 'a' and 'b'.
class OMPExclusiveClause final
    : public OMPVarListClause<OMPExclusiveClause>,
      private llvm::TrailingObjects<OMPExclusiveClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Build clause with number of variables \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of the variables in the clause.
  OMPExclusiveClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                     SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPExclusiveClause>(llvm::omp::OMPC_exclusive,
                                             StartLoc, LParenLoc, EndLoc, N) {}

  /// Build an empty clause.
  ///
  /// \param N Number of variables.
  explicit OMPExclusiveClause(unsigned N)
      : OMPVarListClause<OMPExclusiveClause>(llvm::omp::OMPC_exclusive,
                                             SourceLocation(), SourceLocation(),
                                             SourceLocation(), N) {}

public:
  /// Creates clause with a list of variables \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param VL List of references to the original variables.
  static OMPExclusiveClause *Create(const ASTContext &C,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc, ArrayRef<Expr *> VL);

  /// Creates an empty clause with the place for \a N variables.
  ///
  /// \param C AST context.
  /// \param N The number of variables.
  static OMPExclusiveClause *CreateEmpty(const ASTContext &C, unsigned N);

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPExclusiveClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_exclusive;
  }
};

/// This represents clause 'uses_allocators' in the '#pragma omp target'-based
/// directives.
///
/// \code
/// #pragma omp target uses_allocators(default_allocator, my_allocator(traits))
/// \endcode
/// In this example directive '#pragma omp target' has clause 'uses_allocators'
/// with the allocators 'default_allocator' and user-defined 'my_allocator'.
class OMPUsesAllocatorsClause final
    : public OMPClause,
      private llvm::TrailingObjects<OMPUsesAllocatorsClause, Expr *,
                                    SourceLocation> {
public:
  /// Data for list of allocators.
  struct Data {
    /// Allocator.
    Expr *Allocator = nullptr;
    /// Allocator traits.
    Expr *AllocatorTraits = nullptr;
    /// Locations of '(' and ')' symbols.
    SourceLocation LParenLoc, RParenLoc;
  };

private:
  friend class OMPClauseReader;
  friend TrailingObjects;

  enum class ExprOffsets {
    Allocator,
    AllocatorTraits,
    Total,
  };

  enum class ParenLocsOffsets {
    LParen,
    RParen,
    Total,
  };

  /// Location of '('.
  SourceLocation LParenLoc;
  /// Total number of allocators in the clause.
  unsigned NumOfAllocators = 0;

  /// Build clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of allocators associated with the clause.
  OMPUsesAllocatorsClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                          SourceLocation EndLoc, unsigned N)
      : OMPClause(llvm::omp::OMPC_uses_allocators, StartLoc, EndLoc),
        LParenLoc(LParenLoc), NumOfAllocators(N) {}

  /// Build an empty clause.
  /// \param N Number of allocators associated with the clause.
  ///
  explicit OMPUsesAllocatorsClause(unsigned N)
      : OMPClause(llvm::omp::OMPC_uses_allocators, SourceLocation(),
                  SourceLocation()),
        NumOfAllocators(N) {}

  unsigned numTrailingObjects(OverloadToken<Expr *>) const {
    return NumOfAllocators * static_cast<int>(ExprOffsets::Total);
  }

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Sets the allocators data for the clause.
  void setAllocatorsData(ArrayRef<OMPUsesAllocatorsClause::Data> Data);

public:
  /// Creates clause with a list of allocators \p Data.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param Data List of allocators.
  static OMPUsesAllocatorsClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation EndLoc, ArrayRef<OMPUsesAllocatorsClause::Data> Data);

  /// Creates an empty clause with the place for \p N allocators.
  ///
  /// \param C AST context.
  /// \param N The number of allocators.
  static OMPUsesAllocatorsClause *CreateEmpty(const ASTContext &C, unsigned N);

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns number of allocators associated with the clause.
  unsigned getNumberOfAllocators() const { return NumOfAllocators; }

  /// Returns data for the specified allocator.
  OMPUsesAllocatorsClause::Data getAllocatorData(unsigned I) const;

  // Iterators
  child_range children() {
    Stmt **Begin = reinterpret_cast<Stmt **>(getTrailingObjects<Expr *>());
    return child_range(Begin, Begin + NumOfAllocators *
                                          static_cast<int>(ExprOffsets::Total));
  }
  const_child_range children() const {
    Stmt *const *Begin =
        reinterpret_cast<Stmt *const *>(getTrailingObjects<Expr *>());
    return const_child_range(
        Begin, Begin + NumOfAllocators * static_cast<int>(ExprOffsets::Total));
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_uses_allocators;
  }
};

/// This represents clause 'affinity' in the '#pragma omp task'-based
/// directives.
///
/// \code
/// #pragma omp task affinity(iterator(i = 0:n) : ([3][n])a, b[:n], c[i])
/// \endcode
/// In this example directive '#pragma omp task' has clause 'affinity' with the
/// affinity modifer 'iterator(i = 0:n)' and locator items '([3][n])a', 'b[:n]'
/// and 'c[i]'.
class OMPAffinityClause final
    : public OMPVarListClause<OMPAffinityClause>,
      private llvm::TrailingObjects<OMPAffinityClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Location of ':' symbol.
  SourceLocation ColonLoc;

  /// Build clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ColonLoc Location of ':'.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of locators associated with the clause.
  OMPAffinityClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                    SourceLocation ColonLoc, SourceLocation EndLoc, unsigned N)
      : OMPVarListClause<OMPAffinityClause>(llvm::omp::OMPC_affinity, StartLoc,
                                            LParenLoc, EndLoc, N) {}

  /// Build an empty clause.
  /// \param N Number of locators associated with the clause.
  ///
  explicit OMPAffinityClause(unsigned N)
      : OMPVarListClause<OMPAffinityClause>(llvm::omp::OMPC_affinity,
                                            SourceLocation(), SourceLocation(),
                                            SourceLocation(), N) {}

  /// Sets the affinity modifier for the clause, if any.
  void setModifier(Expr *E) {
    getTrailingObjects<Expr *>()[varlist_size()] = E;
  }

  /// Sets the location of ':' symbol.
  void setColonLoc(SourceLocation Loc) { ColonLoc = Loc; }

public:
  /// Creates clause with a modifier a list of locator items.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param ColonLoc Location of ':'.
  /// \param EndLoc Ending location of the clause.
  /// \param Locators List of locator items.
  static OMPAffinityClause *Create(const ASTContext &C, SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation ColonLoc,
                                   SourceLocation EndLoc, Expr *Modifier,
                                   ArrayRef<Expr *> Locators);

  /// Creates an empty clause with the place for \p N locator items.
  ///
  /// \param C AST context.
  /// \param N The number of locator items.
  static OMPAffinityClause *CreateEmpty(const ASTContext &C, unsigned N);

  /// Gets affinity modifier.
  Expr *getModifier() { return getTrailingObjects<Expr *>()[varlist_size()]; }
  Expr *getModifier() const {
    return getTrailingObjects<Expr *>()[varlist_size()];
  }

  /// Gets the location of ':' symbol.
  SourceLocation getColonLoc() const { return ColonLoc; }

  // Iterators
  child_range children() {
    int Offset = getModifier() ? 1 : 0;
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end() + Offset));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPAffinityClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_affinity;
  }
};

/// This represents 'filter' clause in the '#pragma omp ...' directive.
///
/// \code
/// #pragma omp masked filter(tid)
/// \endcode
/// In this example directive '#pragma omp masked' has 'filter' clause with
/// thread id.
class OMPFilterClause final
    : public OMPOneStmtClause<llvm::omp::OMPC_filter, OMPClause>,
      public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Sets the thread identifier.
  void setThreadID(Expr *TID) { setStmt(TID); }

public:
  /// Build 'filter' clause with thread-id \a ThreadID.
  ///
  /// \param ThreadID Thread identifier.
  /// \param HelperE Helper expression associated with this clause.
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// clause must be captured.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPFilterClause(Expr *ThreadID, Stmt *HelperE,
                  OpenMPDirectiveKind CaptureRegion, SourceLocation StartLoc,
                  SourceLocation LParenLoc, SourceLocation EndLoc)
      : OMPOneStmtClause(ThreadID, StartLoc, LParenLoc, EndLoc),
        OMPClauseWithPreInit(this) {
    setPreInitStmt(HelperE, CaptureRegion);
  }

  /// Build an empty clause.
  OMPFilterClause() : OMPOneStmtClause(), OMPClauseWithPreInit(this) {}

  /// Return thread identifier.
  Expr *getThreadID() const { return getStmtAs<Expr>(); }

  /// Return thread identifier.
  Expr *getThreadID() { return getStmtAs<Expr>(); }
};

/// This represents 'bind' clause in the '#pragma omp ...' directives.
///
/// \code
/// #pragma omp loop bind(parallel)
/// \endcode
class OMPBindClause final : public OMPNoChildClause<llvm::omp::OMPC_bind> {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// The binding kind of 'bind' clause.
  OpenMPBindClauseKind Kind = OMPC_BIND_unknown;

  /// Start location of the kind in source code.
  SourceLocation KindLoc;

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Set the binding kind.
  void setBindKind(OpenMPBindClauseKind K) { Kind = K; }

  /// Set the binding kind location.
  void setBindKindLoc(SourceLocation KLoc) { KindLoc = KLoc; }

  /// Build 'bind' clause with kind \a K ('teams', 'parallel', or 'thread').
  ///
  /// \param K Binding kind of the clause ('teams', 'parallel' or 'thread').
  /// \param KLoc Starting location of the binding kind.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPBindClause(OpenMPBindClauseKind K, SourceLocation KLoc,
                SourceLocation StartLoc, SourceLocation LParenLoc,
                SourceLocation EndLoc)
      : OMPNoChildClause(StartLoc, EndLoc), LParenLoc(LParenLoc), Kind(K),
        KindLoc(KLoc) {}

  /// Build an empty clause.
  OMPBindClause() : OMPNoChildClause() {}

public:
  /// Build 'bind' clause with kind \a K ('teams', 'parallel', or 'thread').
  ///
  /// \param C AST context
  /// \param K Binding kind of the clause ('teams', 'parallel' or 'thread').
  /// \param KLoc Starting location of the binding kind.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  static OMPBindClause *Create(const ASTContext &C, OpenMPBindClauseKind K,
                               SourceLocation KLoc, SourceLocation StartLoc,
                               SourceLocation LParenLoc, SourceLocation EndLoc);

  /// Build an empty 'bind' clause.
  ///
  /// \param C AST context
  static OMPBindClause *CreateEmpty(const ASTContext &C);

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returns kind of the clause.
  OpenMPBindClauseKind getBindKind() const { return Kind; }

  /// Returns location of clause kind.
  SourceLocation getBindKindLoc() const { return KindLoc; }
};

/// This class implements a simple visitor for OMPClause
/// subclasses.
template<class ImplClass, template <typename> class Ptr, typename RetTy>
class OMPClauseVisitorBase {
public:
#define PTR(CLASS) Ptr<CLASS>
#define DISPATCH(CLASS) \
  return static_cast<ImplClass*>(this)->Visit##CLASS(static_cast<PTR(CLASS)>(S))

#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class)                                         \
  RetTy Visit##Class(PTR(Class) S) { DISPATCH(Class); }
#include "llvm/Frontend/OpenMP/OMP.inc"

  RetTy Visit(PTR(OMPClause) S) {
    // Top switch clause: visit each OMPClause.
    switch (S->getClauseKind()) {
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class)                                         \
  case llvm::omp::Clause::Enum:                                                \
    return Visit##Class(static_cast<PTR(Class)>(S));
#define CLAUSE_NO_CLASS(Enum, Str)                                             \
  case llvm::omp::Clause::Enum:                                                \
    break;
#include "llvm/Frontend/OpenMP/OMP.inc"
    }
  }
  // Base case, ignore it. :)
  RetTy VisitOMPClause(PTR(OMPClause) Node) { return RetTy(); }
#undef PTR
#undef DISPATCH
};

template <typename T> using const_ptr = std::add_pointer_t<std::add_const_t<T>>;

template <class ImplClass, typename RetTy = void>
class OMPClauseVisitor
    : public OMPClauseVisitorBase<ImplClass, std::add_pointer_t, RetTy> {};
template<class ImplClass, typename RetTy = void>
class ConstOMPClauseVisitor :
      public OMPClauseVisitorBase <ImplClass, const_ptr, RetTy> {};

class OMPClausePrinter final : public OMPClauseVisitor<OMPClausePrinter> {
  raw_ostream &OS;
  const PrintingPolicy &Policy;

  /// Process clauses with list of variables.
  template <typename T> void VisitOMPClauseList(T *Node, char StartSym);
  /// Process motion clauses.
  template <typename T> void VisitOMPMotionClause(T *Node);

public:
  OMPClausePrinter(raw_ostream &OS, const PrintingPolicy &Policy)
      : OS(OS), Policy(Policy) {}

#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class) void Visit##Class(Class *S);
#include "llvm/Frontend/OpenMP/OMP.inc"
};

struct OMPTraitProperty {
  llvm::omp::TraitProperty Kind = llvm::omp::TraitProperty::invalid;

  /// The raw string as we parsed it. This is needed for the `isa` trait set
  /// (which accepts anything) and (later) extensions.
  StringRef RawString;
};
struct OMPTraitSelector {
  Expr *ScoreOrCondition = nullptr;
  llvm::omp::TraitSelector Kind = llvm::omp::TraitSelector::invalid;
  llvm::SmallVector<OMPTraitProperty, 1> Properties;
};
struct OMPTraitSet {
  llvm::omp::TraitSet Kind = llvm::omp::TraitSet::invalid;
  llvm::SmallVector<OMPTraitSelector, 2> Selectors;
};

/// Helper data structure representing the traits in a match clause of an
/// `declare variant` or `metadirective`. The outer level is an ordered
/// collection of selector sets, each with an associated kind and an ordered
/// collection of selectors. A selector has a kind, an optional score/condition,
/// and an ordered collection of properties.
class OMPTraitInfo {
  /// Private constructor accesible only by ASTContext.
  OMPTraitInfo() {}
  friend class ASTContext;

public:
  /// Reconstruct a (partial) OMPTraitInfo object from a mangled name.
  OMPTraitInfo(StringRef MangledName);

  /// The outermost level of selector sets.
  llvm::SmallVector<OMPTraitSet, 2> Sets;

  bool anyScoreOrCondition(
      llvm::function_ref<bool(Expr *&, bool /* IsScore */)> Cond) {
    return llvm::any_of(Sets, [&](OMPTraitSet &Set) {
      return llvm::any_of(
          Set.Selectors, [&](OMPTraitSelector &Selector) {
            return Cond(Selector.ScoreOrCondition,
                        /* IsScore */ Selector.Kind !=
                            llvm::omp::TraitSelector::user_condition);
          });
    });
  }

  /// Create a variant match info object from this trait info object. While the
  /// former is a flat representation the actual main difference is that the
  /// latter uses clang::Expr to store the score/condition while the former is
  /// independent of clang. Thus, expressions and conditions are evaluated in
  /// this method.
  void getAsVariantMatchInfo(ASTContext &ASTCtx,
                             llvm::omp::VariantMatchInfo &VMI) const;

  /// Return a string representation identifying this context selector.
  std::string getMangledName() const;

  /// Check the extension trait \p TP is active.
  bool isExtensionActive(llvm::omp::TraitProperty TP) {
    for (const OMPTraitSet &Set : Sets) {
      if (Set.Kind != llvm::omp::TraitSet::implementation)
        continue;
      for (const OMPTraitSelector &Selector : Set.Selectors) {
        if (Selector.Kind != llvm::omp::TraitSelector::implementation_extension)
          continue;
        for (const OMPTraitProperty &Property : Selector.Properties) {
          if (Property.Kind == TP)
            return true;
        }
      }
    }
    return false;
  }

  /// Print a human readable representation into \p OS.
  void print(llvm::raw_ostream &OS, const PrintingPolicy &Policy) const;
};
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const OMPTraitInfo &TI);
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const OMPTraitInfo *TI);

/// Clang specific specialization of the OMPContext to lookup target features.
struct TargetOMPContext final : public llvm::omp::OMPContext {
  TargetOMPContext(ASTContext &ASTCtx,
                   std::function<void(StringRef)> &&DiagUnknownTrait,
                   const FunctionDecl *CurrentFunctionDecl,
                   ArrayRef<llvm::omp::TraitProperty> ConstructTraits);

  virtual ~TargetOMPContext() = default;

  /// See llvm::omp::OMPContext::matchesISATrait
  bool matchesISATrait(StringRef RawString) const override;

private:
  std::function<bool(StringRef)> FeatureValidityCheck;
  std::function<void(StringRef)> DiagUnknownTrait;
  llvm::StringMap<bool> FeatureMap;
};

/// Contains data for OpenMP directives: clauses, children
/// expressions/statements (helpers for codegen) and associated statement, if
/// any.
class OMPChildren final
    : private llvm::TrailingObjects<OMPChildren, OMPClause *, Stmt *> {
  friend TrailingObjects;
  friend class OMPClauseReader;
  friend class OMPExecutableDirective;
  template <typename T> friend class OMPDeclarativeDirective;

  /// Numbers of clauses.
  unsigned NumClauses = 0;
  /// Number of child expressions/stmts.
  unsigned NumChildren = 0;
  /// true if the directive has associated statement.
  bool HasAssociatedStmt = false;

  /// Define the sizes of each trailing object array except the last one. This
  /// is required for TrailingObjects to work properly.
  size_t numTrailingObjects(OverloadToken<OMPClause *>) const {
    return NumClauses;
  }

  OMPChildren() = delete;

  OMPChildren(unsigned NumClauses, unsigned NumChildren, bool HasAssociatedStmt)
      : NumClauses(NumClauses), NumChildren(NumChildren),
        HasAssociatedStmt(HasAssociatedStmt) {}

  static size_t size(unsigned NumClauses, bool HasAssociatedStmt,
                     unsigned NumChildren);

  static OMPChildren *Create(void *Mem, ArrayRef<OMPClause *> Clauses);
  static OMPChildren *Create(void *Mem, ArrayRef<OMPClause *> Clauses, Stmt *S,
                             unsigned NumChildren = 0);
  static OMPChildren *CreateEmpty(void *Mem, unsigned NumClauses,
                                  bool HasAssociatedStmt = false,
                                  unsigned NumChildren = 0);

public:
  unsigned getNumClauses() const { return NumClauses; }
  unsigned getNumChildren() const { return NumChildren; }
  bool hasAssociatedStmt() const { return HasAssociatedStmt; }

  /// Set associated statement.
  void setAssociatedStmt(Stmt *S) {
    getTrailingObjects<Stmt *>()[NumChildren] = S;
  }

  void setChildren(ArrayRef<Stmt *> Children);

  /// Sets the list of variables for this clause.
  ///
  /// \param Clauses The list of clauses for the directive.
  ///
  void setClauses(ArrayRef<OMPClause *> Clauses);

  /// Returns statement associated with the directive.
  const Stmt *getAssociatedStmt() const {
    return const_cast<OMPChildren *>(this)->getAssociatedStmt();
  }
  Stmt *getAssociatedStmt() {
    assert(HasAssociatedStmt &&
           "Expected directive with the associated statement.");
    return getTrailingObjects<Stmt *>()[NumChildren];
  }

  /// Get the clauses storage.
  MutableArrayRef<OMPClause *> getClauses() {
    return llvm::MutableArrayRef(getTrailingObjects<OMPClause *>(),
                                     NumClauses);
  }
  ArrayRef<OMPClause *> getClauses() const {
    return const_cast<OMPChildren *>(this)->getClauses();
  }

  /// Returns the captured statement associated with the
  /// component region within the (combined) directive.
  ///
  /// \param RegionKind Component region kind.
  const CapturedStmt *
  getCapturedStmt(OpenMPDirectiveKind RegionKind,
                  ArrayRef<OpenMPDirectiveKind> CaptureRegions) const {
    assert(llvm::is_contained(CaptureRegions, RegionKind) &&
           "RegionKind not found in OpenMP CaptureRegions.");
    auto *CS = cast<CapturedStmt>(getAssociatedStmt());
    for (auto ThisCaptureRegion : CaptureRegions) {
      if (ThisCaptureRegion == RegionKind)
        return CS;
      CS = cast<CapturedStmt>(CS->getCapturedStmt());
    }
    llvm_unreachable("Incorrect RegionKind specified for directive.");
  }

  /// Get innermost captured statement for the construct.
  CapturedStmt *
  getInnermostCapturedStmt(ArrayRef<OpenMPDirectiveKind> CaptureRegions) {
    assert(hasAssociatedStmt() && "Must have associated captured statement.");
    assert(!CaptureRegions.empty() &&
           "At least one captured statement must be provided.");
    auto *CS = cast<CapturedStmt>(getAssociatedStmt());
    for (unsigned Level = CaptureRegions.size(); Level > 1; --Level)
      CS = cast<CapturedStmt>(CS->getCapturedStmt());
    return CS;
  }

  const CapturedStmt *
  getInnermostCapturedStmt(ArrayRef<OpenMPDirectiveKind> CaptureRegions) const {
    return const_cast<OMPChildren *>(this)->getInnermostCapturedStmt(
        CaptureRegions);
  }

  MutableArrayRef<Stmt *> getChildren();
  ArrayRef<Stmt *> getChildren() const {
    return const_cast<OMPChildren *>(this)->getChildren();
  }

  Stmt *getRawStmt() {
    assert(HasAssociatedStmt &&
           "Expected directive with the associated statement.");
    if (auto *CS = dyn_cast<CapturedStmt>(getAssociatedStmt())) {
      Stmt *S = nullptr;
      do {
        S = CS->getCapturedStmt();
        CS = dyn_cast<CapturedStmt>(S);
      } while (CS);
      return S;
    }
    return getAssociatedStmt();
  }
  const Stmt *getRawStmt() const {
    return const_cast<OMPChildren *>(this)->getRawStmt();
  }

  Stmt::child_range getAssociatedStmtAsRange() {
    if (!HasAssociatedStmt)
      return Stmt::child_range(Stmt::child_iterator(), Stmt::child_iterator());
    return Stmt::child_range(&getTrailingObjects<Stmt *>()[NumChildren],
                             &getTrailingObjects<Stmt *>()[NumChildren + 1]);
  }
};

/// This represents 'ompx_dyn_cgroup_mem' clause in the '#pragma omp target ...'
/// directive.
///
/// \code
/// #pragma omp target [...] ompx_dyn_cgroup_mem(N)
/// \endcode
class OMPXDynCGroupMemClause
    : public OMPOneStmtClause<llvm::omp::OMPC_ompx_dyn_cgroup_mem, OMPClause>,
      public OMPClauseWithPreInit {
  friend class OMPClauseReader;

  /// Set size.
  void setSize(Expr *E) { setStmt(E); }

public:
  /// Build 'ompx_dyn_cgroup_mem' clause.
  ///
  /// \param Size Size expression.
  /// \param HelperSize Helper Size expression
  /// \param CaptureRegion Innermost OpenMP region where expressions in this
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPXDynCGroupMemClause(Expr *Size, Stmt *HelperSize,
                         OpenMPDirectiveKind CaptureRegion,
                         SourceLocation StartLoc, SourceLocation LParenLoc,
                         SourceLocation EndLoc)
      : OMPOneStmtClause(Size, StartLoc, LParenLoc, EndLoc),
        OMPClauseWithPreInit(this) {
    setPreInitStmt(HelperSize, CaptureRegion);
  }

  /// Build an empty clause.
  OMPXDynCGroupMemClause() : OMPOneStmtClause(), OMPClauseWithPreInit(this) {}

  /// Return the size expression.
  Expr *getSize() { return getStmtAs<Expr>(); }

  /// Return the size expression.
  Expr *getSize() const { return getStmtAs<Expr>(); }
};

/// This represents the 'doacross' clause for the '#pragma omp ordered'
/// directive.
///
/// \code
/// #pragma omp ordered doacross(sink: i-1, j-1)
/// \endcode
/// In this example directive '#pragma omp ordered' with clause 'doacross' with
/// a dependence-type 'sink' and loop-iteration vector expressions i-1 and j-1.
class OMPDoacrossClause final
    : public OMPVarListClause<OMPDoacrossClause>,
      private llvm::TrailingObjects<OMPDoacrossClause, Expr *> {
  friend class OMPClauseReader;
  friend OMPVarListClause;
  friend TrailingObjects;

  /// Dependence type (sink or source).
  OpenMPDoacrossClauseModifier DepType = OMPC_DOACROSS_unknown;

  /// Dependence type location.
  SourceLocation DepLoc;

  /// Colon location.
  SourceLocation ColonLoc;

  /// Number of loops, associated with the doacross clause.
  unsigned NumLoops = 0;

  /// Build clause with number of expressions \a N.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param N Number of expressions in the clause.
  /// \param NumLoops Number of loops associated with the clause.
  OMPDoacrossClause(SourceLocation StartLoc, SourceLocation LParenLoc,
                    SourceLocation EndLoc, unsigned N, unsigned NumLoops)
      : OMPVarListClause<OMPDoacrossClause>(llvm::omp::OMPC_doacross, StartLoc,
                                            LParenLoc, EndLoc, N),
        NumLoops(NumLoops) {}

  /// Build an empty clause.
  ///
  /// \param N Number of expressions in the clause.
  /// \param NumLoops Number of loops associated with the clause.
  explicit OMPDoacrossClause(unsigned N, unsigned NumLoops)
      : OMPVarListClause<OMPDoacrossClause>(llvm::omp::OMPC_doacross,
                                            SourceLocation(), SourceLocation(),
                                            SourceLocation(), N),
        NumLoops(NumLoops) {}

  /// Set dependence type.
  void setDependenceType(OpenMPDoacrossClauseModifier M) { DepType = M; }

  /// Set dependence type location.
  void setDependenceLoc(SourceLocation Loc) { DepLoc = Loc; }

  /// Set colon location.
  void setColonLoc(SourceLocation Loc) { ColonLoc = Loc; }

public:
  /// Creates clause with a list of expressions \a VL.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  /// \param DepType The dependence type.
  /// \param DepLoc Location of the dependence type.
  /// \param ColonLoc Location of ':'.
  /// \param VL List of references to the expressions.
  /// \param NumLoops Number of loops that associated with the clause.
  static OMPDoacrossClause *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation LParenLoc,
         SourceLocation EndLoc, OpenMPDoacrossClauseModifier DepType,
         SourceLocation DepLoc, SourceLocation ColonLoc, ArrayRef<Expr *> VL,
         unsigned NumLoops);

  /// Creates an empty clause with \a N expressions.
  ///
  /// \param C AST context.
  /// \param N The number of expressions.
  /// \param NumLoops Number of loops that is associated with this clause.
  static OMPDoacrossClause *CreateEmpty(const ASTContext &C, unsigned N,
                                        unsigned NumLoops);

  /// Get dependence type.
  OpenMPDoacrossClauseModifier getDependenceType() const { return DepType; }

  /// Get dependence type location.
  SourceLocation getDependenceLoc() const { return DepLoc; }

  /// Get colon location.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Get number of loops associated with the clause.
  unsigned getNumLoops() const { return NumLoops; }

  /// Set the loop data.
  void setLoopData(unsigned NumLoop, Expr *Cnt);

  /// Get the loop data.
  Expr *getLoopData(unsigned NumLoop);
  const Expr *getLoopData(unsigned NumLoop) const;

  child_range children() {
    return child_range(reinterpret_cast<Stmt **>(varlist_begin()),
                       reinterpret_cast<Stmt **>(varlist_end()));
  }

  const_child_range children() const {
    auto Children = const_cast<OMPDoacrossClause *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_range used_children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range used_children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  static bool classof(const OMPClause *T) {
    return T->getClauseKind() == llvm::omp::OMPC_doacross;
  }
};

/// This represents 'ompx_attribute' clause in a directive that might generate
/// an outlined function. An example is given below.
///
/// \code
/// #pragma omp target [...] ompx_attribute(flatten)
/// \endcode
class OMPXAttributeClause
    : public OMPNoChildClause<llvm::omp::OMPC_ompx_attribute> {
  friend class OMPClauseReader;

  /// Location of '('.
  SourceLocation LParenLoc;

  /// The parsed attributes (clause arguments)
  SmallVector<const Attr *> Attrs;

public:
  /// Build 'ompx_attribute' clause.
  ///
  /// \param Attrs The parsed attributes (clause arguments)
  /// \param StartLoc Starting location of the clause.
  /// \param LParenLoc Location of '('.
  /// \param EndLoc Ending location of the clause.
  OMPXAttributeClause(ArrayRef<const Attr *> Attrs, SourceLocation StartLoc,
                      SourceLocation LParenLoc, SourceLocation EndLoc)
      : OMPNoChildClause(StartLoc, EndLoc), LParenLoc(LParenLoc), Attrs(Attrs) {
  }

  /// Build an empty clause.
  OMPXAttributeClause() : OMPNoChildClause() {}

  /// Sets the location of '('.
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }

  /// Returns the location of '('.
  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// Returned the attributes parsed from this clause.
  ArrayRef<const Attr *> getAttrs() const { return Attrs; }

private:
  /// Replace the attributes with \p NewAttrs.
  void setAttrs(ArrayRef<Attr *> NewAttrs) {
    Attrs.clear();
    Attrs.append(NewAttrs.begin(), NewAttrs.end());
  }
};

/// This represents 'ompx_bare' clause in the '#pragma omp target teams ...'
/// directive.
///
/// \code
/// #pragma omp target teams ompx_bare
/// \endcode
/// In this example directive '#pragma omp target teams' has a 'ompx_bare'
/// clause.
class OMPXBareClause : public OMPNoChildClause<llvm::omp::OMPC_ompx_bare> {
public:
  /// Build 'ompx_bare' clause.
  ///
  /// \param StartLoc Starting location of the clause.
  /// \param EndLoc Ending location of the clause.
  OMPXBareClause(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPNoChildClause(StartLoc, EndLoc) {}

  /// Build an empty clause.
  OMPXBareClause() = default;
};

} // namespace clang

#endif // LLVM_CLANG_AST_OPENMPCLAUSE_H
