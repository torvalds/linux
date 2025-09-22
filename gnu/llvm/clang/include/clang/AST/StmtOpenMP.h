//===- StmtOpenMP.h - Classes for OpenMP directives  ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines OpenMP AST classes for executable directives and
/// clauses.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_STMTOPENMP_H
#define LLVM_CLANG_AST_STMTOPENMP_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/SourceLocation.h"

namespace clang {

//===----------------------------------------------------------------------===//
// AST classes for directives.
//===----------------------------------------------------------------------===//

/// Representation of an OpenMP canonical loop.
///
/// OpenMP 1.0 C/C++, section 2.4.1 for Construct; canonical-shape
/// OpenMP 2.0 C/C++, section 2.4.1 for Construct; canonical-shape
/// OpenMP 2.5, section 2.5.1 Loop Construct; canonical form
/// OpenMP 3.1, section 2.5.1 Loop Construct; canonical form
/// OpenMP 4.0, section 2.6 Canonical Loop Form
/// OpenMP 4.5, section 2.6 Canonical Loop Form
/// OpenMP 5.0, section 2.9.1 Canonical Loop Form
/// OpenMP 5.1, section 2.11.1 Canonical Loop Nest Form
///
/// An OpenMP canonical loop is a for-statement or range-based for-statement
/// with additional requirements that ensure that the number of iterations is
/// known before entering the loop and allow skipping to an arbitrary iteration.
/// The OMPCanonicalLoop AST node wraps a ForStmt or CXXForRangeStmt that is
/// known to fulfill OpenMP's canonical loop requirements because of being
/// associated to an OMPLoopBasedDirective. That is, the general structure is:
///
///  OMPLoopBasedDirective
/// [`- CapturedStmt   ]
/// [   `- CapturedDecl]
///        ` OMPCanonicalLoop
///          `- ForStmt/CXXForRangeStmt
///             `- Stmt
///
/// One or multiple CapturedStmt/CapturedDecl pairs may be inserted by some
/// directives such as OMPParallelForDirective, but others do not need them
/// (such as OMPTileDirective). In  The OMPCanonicalLoop and
/// ForStmt/CXXForRangeStmt pair is repeated for loop associated with the
/// directive. A OMPCanonicalLoop must not appear in the AST unless associated
/// with a OMPLoopBasedDirective. In an imperfectly nested loop nest, the
/// OMPCanonicalLoop may also be wrapped in a CompoundStmt:
///
/// [...]
///  ` OMPCanonicalLoop
///    `- ForStmt/CXXForRangeStmt
///       `- CompoundStmt
///          |- Leading in-between code (if any)
///          |- OMPCanonicalLoop
///          |  `- ForStmt/CXXForRangeStmt
///          |     `- ...
///          `- Trailing in-between code (if any)
///
/// The leading/trailing in-between code must not itself be a OMPCanonicalLoop
/// to avoid confusion which loop belongs to the nesting.
///
/// There are three different kinds of iteration variables for different
/// purposes:
/// * Loop user variable: The user-accessible variable with different value for
///   each iteration.
/// * Loop iteration variable: The variable used to identify a loop iteration;
///   for range-based for-statement, this is the hidden iterator '__begin'. For
///   other loops, it is identical to the loop user variable. Must be a
///   random-access iterator, pointer or integer type.
/// * Logical iteration counter: Normalized loop counter starting at 0 and
///   incrementing by one at each iteration. Allows abstracting over the type
///   of the loop iteration variable and is always an unsigned integer type
///   appropriate to represent the range of the loop iteration variable. Its
///   value corresponds to the logical iteration number in the OpenMP
///   specification.
///
/// This AST node provides two captured statements:
/// * The distance function which computes the number of iterations.
/// * The loop user variable function that computes the loop user variable when
///   given a logical iteration number.
///
/// These captured statements provide the link between C/C++ semantics and the
/// logical iteration counters used by the OpenMPIRBuilder which is
/// language-agnostic and therefore does not know e.g. how to advance a
/// random-access iterator. The OpenMPIRBuilder will use this information to
/// apply simd, workshare-loop, distribute, taskloop and loop directives to the
/// loop. For compatibility with the non-OpenMPIRBuilder codegen path, an
/// OMPCanonicalLoop can itself also be wrapped into the CapturedStmts of an
/// OMPLoopDirective and skipped when searching for the associated syntactical
/// loop.
///
/// Example:
/// <code>
///   std::vector<std::string> Container{1,2,3};
///   for (std::string Str : Container)
///      Body(Str);
/// </code>
/// which is syntactic sugar for approximately:
/// <code>
///   auto &&__range = Container;
///   auto __begin = std::begin(__range);
///   auto __end = std::end(__range);
///   for (; __begin != __end; ++__begin) {
///     std::String Str = *__begin;
///     Body(Str);
///   }
/// </code>
/// In this example, the loop user variable is `Str`, the loop iteration
/// variable is `__begin` of type `std::vector<std::string>::iterator` and the
/// logical iteration number type is `size_t` (unsigned version of
/// `std::vector<std::string>::iterator::difference_type` aka `ptrdiff_t`).
/// Therefore, the distance function will be
/// <code>
///   [&](size_t &Result) { Result = __end - __begin; }
/// </code>
/// and the loop variable function is
/// <code>
///   [&,__begin](std::vector<std::string>::iterator &Result, size_t Logical) {
///     Result = __begin + Logical;
///   }
/// </code>
/// The variable `__begin`, aka the loop iteration variable, is captured by
/// value because it is modified in the loop body, but both functions require
/// the initial value. The OpenMP specification explicitly leaves unspecified
/// when the loop expressions are evaluated such that a capture by reference is
/// sufficient.
class OMPCanonicalLoop : public Stmt {
  friend class ASTStmtReader;
  friend class ASTStmtWriter;

  /// Children of this AST node.
  enum {
    LOOP_STMT,
    DISTANCE_FUNC,
    LOOPVAR_FUNC,
    LOOPVAR_REF,
    LastSubStmt = LOOPVAR_REF
  };

private:
  /// This AST node's children.
  Stmt *SubStmts[LastSubStmt + 1] = {};

  OMPCanonicalLoop() : Stmt(StmtClass::OMPCanonicalLoopClass) {}

public:
  /// Create a new OMPCanonicalLoop.
  static OMPCanonicalLoop *create(const ASTContext &Ctx, Stmt *LoopStmt,
                                  CapturedStmt *DistanceFunc,
                                  CapturedStmt *LoopVarFunc,
                                  DeclRefExpr *LoopVarRef) {
    OMPCanonicalLoop *S = new (Ctx) OMPCanonicalLoop();
    S->setLoopStmt(LoopStmt);
    S->setDistanceFunc(DistanceFunc);
    S->setLoopVarFunc(LoopVarFunc);
    S->setLoopVarRef(LoopVarRef);
    return S;
  }

  /// Create an empty OMPCanonicalLoop for deserialization.
  static OMPCanonicalLoop *createEmpty(const ASTContext &Ctx) {
    return new (Ctx) OMPCanonicalLoop();
  }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() == StmtClass::OMPCanonicalLoopClass;
  }

  SourceLocation getBeginLoc() const { return getLoopStmt()->getBeginLoc(); }
  SourceLocation getEndLoc() const { return getLoopStmt()->getEndLoc(); }

  /// Return this AST node's children.
  /// @{
  child_range children() {
    return child_range(&SubStmts[0], &SubStmts[0] + LastSubStmt + 1);
  }
  const_child_range children() const {
    return const_child_range(&SubStmts[0], &SubStmts[0] + LastSubStmt + 1);
  }
  /// @}

  /// The wrapped syntactic loop statement (ForStmt or CXXForRangeStmt).
  /// @{
  Stmt *getLoopStmt() { return SubStmts[LOOP_STMT]; }
  const Stmt *getLoopStmt() const { return SubStmts[LOOP_STMT]; }
  void setLoopStmt(Stmt *S) {
    assert((isa<ForStmt>(S) || isa<CXXForRangeStmt>(S)) &&
           "Canonical loop must be a for loop (range-based or otherwise)");
    SubStmts[LOOP_STMT] = S;
  }
  /// @}

  /// The function that computes the number of loop iterations. Can be evaluated
  /// before entering the loop but after the syntactical loop's init
  /// statement(s).
  ///
  /// Function signature: void(LogicalTy &Result)
  /// Any values necessary to compute the distance are captures of the closure.
  /// @{
  CapturedStmt *getDistanceFunc() {
    return cast<CapturedStmt>(SubStmts[DISTANCE_FUNC]);
  }
  const CapturedStmt *getDistanceFunc() const {
    return cast<CapturedStmt>(SubStmts[DISTANCE_FUNC]);
  }
  void setDistanceFunc(CapturedStmt *S) {
    assert(S && "Expected non-null captured statement");
    SubStmts[DISTANCE_FUNC] = S;
  }
  /// @}

  /// The function that computes the loop user variable from a logical iteration
  /// counter. Can be evaluated as first statement in the loop.
  ///
  /// Function signature: void(LoopVarTy &Result, LogicalTy Number)
  /// Any other values required to compute the loop user variable (such as start
  /// value, step size) are captured by the closure. In particular, the initial
  /// value of loop iteration variable is captured by value to be unaffected by
  /// previous iterations.
  /// @{
  CapturedStmt *getLoopVarFunc() {
    return cast<CapturedStmt>(SubStmts[LOOPVAR_FUNC]);
  }
  const CapturedStmt *getLoopVarFunc() const {
    return cast<CapturedStmt>(SubStmts[LOOPVAR_FUNC]);
  }
  void setLoopVarFunc(CapturedStmt *S) {
    assert(S && "Expected non-null captured statement");
    SubStmts[LOOPVAR_FUNC] = S;
  }
  /// @}

  /// Reference to the loop user variable as accessed in the loop body.
  /// @{
  DeclRefExpr *getLoopVarRef() {
    return cast<DeclRefExpr>(SubStmts[LOOPVAR_REF]);
  }
  const DeclRefExpr *getLoopVarRef() const {
    return cast<DeclRefExpr>(SubStmts[LOOPVAR_REF]);
  }
  void setLoopVarRef(DeclRefExpr *E) {
    assert(E && "Expected non-null loop variable");
    SubStmts[LOOPVAR_REF] = E;
  }
  /// @}
};

/// This is a basic class for representing single OpenMP executable
/// directive.
///
class OMPExecutableDirective : public Stmt {
  friend class ASTStmtReader;
  friend class ASTStmtWriter;

  /// Kind of the directive.
  OpenMPDirectiveKind Kind = llvm::omp::OMPD_unknown;
  /// Starting location of the directive (directive keyword).
  SourceLocation StartLoc;
  /// Ending location of the directive.
  SourceLocation EndLoc;

  /// Get the clauses storage.
  MutableArrayRef<OMPClause *> getClauses() {
    if (!Data)
      return std::nullopt;
    return Data->getClauses();
  }

  /// Was this directive mapped from an another directive?
  /// e.g. 1) omp loop bind(parallel) is mapped to OMPD_for
  ///      2) omp loop bind(teams) is mapped to OMPD_distribute
  ///      3) omp loop bind(thread) is mapped to OMPD_simd
  /// It was necessary to note it down in the Directive because of
  /// clang::TreeTransform::TransformOMPExecutableDirective() pass in
  /// the frontend.
  OpenMPDirectiveKind PrevMappedDirective = llvm::omp::OMPD_unknown;

protected:
  /// Data, associated with the directive.
  OMPChildren *Data = nullptr;

  /// Build instance of directive of class \a K.
  ///
  /// \param SC Statement class.
  /// \param K Kind of OpenMP directive.
  /// \param StartLoc Starting location of the directive (directive keyword).
  /// \param EndLoc Ending location of the directive.
  ///
  OMPExecutableDirective(StmtClass SC, OpenMPDirectiveKind K,
                         SourceLocation StartLoc, SourceLocation EndLoc)
      : Stmt(SC), Kind(K), StartLoc(std::move(StartLoc)),
        EndLoc(std::move(EndLoc)) {}

  template <typename T, typename... Params>
  static T *createDirective(const ASTContext &C, ArrayRef<OMPClause *> Clauses,
                            Stmt *AssociatedStmt, unsigned NumChildren,
                            Params &&... P) {
    void *Mem =
        C.Allocate(sizeof(T) + OMPChildren::size(Clauses.size(), AssociatedStmt,
                                                 NumChildren),
                   alignof(T));

    auto *Data = OMPChildren::Create(reinterpret_cast<T *>(Mem) + 1, Clauses,
                                     AssociatedStmt, NumChildren);
    auto *Inst = new (Mem) T(std::forward<Params>(P)...);
    Inst->Data = Data;
    return Inst;
  }

  template <typename T, typename... Params>
  static T *createEmptyDirective(const ASTContext &C, unsigned NumClauses,
                                 bool HasAssociatedStmt, unsigned NumChildren,
                                 Params &&... P) {
    void *Mem =
        C.Allocate(sizeof(T) + OMPChildren::size(NumClauses, HasAssociatedStmt,
                                                 NumChildren),
                   alignof(T));
    auto *Data =
        OMPChildren::CreateEmpty(reinterpret_cast<T *>(Mem) + 1, NumClauses,
                                 HasAssociatedStmt, NumChildren);
    auto *Inst = new (Mem) T(std::forward<Params>(P)...);
    Inst->Data = Data;
    return Inst;
  }

  template <typename T>
  static T *createEmptyDirective(const ASTContext &C, unsigned NumClauses,
                                 bool HasAssociatedStmt = false,
                                 unsigned NumChildren = 0) {
    void *Mem =
        C.Allocate(sizeof(T) + OMPChildren::size(NumClauses, HasAssociatedStmt,
                                                 NumChildren),
                   alignof(T));
    auto *Data =
        OMPChildren::CreateEmpty(reinterpret_cast<T *>(Mem) + 1, NumClauses,
                                 HasAssociatedStmt, NumChildren);
    auto *Inst = new (Mem) T;
    Inst->Data = Data;
    return Inst;
  }

  void setMappedDirective(OpenMPDirectiveKind MappedDirective) {
    PrevMappedDirective = MappedDirective;
  }

public:
  /// Iterates over expressions/statements used in the construct.
  class used_clauses_child_iterator
      : public llvm::iterator_adaptor_base<
            used_clauses_child_iterator, ArrayRef<OMPClause *>::iterator,
            std::forward_iterator_tag, Stmt *, ptrdiff_t, Stmt *, Stmt *> {
    ArrayRef<OMPClause *>::iterator End;
    OMPClause::child_iterator ChildI, ChildEnd;

    void MoveToNext() {
      if (ChildI != ChildEnd)
        return;
      while (this->I != End) {
        ++this->I;
        if (this->I != End) {
          ChildI = (*this->I)->used_children().begin();
          ChildEnd = (*this->I)->used_children().end();
          if (ChildI != ChildEnd)
            return;
        }
      }
    }

  public:
    explicit used_clauses_child_iterator(ArrayRef<OMPClause *> Clauses)
        : used_clauses_child_iterator::iterator_adaptor_base(Clauses.begin()),
          End(Clauses.end()) {
      if (this->I != End) {
        ChildI = (*this->I)->used_children().begin();
        ChildEnd = (*this->I)->used_children().end();
        MoveToNext();
      }
    }
    Stmt *operator*() const { return *ChildI; }
    Stmt *operator->() const { return **this; }

    used_clauses_child_iterator &operator++() {
      ++ChildI;
      if (ChildI != ChildEnd)
        return *this;
      if (this->I != End) {
        ++this->I;
        if (this->I != End) {
          ChildI = (*this->I)->used_children().begin();
          ChildEnd = (*this->I)->used_children().end();
        }
      }
      MoveToNext();
      return *this;
    }
  };

  static llvm::iterator_range<used_clauses_child_iterator>
  used_clauses_children(ArrayRef<OMPClause *> Clauses) {
    return {
        used_clauses_child_iterator(Clauses),
        used_clauses_child_iterator(llvm::ArrayRef(Clauses.end(), (size_t)0))};
  }

  /// Iterates over a filtered subrange of clauses applied to a
  /// directive.
  ///
  /// This iterator visits only clauses of type SpecificClause.
  template <typename SpecificClause>
  class specific_clause_iterator
      : public llvm::iterator_adaptor_base<
            specific_clause_iterator<SpecificClause>,
            ArrayRef<OMPClause *>::const_iterator, std::forward_iterator_tag,
            const SpecificClause *, ptrdiff_t, const SpecificClause *,
            const SpecificClause *> {
    ArrayRef<OMPClause *>::const_iterator End;

    void SkipToNextClause() {
      while (this->I != End && !isa<SpecificClause>(*this->I))
        ++this->I;
    }

  public:
    explicit specific_clause_iterator(ArrayRef<OMPClause *> Clauses)
        : specific_clause_iterator::iterator_adaptor_base(Clauses.begin()),
          End(Clauses.end()) {
      SkipToNextClause();
    }

    const SpecificClause *operator*() const {
      return cast<SpecificClause>(*this->I);
    }
    const SpecificClause *operator->() const { return **this; }

    specific_clause_iterator &operator++() {
      ++this->I;
      SkipToNextClause();
      return *this;
    }
  };

  template <typename SpecificClause>
  static llvm::iterator_range<specific_clause_iterator<SpecificClause>>
  getClausesOfKind(ArrayRef<OMPClause *> Clauses) {
    return {specific_clause_iterator<SpecificClause>(Clauses),
            specific_clause_iterator<SpecificClause>(
                llvm::ArrayRef(Clauses.end(), (size_t)0))};
  }

  template <typename SpecificClause>
  llvm::iterator_range<specific_clause_iterator<SpecificClause>>
  getClausesOfKind() const {
    return getClausesOfKind<SpecificClause>(clauses());
  }

  /// Gets a single clause of the specified kind associated with the
  /// current directive iff there is only one clause of this kind (and assertion
  /// is fired if there is more than one clause is associated with the
  /// directive). Returns nullptr if no clause of this kind is associated with
  /// the directive.
  template <typename SpecificClause>
  static const SpecificClause *getSingleClause(ArrayRef<OMPClause *> Clauses) {
    auto ClausesOfKind = getClausesOfKind<SpecificClause>(Clauses);

    if (ClausesOfKind.begin() != ClausesOfKind.end()) {
      assert(std::next(ClausesOfKind.begin()) == ClausesOfKind.end() &&
             "There are at least 2 clauses of the specified kind");
      return *ClausesOfKind.begin();
    }
    return nullptr;
  }

  template <typename SpecificClause>
  const SpecificClause *getSingleClause() const {
    return getSingleClause<SpecificClause>(clauses());
  }

  /// Returns true if the current directive has one or more clauses of a
  /// specific kind.
  template <typename SpecificClause>
  bool hasClausesOfKind() const {
    auto Clauses = getClausesOfKind<SpecificClause>();
    return Clauses.begin() != Clauses.end();
  }

  /// Returns starting location of directive kind.
  SourceLocation getBeginLoc() const { return StartLoc; }
  /// Returns ending location of directive.
  SourceLocation getEndLoc() const { return EndLoc; }

  /// Set starting location of directive kind.
  ///
  /// \param Loc New starting location of directive.
  ///
  void setLocStart(SourceLocation Loc) { StartLoc = Loc; }
  /// Set ending location of directive.
  ///
  /// \param Loc New ending location of directive.
  ///
  void setLocEnd(SourceLocation Loc) { EndLoc = Loc; }

  /// Get number of clauses.
  unsigned getNumClauses() const {
    if (!Data)
      return 0;
    return Data->getNumClauses();
  }

  /// Returns specified clause.
  ///
  /// \param I Number of clause.
  ///
  OMPClause *getClause(unsigned I) const { return clauses()[I]; }

  /// Returns true if directive has associated statement.
  bool hasAssociatedStmt() const { return Data && Data->hasAssociatedStmt(); }

  /// Returns statement associated with the directive.
  const Stmt *getAssociatedStmt() const {
    return const_cast<OMPExecutableDirective *>(this)->getAssociatedStmt();
  }
  Stmt *getAssociatedStmt() {
    assert(hasAssociatedStmt() &&
           "Expected directive with the associated statement.");
    return Data->getAssociatedStmt();
  }

  /// Returns the captured statement associated with the
  /// component region within the (combined) directive.
  ///
  /// \param RegionKind Component region kind.
  const CapturedStmt *getCapturedStmt(OpenMPDirectiveKind RegionKind) const {
    assert(hasAssociatedStmt() &&
           "Expected directive with the associated statement.");
    SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
    getOpenMPCaptureRegions(CaptureRegions, getDirectiveKind());
    return Data->getCapturedStmt(RegionKind, CaptureRegions);
  }

  /// Get innermost captured statement for the construct.
  CapturedStmt *getInnermostCapturedStmt() {
    assert(hasAssociatedStmt() &&
           "Expected directive with the associated statement.");
    SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
    getOpenMPCaptureRegions(CaptureRegions, getDirectiveKind());
    return Data->getInnermostCapturedStmt(CaptureRegions);
  }

  const CapturedStmt *getInnermostCapturedStmt() const {
    return const_cast<OMPExecutableDirective *>(this)
        ->getInnermostCapturedStmt();
  }

  OpenMPDirectiveKind getDirectiveKind() const { return Kind; }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() >= firstOMPExecutableDirectiveConstant &&
           S->getStmtClass() <= lastOMPExecutableDirectiveConstant;
  }

  child_range children() {
    if (!Data)
      return child_range(child_iterator(), child_iterator());
    return Data->getAssociatedStmtAsRange();
  }

  const_child_range children() const {
    return const_cast<OMPExecutableDirective *>(this)->children();
  }

  ArrayRef<OMPClause *> clauses() const {
    if (!Data)
      return std::nullopt;
    return Data->getClauses();
  }

  /// Returns whether or not this is a Standalone directive.
  ///
  /// Stand-alone directives are executable directives
  /// that have no associated user code.
  bool isStandaloneDirective() const;

  /// Returns the AST node representing OpenMP structured-block of this
  /// OpenMP executable directive,
  /// Prerequisite: Executable Directive must not be Standalone directive.
  const Stmt *getStructuredBlock() const {
    return const_cast<OMPExecutableDirective *>(this)->getStructuredBlock();
  }
  Stmt *getStructuredBlock();

  const Stmt *getRawStmt() const {
    return const_cast<OMPExecutableDirective *>(this)->getRawStmt();
  }
  Stmt *getRawStmt() {
    assert(hasAssociatedStmt() &&
           "Expected directive with the associated statement.");
    return Data->getRawStmt();
  }

  OpenMPDirectiveKind getMappedDirective() const { return PrevMappedDirective; }
};

/// This represents '#pragma omp parallel' directive.
///
/// \code
/// #pragma omp parallel private(a,b) reduction(+: c,d)
/// \endcode
/// In this example directive '#pragma omp parallel' has clauses 'private'
/// with the variables 'a' and 'b' and 'reduction' with operator '+' and
/// variables 'c' and 'd'.
///
class OMPParallelDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive (directive keyword).
  /// \param EndLoc Ending Location of the directive.
  ///
  OMPParallelDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPParallelDirectiveClass,
                               llvm::omp::OMPD_parallel, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPParallelDirective()
      : OMPExecutableDirective(OMPParallelDirectiveClass,
                               llvm::omp::OMPD_parallel, SourceLocation(),
                               SourceLocation()) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) { Data->getChildren()[0] = E; }

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement associated with the directive.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPParallelDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef,
         bool HasCancel);

  /// Creates an empty directive with the place for \a N clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses, EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[0]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPParallelDirective *>(this)->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelDirectiveClass;
  }
};

/// The base class for all loop-based directives, including loop transformation
/// directives.
class OMPLoopBasedDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;

protected:
  /// Number of collapsed loops as specified by 'collapse' clause.
  unsigned NumAssociatedLoops = 0;

  /// Build instance of loop directive of class \a Kind.
  ///
  /// \param SC Statement class.
  /// \param Kind Kind of OpenMP directive.
  /// \param StartLoc Starting location of the directive (directive keyword).
  /// \param EndLoc Ending location of the directive.
  /// \param NumAssociatedLoops Number of loops associated with the construct.
  ///
  OMPLoopBasedDirective(StmtClass SC, OpenMPDirectiveKind Kind,
                        SourceLocation StartLoc, SourceLocation EndLoc,
                        unsigned NumAssociatedLoops)
      : OMPExecutableDirective(SC, Kind, StartLoc, EndLoc),
        NumAssociatedLoops(NumAssociatedLoops) {}

public:
  /// The expressions built to support OpenMP loops in combined/composite
  /// pragmas (e.g. pragma omp distribute parallel for)
  struct DistCombinedHelperExprs {
    /// DistributeLowerBound - used when composing 'omp distribute' with
    /// 'omp for' in a same construct.
    Expr *LB;
    /// DistributeUpperBound - used when composing 'omp distribute' with
    /// 'omp for' in a same construct.
    Expr *UB;
    /// DistributeEnsureUpperBound - used when composing 'omp distribute'
    ///  with 'omp for' in a same construct, EUB depends on DistUB
    Expr *EUB;
    /// Distribute loop iteration variable init used when composing 'omp
    /// distribute'
    ///  with 'omp for' in a same construct
    Expr *Init;
    /// Distribute Loop condition used when composing 'omp distribute'
    ///  with 'omp for' in a same construct
    Expr *Cond;
    /// Update of LowerBound for statically scheduled omp loops for
    /// outer loop in combined constructs (e.g. 'distribute parallel for')
    Expr *NLB;
    /// Update of UpperBound for statically scheduled omp loops for
    /// outer loop in combined constructs (e.g. 'distribute parallel for')
    Expr *NUB;
    /// Distribute Loop condition used when composing 'omp distribute'
    ///  with 'omp for' in a same construct when schedule is chunked.
    Expr *DistCond;
    /// 'omp parallel for' loop condition used when composed with
    /// 'omp distribute' in the same construct and when schedule is
    /// chunked and the chunk size is 1.
    Expr *ParForInDistCond;
  };

  /// The expressions built for the OpenMP loop CodeGen for the
  /// whole collapsed loop nest.
  struct HelperExprs {
    /// Loop iteration variable.
    Expr *IterationVarRef;
    /// Loop last iteration number.
    Expr *LastIteration;
    /// Loop number of iterations.
    Expr *NumIterations;
    /// Calculation of last iteration.
    Expr *CalcLastIteration;
    /// Loop pre-condition.
    Expr *PreCond;
    /// Loop condition.
    Expr *Cond;
    /// Loop iteration variable init.
    Expr *Init;
    /// Loop increment.
    Expr *Inc;
    /// IsLastIteration - local flag variable passed to runtime.
    Expr *IL;
    /// LowerBound - local variable passed to runtime.
    Expr *LB;
    /// UpperBound - local variable passed to runtime.
    Expr *UB;
    /// Stride - local variable passed to runtime.
    Expr *ST;
    /// EnsureUpperBound -- expression UB = min(UB, NumIterations).
    Expr *EUB;
    /// Update of LowerBound for statically scheduled 'omp for' loops.
    Expr *NLB;
    /// Update of UpperBound for statically scheduled 'omp for' loops.
    Expr *NUB;
    /// PreviousLowerBound - local variable passed to runtime in the
    /// enclosing schedule or null if that does not apply.
    Expr *PrevLB;
    /// PreviousUpperBound - local variable passed to runtime in the
    /// enclosing schedule or null if that does not apply.
    Expr *PrevUB;
    /// DistInc - increment expression for distribute loop when found
    /// combined with a further loop level (e.g. in 'distribute parallel for')
    /// expression IV = IV + ST
    Expr *DistInc;
    /// PrevEUB - expression similar to EUB but to be used when loop
    /// scheduling uses PrevLB and PrevUB (e.g.  in 'distribute parallel for'
    /// when ensuring that the UB is either the calculated UB by the runtime or
    /// the end of the assigned distribute chunk)
    /// expression UB = min (UB, PrevUB)
    Expr *PrevEUB;
    /// Counters Loop counters.
    SmallVector<Expr *, 4> Counters;
    /// PrivateCounters Loop counters.
    SmallVector<Expr *, 4> PrivateCounters;
    /// Expressions for loop counters inits for CodeGen.
    SmallVector<Expr *, 4> Inits;
    /// Expressions for loop counters update for CodeGen.
    SmallVector<Expr *, 4> Updates;
    /// Final loop counter values for GodeGen.
    SmallVector<Expr *, 4> Finals;
    /// List of counters required for the generation of the non-rectangular
    /// loops.
    SmallVector<Expr *, 4> DependentCounters;
    /// List of initializers required for the generation of the non-rectangular
    /// loops.
    SmallVector<Expr *, 4> DependentInits;
    /// List of final conditions required for the generation of the
    /// non-rectangular loops.
    SmallVector<Expr *, 4> FinalsConditions;
    /// Init statement for all captured expressions.
    Stmt *PreInits;

    /// Expressions used when combining OpenMP loop pragmas
    DistCombinedHelperExprs DistCombinedFields;

    /// Check if all the expressions are built (does not check the
    /// worksharing ones).
    bool builtAll() {
      return IterationVarRef != nullptr && LastIteration != nullptr &&
             NumIterations != nullptr && PreCond != nullptr &&
             Cond != nullptr && Init != nullptr && Inc != nullptr;
    }

    /// Initialize all the fields to null.
    /// \param Size Number of elements in the
    /// counters/finals/updates/dependent_counters/dependent_inits/finals_conditions
    /// arrays.
    void clear(unsigned Size) {
      IterationVarRef = nullptr;
      LastIteration = nullptr;
      CalcLastIteration = nullptr;
      PreCond = nullptr;
      Cond = nullptr;
      Init = nullptr;
      Inc = nullptr;
      IL = nullptr;
      LB = nullptr;
      UB = nullptr;
      ST = nullptr;
      EUB = nullptr;
      NLB = nullptr;
      NUB = nullptr;
      NumIterations = nullptr;
      PrevLB = nullptr;
      PrevUB = nullptr;
      DistInc = nullptr;
      PrevEUB = nullptr;
      Counters.resize(Size);
      PrivateCounters.resize(Size);
      Inits.resize(Size);
      Updates.resize(Size);
      Finals.resize(Size);
      DependentCounters.resize(Size);
      DependentInits.resize(Size);
      FinalsConditions.resize(Size);
      for (unsigned I = 0; I < Size; ++I) {
        Counters[I] = nullptr;
        PrivateCounters[I] = nullptr;
        Inits[I] = nullptr;
        Updates[I] = nullptr;
        Finals[I] = nullptr;
        DependentCounters[I] = nullptr;
        DependentInits[I] = nullptr;
        FinalsConditions[I] = nullptr;
      }
      PreInits = nullptr;
      DistCombinedFields.LB = nullptr;
      DistCombinedFields.UB = nullptr;
      DistCombinedFields.EUB = nullptr;
      DistCombinedFields.Init = nullptr;
      DistCombinedFields.Cond = nullptr;
      DistCombinedFields.NLB = nullptr;
      DistCombinedFields.NUB = nullptr;
      DistCombinedFields.DistCond = nullptr;
      DistCombinedFields.ParForInDistCond = nullptr;
    }
  };

  /// Get number of collapsed loops.
  unsigned getLoopsNumber() const { return NumAssociatedLoops; }

  /// Try to find the next loop sub-statement in the specified statement \p
  /// CurStmt.
  /// \param TryImperfectlyNestedLoops true, if we need to try to look for the
  /// imperfectly nested loop.
  static Stmt *tryToFindNextInnerLoop(Stmt *CurStmt,
                                      bool TryImperfectlyNestedLoops);
  static const Stmt *tryToFindNextInnerLoop(const Stmt *CurStmt,
                                            bool TryImperfectlyNestedLoops) {
    return tryToFindNextInnerLoop(const_cast<Stmt *>(CurStmt),
                                  TryImperfectlyNestedLoops);
  }

  /// Calls the specified callback function for all the loops in \p CurStmt,
  /// from the outermost to the innermost.
  static bool
  doForAllLoops(Stmt *CurStmt, bool TryImperfectlyNestedLoops,
                unsigned NumLoops,
                llvm::function_ref<bool(unsigned, Stmt *)> Callback,
                llvm::function_ref<void(OMPLoopTransformationDirective *)>
                    OnTransformationCallback);
  static bool
  doForAllLoops(const Stmt *CurStmt, bool TryImperfectlyNestedLoops,
                unsigned NumLoops,
                llvm::function_ref<bool(unsigned, const Stmt *)> Callback,
                llvm::function_ref<void(const OMPLoopTransformationDirective *)>
                    OnTransformationCallback) {
    auto &&NewCallback = [Callback](unsigned Cnt, Stmt *CurStmt) {
      return Callback(Cnt, CurStmt);
    };
    auto &&NewTransformCb =
        [OnTransformationCallback](OMPLoopTransformationDirective *A) {
          OnTransformationCallback(A);
        };
    return doForAllLoops(const_cast<Stmt *>(CurStmt), TryImperfectlyNestedLoops,
                         NumLoops, NewCallback, NewTransformCb);
  }

  /// Calls the specified callback function for all the loops in \p CurStmt,
  /// from the outermost to the innermost.
  static bool
  doForAllLoops(Stmt *CurStmt, bool TryImperfectlyNestedLoops,
                unsigned NumLoops,
                llvm::function_ref<bool(unsigned, Stmt *)> Callback) {
    auto &&TransformCb = [](OMPLoopTransformationDirective *) {};
    return doForAllLoops(CurStmt, TryImperfectlyNestedLoops, NumLoops, Callback,
                         TransformCb);
  }
  static bool
  doForAllLoops(const Stmt *CurStmt, bool TryImperfectlyNestedLoops,
                unsigned NumLoops,
                llvm::function_ref<bool(unsigned, const Stmt *)> Callback) {
    auto &&NewCallback = [Callback](unsigned Cnt, const Stmt *CurStmt) {
      return Callback(Cnt, CurStmt);
    };
    return doForAllLoops(const_cast<Stmt *>(CurStmt), TryImperfectlyNestedLoops,
                         NumLoops, NewCallback);
  }

  /// Calls the specified callback function for all the loop bodies in \p
  /// CurStmt, from the outermost loop to the innermost.
  static void doForAllLoopsBodies(
      Stmt *CurStmt, bool TryImperfectlyNestedLoops, unsigned NumLoops,
      llvm::function_ref<void(unsigned, Stmt *, Stmt *)> Callback);
  static void doForAllLoopsBodies(
      const Stmt *CurStmt, bool TryImperfectlyNestedLoops, unsigned NumLoops,
      llvm::function_ref<void(unsigned, const Stmt *, const Stmt *)> Callback) {
    auto &&NewCallback = [Callback](unsigned Cnt, Stmt *Loop, Stmt *Body) {
      Callback(Cnt, Loop, Body);
    };
    doForAllLoopsBodies(const_cast<Stmt *>(CurStmt), TryImperfectlyNestedLoops,
                        NumLoops, NewCallback);
  }

  static bool classof(const Stmt *T) {
    if (auto *D = dyn_cast<OMPExecutableDirective>(T))
      return isOpenMPLoopDirective(D->getDirectiveKind());
    return false;
  }
};

/// The base class for all loop transformation directives.
class OMPLoopTransformationDirective : public OMPLoopBasedDirective {
  friend class ASTStmtReader;

  /// Number of loops generated by this loop transformation.
  unsigned NumGeneratedLoops = 0;

protected:
  explicit OMPLoopTransformationDirective(StmtClass SC,
                                          OpenMPDirectiveKind Kind,
                                          SourceLocation StartLoc,
                                          SourceLocation EndLoc,
                                          unsigned NumAssociatedLoops)
      : OMPLoopBasedDirective(SC, Kind, StartLoc, EndLoc, NumAssociatedLoops) {}

  /// Set the number of loops generated by this loop transformation.
  void setNumGeneratedLoops(unsigned Num) { NumGeneratedLoops = Num; }

public:
  /// Return the number of associated (consumed) loops.
  unsigned getNumAssociatedLoops() const { return getLoopsNumber(); }

  /// Return the number of loops generated by this loop transformation.
  unsigned getNumGeneratedLoops() const { return NumGeneratedLoops; }

  /// Get the de-sugared statements after the loop transformation.
  ///
  /// Might be nullptr if either the directive generates no loops and is handled
  /// directly in CodeGen, or resolving a template-dependence context is
  /// required.
  Stmt *getTransformedStmt() const;

  /// Return preinits statement.
  Stmt *getPreInits() const;

  static bool classof(const Stmt *T) {
    Stmt::StmtClass C = T->getStmtClass();
    return C == OMPTileDirectiveClass || C == OMPUnrollDirectiveClass ||
           C == OMPReverseDirectiveClass || C == OMPInterchangeDirectiveClass;
  }
};

/// This is a common base class for loop directives ('omp simd', 'omp
/// for', 'omp for simd' etc.). It is responsible for the loop code generation.
///
class OMPLoopDirective : public OMPLoopBasedDirective {
  friend class ASTStmtReader;

  /// Offsets to the stored exprs.
  /// This enumeration contains offsets to all the pointers to children
  /// expressions stored in OMPLoopDirective.
  /// The first 9 children are necessary for all the loop directives,
  /// the next 8 are specific to the worksharing ones, and the next 11 are
  /// used for combined constructs containing two pragmas associated to loops.
  /// After the fixed children, three arrays of length NumAssociatedLoops are
  /// allocated: loop counters, their updates and final values.
  /// PrevLowerBound and PrevUpperBound are used to communicate blocking
  /// information in composite constructs which require loop blocking
  /// DistInc is used to generate the increment expression for the distribute
  /// loop when combined with a further nested loop
  /// PrevEnsureUpperBound is used as the EnsureUpperBound expression for the
  /// for loop when combined with a previous distribute loop in the same pragma
  /// (e.g. 'distribute parallel for')
  ///
  enum {
    IterationVariableOffset = 0,
    LastIterationOffset = 1,
    CalcLastIterationOffset = 2,
    PreConditionOffset = 3,
    CondOffset = 4,
    InitOffset = 5,
    IncOffset = 6,
    PreInitsOffset = 7,
    // The '...End' enumerators do not correspond to child expressions - they
    // specify the offset to the end (and start of the following counters/
    // updates/finals/dependent_counters/dependent_inits/finals_conditions
    // arrays).
    DefaultEnd = 8,
    // The following 8 exprs are used by worksharing and distribute loops only.
    IsLastIterVariableOffset = 8,
    LowerBoundVariableOffset = 9,
    UpperBoundVariableOffset = 10,
    StrideVariableOffset = 11,
    EnsureUpperBoundOffset = 12,
    NextLowerBoundOffset = 13,
    NextUpperBoundOffset = 14,
    NumIterationsOffset = 15,
    // Offset to the end for worksharing loop directives.
    WorksharingEnd = 16,
    PrevLowerBoundVariableOffset = 16,
    PrevUpperBoundVariableOffset = 17,
    DistIncOffset = 18,
    PrevEnsureUpperBoundOffset = 19,
    CombinedLowerBoundVariableOffset = 20,
    CombinedUpperBoundVariableOffset = 21,
    CombinedEnsureUpperBoundOffset = 22,
    CombinedInitOffset = 23,
    CombinedConditionOffset = 24,
    CombinedNextLowerBoundOffset = 25,
    CombinedNextUpperBoundOffset = 26,
    CombinedDistConditionOffset = 27,
    CombinedParForInDistConditionOffset = 28,
    // Offset to the end (and start of the following
    // counters/updates/finals/dependent_counters/dependent_inits/finals_conditions
    // arrays) for combined distribute loop directives.
    CombinedDistributeEnd = 29,
  };

  /// Get the counters storage.
  MutableArrayRef<Expr *> getCounters() {
    auto **Storage = reinterpret_cast<Expr **>(
        &Data->getChildren()[getArraysOffset(getDirectiveKind())]);
    return llvm::MutableArrayRef(Storage, getLoopsNumber());
  }

  /// Get the private counters storage.
  MutableArrayRef<Expr *> getPrivateCounters() {
    auto **Storage = reinterpret_cast<Expr **>(
        &Data->getChildren()[getArraysOffset(getDirectiveKind()) +
                             getLoopsNumber()]);
    return llvm::MutableArrayRef(Storage, getLoopsNumber());
  }

  /// Get the updates storage.
  MutableArrayRef<Expr *> getInits() {
    auto **Storage = reinterpret_cast<Expr **>(
        &Data->getChildren()[getArraysOffset(getDirectiveKind()) +
                             2 * getLoopsNumber()]);
    return llvm::MutableArrayRef(Storage, getLoopsNumber());
  }

  /// Get the updates storage.
  MutableArrayRef<Expr *> getUpdates() {
    auto **Storage = reinterpret_cast<Expr **>(
        &Data->getChildren()[getArraysOffset(getDirectiveKind()) +
                             3 * getLoopsNumber()]);
    return llvm::MutableArrayRef(Storage, getLoopsNumber());
  }

  /// Get the final counter updates storage.
  MutableArrayRef<Expr *> getFinals() {
    auto **Storage = reinterpret_cast<Expr **>(
        &Data->getChildren()[getArraysOffset(getDirectiveKind()) +
                             4 * getLoopsNumber()]);
    return llvm::MutableArrayRef(Storage, getLoopsNumber());
  }

  /// Get the dependent counters storage.
  MutableArrayRef<Expr *> getDependentCounters() {
    auto **Storage = reinterpret_cast<Expr **>(
        &Data->getChildren()[getArraysOffset(getDirectiveKind()) +
                             5 * getLoopsNumber()]);
    return llvm::MutableArrayRef(Storage, getLoopsNumber());
  }

  /// Get the dependent inits storage.
  MutableArrayRef<Expr *> getDependentInits() {
    auto **Storage = reinterpret_cast<Expr **>(
        &Data->getChildren()[getArraysOffset(getDirectiveKind()) +
                             6 * getLoopsNumber()]);
    return llvm::MutableArrayRef(Storage, getLoopsNumber());
  }

  /// Get the finals conditions storage.
  MutableArrayRef<Expr *> getFinalsConditions() {
    auto **Storage = reinterpret_cast<Expr **>(
        &Data->getChildren()[getArraysOffset(getDirectiveKind()) +
                             7 * getLoopsNumber()]);
    return llvm::MutableArrayRef(Storage, getLoopsNumber());
  }

protected:
  /// Build instance of loop directive of class \a Kind.
  ///
  /// \param SC Statement class.
  /// \param Kind Kind of OpenMP directive.
  /// \param StartLoc Starting location of the directive (directive keyword).
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed loops from 'collapse' clause.
  ///
  OMPLoopDirective(StmtClass SC, OpenMPDirectiveKind Kind,
                   SourceLocation StartLoc, SourceLocation EndLoc,
                   unsigned CollapsedNum)
      : OMPLoopBasedDirective(SC, Kind, StartLoc, EndLoc, CollapsedNum) {}

  /// Offset to the start of children expression arrays.
  static unsigned getArraysOffset(OpenMPDirectiveKind Kind) {
    if (isOpenMPLoopBoundSharingDirective(Kind))
      return CombinedDistributeEnd;
    if (isOpenMPWorksharingDirective(Kind) || isOpenMPTaskLoopDirective(Kind) ||
        isOpenMPGenericLoopDirective(Kind) || isOpenMPDistributeDirective(Kind))
      return WorksharingEnd;
    return DefaultEnd;
  }

  /// Children number.
  static unsigned numLoopChildren(unsigned CollapsedNum,
                                  OpenMPDirectiveKind Kind) {
    return getArraysOffset(Kind) +
           8 * CollapsedNum; // Counters, PrivateCounters, Inits,
                             // Updates, Finals, DependentCounters,
                             // DependentInits, FinalsConditions.
  }

  void setIterationVariable(Expr *IV) {
    Data->getChildren()[IterationVariableOffset] = IV;
  }
  void setLastIteration(Expr *LI) {
    Data->getChildren()[LastIterationOffset] = LI;
  }
  void setCalcLastIteration(Expr *CLI) {
    Data->getChildren()[CalcLastIterationOffset] = CLI;
  }
  void setPreCond(Expr *PC) { Data->getChildren()[PreConditionOffset] = PC; }
  void setCond(Expr *Cond) { Data->getChildren()[CondOffset] = Cond; }
  void setInit(Expr *Init) { Data->getChildren()[InitOffset] = Init; }
  void setInc(Expr *Inc) { Data->getChildren()[IncOffset] = Inc; }
  void setPreInits(Stmt *PreInits) {
    Data->getChildren()[PreInitsOffset] = PreInits;
  }
  void setIsLastIterVariable(Expr *IL) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    Data->getChildren()[IsLastIterVariableOffset] = IL;
  }
  void setLowerBoundVariable(Expr *LB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    Data->getChildren()[LowerBoundVariableOffset] = LB;
  }
  void setUpperBoundVariable(Expr *UB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    Data->getChildren()[UpperBoundVariableOffset] = UB;
  }
  void setStrideVariable(Expr *ST) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    Data->getChildren()[StrideVariableOffset] = ST;
  }
  void setEnsureUpperBound(Expr *EUB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    Data->getChildren()[EnsureUpperBoundOffset] = EUB;
  }
  void setNextLowerBound(Expr *NLB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    Data->getChildren()[NextLowerBoundOffset] = NLB;
  }
  void setNextUpperBound(Expr *NUB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    Data->getChildren()[NextUpperBoundOffset] = NUB;
  }
  void setNumIterations(Expr *NI) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    Data->getChildren()[NumIterationsOffset] = NI;
  }
  void setPrevLowerBoundVariable(Expr *PrevLB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[PrevLowerBoundVariableOffset] = PrevLB;
  }
  void setPrevUpperBoundVariable(Expr *PrevUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[PrevUpperBoundVariableOffset] = PrevUB;
  }
  void setDistInc(Expr *DistInc) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[DistIncOffset] = DistInc;
  }
  void setPrevEnsureUpperBound(Expr *PrevEUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[PrevEnsureUpperBoundOffset] = PrevEUB;
  }
  void setCombinedLowerBoundVariable(Expr *CombLB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[CombinedLowerBoundVariableOffset] = CombLB;
  }
  void setCombinedUpperBoundVariable(Expr *CombUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[CombinedUpperBoundVariableOffset] = CombUB;
  }
  void setCombinedEnsureUpperBound(Expr *CombEUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[CombinedEnsureUpperBoundOffset] = CombEUB;
  }
  void setCombinedInit(Expr *CombInit) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[CombinedInitOffset] = CombInit;
  }
  void setCombinedCond(Expr *CombCond) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[CombinedConditionOffset] = CombCond;
  }
  void setCombinedNextLowerBound(Expr *CombNLB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[CombinedNextLowerBoundOffset] = CombNLB;
  }
  void setCombinedNextUpperBound(Expr *CombNUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    Data->getChildren()[CombinedNextUpperBoundOffset] = CombNUB;
  }
  void setCombinedDistCond(Expr *CombDistCond) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound distribute sharing directive");
    Data->getChildren()[CombinedDistConditionOffset] = CombDistCond;
  }
  void setCombinedParForInDistCond(Expr *CombParForInDistCond) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound distribute sharing directive");
    Data->getChildren()[CombinedParForInDistConditionOffset] =
        CombParForInDistCond;
  }
  void setCounters(ArrayRef<Expr *> A);
  void setPrivateCounters(ArrayRef<Expr *> A);
  void setInits(ArrayRef<Expr *> A);
  void setUpdates(ArrayRef<Expr *> A);
  void setFinals(ArrayRef<Expr *> A);
  void setDependentCounters(ArrayRef<Expr *> A);
  void setDependentInits(ArrayRef<Expr *> A);
  void setFinalsConditions(ArrayRef<Expr *> A);

public:
  Expr *getIterationVariable() const {
    return cast<Expr>(Data->getChildren()[IterationVariableOffset]);
  }
  Expr *getLastIteration() const {
    return cast<Expr>(Data->getChildren()[LastIterationOffset]);
  }
  Expr *getCalcLastIteration() const {
    return cast<Expr>(Data->getChildren()[CalcLastIterationOffset]);
  }
  Expr *getPreCond() const {
    return cast<Expr>(Data->getChildren()[PreConditionOffset]);
  }
  Expr *getCond() const { return cast<Expr>(Data->getChildren()[CondOffset]); }
  Expr *getInit() const { return cast<Expr>(Data->getChildren()[InitOffset]); }
  Expr *getInc() const { return cast<Expr>(Data->getChildren()[IncOffset]); }
  const Stmt *getPreInits() const {
    return Data->getChildren()[PreInitsOffset];
  }
  Stmt *getPreInits() { return Data->getChildren()[PreInitsOffset]; }
  Expr *getIsLastIterVariable() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return cast<Expr>(Data->getChildren()[IsLastIterVariableOffset]);
  }
  Expr *getLowerBoundVariable() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return cast<Expr>(Data->getChildren()[LowerBoundVariableOffset]);
  }
  Expr *getUpperBoundVariable() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return cast<Expr>(Data->getChildren()[UpperBoundVariableOffset]);
  }
  Expr *getStrideVariable() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return cast<Expr>(Data->getChildren()[StrideVariableOffset]);
  }
  Expr *getEnsureUpperBound() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return cast<Expr>(Data->getChildren()[EnsureUpperBoundOffset]);
  }
  Expr *getNextLowerBound() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return cast<Expr>(Data->getChildren()[NextLowerBoundOffset]);
  }
  Expr *getNextUpperBound() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return cast<Expr>(Data->getChildren()[NextUpperBoundOffset]);
  }
  Expr *getNumIterations() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPGenericLoopDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return cast<Expr>(Data->getChildren()[NumIterationsOffset]);
  }
  Expr *getPrevLowerBoundVariable() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[PrevLowerBoundVariableOffset]);
  }
  Expr *getPrevUpperBoundVariable() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[PrevUpperBoundVariableOffset]);
  }
  Expr *getDistInc() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[DistIncOffset]);
  }
  Expr *getPrevEnsureUpperBound() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[PrevEnsureUpperBoundOffset]);
  }
  Expr *getCombinedLowerBoundVariable() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[CombinedLowerBoundVariableOffset]);
  }
  Expr *getCombinedUpperBoundVariable() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[CombinedUpperBoundVariableOffset]);
  }
  Expr *getCombinedEnsureUpperBound() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[CombinedEnsureUpperBoundOffset]);
  }
  Expr *getCombinedInit() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[CombinedInitOffset]);
  }
  Expr *getCombinedCond() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[CombinedConditionOffset]);
  }
  Expr *getCombinedNextLowerBound() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[CombinedNextLowerBoundOffset]);
  }
  Expr *getCombinedNextUpperBound() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return cast<Expr>(Data->getChildren()[CombinedNextUpperBoundOffset]);
  }
  Expr *getCombinedDistCond() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound distribute sharing directive");
    return cast<Expr>(Data->getChildren()[CombinedDistConditionOffset]);
  }
  Expr *getCombinedParForInDistCond() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound distribute sharing directive");
    return cast<Expr>(Data->getChildren()[CombinedParForInDistConditionOffset]);
  }
  Stmt *getBody();
  const Stmt *getBody() const {
    return const_cast<OMPLoopDirective *>(this)->getBody();
  }

  ArrayRef<Expr *> counters() { return getCounters(); }

  ArrayRef<Expr *> counters() const {
    return const_cast<OMPLoopDirective *>(this)->getCounters();
  }

  ArrayRef<Expr *> private_counters() { return getPrivateCounters(); }

  ArrayRef<Expr *> private_counters() const {
    return const_cast<OMPLoopDirective *>(this)->getPrivateCounters();
  }

  ArrayRef<Expr *> inits() { return getInits(); }

  ArrayRef<Expr *> inits() const {
    return const_cast<OMPLoopDirective *>(this)->getInits();
  }

  ArrayRef<Expr *> updates() { return getUpdates(); }

  ArrayRef<Expr *> updates() const {
    return const_cast<OMPLoopDirective *>(this)->getUpdates();
  }

  ArrayRef<Expr *> finals() { return getFinals(); }

  ArrayRef<Expr *> finals() const {
    return const_cast<OMPLoopDirective *>(this)->getFinals();
  }

  ArrayRef<Expr *> dependent_counters() { return getDependentCounters(); }

  ArrayRef<Expr *> dependent_counters() const {
    return const_cast<OMPLoopDirective *>(this)->getDependentCounters();
  }

  ArrayRef<Expr *> dependent_inits() { return getDependentInits(); }

  ArrayRef<Expr *> dependent_inits() const {
    return const_cast<OMPLoopDirective *>(this)->getDependentInits();
  }

  ArrayRef<Expr *> finals_conditions() { return getFinalsConditions(); }

  ArrayRef<Expr *> finals_conditions() const {
    return const_cast<OMPLoopDirective *>(this)->getFinalsConditions();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSimdDirectiveClass ||
           T->getStmtClass() == OMPForDirectiveClass ||
           T->getStmtClass() == OMPForSimdDirectiveClass ||
           T->getStmtClass() == OMPParallelForDirectiveClass ||
           T->getStmtClass() == OMPParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPTaskLoopDirectiveClass ||
           T->getStmtClass() == OMPTaskLoopSimdDirectiveClass ||
           T->getStmtClass() == OMPMaskedTaskLoopDirectiveClass ||
           T->getStmtClass() == OMPMaskedTaskLoopSimdDirectiveClass ||
           T->getStmtClass() == OMPMasterTaskLoopDirectiveClass ||
           T->getStmtClass() == OMPMasterTaskLoopSimdDirectiveClass ||
           T->getStmtClass() == OMPGenericLoopDirectiveClass ||
           T->getStmtClass() == OMPTeamsGenericLoopDirectiveClass ||
           T->getStmtClass() == OMPTargetTeamsGenericLoopDirectiveClass ||
           T->getStmtClass() == OMPParallelGenericLoopDirectiveClass ||
           T->getStmtClass() == OMPTargetParallelGenericLoopDirectiveClass ||
           T->getStmtClass() == OMPParallelMaskedTaskLoopDirectiveClass ||
           T->getStmtClass() == OMPParallelMaskedTaskLoopSimdDirectiveClass ||
           T->getStmtClass() == OMPParallelMasterTaskLoopDirectiveClass ||
           T->getStmtClass() == OMPParallelMasterTaskLoopSimdDirectiveClass ||
           T->getStmtClass() == OMPDistributeDirectiveClass ||
           T->getStmtClass() == OMPTargetParallelForDirectiveClass ||
           T->getStmtClass() == OMPDistributeParallelForDirectiveClass ||
           T->getStmtClass() == OMPDistributeParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPDistributeSimdDirectiveClass ||
           T->getStmtClass() == OMPTargetParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPTargetSimdDirectiveClass ||
           T->getStmtClass() == OMPTeamsDistributeDirectiveClass ||
           T->getStmtClass() == OMPTeamsDistributeSimdDirectiveClass ||
           T->getStmtClass() ==
               OMPTeamsDistributeParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPTeamsDistributeParallelForDirectiveClass ||
           T->getStmtClass() ==
               OMPTargetTeamsDistributeParallelForDirectiveClass ||
           T->getStmtClass() ==
               OMPTargetTeamsDistributeParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPTargetTeamsDistributeDirectiveClass ||
           T->getStmtClass() == OMPTargetTeamsDistributeSimdDirectiveClass;
  }
};

/// This represents '#pragma omp simd' directive.
///
/// \code
/// #pragma omp simd private(a,b) linear(i,j:s) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp simd' has clauses 'private'
/// with the variables 'a' and 'b', 'linear' with variables 'i', 'j' and
/// linear step 's', 'reduction' with operator '+' and variables 'c' and 'd'.
///
class OMPSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                   unsigned CollapsedNum)
      : OMPLoopDirective(OMPSimdDirectiveClass, llvm::omp::OMPD_simd, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPSimdDirectiveClass, llvm::omp::OMPD_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPSimdDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation EndLoc, unsigned CollapsedNum,
                                  ArrayRef<OMPClause *> Clauses,
                                  Stmt *AssociatedStmt,
                                  const HelperExprs &Exprs,
                                  OpenMPDirectiveKind ParamPrevMappedDirective);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPSimdDirective *CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                       unsigned CollapsedNum, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSimdDirectiveClass;
  }
};

/// This represents '#pragma omp for' directive.
///
/// \code
/// #pragma omp for private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp for' has clauses 'private' with the
/// variables 'a' and 'b' and 'reduction' with operator '+' and variables 'c'
/// and 'd'.
///
class OMPForDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if current directive has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPForDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                  unsigned CollapsedNum)
      : OMPLoopDirective(OMPForDirectiveClass, llvm::omp::OMPD_for, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPForDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPForDirectiveClass, llvm::omp::OMPD_for,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) {
    Data->getChildren()[numLoopChildren(getLoopsNumber(),
                                        llvm::omp::OMPD_for)] = E;
  }

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if current directive has inner cancel directive.
  ///
  static OMPForDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                 SourceLocation EndLoc, unsigned CollapsedNum,
                                 ArrayRef<OMPClause *> Clauses,
                                 Stmt *AssociatedStmt, const HelperExprs &Exprs,
                                 Expr *TaskRedRef, bool HasCancel,
                                 OpenMPDirectiveKind ParamPrevMappedDirective);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPForDirective *CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                      unsigned CollapsedNum, EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[numLoopChildren(
        getLoopsNumber(), llvm::omp::OMPD_for)]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPForDirective *>(this)->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPForDirectiveClass;
  }
};

/// This represents '#pragma omp for simd' directive.
///
/// \code
/// #pragma omp for simd private(a,b) linear(i,j:s) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp for simd' has clauses 'private'
/// with the variables 'a' and 'b', 'linear' with variables 'i', 'j' and
/// linear step 's', 'reduction' with operator '+' and variables 'c' and 'd'.
///
class OMPForSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPForSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                      unsigned CollapsedNum)
      : OMPLoopDirective(OMPForSimdDirectiveClass, llvm::omp::OMPD_for_simd,
                         StartLoc, EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPForSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPForSimdDirectiveClass, llvm::omp::OMPD_for_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPForSimdDirective *CreateEmpty(const ASTContext &C,
                                          unsigned NumClauses,
                                          unsigned CollapsedNum, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp sections' directive.
///
/// \code
/// #pragma omp sections private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp sections' has clauses 'private' with
/// the variables 'a' and 'b' and 'reduction' with operator '+' and variables
/// 'c' and 'd'.
///
class OMPSectionsDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// true if current directive has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPSectionsDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPSectionsDirectiveClass,
                               llvm::omp::OMPD_sections, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPSectionsDirective()
      : OMPExecutableDirective(OMPSectionsDirectiveClass,
                               llvm::omp::OMPD_sections, SourceLocation(),
                               SourceLocation()) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) { Data->getChildren()[0] = E; }

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if current directive has inner directive.
  ///
  static OMPSectionsDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef,
         bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPSectionsDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses, EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[0]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPSectionsDirective *>(this)->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSectionsDirectiveClass;
  }
};

/// This represents '#pragma omp section' directive.
///
/// \code
/// #pragma omp section
/// \endcode
///
class OMPSectionDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// true if current directive has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPSectionDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPSectionDirectiveClass,
                               llvm::omp::OMPD_section, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPSectionDirective()
      : OMPExecutableDirective(OMPSectionDirectiveClass,
                               llvm::omp::OMPD_section, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param HasCancel true if current directive has inner directive.
  ///
  static OMPSectionDirective *Create(const ASTContext &C,
                                     SourceLocation StartLoc,
                                     SourceLocation EndLoc,
                                     Stmt *AssociatedStmt, bool HasCancel);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPSectionDirective *CreateEmpty(const ASTContext &C, EmptyShell);

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSectionDirectiveClass;
  }
};

/// This represents '#pragma omp scope' directive.
/// \code
/// #pragma omp scope private(a,b) nowait
/// \endcode
/// In this example directive '#pragma omp scope' has clauses 'private' with
/// the variables 'a' and 'b' and nowait.
///
class OMPScopeDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPScopeDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPScopeDirectiveClass, llvm::omp::OMPD_scope,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPScopeDirective()
      : OMPExecutableDirective(OMPScopeDirectiveClass, llvm::omp::OMPD_scope,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPScopeDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                   SourceLocation EndLoc,
                                   ArrayRef<OMPClause *> Clauses,
                                   Stmt *AssociatedStmt);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPScopeDirective *CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPScopeDirectiveClass;
  }
};

/// This represents '#pragma omp single' directive.
///
/// \code
/// #pragma omp single private(a,b) copyprivate(c,d)
/// \endcode
/// In this example directive '#pragma omp single' has clauses 'private' with
/// the variables 'a' and 'b' and 'copyprivate' with variables 'c' and 'd'.
///
class OMPSingleDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPSingleDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPSingleDirectiveClass, llvm::omp::OMPD_single,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPSingleDirective()
      : OMPExecutableDirective(OMPSingleDirectiveClass, llvm::omp::OMPD_single,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPSingleDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPSingleDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSingleDirectiveClass;
  }
};

/// This represents '#pragma omp master' directive.
///
/// \code
/// #pragma omp master
/// \endcode
///
class OMPMasterDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPMasterDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPMasterDirectiveClass, llvm::omp::OMPD_master,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPMasterDirective()
      : OMPExecutableDirective(OMPMasterDirectiveClass, llvm::omp::OMPD_master,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPMasterDirective *Create(const ASTContext &C,
                                    SourceLocation StartLoc,
                                    SourceLocation EndLoc,
                                    Stmt *AssociatedStmt);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPMasterDirective *CreateEmpty(const ASTContext &C, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPMasterDirectiveClass;
  }
};

/// This represents '#pragma omp critical' directive.
///
/// \code
/// #pragma omp critical
/// \endcode
///
class OMPCriticalDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Name of the directive.
  DeclarationNameInfo DirName;
  /// Build directive with the given start and end location.
  ///
  /// \param Name Name of the directive.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPCriticalDirective(const DeclarationNameInfo &Name, SourceLocation StartLoc,
                       SourceLocation EndLoc)
      : OMPExecutableDirective(OMPCriticalDirectiveClass,
                               llvm::omp::OMPD_critical, StartLoc, EndLoc),
        DirName(Name) {}

  /// Build an empty directive.
  ///
  explicit OMPCriticalDirective()
      : OMPExecutableDirective(OMPCriticalDirectiveClass,
                               llvm::omp::OMPD_critical, SourceLocation(),
                               SourceLocation()) {}

  /// Set name of the directive.
  ///
  /// \param Name Name of the directive.
  ///
  void setDirectiveName(const DeclarationNameInfo &Name) { DirName = Name; }

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param Name Name of the directive.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPCriticalDirective *
  Create(const ASTContext &C, const DeclarationNameInfo &Name,
         SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPCriticalDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses, EmptyShell);

  /// Return name of the directive.
  ///
  DeclarationNameInfo getDirectiveName() const { return DirName; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPCriticalDirectiveClass;
  }
};

/// This represents '#pragma omp parallel for' directive.
///
/// \code
/// #pragma omp parallel for private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp parallel for' has clauses 'private'
/// with the variables 'a' and 'b' and 'reduction' with operator '+' and
/// variables 'c' and 'd'.
///
class OMPParallelForDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// true if current region has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPParallelForDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                          unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelForDirectiveClass,
                         llvm::omp::OMPD_parallel_for, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPParallelForDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelForDirectiveClass,
                         llvm::omp::OMPD_parallel_for, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) {
    Data->getChildren()[numLoopChildren(getLoopsNumber(),
                                        llvm::omp::OMPD_parallel_for)] = E;
  }

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if current directive has inner cancel directive.
  ///
  static OMPParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, Expr *TaskRedRef,
         bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelForDirective *CreateEmpty(const ASTContext &C,
                                              unsigned NumClauses,
                                              unsigned CollapsedNum,
                                              EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[numLoopChildren(
        getLoopsNumber(), llvm::omp::OMPD_parallel_for)]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPParallelForDirective *>(this)
        ->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp parallel for simd' directive.
///
/// \code
/// #pragma omp parallel for simd private(a,b) linear(i,j:s) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp parallel for simd' has clauses
/// 'private' with the variables 'a' and 'b', 'linear' with variables 'i', 'j'
/// and linear step 's', 'reduction' with operator '+' and variables 'c' and
/// 'd'.
///
class OMPParallelForSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPParallelForSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                              unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelForSimdDirectiveClass,
                         llvm::omp::OMPD_parallel_for_simd, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPParallelForSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelForSimdDirectiveClass,
                         llvm::omp::OMPD_parallel_for_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPParallelForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelForSimdDirective *CreateEmpty(const ASTContext &C,
                                                  unsigned NumClauses,
                                                  unsigned CollapsedNum,
                                                  EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp parallel master' directive.
///
/// \code
/// #pragma omp parallel master private(a,b)
/// \endcode
/// In this example directive '#pragma omp parallel master' has clauses
/// 'private' with the variables 'a' and 'b'
///
class OMPParallelMasterDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  OMPParallelMasterDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPParallelMasterDirectiveClass,
                               llvm::omp::OMPD_parallel_master, StartLoc,
                               EndLoc) {}

  explicit OMPParallelMasterDirective()
      : OMPExecutableDirective(OMPParallelMasterDirectiveClass,
                               llvm::omp::OMPD_parallel_master,
                               SourceLocation(), SourceLocation()) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) { Data->getChildren()[0] = E; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  ///
  static OMPParallelMasterDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelMasterDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[0]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPParallelMasterDirective *>(this)
        ->getTaskReductionRefExpr();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelMasterDirectiveClass;
  }
};

/// This represents '#pragma omp parallel masked' directive.
///
/// \code
/// #pragma omp parallel masked filter(tid)
/// \endcode
/// In this example directive '#pragma omp parallel masked' has a clause
/// 'filter' with the variable tid
///
class OMPParallelMaskedDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  OMPParallelMaskedDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPParallelMaskedDirectiveClass,
                               llvm::omp::OMPD_parallel_masked, StartLoc,
                               EndLoc) {}

  explicit OMPParallelMaskedDirective()
      : OMPExecutableDirective(OMPParallelMaskedDirectiveClass,
                               llvm::omp::OMPD_parallel_masked,
                               SourceLocation(), SourceLocation()) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) { Data->getChildren()[0] = E; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  ///
  static OMPParallelMaskedDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelMaskedDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[0]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPParallelMaskedDirective *>(this)
        ->getTaskReductionRefExpr();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelMaskedDirectiveClass;
  }
};

/// This represents '#pragma omp parallel sections' directive.
///
/// \code
/// #pragma omp parallel sections private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp parallel sections' has clauses
/// 'private' with the variables 'a' and 'b' and 'reduction' with operator '+'
/// and variables 'c' and 'd'.
///
class OMPParallelSectionsDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// true if current directive has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPParallelSectionsDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPParallelSectionsDirectiveClass,
                               llvm::omp::OMPD_parallel_sections, StartLoc,
                               EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPParallelSectionsDirective()
      : OMPExecutableDirective(OMPParallelSectionsDirectiveClass,
                               llvm::omp::OMPD_parallel_sections,
                               SourceLocation(), SourceLocation()) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) { Data->getChildren()[0] = E; }

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if current directive has inner cancel directive.
  ///
  static OMPParallelSectionsDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef,
         bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelSectionsDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[0]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPParallelSectionsDirective *>(this)
        ->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelSectionsDirectiveClass;
  }
};

/// This represents '#pragma omp task' directive.
///
/// \code
/// #pragma omp task private(a,b) final(d)
/// \endcode
/// In this example directive '#pragma omp task' has clauses 'private' with the
/// variables 'a' and 'b' and 'final' with condition 'd'.
///
class OMPTaskDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if this directive has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTaskDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTaskDirectiveClass, llvm::omp::OMPD_task,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTaskDirective()
      : OMPExecutableDirective(OMPTaskDirectiveClass, llvm::omp::OMPD_task,
                               SourceLocation(), SourceLocation()) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param HasCancel true, if current directive has inner cancel directive.
  ///
  static OMPTaskDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation EndLoc,
                                  ArrayRef<OMPClause *> Clauses,
                                  Stmt *AssociatedStmt, bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTaskDirective *CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                       EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskDirectiveClass;
  }
};

/// This represents '#pragma omp taskyield' directive.
///
/// \code
/// #pragma omp taskyield
/// \endcode
///
class OMPTaskyieldDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTaskyieldDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTaskyieldDirectiveClass,
                               llvm::omp::OMPD_taskyield, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTaskyieldDirective()
      : OMPExecutableDirective(OMPTaskyieldDirectiveClass,
                               llvm::omp::OMPD_taskyield, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  static OMPTaskyieldDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPTaskyieldDirective *CreateEmpty(const ASTContext &C, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskyieldDirectiveClass;
  }
};

/// This represents '#pragma omp barrier' directive.
///
/// \code
/// #pragma omp barrier
/// \endcode
///
class OMPBarrierDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPBarrierDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPBarrierDirectiveClass,
                               llvm::omp::OMPD_barrier, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPBarrierDirective()
      : OMPExecutableDirective(OMPBarrierDirectiveClass,
                               llvm::omp::OMPD_barrier, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  static OMPBarrierDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPBarrierDirective *CreateEmpty(const ASTContext &C, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPBarrierDirectiveClass;
  }
};

/// This represents '#pragma omp taskwait' directive.
///
/// \code
/// #pragma omp taskwait
/// \endcode
///
class OMPTaskwaitDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTaskwaitDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTaskwaitDirectiveClass,
                               llvm::omp::OMPD_taskwait, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTaskwaitDirective()
      : OMPExecutableDirective(OMPTaskwaitDirectiveClass,
                               llvm::omp::OMPD_taskwait, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  ///
  static OMPTaskwaitDirective *Create(const ASTContext &C,
                                      SourceLocation StartLoc,
                                      SourceLocation EndLoc,
                                      ArrayRef<OMPClause *> Clauses);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTaskwaitDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskwaitDirectiveClass;
  }
};

/// This represents '#pragma omp taskgroup' directive.
///
/// \code
/// #pragma omp taskgroup
/// \endcode
///
class OMPTaskgroupDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTaskgroupDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTaskgroupDirectiveClass,
                               llvm::omp::OMPD_taskgroup, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTaskgroupDirective()
      : OMPExecutableDirective(OMPTaskgroupDirectiveClass,
                               llvm::omp::OMPD_taskgroup, SourceLocation(),
                               SourceLocation()) {}

  /// Sets the task_reduction return variable.
  void setReductionRef(Expr *RR) { Data->getChildren()[0] = RR; }

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param ReductionRef Reference to the task_reduction return variable.
  ///
  static OMPTaskgroupDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
         Expr *ReductionRef);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTaskgroupDirective *CreateEmpty(const ASTContext &C,
                                            unsigned NumClauses, EmptyShell);


  /// Returns reference to the task_reduction return variable.
  const Expr *getReductionRef() const {
    return const_cast<OMPTaskgroupDirective *>(this)->getReductionRef();
  }
  Expr *getReductionRef() { return cast_or_null<Expr>(Data->getChildren()[0]); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskgroupDirectiveClass;
  }
};

/// This represents '#pragma omp flush' directive.
///
/// \code
/// #pragma omp flush(a,b)
/// \endcode
/// In this example directive '#pragma omp flush' has 2 arguments- variables 'a'
/// and 'b'.
/// 'omp flush' directive does not have clauses but have an optional list of
/// variables to flush. This list of variables is stored within some fake clause
/// FlushClause.
class OMPFlushDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPFlushDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPFlushDirectiveClass, llvm::omp::OMPD_flush,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPFlushDirective()
      : OMPExecutableDirective(OMPFlushDirectiveClass, llvm::omp::OMPD_flush,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses (only single OMPFlushClause clause is
  /// allowed).
  ///
  static OMPFlushDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                   SourceLocation EndLoc,
                                   ArrayRef<OMPClause *> Clauses);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPFlushDirective *CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPFlushDirectiveClass;
  }
};

/// This represents '#pragma omp depobj' directive.
///
/// \code
/// #pragma omp depobj(a) depend(in:x,y)
/// \endcode
/// In this example directive '#pragma omp  depobj' initializes a depobj object
/// 'a' with dependence type 'in' and a list with 'x' and 'y' locators.
class OMPDepobjDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPDepobjDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPDepobjDirectiveClass, llvm::omp::OMPD_depobj,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPDepobjDirective()
      : OMPExecutableDirective(OMPDepobjDirectiveClass, llvm::omp::OMPD_depobj,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  ///
  static OMPDepobjDirective *Create(const ASTContext &C,
                                    SourceLocation StartLoc,
                                    SourceLocation EndLoc,
                                    ArrayRef<OMPClause *> Clauses);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDepobjDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDepobjDirectiveClass;
  }
};

/// This represents '#pragma omp ordered' directive.
///
/// \code
/// #pragma omp ordered
/// \endcode
///
class OMPOrderedDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPOrderedDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPOrderedDirectiveClass,
                               llvm::omp::OMPD_ordered, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPOrderedDirective()
      : OMPExecutableDirective(OMPOrderedDirectiveClass,
                               llvm::omp::OMPD_ordered, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPOrderedDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  /// \param IsStandalone true, if the standalone directive is created.
  ///
  static OMPOrderedDirective *CreateEmpty(const ASTContext &C,
                                          unsigned NumClauses,
                                          bool IsStandalone, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPOrderedDirectiveClass;
  }
};

/// This represents '#pragma omp atomic' directive.
///
/// \code
/// #pragma omp atomic capture
/// \endcode
/// In this example directive '#pragma omp atomic' has clause 'capture'.
///
class OMPAtomicDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  struct FlagTy {
    /// Used for 'atomic update' or 'atomic capture' constructs. They may
    /// have atomic expressions of forms:
    /// \code
    /// x = x binop expr;
    /// x = expr binop x;
    /// \endcode
    /// This field is 1 for the first form of the expression and 0 for the
    /// second. Required for correct codegen of non-associative operations (like
    /// << or >>).
    LLVM_PREFERRED_TYPE(bool)
    uint8_t IsXLHSInRHSPart : 1;
    /// Used for 'atomic update' or 'atomic capture' constructs. They may
    /// have atomic expressions of forms:
    /// \code
    /// v = x; <update x>;
    /// <update x>; v = x;
    /// \endcode
    /// This field is 1 for the first(postfix) form of the expression and 0
    /// otherwise.
    LLVM_PREFERRED_TYPE(bool)
    uint8_t IsPostfixUpdate : 1;
    /// 1 if 'v' is updated only when the condition is false (compare capture
    /// only).
    LLVM_PREFERRED_TYPE(bool)
    uint8_t IsFailOnly : 1;
  } Flags;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPAtomicDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPAtomicDirectiveClass, llvm::omp::OMPD_atomic,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPAtomicDirective()
      : OMPExecutableDirective(OMPAtomicDirectiveClass, llvm::omp::OMPD_atomic,
                               SourceLocation(), SourceLocation()) {}

  enum DataPositionTy : size_t {
    POS_X = 0,
    POS_V,
    POS_E,
    POS_UpdateExpr,
    POS_D,
    POS_Cond,
    POS_R,
  };

  /// Set 'x' part of the associated expression/statement.
  void setX(Expr *X) { Data->getChildren()[DataPositionTy::POS_X] = X; }
  /// Set helper expression of the form
  /// 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' or
  /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
  void setUpdateExpr(Expr *UE) {
    Data->getChildren()[DataPositionTy::POS_UpdateExpr] = UE;
  }
  /// Set 'v' part of the associated expression/statement.
  void setV(Expr *V) { Data->getChildren()[DataPositionTy::POS_V] = V; }
  /// Set 'r' part of the associated expression/statement.
  void setR(Expr *R) { Data->getChildren()[DataPositionTy::POS_R] = R; }
  /// Set 'expr' part of the associated expression/statement.
  void setExpr(Expr *E) { Data->getChildren()[DataPositionTy::POS_E] = E; }
  /// Set 'd' part of the associated expression/statement.
  void setD(Expr *D) { Data->getChildren()[DataPositionTy::POS_D] = D; }
  /// Set conditional expression in `atomic compare`.
  void setCond(Expr *C) { Data->getChildren()[DataPositionTy::POS_Cond] = C; }

public:
  struct Expressions {
    /// 'x' part of the associated expression/statement.
    Expr *X = nullptr;
    /// 'v' part of the associated expression/statement.
    Expr *V = nullptr;
    // 'r' part of the associated expression/statement.
    Expr *R = nullptr;
    /// 'expr' part of the associated expression/statement.
    Expr *E = nullptr;
    /// UE Helper expression of the form:
    /// 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' or
    /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
    Expr *UE = nullptr;
    /// 'd' part of the associated expression/statement.
    Expr *D = nullptr;
    /// Conditional expression in `atomic compare` construct.
    Expr *Cond = nullptr;
    /// True if UE has the first form and false if the second.
    bool IsXLHSInRHSPart;
    /// True if original value of 'x' must be stored in 'v', not an updated one.
    bool IsPostfixUpdate;
    /// True if 'v' is updated only when the condition is false (compare capture
    /// only).
    bool IsFailOnly;
  };

  /// Creates directive with a list of \a Clauses and 'x', 'v' and 'expr'
  /// parts of the atomic construct (see Section 2.12.6, atomic Construct, for
  /// detailed description of 'x', 'v' and 'expr').
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Associated expressions or statements.
  static OMPAtomicDirective *Create(const ASTContext &C,
                                    SourceLocation StartLoc,
                                    SourceLocation EndLoc,
                                    ArrayRef<OMPClause *> Clauses,
                                    Stmt *AssociatedStmt, Expressions Exprs);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPAtomicDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  /// Get 'x' part of the associated expression/statement.
  Expr *getX() {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_X]);
  }
  const Expr *getX() const {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_X]);
  }
  /// Get helper expression of the form
  /// 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' or
  /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
  Expr *getUpdateExpr() {
    return cast_or_null<Expr>(
        Data->getChildren()[DataPositionTy::POS_UpdateExpr]);
  }
  const Expr *getUpdateExpr() const {
    return cast_or_null<Expr>(
        Data->getChildren()[DataPositionTy::POS_UpdateExpr]);
  }
  /// Return true if helper update expression has form
  /// 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' and false if it has form
  /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
  bool isXLHSInRHSPart() const { return Flags.IsXLHSInRHSPart; }
  /// Return true if 'v' expression must be updated to original value of
  /// 'x', false if 'v' must be updated to the new value of 'x'.
  bool isPostfixUpdate() const { return Flags.IsPostfixUpdate; }
  /// Return true if 'v' is updated only when the condition is evaluated false
  /// (compare capture only).
  bool isFailOnly() const { return Flags.IsFailOnly; }
  /// Get 'v' part of the associated expression/statement.
  Expr *getV() {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_V]);
  }
  const Expr *getV() const {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_V]);
  }
  /// Get 'r' part of the associated expression/statement.
  Expr *getR() {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_R]);
  }
  const Expr *getR() const {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_R]);
  }
  /// Get 'expr' part of the associated expression/statement.
  Expr *getExpr() {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_E]);
  }
  const Expr *getExpr() const {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_E]);
  }
  /// Get 'd' part of the associated expression/statement.
  Expr *getD() {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_D]);
  }
  Expr *getD() const {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_D]);
  }
  /// Get the 'cond' part of the source atomic expression.
  Expr *getCondExpr() {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_Cond]);
  }
  Expr *getCondExpr() const {
    return cast_or_null<Expr>(Data->getChildren()[DataPositionTy::POS_Cond]);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPAtomicDirectiveClass;
  }
};

/// This represents '#pragma omp target' directive.
///
/// \code
/// #pragma omp target if(a)
/// \endcode
/// In this example directive '#pragma omp target' has clause 'if' with
/// condition 'a'.
///
class OMPTargetDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTargetDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTargetDirectiveClass, llvm::omp::OMPD_target,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTargetDirective()
      : OMPExecutableDirective(OMPTargetDirectiveClass, llvm::omp::OMPD_target,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetDirectiveClass;
  }
};

/// This represents '#pragma omp target data' directive.
///
/// \code
/// #pragma omp target data device(0) if(a) map(b[:])
/// \endcode
/// In this example directive '#pragma omp target data' has clauses 'device'
/// with the value '0', 'if' with condition 'a' and 'map' with array
/// section 'b[:]'.
///
class OMPTargetDataDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  OMPTargetDataDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTargetDataDirectiveClass,
                               llvm::omp::OMPD_target_data, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTargetDataDirective()
      : OMPExecutableDirective(OMPTargetDataDirectiveClass,
                               llvm::omp::OMPD_target_data, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetDataDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a N clauses.
  ///
  /// \param C AST context.
  /// \param N The number of clauses.
  ///
  static OMPTargetDataDirective *CreateEmpty(const ASTContext &C, unsigned N,
                                             EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetDataDirectiveClass;
  }
};

/// This represents '#pragma omp target enter data' directive.
///
/// \code
/// #pragma omp target enter data device(0) if(a) map(b[:])
/// \endcode
/// In this example directive '#pragma omp target enter data' has clauses
/// 'device' with the value '0', 'if' with condition 'a' and 'map' with array
/// section 'b[:]'.
///
class OMPTargetEnterDataDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  OMPTargetEnterDataDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTargetEnterDataDirectiveClass,
                               llvm::omp::OMPD_target_enter_data, StartLoc,
                               EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTargetEnterDataDirective()
      : OMPExecutableDirective(OMPTargetEnterDataDirectiveClass,
                               llvm::omp::OMPD_target_enter_data,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetEnterDataDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a N clauses.
  ///
  /// \param C AST context.
  /// \param N The number of clauses.
  ///
  static OMPTargetEnterDataDirective *CreateEmpty(const ASTContext &C,
                                                  unsigned N, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetEnterDataDirectiveClass;
  }
};

/// This represents '#pragma omp target exit data' directive.
///
/// \code
/// #pragma omp target exit data device(0) if(a) map(b[:])
/// \endcode
/// In this example directive '#pragma omp target exit data' has clauses
/// 'device' with the value '0', 'if' with condition 'a' and 'map' with array
/// section 'b[:]'.
///
class OMPTargetExitDataDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  OMPTargetExitDataDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTargetExitDataDirectiveClass,
                               llvm::omp::OMPD_target_exit_data, StartLoc,
                               EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTargetExitDataDirective()
      : OMPExecutableDirective(OMPTargetExitDataDirectiveClass,
                               llvm::omp::OMPD_target_exit_data,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetExitDataDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a N clauses.
  ///
  /// \param C AST context.
  /// \param N The number of clauses.
  ///
  static OMPTargetExitDataDirective *CreateEmpty(const ASTContext &C,
                                                 unsigned N, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetExitDataDirectiveClass;
  }
};

/// This represents '#pragma omp target parallel' directive.
///
/// \code
/// #pragma omp target parallel if(a)
/// \endcode
/// In this example directive '#pragma omp target parallel' has clause 'if' with
/// condition 'a'.
///
class OMPTargetParallelDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTargetParallelDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTargetParallelDirectiveClass,
                               llvm::omp::OMPD_target_parallel, StartLoc,
                               EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTargetParallelDirective()
      : OMPExecutableDirective(OMPTargetParallelDirectiveClass,
                               llvm::omp::OMPD_target_parallel,
                               SourceLocation(), SourceLocation()) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) { Data->getChildren()[0] = E; }
  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPTargetParallelDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *TaskRedRef,
         bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetParallelDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[0]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPTargetParallelDirective *>(this)
        ->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetParallelDirectiveClass;
  }
};

/// This represents '#pragma omp target parallel for' directive.
///
/// \code
/// #pragma omp target parallel for private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp target parallel for' has clauses
/// 'private' with the variables 'a' and 'b' and 'reduction' with operator '+'
/// and variables 'c' and 'd'.
///
class OMPTargetParallelForDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// true if current region has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTargetParallelForDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                                unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetParallelForDirectiveClass,
                         llvm::omp::OMPD_target_parallel_for, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTargetParallelForDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetParallelForDirectiveClass,
                         llvm::omp::OMPD_target_parallel_for, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) {
    Data->getChildren()[numLoopChildren(
        getLoopsNumber(), llvm::omp::OMPD_target_parallel_for)] = E;
  }

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if current directive has inner cancel directive.
  ///
  static OMPTargetParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, Expr *TaskRedRef,
         bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetParallelForDirective *CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    unsigned CollapsedNum,
                                                    EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[numLoopChildren(
        getLoopsNumber(), llvm::omp::OMPD_target_parallel_for)]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPTargetParallelForDirective *>(this)
        ->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp teams' directive.
///
/// \code
/// #pragma omp teams if(a)
/// \endcode
/// In this example directive '#pragma omp teams' has clause 'if' with
/// condition 'a'.
///
class OMPTeamsDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTeamsDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTeamsDirectiveClass, llvm::omp::OMPD_teams,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTeamsDirective()
      : OMPExecutableDirective(OMPTeamsDirectiveClass, llvm::omp::OMPD_teams,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTeamsDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                   SourceLocation EndLoc,
                                   ArrayRef<OMPClause *> Clauses,
                                   Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDirective *CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDirectiveClass;
  }
};

/// This represents '#pragma omp cancellation point' directive.
///
/// \code
/// #pragma omp cancellation point for
/// \endcode
///
/// In this example a cancellation point is created for innermost 'for' region.
class OMPCancellationPointDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  OpenMPDirectiveKind CancelRegion = llvm::omp::OMPD_unknown;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// statements and child expressions.
  ///
  OMPCancellationPointDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPCancellationPointDirectiveClass,
                               llvm::omp::OMPD_cancellation_point, StartLoc,
                               EndLoc) {}

  /// Build an empty directive.
  explicit OMPCancellationPointDirective()
      : OMPExecutableDirective(OMPCancellationPointDirectiveClass,
                               llvm::omp::OMPD_cancellation_point,
                               SourceLocation(), SourceLocation()) {}

  /// Set cancel region for current cancellation point.
  /// \param CR Cancellation region.
  void setCancelRegion(OpenMPDirectiveKind CR) { CancelRegion = CR; }

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  static OMPCancellationPointDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         OpenMPDirectiveKind CancelRegion);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPCancellationPointDirective *CreateEmpty(const ASTContext &C,
                                                    EmptyShell);

  /// Get cancellation region for the current cancellation point.
  OpenMPDirectiveKind getCancelRegion() const { return CancelRegion; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPCancellationPointDirectiveClass;
  }
};

/// This represents '#pragma omp cancel' directive.
///
/// \code
/// #pragma omp cancel for
/// \endcode
///
/// In this example a cancel is created for innermost 'for' region.
class OMPCancelDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  OpenMPDirectiveKind CancelRegion = llvm::omp::OMPD_unknown;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPCancelDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPCancelDirectiveClass, llvm::omp::OMPD_cancel,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPCancelDirective()
      : OMPExecutableDirective(OMPCancelDirectiveClass, llvm::omp::OMPD_cancel,
                               SourceLocation(), SourceLocation()) {}

  /// Set cancel region for current cancellation point.
  /// \param CR Cancellation region.
  void setCancelRegion(OpenMPDirectiveKind CR) { CancelRegion = CR; }

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  ///
  static OMPCancelDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, OpenMPDirectiveKind CancelRegion);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPCancelDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  /// Get cancellation region for the current cancellation point.
  OpenMPDirectiveKind getCancelRegion() const { return CancelRegion; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPCancelDirectiveClass;
  }
};

/// This represents '#pragma omp taskloop' directive.
///
/// \code
/// #pragma omp taskloop private(a,b) grainsize(val) num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp taskloop' has clauses 'private'
/// with the variables 'a' and 'b', 'grainsize' with expression 'val' and
/// 'num_tasks' with expression 'num'.
///
class OMPTaskLoopDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTaskLoopDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                       unsigned CollapsedNum)
      : OMPLoopDirective(OMPTaskLoopDirectiveClass, llvm::omp::OMPD_taskloop,
                         StartLoc, EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTaskLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTaskLoopDirectiveClass, llvm::omp::OMPD_taskloop,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPTaskLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTaskLoopDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses,
                                           unsigned CollapsedNum, EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskLoopDirectiveClass;
  }
};

/// This represents '#pragma omp taskloop simd' directive.
///
/// \code
/// #pragma omp taskloop simd private(a,b) grainsize(val) num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp taskloop simd' has clauses 'private'
/// with the variables 'a' and 'b', 'grainsize' with expression 'val' and
/// 'num_tasks' with expression 'num'.
///
class OMPTaskLoopSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTaskLoopSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                           unsigned CollapsedNum)
      : OMPLoopDirective(OMPTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_taskloop_simd, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTaskLoopSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_taskloop_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTaskLoopSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTaskLoopSimdDirective *CreateEmpty(const ASTContext &C,
                                               unsigned NumClauses,
                                               unsigned CollapsedNum,
                                               EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskLoopSimdDirectiveClass;
  }
};

/// This represents '#pragma omp master taskloop' directive.
///
/// \code
/// #pragma omp master taskloop private(a,b) grainsize(val) num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp master taskloop' has clauses
/// 'private' with the variables 'a' and 'b', 'grainsize' with expression 'val'
/// and 'num_tasks' with expression 'num'.
///
class OMPMasterTaskLoopDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPMasterTaskLoopDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                             unsigned CollapsedNum)
      : OMPLoopDirective(OMPMasterTaskLoopDirectiveClass,
                         llvm::omp::OMPD_master_taskloop, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPMasterTaskLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPMasterTaskLoopDirectiveClass,
                         llvm::omp::OMPD_master_taskloop, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPMasterTaskLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPMasterTaskLoopDirective *CreateEmpty(const ASTContext &C,
                                                 unsigned NumClauses,
                                                 unsigned CollapsedNum,
                                                 EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPMasterTaskLoopDirectiveClass;
  }
};

/// This represents '#pragma omp masked taskloop' directive.
///
/// \code
/// #pragma omp masked taskloop private(a,b) grainsize(val) num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp masked taskloop' has clauses
/// 'private' with the variables 'a' and 'b', 'grainsize' with expression 'val'
/// and 'num_tasks' with expression 'num'.
///
class OMPMaskedTaskLoopDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPMaskedTaskLoopDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                             unsigned CollapsedNum)
      : OMPLoopDirective(OMPMaskedTaskLoopDirectiveClass,
                         llvm::omp::OMPD_masked_taskloop, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPMaskedTaskLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPMaskedTaskLoopDirectiveClass,
                         llvm::omp::OMPD_masked_taskloop, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPMaskedTaskLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPMaskedTaskLoopDirective *CreateEmpty(const ASTContext &C,
                                                 unsigned NumClauses,
                                                 unsigned CollapsedNum,
                                                 EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPMaskedTaskLoopDirectiveClass;
  }
};

/// This represents '#pragma omp master taskloop simd' directive.
///
/// \code
/// #pragma omp master taskloop simd private(a,b) grainsize(val) num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp master taskloop simd' has clauses
/// 'private' with the variables 'a' and 'b', 'grainsize' with expression 'val'
/// and 'num_tasks' with expression 'num'.
///
class OMPMasterTaskLoopSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPMasterTaskLoopSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                                 unsigned CollapsedNum)
      : OMPLoopDirective(OMPMasterTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_master_taskloop_simd, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPMasterTaskLoopSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPMasterTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_master_taskloop_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \p Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPMasterTaskLoopSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \p NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPMasterTaskLoopSimdDirective *CreateEmpty(const ASTContext &C,
                                                     unsigned NumClauses,
                                                     unsigned CollapsedNum,
                                                     EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPMasterTaskLoopSimdDirectiveClass;
  }
};

/// This represents '#pragma omp masked taskloop simd' directive.
///
/// \code
/// #pragma omp masked taskloop simd private(a,b) grainsize(val) num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp masked taskloop simd' has clauses
/// 'private' with the variables 'a' and 'b', 'grainsize' with expression 'val'
/// and 'num_tasks' with expression 'num'.
///
class OMPMaskedTaskLoopSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPMaskedTaskLoopSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                                 unsigned CollapsedNum)
      : OMPLoopDirective(OMPMaskedTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_masked_taskloop_simd, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPMaskedTaskLoopSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPMaskedTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_masked_taskloop_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \p Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPMaskedTaskLoopSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \p NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPMaskedTaskLoopSimdDirective *CreateEmpty(const ASTContext &C,
                                                     unsigned NumClauses,
                                                     unsigned CollapsedNum,
                                                     EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPMaskedTaskLoopSimdDirectiveClass;
  }
};

/// This represents '#pragma omp parallel master taskloop' directive.
///
/// \code
/// #pragma omp parallel master taskloop private(a,b) grainsize(val)
/// num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp parallel master taskloop' has clauses
/// 'private' with the variables 'a' and 'b', 'grainsize' with expression 'val'
/// and 'num_tasks' with expression 'num'.
///
class OMPParallelMasterTaskLoopDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPParallelMasterTaskLoopDirective(SourceLocation StartLoc,
                                     SourceLocation EndLoc,
                                     unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelMasterTaskLoopDirectiveClass,
                         llvm::omp::OMPD_parallel_master_taskloop, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPParallelMasterTaskLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelMasterTaskLoopDirectiveClass,
                         llvm::omp::OMPD_parallel_master_taskloop,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPParallelMasterTaskLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelMasterTaskLoopDirective *CreateEmpty(const ASTContext &C,
                                                         unsigned NumClauses,
                                                         unsigned CollapsedNum,
                                                         EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelMasterTaskLoopDirectiveClass;
  }
};

/// This represents '#pragma omp parallel masked taskloop' directive.
///
/// \code
/// #pragma omp parallel masked taskloop private(a,b) grainsize(val)
/// num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp parallel masked taskloop' has clauses
/// 'private' with the variables 'a' and 'b', 'grainsize' with expression 'val'
/// and 'num_tasks' with expression 'num'.
///
class OMPParallelMaskedTaskLoopDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPParallelMaskedTaskLoopDirective(SourceLocation StartLoc,
                                     SourceLocation EndLoc,
                                     unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelMaskedTaskLoopDirectiveClass,
                         llvm::omp::OMPD_parallel_masked_taskloop, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPParallelMaskedTaskLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelMaskedTaskLoopDirectiveClass,
                         llvm::omp::OMPD_parallel_masked_taskloop,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPParallelMaskedTaskLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelMaskedTaskLoopDirective *CreateEmpty(const ASTContext &C,
                                                         unsigned NumClauses,
                                                         unsigned CollapsedNum,
                                                         EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelMaskedTaskLoopDirectiveClass;
  }
};

/// This represents '#pragma omp parallel master taskloop simd' directive.
///
/// \code
/// #pragma omp parallel master taskloop simd private(a,b) grainsize(val)
/// num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp parallel master taskloop simd' has
/// clauses 'private' with the variables 'a' and 'b', 'grainsize' with
/// expression 'val' and 'num_tasks' with expression 'num'.
///
class OMPParallelMasterTaskLoopSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPParallelMasterTaskLoopSimdDirective(SourceLocation StartLoc,
                                         SourceLocation EndLoc,
                                         unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelMasterTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_parallel_master_taskloop_simd,
                         StartLoc, EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPParallelMasterTaskLoopSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelMasterTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_parallel_master_taskloop_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \p Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPParallelMasterTaskLoopSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelMasterTaskLoopSimdDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelMasterTaskLoopSimdDirectiveClass;
  }
};

/// This represents '#pragma omp parallel masked taskloop simd' directive.
///
/// \code
/// #pragma omp parallel masked taskloop simd private(a,b) grainsize(val)
/// num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp parallel masked taskloop simd' has
/// clauses 'private' with the variables 'a' and 'b', 'grainsize' with
/// expression 'val' and 'num_tasks' with expression 'num'.
///
class OMPParallelMaskedTaskLoopSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPParallelMaskedTaskLoopSimdDirective(SourceLocation StartLoc,
                                         SourceLocation EndLoc,
                                         unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelMaskedTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_parallel_masked_taskloop_simd,
                         StartLoc, EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPParallelMaskedTaskLoopSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelMaskedTaskLoopSimdDirectiveClass,
                         llvm::omp::OMPD_parallel_masked_taskloop_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \p Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPParallelMaskedTaskLoopSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelMaskedTaskLoopSimdDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelMaskedTaskLoopSimdDirectiveClass;
  }
};

/// This represents '#pragma omp distribute' directive.
///
/// \code
/// #pragma omp distribute private(a,b)
/// \endcode
/// In this example directive '#pragma omp distribute' has clauses 'private'
/// with the variables 'a' and 'b'
///
class OMPDistributeDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPDistributeDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                         unsigned CollapsedNum)
      : OMPLoopDirective(OMPDistributeDirectiveClass,
                         llvm::omp::OMPD_distribute, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPDistributeDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPDistributeDirectiveClass,
                         llvm::omp::OMPD_distribute, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPDistributeDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs,
         OpenMPDirectiveKind ParamPrevMappedDirective);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDistributeDirective *CreateEmpty(const ASTContext &C,
                                             unsigned NumClauses,
                                             unsigned CollapsedNum, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDistributeDirectiveClass;
  }
};

/// This represents '#pragma omp target update' directive.
///
/// \code
/// #pragma omp target update to(a) from(b) device(1)
/// \endcode
/// In this example directive '#pragma omp target update' has clause 'to' with
/// argument 'a', clause 'from' with argument 'b' and clause 'device' with
/// argument '1'.
///
class OMPTargetUpdateDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  OMPTargetUpdateDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTargetUpdateDirectiveClass,
                               llvm::omp::OMPD_target_update, StartLoc,
                               EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPTargetUpdateDirective()
      : OMPExecutableDirective(OMPTargetUpdateDirectiveClass,
                               llvm::omp::OMPD_target_update, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetUpdateDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses The number of clauses.
  ///
  static OMPTargetUpdateDirective *CreateEmpty(const ASTContext &C,
                                               unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetUpdateDirectiveClass;
  }
};

/// This represents '#pragma omp distribute parallel for' composite
///  directive.
///
/// \code
/// #pragma omp distribute parallel for private(a,b)
/// \endcode
/// In this example directive '#pragma omp distribute parallel for' has clause
/// 'private' with the variables 'a' and 'b'
///
class OMPDistributeParallelForDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPDistributeParallelForDirective(SourceLocation StartLoc,
                                    SourceLocation EndLoc,
                                    unsigned CollapsedNum)
      : OMPLoopDirective(OMPDistributeParallelForDirectiveClass,
                         llvm::omp::OMPD_distribute_parallel_for, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPDistributeParallelForDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPDistributeParallelForDirectiveClass,
                         llvm::omp::OMPD_distribute_parallel_for,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) {
    Data->getChildren()[numLoopChildren(
        getLoopsNumber(), llvm::omp::OMPD_distribute_parallel_for)] = E;
  }

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPDistributeParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, Expr *TaskRedRef,
         bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDistributeParallelForDirective *CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        unsigned CollapsedNum,
                                                        EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[numLoopChildren(
        getLoopsNumber(), llvm::omp::OMPD_distribute_parallel_for)]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPDistributeParallelForDirective *>(this)
        ->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDistributeParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp distribute parallel for simd' composite
/// directive.
///
/// \code
/// #pragma omp distribute parallel for simd private(x)
/// \endcode
/// In this example directive '#pragma omp distribute parallel for simd' has
/// clause 'private' with the variables 'x'
///
class OMPDistributeParallelForSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPDistributeParallelForSimdDirective(SourceLocation StartLoc,
                                        SourceLocation EndLoc,
                                        unsigned CollapsedNum)
      : OMPLoopDirective(OMPDistributeParallelForSimdDirectiveClass,
                         llvm::omp::OMPD_distribute_parallel_for_simd, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPDistributeParallelForSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPDistributeParallelForSimdDirectiveClass,
                         llvm::omp::OMPD_distribute_parallel_for_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPDistributeParallelForSimdDirective *Create(
      const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
      unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
      Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDistributeParallelForSimdDirective *CreateEmpty(
      const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
      EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDistributeParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp distribute simd' composite directive.
///
/// \code
/// #pragma omp distribute simd private(x)
/// \endcode
/// In this example directive '#pragma omp distribute simd' has clause
/// 'private' with the variables 'x'
///
class OMPDistributeSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPDistributeSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                             unsigned CollapsedNum)
      : OMPLoopDirective(OMPDistributeSimdDirectiveClass,
                         llvm::omp::OMPD_distribute_simd, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPDistributeSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPDistributeSimdDirectiveClass,
                         llvm::omp::OMPD_distribute_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPDistributeSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDistributeSimdDirective *CreateEmpty(const ASTContext &C,
                                                 unsigned NumClauses,
                                                 unsigned CollapsedNum,
                                                 EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDistributeSimdDirectiveClass;
  }
};

/// This represents '#pragma omp target parallel for simd' directive.
///
/// \code
/// #pragma omp target parallel for simd private(a) map(b) safelen(c)
/// \endcode
/// In this example directive '#pragma omp target parallel for simd' has clauses
/// 'private' with the variable 'a', 'map' with the variable 'b' and 'safelen'
/// with the variable 'c'.
///
class OMPTargetParallelForSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTargetParallelForSimdDirective(SourceLocation StartLoc,
                                    SourceLocation EndLoc,
                                    unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetParallelForSimdDirectiveClass,
                         llvm::omp::OMPD_target_parallel_for_simd, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTargetParallelForSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetParallelForSimdDirectiveClass,
                         llvm::omp::OMPD_target_parallel_for_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetParallelForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetParallelForSimdDirective *CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        unsigned CollapsedNum,
                                                        EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp target simd' directive.
///
/// \code
/// #pragma omp target simd private(a) map(b) safelen(c)
/// \endcode
/// In this example directive '#pragma omp target simd' has clauses 'private'
/// with the variable 'a', 'map' with the variable 'b' and 'safelen' with
/// the variable 'c'.
///
class OMPTargetSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTargetSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                         unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetSimdDirectiveClass,
                         llvm::omp::OMPD_target_simd, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTargetSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetSimdDirectiveClass,
                         llvm::omp::OMPD_target_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetSimdDirective *CreateEmpty(const ASTContext &C,
                                             unsigned NumClauses,
                                             unsigned CollapsedNum,
                                             EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetSimdDirectiveClass;
  }
};

/// This represents '#pragma omp teams distribute' directive.
///
/// \code
/// #pragma omp teams distribute private(a,b)
/// \endcode
/// In this example directive '#pragma omp teams distribute' has clauses
/// 'private' with the variables 'a' and 'b'
///
class OMPTeamsDistributeDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTeamsDistributeDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                              unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsDistributeDirectiveClass,
                         llvm::omp::OMPD_teams_distribute, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTeamsDistributeDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsDistributeDirectiveClass,
                         llvm::omp::OMPD_teams_distribute, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTeamsDistributeDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDistributeDirective *CreateEmpty(const ASTContext &C,
                                                  unsigned NumClauses,
                                                  unsigned CollapsedNum,
                                                  EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDistributeDirectiveClass;
  }
};

/// This represents '#pragma omp teams distribute simd'
/// combined directive.
///
/// \code
/// #pragma omp teams distribute simd private(a,b)
/// \endcode
/// In this example directive '#pragma omp teams distribute simd'
/// has clause 'private' with the variables 'a' and 'b'
///
class OMPTeamsDistributeSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTeamsDistributeSimdDirective(SourceLocation StartLoc,
                                  SourceLocation EndLoc, unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsDistributeSimdDirectiveClass,
                         llvm::omp::OMPD_teams_distribute_simd, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTeamsDistributeSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsDistributeSimdDirectiveClass,
                         llvm::omp::OMPD_teams_distribute_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTeamsDistributeSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDistributeSimdDirective *CreateEmpty(const ASTContext &C,
                                                      unsigned NumClauses,
                                                      unsigned CollapsedNum,
                                                      EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDistributeSimdDirectiveClass;
  }
};

/// This represents '#pragma omp teams distribute parallel for simd' composite
/// directive.
///
/// \code
/// #pragma omp teams distribute parallel for simd private(x)
/// \endcode
/// In this example directive '#pragma omp teams distribute parallel for simd'
/// has clause 'private' with the variables 'x'
///
class OMPTeamsDistributeParallelForSimdDirective final
    : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTeamsDistributeParallelForSimdDirective(SourceLocation StartLoc,
                                             SourceLocation EndLoc,
                                             unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsDistributeParallelForSimdDirectiveClass,
                         llvm::omp::OMPD_teams_distribute_parallel_for_simd,
                         StartLoc, EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTeamsDistributeParallelForSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsDistributeParallelForSimdDirectiveClass,
                         llvm::omp::OMPD_teams_distribute_parallel_for_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTeamsDistributeParallelForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDistributeParallelForSimdDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDistributeParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp teams distribute parallel for' composite
/// directive.
///
/// \code
/// #pragma omp teams distribute parallel for private(x)
/// \endcode
/// In this example directive '#pragma omp teams distribute parallel for'
/// has clause 'private' with the variables 'x'
///
class OMPTeamsDistributeParallelForDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTeamsDistributeParallelForDirective(SourceLocation StartLoc,
                                         SourceLocation EndLoc,
                                         unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsDistributeParallelForDirectiveClass,
                         llvm::omp::OMPD_teams_distribute_parallel_for,
                         StartLoc, EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTeamsDistributeParallelForDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsDistributeParallelForDirectiveClass,
                         llvm::omp::OMPD_teams_distribute_parallel_for,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) {
    Data->getChildren()[numLoopChildren(
        getLoopsNumber(), llvm::omp::OMPD_teams_distribute_parallel_for)] = E;
  }

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPTeamsDistributeParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, Expr *TaskRedRef,
         bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDistributeParallelForDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[numLoopChildren(
        getLoopsNumber(), llvm::omp::OMPD_teams_distribute_parallel_for)]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPTeamsDistributeParallelForDirective *>(this)
        ->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDistributeParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp target teams' directive.
///
/// \code
/// #pragma omp target teams if(a>0)
/// \endcode
/// In this example directive '#pragma omp target teams' has clause 'if' with
/// condition 'a>0'.
///
class OMPTargetTeamsDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTargetTeamsDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPTargetTeamsDirectiveClass,
                               llvm::omp::OMPD_target_teams, StartLoc, EndLoc) {
  }

  /// Build an empty directive.
  ///
  explicit OMPTargetTeamsDirective()
      : OMPExecutableDirective(OMPTargetTeamsDirectiveClass,
                               llvm::omp::OMPD_target_teams, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetTeamsDirective *Create(const ASTContext &C,
                                         SourceLocation StartLoc,
                                         SourceLocation EndLoc,
                                         ArrayRef<OMPClause *> Clauses,
                                         Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDirective *CreateEmpty(const ASTContext &C,
                                              unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetTeamsDirectiveClass;
  }
};

/// This represents '#pragma omp target teams distribute' combined directive.
///
/// \code
/// #pragma omp target teams distribute private(x)
/// \endcode
/// In this example directive '#pragma omp target teams distribute' has clause
/// 'private' with the variables 'x'
///
class OMPTargetTeamsDistributeDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTargetTeamsDistributeDirective(SourceLocation StartLoc,
                                    SourceLocation EndLoc,
                                    unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetTeamsDistributeDirectiveClass,
                         llvm::omp::OMPD_target_teams_distribute, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTargetTeamsDistributeDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetTeamsDistributeDirectiveClass,
                         llvm::omp::OMPD_target_teams_distribute,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetTeamsDistributeDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDistributeDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetTeamsDistributeDirectiveClass;
  }
};

/// This represents '#pragma omp target teams distribute parallel for' combined
/// directive.
///
/// \code
/// #pragma omp target teams distribute parallel for private(x)
/// \endcode
/// In this example directive '#pragma omp target teams distribute parallel
/// for' has clause 'private' with the variables 'x'
///
class OMPTargetTeamsDistributeParallelForDirective final
    : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTargetTeamsDistributeParallelForDirective(SourceLocation StartLoc,
                                               SourceLocation EndLoc,
                                               unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetTeamsDistributeParallelForDirectiveClass,
                         llvm::omp::OMPD_target_teams_distribute_parallel_for,
                         StartLoc, EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTargetTeamsDistributeParallelForDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetTeamsDistributeParallelForDirectiveClass,
                         llvm::omp::OMPD_target_teams_distribute_parallel_for,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

  /// Sets special task reduction descriptor.
  void setTaskReductionRefExpr(Expr *E) {
    Data->getChildren()[numLoopChildren(
        getLoopsNumber(),
        llvm::omp::OMPD_target_teams_distribute_parallel_for)] = E;
  }

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param TaskRedRef Task reduction special reference expression to handle
  /// taskgroup descriptor.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPTargetTeamsDistributeParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, Expr *TaskRedRef,
         bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDistributeParallelForDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  /// Returns special task reduction reference expression.
  Expr *getTaskReductionRefExpr() {
    return cast_or_null<Expr>(Data->getChildren()[numLoopChildren(
        getLoopsNumber(),
        llvm::omp::OMPD_target_teams_distribute_parallel_for)]);
  }
  const Expr *getTaskReductionRefExpr() const {
    return const_cast<OMPTargetTeamsDistributeParallelForDirective *>(this)
        ->getTaskReductionRefExpr();
  }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() ==
           OMPTargetTeamsDistributeParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp target teams distribute parallel for simd'
/// combined directive.
///
/// \code
/// #pragma omp target teams distribute parallel for simd private(x)
/// \endcode
/// In this example directive '#pragma omp target teams distribute parallel
/// for simd' has clause 'private' with the variables 'x'
///
class OMPTargetTeamsDistributeParallelForSimdDirective final
    : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTargetTeamsDistributeParallelForSimdDirective(SourceLocation StartLoc,
                                                   SourceLocation EndLoc,
                                                   unsigned CollapsedNum)
      : OMPLoopDirective(
            OMPTargetTeamsDistributeParallelForSimdDirectiveClass,
            llvm::omp::OMPD_target_teams_distribute_parallel_for_simd, StartLoc,
            EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTargetTeamsDistributeParallelForSimdDirective(
      unsigned CollapsedNum)
      : OMPLoopDirective(
            OMPTargetTeamsDistributeParallelForSimdDirectiveClass,
            llvm::omp::OMPD_target_teams_distribute_parallel_for_simd,
            SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetTeamsDistributeParallelForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDistributeParallelForSimdDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() ==
           OMPTargetTeamsDistributeParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp target teams distribute simd' combined
/// directive.
///
/// \code
/// #pragma omp target teams distribute simd private(x)
/// \endcode
/// In this example directive '#pragma omp target teams distribute simd'
/// has clause 'private' with the variables 'x'
///
class OMPTargetTeamsDistributeSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTargetTeamsDistributeSimdDirective(SourceLocation StartLoc,
                                        SourceLocation EndLoc,
                                        unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetTeamsDistributeSimdDirectiveClass,
                         llvm::omp::OMPD_target_teams_distribute_simd, StartLoc,
                         EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTargetTeamsDistributeSimdDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetTeamsDistributeSimdDirectiveClass,
                         llvm::omp::OMPD_target_teams_distribute_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetTeamsDistributeSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDistributeSimdDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetTeamsDistributeSimdDirectiveClass;
  }
};

/// This represents the '#pragma omp tile' loop transformation directive.
class OMPTileDirective final : public OMPLoopTransformationDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Default list of offsets.
  enum {
    PreInitsOffset = 0,
    TransformedStmtOffset,
  };

  explicit OMPTileDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                            unsigned NumLoops)
      : OMPLoopTransformationDirective(OMPTileDirectiveClass,
                                       llvm::omp::OMPD_tile, StartLoc, EndLoc,
                                       NumLoops) {
    setNumGeneratedLoops(3 * NumLoops);
  }

  void setPreInits(Stmt *PreInits) {
    Data->getChildren()[PreInitsOffset] = PreInits;
  }

  void setTransformedStmt(Stmt *S) {
    Data->getChildren()[TransformedStmtOffset] = S;
  }

public:
  /// Create a new AST node representation for '#pragma omp tile'.
  ///
  /// \param C         Context of the AST.
  /// \param StartLoc  Location of the introducer (e.g. the 'omp' token).
  /// \param EndLoc    Location of the directive's end (e.g. the tok::eod).
  /// \param Clauses   The directive's clauses.
  /// \param NumLoops  Number of associated loops (number of items in the
  ///                  'sizes' clause).
  /// \param AssociatedStmt The outermost associated loop.
  /// \param TransformedStmt The loop nest after tiling, or nullptr in
  ///                        dependent contexts.
  /// \param PreInits Helper preinits statements for the loop nest.
  static OMPTileDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation EndLoc,
                                  ArrayRef<OMPClause *> Clauses,
                                  unsigned NumLoops, Stmt *AssociatedStmt,
                                  Stmt *TransformedStmt, Stmt *PreInits);

  /// Build an empty '#pragma omp tile' AST node for deserialization.
  ///
  /// \param C          Context of the AST.
  /// \param NumClauses Number of clauses to allocate.
  /// \param NumLoops   Number of associated loops to allocate.
  static OMPTileDirective *CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                       unsigned NumLoops);

  /// Gets/sets the associated loops after tiling.
  ///
  /// This is in de-sugared format stored as a CompoundStmt.
  ///
  /// \code
  ///   for (...)
  ///     ...
  /// \endcode
  ///
  /// Note that if the generated loops a become associated loops of another
  /// directive, they may need to be hoisted before them.
  Stmt *getTransformedStmt() const {
    return Data->getChildren()[TransformedStmtOffset];
  }

  /// Return preinits statement.
  Stmt *getPreInits() const { return Data->getChildren()[PreInitsOffset]; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTileDirectiveClass;
  }
};

/// This represents the '#pragma omp unroll' loop transformation directive.
///
/// \code
/// #pragma omp unroll
/// for (int i = 0; i < 64; ++i)
/// \endcode
class OMPUnrollDirective final : public OMPLoopTransformationDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Default list of offsets.
  enum {
    PreInitsOffset = 0,
    TransformedStmtOffset,
  };

  explicit OMPUnrollDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPLoopTransformationDirective(OMPUnrollDirectiveClass,
                                       llvm::omp::OMPD_unroll, StartLoc, EndLoc,
                                       1) {}

  /// Set the pre-init statements.
  void setPreInits(Stmt *PreInits) {
    Data->getChildren()[PreInitsOffset] = PreInits;
  }

  /// Set the de-sugared statement.
  void setTransformedStmt(Stmt *S) {
    Data->getChildren()[TransformedStmtOffset] = S;
  }

public:
  /// Create a new AST node representation for '#pragma omp unroll'.
  ///
  /// \param C         Context of the AST.
  /// \param StartLoc  Location of the introducer (e.g. the 'omp' token).
  /// \param EndLoc    Location of the directive's end (e.g. the tok::eod).
  /// \param Clauses   The directive's clauses.
  /// \param AssociatedStmt The outermost associated loop.
  /// \param TransformedStmt The loop nest after tiling, or nullptr in
  ///                        dependent contexts.
  /// \param PreInits   Helper preinits statements for the loop nest.
  static OMPUnrollDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
         unsigned NumGeneratedLoops, Stmt *TransformedStmt, Stmt *PreInits);

  /// Build an empty '#pragma omp unroll' AST node for deserialization.
  ///
  /// \param C          Context of the AST.
  /// \param NumClauses Number of clauses to allocate.
  static OMPUnrollDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses);

  /// Get the de-sugared associated loops after unrolling.
  ///
  /// This is only used if the unrolled loop becomes an associated loop of
  /// another directive, otherwise the loop is emitted directly using loop
  /// transformation metadata. When the unrolled loop cannot be used by another
  /// directive (e.g. because of the full clause), the transformed stmt can also
  /// be nullptr.
  Stmt *getTransformedStmt() const {
    return Data->getChildren()[TransformedStmtOffset];
  }

  /// Return the pre-init statements.
  Stmt *getPreInits() const { return Data->getChildren()[PreInitsOffset]; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPUnrollDirectiveClass;
  }
};

/// Represents the '#pragma omp reverse' loop transformation directive.
///
/// \code
/// #pragma omp reverse
/// for (int i = 0; i < n; ++i)
///   ...
/// \endcode
class OMPReverseDirective final : public OMPLoopTransformationDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Offsets of child members.
  enum {
    PreInitsOffset = 0,
    TransformedStmtOffset,
  };

  explicit OMPReverseDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPLoopTransformationDirective(OMPReverseDirectiveClass,
                                       llvm::omp::OMPD_reverse, StartLoc,
                                       EndLoc, 1) {}

  void setPreInits(Stmt *PreInits) {
    Data->getChildren()[PreInitsOffset] = PreInits;
  }

  void setTransformedStmt(Stmt *S) {
    Data->getChildren()[TransformedStmtOffset] = S;
  }

public:
  /// Create a new AST node representation for '#pragma omp reverse'.
  ///
  /// \param C         Context of the AST.
  /// \param StartLoc  Location of the introducer (e.g. the 'omp' token).
  /// \param EndLoc    Location of the directive's end (e.g. the tok::eod).
  /// \param AssociatedStmt  The outermost associated loop.
  /// \param TransformedStmt The loop nest after tiling, or nullptr in
  ///                        dependent contexts.
  /// \param PreInits   Helper preinits statements for the loop nest.
  static OMPReverseDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         Stmt *AssociatedStmt, Stmt *TransformedStmt, Stmt *PreInits);

  /// Build an empty '#pragma omp reverse' AST node for deserialization.
  ///
  /// \param C          Context of the AST.
  /// \param NumClauses Number of clauses to allocate.
  static OMPReverseDirective *CreateEmpty(const ASTContext &C);

  /// Gets/sets the associated loops after the transformation, i.e. after
  /// de-sugaring.
  Stmt *getTransformedStmt() const {
    return Data->getChildren()[TransformedStmtOffset];
  }

  /// Return preinits statement.
  Stmt *getPreInits() const { return Data->getChildren()[PreInitsOffset]; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPReverseDirectiveClass;
  }
};

/// Represents the '#pragma omp interchange' loop transformation directive.
///
/// \code{c}
///   #pragma omp interchange
///   for (int i = 0; i < m; ++i)
///     for (int j = 0; j < n; ++j)
///       ..
/// \endcode
class OMPInterchangeDirective final : public OMPLoopTransformationDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Offsets of child members.
  enum {
    PreInitsOffset = 0,
    TransformedStmtOffset,
  };

  explicit OMPInterchangeDirective(SourceLocation StartLoc,
                                   SourceLocation EndLoc, unsigned NumLoops)
      : OMPLoopTransformationDirective(OMPInterchangeDirectiveClass,
                                       llvm::omp::OMPD_interchange, StartLoc,
                                       EndLoc, NumLoops) {
    setNumGeneratedLoops(3 * NumLoops);
  }

  void setPreInits(Stmt *PreInits) {
    Data->getChildren()[PreInitsOffset] = PreInits;
  }

  void setTransformedStmt(Stmt *S) {
    Data->getChildren()[TransformedStmtOffset] = S;
  }

public:
  /// Create a new AST node representation for '#pragma omp interchange'.
  ///
  /// \param C         Context of the AST.
  /// \param StartLoc  Location of the introducer (e.g. the 'omp' token).
  /// \param EndLoc    Location of the directive's end (e.g. the tok::eod).
  /// \param Clauses   The directive's clauses.
  /// \param NumLoops  Number of affected loops
  ///                  (number of items in the 'permutation' clause if present).
  /// \param AssociatedStmt  The outermost associated loop.
  /// \param TransformedStmt The loop nest after tiling, or nullptr in
  ///                        dependent contexts.
  /// \param PreInits  Helper preinits statements for the loop nest.
  static OMPInterchangeDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, unsigned NumLoops, Stmt *AssociatedStmt,
         Stmt *TransformedStmt, Stmt *PreInits);

  /// Build an empty '#pragma omp interchange' AST node for deserialization.
  ///
  /// \param C          Context of the AST.
  /// \param NumClauses Number of clauses to allocate.
  /// \param NumLoops   Number of associated loops to allocate.
  static OMPInterchangeDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned NumLoops);

  /// Gets the associated loops after the transformation. This is the de-sugared
  /// replacement or nullptr in dependent contexts.
  Stmt *getTransformedStmt() const {
    return Data->getChildren()[TransformedStmtOffset];
  }

  /// Return preinits statement.
  Stmt *getPreInits() const { return Data->getChildren()[PreInitsOffset]; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPInterchangeDirectiveClass;
  }
};

/// This represents '#pragma omp scan' directive.
///
/// \code
/// #pragma omp scan inclusive(a)
/// \endcode
/// In this example directive '#pragma omp scan' has clause 'inclusive' with
/// list item 'a'.
class OMPScanDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPScanDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPScanDirectiveClass, llvm::omp::OMPD_scan,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPScanDirective()
      : OMPExecutableDirective(OMPScanDirectiveClass, llvm::omp::OMPD_scan,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses (only single OMPFlushClause clause is
  /// allowed).
  ///
  static OMPScanDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation EndLoc,
                                  ArrayRef<OMPClause *> Clauses);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPScanDirective *CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                       EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPScanDirectiveClass;
  }
};

/// This represents '#pragma omp interop' directive.
///
/// \code
/// #pragma omp interop init(target:obj) device(x) depend(inout:y) nowait
/// \endcode
/// In this example directive '#pragma omp interop' has
/// clauses 'init', 'device', 'depend' and 'nowait'.
///
class OMPInteropDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPInteropDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPInteropDirectiveClass,
                               llvm::omp::OMPD_interop, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPInteropDirective()
      : OMPExecutableDirective(OMPInteropDirectiveClass,
                               llvm::omp::OMPD_interop, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses The directive's clauses.
  ///
  static OMPInteropDirective *Create(const ASTContext &C,
                                     SourceLocation StartLoc,
                                     SourceLocation EndLoc,
                                     ArrayRef<OMPClause *> Clauses);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPInteropDirective *CreateEmpty(const ASTContext &C,
                                          unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPInteropDirectiveClass;
  }
};

/// This represents '#pragma omp dispatch' directive.
///
/// \code
/// #pragma omp dispatch device(dnum)
/// \endcode
/// This example shows a directive '#pragma omp dispatch' with a
/// device clause with variable 'dnum'.
///
class OMPDispatchDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// The location of the target-call.
  SourceLocation TargetCallLoc;

  /// Set the location of the target-call.
  void setTargetCallLoc(SourceLocation Loc) { TargetCallLoc = Loc; }

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPDispatchDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPDispatchDirectiveClass,
                               llvm::omp::OMPD_dispatch, StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPDispatchDirective()
      : OMPExecutableDirective(OMPDispatchDirectiveClass,
                               llvm::omp::OMPD_dispatch, SourceLocation(),
                               SourceLocation()) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param TargetCallLoc Location of the target-call.
  ///
  static OMPDispatchDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
         SourceLocation TargetCallLoc);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDispatchDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses, EmptyShell);

  /// Return location of target-call.
  SourceLocation getTargetCallLoc() const { return TargetCallLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDispatchDirectiveClass;
  }
};

/// This represents '#pragma omp masked' directive.
/// \code
/// #pragma omp masked filter(tid)
/// \endcode
/// This example shows a directive '#pragma omp masked' with a filter clause
/// with variable 'tid'.
///
class OMPMaskedDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPMaskedDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPMaskedDirectiveClass, llvm::omp::OMPD_masked,
                               StartLoc, EndLoc) {}

  /// Build an empty directive.
  ///
  explicit OMPMaskedDirective()
      : OMPExecutableDirective(OMPMaskedDirectiveClass, llvm::omp::OMPD_masked,
                               SourceLocation(), SourceLocation()) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPMaskedDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPMaskedDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPMaskedDirectiveClass;
  }
};

/// This represents '#pragma omp metadirective' directive.
///
/// \code
/// #pragma omp metadirective when(user={condition(N>10)}: parallel for)
/// \endcode
/// In this example directive '#pragma omp metadirective' has clauses 'when'
/// with a dynamic user condition to check if a variable 'N > 10'
///
class OMPMetaDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  Stmt *IfStmt;

  OMPMetaDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPMetaDirectiveClass,
                               llvm::omp::OMPD_metadirective, StartLoc,
                               EndLoc) {}
  explicit OMPMetaDirective()
      : OMPExecutableDirective(OMPMetaDirectiveClass,
                               llvm::omp::OMPD_metadirective, SourceLocation(),
                               SourceLocation()) {}

  void setIfStmt(Stmt *S) { IfStmt = S; }

public:
  static OMPMetaDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation EndLoc,
                                  ArrayRef<OMPClause *> Clauses,
                                  Stmt *AssociatedStmt, Stmt *IfStmt);
  static OMPMetaDirective *CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                       EmptyShell);
  Stmt *getIfStmt() const { return IfStmt; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPMetaDirectiveClass;
  }
};

/// This represents '#pragma omp loop' directive.
///
/// \code
/// #pragma omp loop private(a,b) binding(parallel) order(concurrent)
/// \endcode
/// In this example directive '#pragma omp loop' has
/// clauses 'private' with the variables 'a' and 'b', 'binding' with
/// modifier 'parallel' and 'order(concurrent).
///
class OMPGenericLoopDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPGenericLoopDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                          unsigned CollapsedNum)
      : OMPLoopDirective(OMPGenericLoopDirectiveClass, llvm::omp::OMPD_loop,
                         StartLoc, EndLoc, CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPGenericLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPGenericLoopDirectiveClass, llvm::omp::OMPD_loop,
                         SourceLocation(), SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \p Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPGenericLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with a place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  static OMPGenericLoopDirective *CreateEmpty(const ASTContext &C,
                                              unsigned NumClauses,
                                              unsigned CollapsedNum,
                                              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPGenericLoopDirectiveClass;
  }
};

/// This represents '#pragma omp teams loop' directive.
///
/// \code
/// #pragma omp teams loop private(a,b) order(concurrent)
/// \endcode
/// In this example directive '#pragma omp teams loop' has
/// clauses 'private' with the variables 'a' and 'b', and order(concurrent).
///
class OMPTeamsGenericLoopDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTeamsGenericLoopDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                               unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsGenericLoopDirectiveClass,
                         llvm::omp::OMPD_teams_loop, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTeamsGenericLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTeamsGenericLoopDirectiveClass,
                         llvm::omp::OMPD_teams_loop, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \p Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTeamsGenericLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsGenericLoopDirective *CreateEmpty(const ASTContext &C,
                                                   unsigned NumClauses,
                                                   unsigned CollapsedNum,
                                                   EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsGenericLoopDirectiveClass;
  }
};

/// This represents '#pragma omp target teams loop' directive.
///
/// \code
/// #pragma omp target teams loop private(a,b) order(concurrent)
/// \endcode
/// In this example directive '#pragma omp target teams loop' has
/// clauses 'private' with the variables 'a' and 'b', and order(concurrent).
///
class OMPTargetTeamsGenericLoopDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// true if loop directive's associated loop can be a parallel for.
  bool CanBeParallelFor = false;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTargetTeamsGenericLoopDirective(SourceLocation StartLoc,
                                     SourceLocation EndLoc,
                                     unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetTeamsGenericLoopDirectiveClass,
                         llvm::omp::OMPD_target_teams_loop, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTargetTeamsGenericLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetTeamsGenericLoopDirectiveClass,
                         llvm::omp::OMPD_target_teams_loop, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

  /// Set whether associated loop can be a parallel for.
  void setCanBeParallelFor(bool ParFor) { CanBeParallelFor = ParFor; }

public:
  /// Creates directive with a list of \p Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetTeamsGenericLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool CanBeParallelFor);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsGenericLoopDirective *CreateEmpty(const ASTContext &C,
                                                         unsigned NumClauses,
                                                         unsigned CollapsedNum,
                                                         EmptyShell);

  /// Return true if current loop directive's associated loop can be a
  /// parallel for.
  bool canBeParallelFor() const { return CanBeParallelFor; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetTeamsGenericLoopDirectiveClass;
  }
};

/// This represents '#pragma omp parallel loop' directive.
///
/// \code
/// #pragma omp parallel loop private(a,b) order(concurrent)
/// \endcode
/// In this example directive '#pragma omp parallel loop' has
/// clauses 'private' with the variables 'a' and 'b', and order(concurrent).
///
class OMPParallelGenericLoopDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPParallelGenericLoopDirective(SourceLocation StartLoc,
                                  SourceLocation EndLoc, unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelGenericLoopDirectiveClass,
                         llvm::omp::OMPD_parallel_loop, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPParallelGenericLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPParallelGenericLoopDirectiveClass,
                         llvm::omp::OMPD_parallel_loop, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \p Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPParallelGenericLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelGenericLoopDirective *CreateEmpty(const ASTContext &C,
                                                      unsigned NumClauses,
                                                      unsigned CollapsedNum,
                                                      EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelGenericLoopDirectiveClass;
  }
};

/// This represents '#pragma omp target parallel loop' directive.
///
/// \code
/// #pragma omp target parallel loop private(a,b) order(concurrent)
/// \endcode
/// In this example directive '#pragma omp target parallel loop' has
/// clauses 'private' with the variables 'a' and 'b', and order(concurrent).
///
class OMPTargetParallelGenericLoopDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  OMPTargetParallelGenericLoopDirective(SourceLocation StartLoc,
                                        SourceLocation EndLoc,
                                        unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetParallelGenericLoopDirectiveClass,
                         llvm::omp::OMPD_target_parallel_loop, StartLoc, EndLoc,
                         CollapsedNum) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  ///
  explicit OMPTargetParallelGenericLoopDirective(unsigned CollapsedNum)
      : OMPLoopDirective(OMPTargetParallelGenericLoopDirectiveClass,
                         llvm::omp::OMPD_target_parallel_loop, SourceLocation(),
                         SourceLocation(), CollapsedNum) {}

public:
  /// Creates directive with a list of \p Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetParallelGenericLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetParallelGenericLoopDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetParallelGenericLoopDirectiveClass;
  }
};

/// This represents '#pragma omp error' directive.
///
/// \code
/// #pragma omp error
/// \endcode
class OMPErrorDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  friend class OMPExecutableDirective;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPErrorDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(OMPErrorDirectiveClass, llvm::omp::OMPD_error,
                               StartLoc, EndLoc) {}
  /// Build an empty directive.
  ///
  explicit OMPErrorDirective()
      : OMPExecutableDirective(OMPErrorDirectiveClass, llvm::omp::OMPD_error,
                               SourceLocation(), SourceLocation()) {}

public:
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  ///
  static OMPErrorDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                   SourceLocation EndLoc,
                                   ArrayRef<OMPClause *> Clauses);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPErrorDirective *CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPErrorDirectiveClass;
  }
};
} // end namespace clang

#endif
